#include "emulate_engine.h"
#include "signal_capture.h"
#include "wave_out.h"
#include "board_pins.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EMU_FOLD_P      1024u
#define EMU_MAX_K       64u
#define EMU_RELOCK_MS   2000u
#define EMU_PROBE_TRIES 3u

typedef enum {
    PH_NONE = 0,
    PH_PROBE_START,
    PH_PROBE_WAIT,
    PH_MAIN_START,
    PH_MAIN_WAIT,
    PH_RELOCK_START,
    PH_RELOCK_WAIT,
} Phase;

static const RlcModel *s_model;
static EmuState s_state = EMU_IDLE;
static Phase    s_phase = PH_NONE;
static uint16_t s_err;
static uint8_t  s_tries;
static uint32_t s_t_last;
static SigStats s_in;
static uint32_t s_snap_hz;
static float    s_freq_used_hz;   /* running-average lock frequency */
static uint32_t s_cap_fs;
static uint32_t s_cap_n;

static const SigChanCal s_cal_main = { CAL_ADC_MAIN_MV_PER_LSB };

static float s_period_mv[EMU_FOLD_P];
static float s_out_mv[EMU_FOLD_P];
static float s_re[EMU_MAX_K + 1u];
static float s_im[EMU_MAX_K + 1u];

/* ------------------------------------------------------------------ */

static void pick_main_capture(float f_hz, uint32_t *fs, uint32_t *n)
{
    uint32_t want = (uint32_t)(f_hz * 24.0f);
    if (want < 100000u) {
        want = 100000u;
    }
    if (want > CAP_MAX_FS_HZ) {
        want = CAP_MAX_FS_HZ;
    }
    *fs = want;
    *n = CAP_MAX_SAMPLES;
}

/* Build the output period through the model and load the DDS table.    */
static void synthesize(float f_hz)
{
    uint32_t K, k, i;
    float hr, hi, hm, mag;
    float d0, mean, dc_in;

    K = (uint32_t)(0.45f * (float)WAVEOUT_FS_HZ / f_hz);
    if (K > EMU_MAX_K) {
        K = EMU_MAX_K;
    }
    if (K < 1u) {
        K = 1u;
    }

    /* The measurement front-end is AC coupled and biased to VDDA/2, so
     * the folded mean is the bias voltage, NOT the input's DC component.
     * Remove it before the DFT...                                        */
    mean = 0.0f;
    for (i = 0u; i < EMU_FOLD_P; ++i) {
        mean += s_period_mv[i];
    }
    mean /= (float)EMU_FOLD_P;
    for (i = 0u; i < EMU_FOLD_P; ++i) {
        s_period_mv[i] -= mean;
    }

    SigMeas_Harmonics(s_period_mv, EMU_FOLD_P, K, s_re, s_im);

    /* ...and reconstruct the true input DC analytically from the
     * classified waveform: a 2 Vpp square with duty d swings +-A around
     * A*(2d-1); sine/triangle have zero DC; "other" is assumed zero.    */
    dc_in = 0.0f;
    if (s_in.wave == SIG_WAVE_SQUARE) {
        float duty = (float)SigMeas_SnapDuty5(s_in.duty_pct10) / 1000.0f;
        dc_in = 0.5f * s_in.vpp_mv * (2.0f * duty - 1.0f);
    }
    RlcModel_Response(s_model, 0.0f, &hr, &hi);
    d0 = dc_in * hr;

    /* harmonics: magnitude from the measured grid, phase from the fit  */
    for (k = 1u; k <= K; ++k) {
        float fk = f_hz * (float)k;
        float cr = s_re[k], ci = s_im[k];
        RlcModel_Response(s_model, fk, &hr, &hi);
        hm = sqrtf(hr * hr + hi * hi);
        mag = RlcModel_Mag(s_model, fk);
        if (hm > 1.0e-9f && mag > 0.0f) {
            float sc = mag / hm;
            hr *= sc;
            hi *= sc;
        }
        s_re[k] = cr * hr - ci * hi;
        s_im[k] = cr * hi + ci * hr;
    }

    /* inverse series via phasor recurrence (no trig in the inner loop) */
    for (i = 0u; i < EMU_FOLD_P; ++i) {
        s_out_mv[i] = d0;
    }
    for (k = 1u; k <= K; ++k) {
        float ang = 2.0f * (float)M_PI * (float)k / (float)EMU_FOLD_P;
        float cs = cosf(ang), sn = sinf(ang);
        float pr = 1.0f, pi = 0.0f;
        float dr = s_re[k], di = s_im[k];
        for (i = 0u; i < EMU_FOLD_P; ++i) {
            s_out_mv[i] += 2.0f * (dr * pr - di * pi);
            {
                float t = pr * cs - pi * sn;
                pi = pr * sn + pi * cs;
                pr = t;
            }
        }
    }

    WaveOut_LoadWave(s_out_mv, EMU_FOLD_P);
}

static void set_output_freq(float f_hz)
{
    /* ALWAYS lock to the measured frequency.  The 200 Hz grid snap is
     * for display and harmonic evaluation only: a real generator sits a
     * few hundred ppm off its dial, and locking to the nominal grid
     * value would make the two scope traces walk apart.                 */
    WaveOut_SetFreqMilliHz((uint64_t)((double)f_hz * 1000.0 + 0.5));
}

/* ------------------------------------------------------------------ */

void Emu_Init(void)
{
    s_state = EMU_IDLE;
    s_phase = PH_NONE;
    s_err = EMU_ERR_NONE;
    s_model = 0;
}

void Emu_Start(const RlcModel *model, uint8_t wave_hint, uint32_t now_ms)
{
    s_model = model;
    /* wave_hint is the operator's button choice - display only.  The
     * engine always trusts its own classification of the input.        */
    (void)wave_hint;
    s_err = EMU_ERR_NONE;
    s_tries = 0u;
    s_snap_hz = 0u;
    s_t_last = now_ms;
    s_state = EMU_MEASURING;
    s_phase = PH_PROBE_START;
    WaveOut_Enable(0u);
}

void Emu_Abort(void)
{
    WaveOut_Enable(0u);
    s_state = EMU_IDLE;
    s_phase = PH_NONE;
}

static void fail(uint16_t code)
{
    WaveOut_Enable(0u);
    s_err = code;
    s_state = EMU_FAILED;
    s_phase = PH_NONE;
}

void Emu_Poll(uint32_t now_ms)
{
    switch (s_phase) {
    case PH_PROBE_START:
        if (Cap_Start(CAP_MAX_FS_HZ, 2048u)) {
            s_phase = PH_PROBE_WAIT;
        }
        break;

    case PH_PROBE_WAIT:
        if (Cap_Busy()) {
            break;
        }
        SigMeas_Analyze(Cap_Main(), Cap_Count(), Cap_Fs(), &s_cal_main, &s_in);
        if (s_in.freq_hz <= 0.0f || s_in.vpp_mv < 100.0f) {
            if (++s_tries >= EMU_PROBE_TRIES) {
                fail(EMU_ERR_NO_INPUT);
            } else {
                s_phase = PH_PROBE_START;
            }
            break;
        }
        if (s_in.freq_hz < 800.0f) {
            fail(EMU_ERR_FREQ_LOW);
            break;
        }
        if (s_in.freq_hz > 52000.0f) {
            fail(EMU_ERR_FREQ_HIGH);
            break;
        }
        pick_main_capture(s_in.freq_hz, &s_cap_fs, &s_cap_n);
        s_phase = PH_MAIN_START;
        break;

    case PH_MAIN_START:
        if (Cap_Start(s_cap_fs, s_cap_n)) {
            s_phase = PH_MAIN_WAIT;
        }
        break;

    case PH_MAIN_WAIT:
        if (Cap_Busy()) {
            break;
        }
        SigMeas_Analyze(Cap_Main(), Cap_Count(), Cap_Fs(), &s_cal_main, &s_in);
        if (s_in.freq_hz <= 0.0f) {
            fail(EMU_ERR_NO_INPUT);
            break;
        }
        s_snap_hz = SigMeas_SnapFreq200(s_in.freq_hz);
        s_freq_used_hz = s_in.freq_hz;
        (void)SigMeas_FoldPeriod(Cap_Main(), Cap_Count(), Cap_Fs(),
                                 s_in.freq_hz, &s_cal_main,
                                 s_period_mv, EMU_FOLD_P);

        if (s_model != 0 && s_model->valid) {
            synthesize((s_snap_hz != 0u) ? (float)s_snap_hz : s_in.freq_hz);
            set_output_freq(s_freq_used_hz);
            WaveOut_Enable(1u);
            s_state = EMU_RUNNING;
        } else {
            s_state = EMU_NO_MODEL;
        }
        s_t_last = now_ms;
        s_phase = PH_RELOCK_START;
        break;

    case PH_RELOCK_START:
        if ((uint32_t)(now_ms - s_t_last) < EMU_RELOCK_MS) {
            break;
        }
        if (Cap_Start(s_cap_fs, s_cap_n)) {
            s_phase = PH_RELOCK_WAIT;
        }
        break;

    case PH_RELOCK_WAIT:
        if (Cap_Busy()) {
            break;
        }
        {
            SigStats st;
            SigMeas_Analyze(Cap_Main(), Cap_Count(), Cap_Fs(),
                            &s_cal_main, &st);
            if (st.freq_hz > 0.0f && st.vpp_mv > 100.0f) {
                s_in = st;
                /* light running average tightens the lock */
                s_freq_used_hz = 0.5f * s_freq_used_hz + 0.5f * st.freq_hz;
                s_snap_hz = SigMeas_SnapFreq200(s_freq_used_hz);
                if (s_state == EMU_RUNNING) {
                    set_output_freq(s_freq_used_hz);
                }
            }
        }
        s_t_last = now_ms;
        s_phase = PH_RELOCK_START;
        break;

    default:
        break;
    }
}

EmuState Emu_State(void)
{
    return s_state;
}

uint16_t Emu_ErrCode(void)
{
    return s_err;
}

const SigStats *Emu_Input(void)
{
    return &s_in;
}

uint32_t Emu_SnappedFreqHz(void)
{
    return s_snap_hz;
}

uint16_t Emu_OutputVpp_mV(void)
{
    return WaveOut_IsEnabled() ? WaveOut_TableVpp() : 0u;
}
