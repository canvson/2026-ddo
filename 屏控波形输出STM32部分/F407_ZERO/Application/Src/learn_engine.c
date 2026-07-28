#include "learn_engine.h"
#include "signal_capture.h"
#include "signal_meas.h"
#include "board_pins.h"

typedef enum {
    ST_IDLE = 0,
    ST_SETTLE,      /* wait for DDS + circuit to settle at the new point  */
    ST_CAP_START,
    ST_CAP_WAIT,
    ST_FIT,
} Phase;

#define SETTLE_MS        7u
#define BUDGET_MS        110000u
#define MIN_SIG_MV       15.0f   /* below this RMS the channel is "dead"  */

static FpgaCtrlWriteFn s_fpga_write;
static RlcModel  s_model;
static LearnState s_state = LEARN_IDLE;
static Phase     s_phase = ST_IDLE;
static uint16_t  s_err;
static uint32_t  s_point;        /* 0..RLC_SWEEP_POINTS-1                */
static uint32_t  s_t_phase;
static uint32_t  s_t_start;
static uint8_t   s_checked_start_freq;

static const SigChanCal s_cal_main = { CAL_ADC_MAIN_MV_PER_LSB };
static const SigChanCal s_cal_ref  = { CAL_ADC_REF_MV_PER_LSB };

static uint32_t point_freq(uint32_t i)
{
    return RLC_SWEEP_F0_HZ + RLC_SWEEP_DF_HZ * i;
}

static void pick_capture(uint32_t f_hz, uint32_t *fs, uint32_t *n)
{
    uint32_t want_fs = f_hz * 64u;
    uint32_t samples;
    if (want_fs > CAP_MAX_FS_HZ) {
        want_fs = CAP_MAX_FS_HZ;
    }
    if (want_fs < 32000u) {
        want_fs = 32000u;
    }
    /* at least 8 periods and at least 256 samples */
    samples = (want_fs / f_hz) * 8u;
    if (samples < 256u) {
        samples = 256u;
    }
    if (samples > 2048u) {
        samples = 2048u;
    }
    *fs = want_fs;
    *n = samples;
}

void Learn_Init(FpgaCtrlWriteFn fpga_write)
{
    s_fpga_write = fpga_write;
    s_model.valid = 0u;
    s_state = LEARN_IDLE;
    s_phase = ST_IDLE;
    s_err = LEARN_ERR_NONE;
}

void Learn_Start(uint32_t now_ms)
{
    uint32_t i;
    for (i = 0u; i < RLC_SWEEP_POINTS; ++i) {
        s_model.mag[i] = 0.0f;
    }
    s_model.valid = 0u;
    s_point = 0u;
    s_err = LEARN_ERR_NONE;
    s_checked_start_freq = 0u;
    s_t_start = now_ms;
    s_t_phase = now_ms;
    s_state = LEARN_RUNNING;
    s_phase = ST_SETTLE; /* point 0 is live right after the FPGA reset */
}

void Learn_Abort(void)
{
    if (s_state == LEARN_RUNNING) {
        s_state = LEARN_IDLE;
        s_phase = ST_IDLE;
    }
}

static void fail(uint16_t code)
{
    s_err = code;
    s_state = LEARN_FAILED;
    s_phase = ST_IDLE;
}

void Learn_Poll(uint32_t now_ms)
{
    if (s_state != LEARN_RUNNING) {
        return;
    }
    if ((uint32_t)(now_ms - s_t_start) > BUDGET_MS) {
        fail(LEARN_ERR_TIMEOUT);
        return;
    }

    switch (s_phase) {
    case ST_SETTLE:
        if ((uint32_t)(now_ms - s_t_phase) >= SETTLE_MS) {
            s_phase = ST_CAP_START;
        }
        break;

    case ST_CAP_START: {
        uint32_t fs, n;
        pick_capture(point_freq(s_point), &fs, &n);
        if (Cap_Start(fs, n)) {
            s_phase = ST_CAP_WAIT;
        }
        break;
    }

    case ST_CAP_WAIT:
        if (Cap_Busy()) {
            break;
        }
        {
            float ref_rms = SigMeas_RmsAc(Cap_Ref(), Cap_Count(), &s_cal_ref);
            float main_rms = SigMeas_RmsAc(Cap_Main(), Cap_Count(), &s_cal_main);

            /* stimulus sanity on the first points */
            if (s_point < 8u &&
                ref_rms < MIN_SIG_MV && main_rms < MIN_SIG_MV) {
                fail(LEARN_ERR_NO_STIMULUS);
                return;
            }

            /* verify the sweep really starts at 1 kHz (FPGA was reset) */
            if (!s_checked_start_freq && ref_rms >= MIN_SIG_MV) {
                SigStats st;
                SigMeas_Analyze(Cap_Ref(), Cap_Count(), Cap_Fs(),
                                &s_cal_ref, &st);
                s_checked_start_freq = 1u;
                if (st.freq_hz > 0.0f) {
                    float f = (float)point_freq(s_point);
                    float err = st.freq_hz - f;
                    if (err < 0.0f) {
                        err = -err;
                    }
                    if (err > 0.10f * f) {
                        fail(LEARN_ERR_NOT_RESET);
                        return;
                    }
                }
            }

            if (ref_rms < MIN_SIG_MV) {
                /* J_REF not wired: fall back to the nominal drive level */
                ref_rms = CAL_LEARN_DRIVE_MVPP / 2.828427f;
            }
            s_model.mag[s_point] = (ref_rms > 1.0f) ? (main_rms / ref_rms)
                                                    : 0.0f;
        }

        ++s_point;
        if (s_point >= RLC_SWEEP_POINTS) {
            s_phase = ST_FIT;
            break;
        }
        (void)FpgaCtrl_SendLearnStep(s_fpga_write);
        s_t_phase = now_ms;
        s_phase = ST_SETTLE;
        break;

    case ST_FIT:
        if (RlcModel_Fit(&s_model)) {
            s_state = LEARN_DONE;
        } else {
            fail(LEARN_ERR_FIT);
        }
        s_phase = ST_IDLE;
        break;

    default:
        break;
    }
}

LearnState Learn_State(void)
{
    return s_state;
}

uint8_t Learn_Percent(void)
{
    if (s_state == LEARN_DONE) {
        return 100u;
    }
    if (s_state != LEARN_RUNNING) {
        return 0u;
    }
    return (uint8_t)((s_point * 100u) / RLC_SWEEP_POINTS);
}

uint16_t Learn_ErrCode(void)
{
    return s_err;
}

uint32_t Learn_CurrentFreqHz(void)
{
    uint32_t i = (s_point < RLC_SWEEP_POINTS) ? s_point
                                              : (RLC_SWEEP_POINTS - 1u);
    return point_freq(i);
}

const RlcModel *Learn_Model(void)
{
    return &s_model;
}
