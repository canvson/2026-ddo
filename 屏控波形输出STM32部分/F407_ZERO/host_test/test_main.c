/*
 * Host verification harness for the G-problem STM32 application layer.
 * Compiles the portable modules with gcc and simulates full flows:
 * HMI frames -> g_app -> FPGA bytes / screen strings, learn sweep with a
 * synthetic RLC plant, emulate flow with a synthetic generator.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "hmi_protocol.h"
#include "known_model.h"
#include "fpga_ctrl.h"
#include "fpga_ctrl_table.h"
#include "signal_meas.h"
#include "rlc_model.h"
#include "wave_out.h"
#include "learn_engine.h"
#include "emulate_engine.h"
#include "g_app.h"
#include "board_pins.h"
#include "signal_capture.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_fail = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { g_fail++; printf("FAIL %s:%d  ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ================= stubs: time, uarts, keys ======================= */

static uint32_t s_fake_ms;
static uint32_t fake_ms(void) { return s_fake_ms; }

static uint8_t s_fpga_bytes[4096];
static uint32_t s_fpga_n;
static void fpga_write(const uint8_t *d, uint16_t n)
{
    while (n--) { if (s_fpga_n < sizeof(s_fpga_bytes)) s_fpga_bytes[s_fpga_n++] = *d++; }
}

static char s_hmi_text[262144];
static uint32_t s_hmi_n;
static void hmi_write(const uint8_t *d, uint16_t n)
{
    uint16_t i;
    for (i = 0; i < n && s_hmi_n < sizeof(s_hmi_text) - 1; ++i) {
        s_hmi_text[s_hmi_n++] = (char)((d[i] == 0xFF) ? '|' : d[i]);
    }
    s_hmi_text[s_hmi_n] = 0;
}

/* ================= stub capture service =========================== */
/* The "world": FPGA DDS drive + unknown plant, or a signal generator. */

typedef struct { float r, l, c; int type; } Plant;
static Plant s_plant;
static int   s_world_mode;      /* 0 = learn world, 1 = generator world */
static float s_gen_freq;        /* generator settings                    */
static float s_gen_vpp;
static float s_gen_duty;
static int   s_gen_wave;        /* 0 sine 1 square                       */
static uint32_t s_learn_steps;  /* bytes received while in learn world   */

static uint8_t  s_cap_busy;
static uint32_t s_cap_fs, s_cap_n;
static uint16_t s_cap_main[CAP_MAX_SAMPLES], s_cap_ref[CAP_MAX_SAMPLES];

/* complex response of the true plant (series/tank per type) */
static void plant_response(float f, float *re, float *im)
{
    /* use canonical biquads driven by the true component values */
    float w0, q, x, dr, di, den, nr, ni;
    float l = s_plant.l, c = s_plant.c, r = s_plant.r;

    w0 = 1.0f / sqrtf(l * c);
    if (s_plant.type == RLC_TYPE_BANDPASS && r * sqrtf(c / l) > 1.0f) {
        q = r * sqrtf(c / l);           /* tank BP */
    } else if (s_plant.type == RLC_TYPE_BANDPASS) {
        q = sqrtf(l / c) / r;           /* series BP */
    } else if (s_plant.type == RLC_TYPE_BANDSTOP) {
        q = r * sqrtf(c / l);           /* parallel trap BS (high-Q) */
    } else {
        q = sqrtf(l / c) / r;           /* series LP/HP */
    }
    x = 2.0f * (float)M_PI * f / w0;
    dr = 1.0f - x * x;
    di = x / q;
    den = dr * dr + di * di;
    switch (s_plant.type) {
    case RLC_TYPE_LOWPASS:  nr = 1.0f;      ni = 0.0f;   break;
    case RLC_TYPE_HIGHPASS: nr = -x * x;    ni = 0.0f;   break;
    case RLC_TYPE_BANDPASS: nr = 0.0f;      ni = x / q;  break;
    default:                nr = 1.0f - x * x; ni = 0.0f; break;
    }
    *re = (nr * dr + ni * di) / den;
    *im = (ni * dr - nr * di) / den;
}

static float plant_mag(float f)
{
    float re, im;
    plant_response(f, &re, &im);
    return sqrtf(re * re + im * im);
}

static float learn_drive_freq(void)
{
    uint32_t i = s_learn_steps;
    if (i > 490u) i = 490u;
    return 1000.0f + 100.0f * (float)i;
}

static uint16_t mv2lsb(float mv)
{
    float v = 2048.0f + mv / (3300.0f / 4096.0f);
    if (v < 0) v = 0;
    if (v > 4095) v = 4095;
    return (uint16_t)(v + 0.5f);
}

/* generator waveform value at time t (jack mV) */
static float gen_value(float t)
{
    float ph = fmodf(t * s_gen_freq, 1.0f);
    if (s_gen_wave == 1) {
        return (ph < s_gen_duty) ? (s_gen_vpp * 0.5f) : (-s_gen_vpp * 0.5f);
    }
    return 0.5f * s_gen_vpp * sinf(2.0f * (float)M_PI * ph);
}

/* steady-state plant output for the generator input (harmonic sum) */
static float gen_plant_out(float t)
{
    int k;
    float acc;
    float re, im;
    if (s_gen_wave == 0) {
        plant_response(s_gen_freq, &re, &im);
        return 0.5f * s_gen_vpp *
               (re * sinf(2.0f * (float)M_PI * s_gen_freq * t) +
                im * cosf(2.0f * (float)M_PI * s_gen_freq * t));
    }
    /* square with duty d, levels +-A: c0 = A*(2d-1), ck = ... */
    plant_response(0.0f, &re, &im);
    acc = (s_gen_vpp * 0.5f) * (2.0f * s_gen_duty - 1.0f) * re;
    for (k = 1; k <= 99; ++k) {
        float a = (s_gen_vpp / ((float)M_PI * k)) * sinf((float)M_PI * k * s_gen_duty);
        float wt = 2.0f * (float)M_PI * k * s_gen_freq * t;
        float ph0 = (float)M_PI * k * s_gen_duty; /* centered pulse phase */
        float cr = a * cosf(ph0), ci = -a * sinf(ph0);
        float orr, oi;
        plant_response(k * s_gen_freq, &orr, &oi);
        {
            float xr = cr * orr - ci * oi;
            float xi = cr * oi + ci * orr;
            acc += 2.0f * (xr * cosf(wt) - xi * sinf(wt));
        }
    }
    return acc;
}

uint8_t Cap_Start(uint32_t fs_hz, uint32_t n_samples)
{
    uint32_t i;
    if (s_cap_busy || n_samples > CAP_MAX_SAMPLES) return 0;
    s_cap_fs = fs_hz;
    s_cap_n = n_samples;
    if (s_world_mode == 0) {
        float f = learn_drive_freq();
        float g_re, g_im;
        plant_response(f, &g_re, &g_im);
        for (i = 0; i < n_samples; ++i) {
            float t = (float)i / (float)fs_hz;
            float wt = 2.0f * (float)M_PI * f * t;
            float drive = 1000.0f * sinf(wt); /* 2 Vpp */
            float outv = 1000.0f * (g_re * sinf(wt) + g_im * cosf(wt));
            s_cap_ref[i] = mv2lsb(drive);
            s_cap_main[i] = mv2lsb(outv);
        }
    } else {
        for (i = 0; i < n_samples; ++i) {
            float t = (float)i / (float)fs_hz;
            s_cap_main[i] = mv2lsb(gen_value(t + 0.1234f / s_gen_freq));
            s_cap_ref[i] = 2048;
        }
    }
    s_cap_busy = 1; /* completes on next Cap_Busy() poll */
    return 1;
}

uint8_t Cap_Busy(void)
{
    if (s_cap_busy) { s_cap_busy = 0; return 1; } /* one poll of latency */
    return 0;
}
uint32_t Cap_Fs(void) { return s_cap_fs; }
uint32_t Cap_Count(void) { return s_cap_n; }
const uint16_t *Cap_Main(void) { return s_cap_main; }
const uint16_t *Cap_Ref(void) { return s_cap_ref; }

/* ================= tests ========================================== */

static void test_known_model(void)
{
    double g0 = KnownModel_Gain(0.0);
    double g1k = KnownModel_Gain(1000.0);
    uint16_t vin = KnownModel_Input_mVpp(1000, 20);
    CHECK(fabs(g0 - 5.0) < 1e-9, "H(0)=%f", g0);
    CHECK(fabs(g1k - 2.5255) < 0.01, "H(1k)=%f", g1k);
    CHECK(abs((int)vin - 792) <= 2, "Vin(1k,2V)=%u mVpp", vin);
    printf("known_model: H(0)=%.3f H(1k)=%.4f Vin(1k,2Vpp)=%umV\n", g0, g1k, vin);
}

static void test_hmi_protocol(void)
{
    static const uint8_t f1[] = {0xAA,0x31,0x06,0x64,0x00,0x00,0x00,0x0A,0x00,0x4F,0x55};
    static const uint8_t f2[] = {0xAA,0x35,0x07,0x40,0x42,0x0F,0x00,0xB8,0x0B,0x01,0x3B,0x55};
    static const uint8_t f3[] = {0xAA,0x35,0x09,0x40,0x42,0x0F,0x00,0xB8,0x0B,0x01,0x2C,0x01,0x6A,0x55};
    HmiParser p;
    HmiEvent ev;
    HmiParseResult r = HMI_PARSE_NONE;
    size_t i;

    HmiProtocol_Init(&p);
    for (i = 0; i < sizeof(f1); ++i) r = HmiProtocol_PushByte(&p, f1[i], &ev);
    CHECK(r == HMI_PARSE_FRAME_OK, "frame1 parse");
    CHECK(ev.cmd == HMI_CMD_START_BASIC && ev.freq_hz == 100 && ev.target_vpp10 == 10,
          "frame1 fields");

    for (i = 0; i < sizeof(f2); ++i) r = HmiProtocol_PushByte(&p, f2[i], &ev);
    CHECK(r == HMI_PARSE_FRAME_OK, "frame2 parse");
    CHECK(ev.cmd == HMI_CMD_CALIB_OUTPUT && ev.freq_hz == 1000000 &&
          ev.output_mVpp == 3000 && ev.wave == 1 && ev.duty_pct10 == 500,
          "frame2 fields");

    for (i = 0; i < sizeof(f3); ++i) r = HmiProtocol_PushByte(&p, f3[i], &ev);
    CHECK(r == HMI_PARSE_FRAME_OK, "frame3 parse");
    CHECK(ev.cmd == HMI_CMD_CALIB_OUTPUT && ev.freq_hz == 1000000 &&
          ev.output_mVpp == 3000 && ev.wave == 1 && ev.duty_pct10 == 300,
          "frame3 fields");
    printf("hmi_protocol: example frames OK\n");
}

static void test_fpga_tables(void)
{
    /* spot-check against DDS_AD9767.v extraction */
    s_fpga_n = 0;
    CHECK(FpgaCtrl_SendBasic2Freq(fpga_write, 1100) == 1, "b2 send");
    CHECK(s_fpga_bytes[0] == 0x10, "1100Hz code 0x%02X", s_fpga_bytes[0]);
    s_fpga_n = 0;
    (void)FpgaCtrl_SendBasic2Freq(fpga_write, 1000000);
    CHECK(s_fpga_bytes[0] == 0x30, "1MHz code");
    s_fpga_n = 0;
    (void)FpgaCtrl_SendBasic2Freq(fpga_write, 3000);
    CHECK(s_fpga_bytes[0] == 0x29, "3kHz code");
    CHECK(FpgaCtrl_SendBasic2Freq(fpga_write, 3100) == 0, "3.1kHz unsupported");

    {
        FpgaCtrlResult r;
        s_fpga_n = 0;
        r = FpgaCtrl_SendBasic4(fpga_write, 1000, 20); /* {01,6d} */
        CHECK(r.sent && r.code_ok, "b4 1k/2.0 sent");
        CHECK(s_fpga_n == 3 && s_fpga_bytes[0] == 0xFF &&
              s_fpga_bytes[1] == 0x01 && s_fpga_bytes[2] == 0x6D,
              "b4 1k/2.0 bytes %02X %02X %02X",
              s_fpga_bytes[0], s_fpga_bytes[1], s_fpga_bytes[2]);
        s_fpga_n = 0;
        r = FpgaCtrl_SendBasic4(fpga_write, 3000, 20); /* {02,4a} */
        CHECK(r.code_ok && s_fpga_bytes[1] == 0x02 && s_fpga_bytes[2] == 0x4A,
              "b4 3k/2.0 bytes");
        s_fpga_n = 0;
        r = FpgaCtrl_SendBasic4(fpga_write, 2400, 10); /* {02,4d} scrambled page */
        CHECK(r.code_ok && s_fpga_bytes[1] == 0x02 && s_fpga_bytes[2] == 0x4D,
              "b4 2.4k/1.0 bytes %02X", s_fpga_bytes[2]);
        r = FpgaCtrl_SendBasic4(fpga_write, 1700, 15); /* FPGA bug zone */
        CHECK(r.sent && !r.code_ok, "b4 1700Hz/1.5 flagged");
        r = FpgaCtrl_SendBasic4(fpga_write, 1700, 10); /* 1.0 Vpp OK */
        CHECK(r.sent && r.code_ok, "b4 1700Hz/1.0 fine");
    }
    printf("fpga_ctrl: byte codes match the Verilog tables\n");
}

static void test_signal_meas(void)
{
    static uint16_t buf[4096];
    SigStats st;
    SigChanCal cal = { 3300.0f / 4096.0f };
    uint32_t i;
    uint32_t fs = 700000;

    /* sine 12.4 kHz, 2 Vpp */
    for (i = 0; i < 4096; ++i) {
        float t = (float)i / (float)fs;
        buf[i] = mv2lsb(1000.0f * sinf(2.0f * (float)M_PI * 12400.0f * t));
    }
    SigMeas_Analyze(buf, 4096, fs, &cal, &st);
    CHECK(fabsf(st.freq_hz - 12400.0f) < 30.0f, "sine freq %.1f", st.freq_hz);
    CHECK(st.wave == SIG_WAVE_SINE, "sine wave class %d", st.wave);
    CHECK(fabsf(st.vpp_mv - 2000.0f) < 80.0f, "sine vpp %.0f", st.vpp_mv);
    CHECK(SigMeas_SnapFreq200(st.freq_hz) == 12400, "snap 12400");

    /* square 5 kHz, duty 30 %, 2 Vpp */
    for (i = 0; i < 4096; ++i) {
        float t = (float)i / (float)fs;
        float ph = fmodf(t * 5000.0f, 1.0f);
        buf[i] = mv2lsb(ph < 0.30f ? 1000.0f : -1000.0f);
    }
    SigMeas_Analyze(buf, 4096, fs, &cal, &st);
    CHECK(fabsf(st.freq_hz - 5000.0f) < 15.0f, "sq freq %.1f", st.freq_hz);
    CHECK(st.wave == SIG_WAVE_SQUARE, "sq wave class %d", st.wave);
    CHECK(SigMeas_SnapDuty5(st.duty_pct10) == 300, "sq duty %u", st.duty_pct10);
    printf("signal_meas: sine + square analysis OK\n");
}

static void run_learn_world(void)
{
    uint32_t guard = 0;
    s_world_mode = 0;
    s_learn_steps = 0;
    s_fpga_n = 0;
    Learn_Init(fpga_write);
    Learn_Start(s_fake_ms);
    while (Learn_State() == LEARN_RUNNING && guard++ < 2000000) {
        uint32_t before = s_fpga_n;
        Learn_Poll(s_fake_ms);
        s_fake_ms += 3;
        if (s_fpga_n > before) {
            s_learn_steps += (s_fpga_n - before);
        }
    }
}

static void test_learn(const char *name, int type, float r, float l, float c,
                       int expect_type)
{
    const RlcModel *m;
    s_plant.type = type;
    s_plant.r = r; s_plant.l = l; s_plant.c = c;
    run_learn_world();
    CHECK(Learn_State() == LEARN_DONE, "%s learn done (state %d err %04X)",
          name, Learn_State(), Learn_ErrCode());
    m = Learn_Model();
    CHECK(m->valid, "%s model valid", name);
    CHECK(m->type == expect_type, "%s type got %s", name,
          RlcModel_TypeText(m->type));
    {
        float f0_true = 1.0f / (2.0f * (float)M_PI * sqrtf(l * c));
        float rel = fabsf(m->f0_hz - f0_true) / f0_true;
        CHECK(rel < 0.08f, "%s f0 %.0f vs true %.0f (%.1f%%)",
              name, m->f0_hz, f0_true, rel * 100.0f);
        printf("learn[%s]: type=%s f0=%.0fHz(true %.0f) Q=%.3f "
               "R=%.0f L=%.2fmH C=%.1fnF steps=%u\n",
               name, RlcModel_TypeText(m->type), m->f0_hz, f0_true, m->q,
               m->r_ohm, m->l_mh, m->c_nf, (unsigned)s_learn_steps);
    }
}

static void test_emulate(void)
{
    /* learn a tank band-pass first */
    s_plant.type = RLC_TYPE_BANDPASS;
    s_plant.r = 3300.0f; s_plant.l = 4.7e-3f; s_plant.c = 33e-9f;
    run_learn_world();
    CHECK(Learn_State() == LEARN_DONE, "emulate: learn done");

    /* now the generator world: square 10 kHz duty 30 % 2 Vpp */
    s_world_mode = 1;
    s_gen_freq = 10000.0f;
    s_gen_vpp = 2000.0f;
    s_gen_duty = 0.30f;
    s_gen_wave = 1;

    Emu_Init();
    Emu_Start(Learn_Model(), 1, s_fake_ms);
    {
        uint32_t guard = 0;
        while ((Emu_State() == EMU_MEASURING) && guard++ < 100000) {
            Emu_Poll(s_fake_ms);
            s_fake_ms += 1;
        }
    }
    CHECK(Emu_State() == EMU_RUNNING, "emulate running (state %d err %04X)",
          Emu_State(), Emu_ErrCode());
    CHECK(Emu_SnappedFreqHz() == 10000, "snap %u", Emu_SnappedFreqHz());
    CHECK(Emu_Input()->wave == SIG_WAVE_SQUARE, "input classified square");

    /* compare synthesized output Vpp with the analytic plant output Vpp */
    {
        float vmin = 1e9f, vmax = -1e9f;
        int i;
        for (i = 0; i < 4000; ++i) {
            float t = (float)i / (4000.0f * s_gen_freq);
            float v = gen_plant_out(t);
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
        {
            float true_vpp = vmax - vmin;
            float got_vpp = (float)Emu_OutputVpp_mV();
            float rel = fabsf(got_vpp - true_vpp) / true_vpp;
            CHECK(rel < 0.10f, "emulate vpp %.0f vs true %.0f (%.1f%%)",
                  got_vpp, true_vpp, rel * 100.0f);
            printf("emulate: square 10k/30%% -> out Vpp %.0f mV (true %.0f, "
                   "err %.1f%%), snap=%u\n",
                   got_vpp, true_vpp, rel * 100.0f, Emu_SnappedFreqHz());
        }
    }

    /* frequency lock resolution: DDS step must represent 10 kHz exactly
       enough that drift < 0.5 period over 10 minutes */
    {
        double step = (double)((uint64_t)10000000ull << 32) /
                      ((double)WAVEOUT_FS_HZ * 1000.0);
        double f_actual = floor(step) * (double)WAVEOUT_FS_HZ / 4294967296.0;
        double drift = fabs(f_actual - 10000.0) * 600.0; /* periods per 10 min */
        CHECK(drift < 0.5, "freq lock drift %.3f periods/10min", drift);
        printf("wave_out: 10kHz DDS drift %.4f periods / 10 min\n", drift);
    }
}


/* LP plant + duty-30% square: exercises the DC path.  The AC-coupled
   front-end hides the input's true DC; the engine must reconstruct it
   from the classified waveform, or the output mean (and clipping) is
   wrong for any H(0) != 0 model. */
static void test_emulate_lp_square_dc(void)
{
    s_plant.type = RLC_TYPE_LOWPASS;
    s_plant.r = 2200.0f; s_plant.l = 3.3e-3f; s_plant.c = 22e-9f;
    run_learn_world();
    CHECK(Learn_State() == LEARN_DONE, "lp-dc: learn done");

    s_world_mode = 1;
    s_gen_freq = 5000.0f;
    s_gen_vpp = 2000.0f;
    s_gen_duty = 0.30f;
    s_gen_wave = 1;

    Emu_Init();
    Emu_Start(Learn_Model(), 1, s_fake_ms);
    {
        uint32_t guard = 0;
        while ((Emu_State() == EMU_MEASURING) && guard++ < 100000) {
            Emu_Poll(s_fake_ms);
            s_fake_ms += 1;
        }
    }
    CHECK(Emu_State() == EMU_RUNNING, "lp-dc running (err %04X)", Emu_ErrCode());

    /* pull the actual DAC stream through the DDS and convert to mV */
    {
        static uint16_t codes[8192];
        double sum = 0.0;
        float vmin = 1e9f, vmax = -1e9f;
        int i;
        WaveOut_FillBlock(codes, 8192);
        for (i = 0; i < 8192; ++i) {
            float mv = ((float)codes[i] - 2048.0f) * (float)CAL_DAC_MV_PER_LSB;
            sum += mv;
            if (mv < vmin) vmin = mv;
            if (mv > vmax) vmax = mv;
        }
        {
            /* analytic truth from the real plant */
            double tsum = 0.0;
            float tmin = 1e9f, tmax = -1e9f;
            for (i = 0; i < 4000; ++i) {
                float t = (float)i / (4000.0f * s_gen_freq);
                float v = gen_plant_out(t);
                tsum += v;
                if (v < tmin) tmin = v;
                if (v > tmax) tmax = v;
            }
            {
                double mean_got = sum / 8192.0, mean_true = tsum / 4000.0;
                float vpp_got = vmax - vmin, vpp_true = tmax - tmin;
                CHECK(fabs(mean_got - mean_true) < 0.12 * fabs(mean_true) + 30.0,
                      "lp-dc mean %.0f vs true %.0f mV", mean_got, mean_true);
                CHECK(fabsf(vpp_got - vpp_true) < 0.10f * vpp_true,
                      "lp-dc vpp %.0f vs true %.0f", vpp_got, vpp_true);
                printf("emulate-LP: square 5k/30%% -> mean %.0f mV (true %.0f), "
                       "Vpp %.0f mV (true %.0f)\n",
                       mean_got, mean_true, vpp_got, vpp_true);
            }
        }
    }
}

/* Off-grid generator: 10,030 Hz reads as "10.0 kHz" on the display but
   the DDS must lock to the MEASURED frequency, or the scope traces
   drift apart at (f_true - f_grid) cycles per second. */
static void test_emulate_offgrid_lock(void)
{
    s_plant.type = RLC_TYPE_LOWPASS;
    s_plant.r = 2200.0f; s_plant.l = 3.3e-3f; s_plant.c = 22e-9f;
    run_learn_world();

    s_world_mode = 1;
    s_gen_freq = 10030.0f;   /* 30 Hz off the 200 Hz grid (0.3%) */
    s_gen_vpp = 2000.0f;
    s_gen_duty = 0.50f;
    s_gen_wave = 0;

    Emu_Init();
    Emu_Start(Learn_Model(), 0, s_fake_ms);
    {
        uint32_t guard = 0;
        while ((Emu_State() == EMU_MEASURING) && guard++ < 100000) {
            Emu_Poll(s_fake_ms);
            s_fake_ms += 1;
        }
    }
    CHECK(Emu_State() == EMU_RUNNING, "offgrid running");
    CHECK(Emu_SnappedFreqHz() == 10000, "offgrid display snap %u",
          Emu_SnappedFreqHz());
    {
        double f_out = (double)WaveOut_GetFreqMilliHz() / 1000.0;
        CHECK(fabs(f_out - 10030.0) < 3.0,
              "offgrid DDS locks to measured: %.1f Hz", f_out);
        CHECK(fabs(f_out - 10000.0) > 20.0,
              "offgrid DDS must NOT sit on the grid value: %.1f", f_out);
        printf("emulate-offgrid: gen 10030 Hz -> display %u Hz, DDS %.1f Hz\n",
               Emu_SnappedFreqHz(), f_out);
    }
}

static void test_g_app_flow(void)
{
    static const uint8_t start_basic[] =
        {0xAA,0x31,0x06,0xB8,0x0B,0x00,0x00,0x14,0x00,0xB8,0x55}; /* 3k, 2.0 */
    GAppIo io = { hmi_write, fpga_write, fake_ms, 0, 0 };
    size_t i;

    s_hmi_n = 0; s_fpga_n = 0;
    GApp_Init(&io);
    CHECK(strstr(s_hmi_text, "page 0") != 0, "boot page0");

    for (i = 0; i < sizeof(start_basic); ++i) GApp_OnHmiRxByte(start_basic[i]);
    GApp_Poll(); /* bytes are drained in the main loop now */
    CHECK(strstr(s_hmi_text, "page 1") != 0, "basic page1");
    CHECK(strstr(s_hmi_text, "3000 Hz") != 0, "basic freq label");
    /* FPGA got flush + {02,4a} for 3000 Hz / 2.0 Vpp */
    CHECK(s_fpga_n == 3 && s_fpga_bytes[0] == 0xFF &&
          s_fpga_bytes[1] == 0x02 && s_fpga_bytes[2] == 0x4A,
          "basic fpga bytes %u: %02X %02X %02X", (unsigned)s_fpga_n,
          s_fpga_bytes[0], s_fpga_bytes[1], s_fpga_bytes[2]);
    CHECK(WaveOut_IsEnabled(), "aux output on");
    /* Vin display: 2.0/|H(3k)| -> |H(3k)|: check text holds x.xxx Vpp */
    {
        uint16_t vin = KnownModel_Input_mVpp(3000, 20);
        char expect[32];
        snprintf(expect, sizeof(expect), "%u.%03u Vpp", vin / 1000, vin % 1000);
        CHECK(strstr(s_hmi_text, expect) != 0, "vin text %s", expect);
    }
    s_fake_ms += 1200;
    GApp_Poll();

    /* STOP turns the aux off */
    {
        static const uint8_t stopf[] = {0xAA,0x34,0x00,0xDE,0x55};
        for (i = 0; i < sizeof(stopf); ++i) GApp_OnHmiRxByte(stopf[i]);
        GApp_Poll();
        CHECK(!WaveOut_IsEnabled(), "aux off after STOP");
    }
    printf("g_app: START_BASIC / STOP flow OK\n");

    /* full learn through g_app with the plant */
    s_plant.type = RLC_TYPE_LOWPASS;
    s_plant.r = 2200.0f; s_plant.l = 3.3e-3f; s_plant.c = 22e-9f;
    s_world_mode = 0; s_learn_steps = 0; s_fpga_n = 0;
    {
        static const uint8_t learnf[] = {0xAA,0x32,0x00,0xDC,0x55};
        uint32_t guard = 0;
        for (i = 0; i < sizeof(learnf); ++i) GApp_OnHmiRxByte(learnf[i]);
        GApp_Poll();
        while (Learn_State() == LEARN_RUNNING && guard++ < 2000000) {
            uint32_t before = s_fpga_n;
            GApp_Poll();
            s_fake_ms += 3;
            if (s_fpga_n > before) s_learn_steps += s_fpga_n - before;
        }
        GApp_Poll();
        CHECK(strstr(s_hmi_text, "\xB5\xCD\xCD\xA8") != 0, "screen shows 低通");
        printf("g_app: learn flow shows type on screen OK\n");
    }
}

int main(void)
{
    test_known_model();
    test_hmi_protocol();
    test_fpga_tables();
    test_signal_meas();

    /* four plants covering low-Q series and high-Q tank topologies */
    test_learn("series-LP", RLC_TYPE_LOWPASS, 2200.0f, 3.3e-3f, 22e-9f,
               RLC_TYPE_LOWPASS);
    test_learn("series-HP", RLC_TYPE_HIGHPASS, 1500.0f, 10.0e-3f, 47e-9f,
               RLC_TYPE_HIGHPASS);
    test_learn("tank-BP", RLC_TYPE_BANDPASS, 3300.0f, 4.7e-3f, 33e-9f,
               RLC_TYPE_BANDPASS);
    test_learn("trap-BS", RLC_TYPE_BANDSTOP, 4700.0f, 2.2e-3f, 68e-9f,
               RLC_TYPE_BANDSTOP);
    /* low-Q series band-pass: the acid test for the classifier */
    test_learn("series-BP", RLC_TYPE_BANDPASS, 1000.0f, 10.0e-3f, 10e-9f,
               RLC_TYPE_BANDPASS);

    test_emulate();
    test_emulate_lp_square_dc();
    test_emulate_offgrid_lock();
    test_g_app_flow();

    if (g_fail) {
        printf("\n== %d FAILURE(S) ==\n", g_fail);
        return 1;
    }
    printf("\n== ALL TESTS PASSED ==\n");
    return 0;
}
