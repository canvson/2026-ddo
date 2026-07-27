#include "g_app.h"

#include "board_pins.h"
#include "emulate_engine.h"
#include "fpga_ctrl.h"
#include "hmi_protocol.h"
#include "hmi_screen.h"
#include "known_model.h"
#include "learn_engine.h"
#include "signal_meas.h"
#include "wave_out.h"

#include <stdio.h>
#include <stdint.h>

/*
 * Application orchestration - real competition flows.
 *
 *  BASIC   (基本要求 3/4): known-model inverse drive.  H(s) gives Vin from
 *          the target Vout; the FPGA Basic_four table is commanded AND the
 *          same sine is produced on the STM32 J_OUT aux jack (this also
 *          covers the 10 broken 1700 Hz FPGA entries).  Open loop by rule:
 *          no feedback from the model-circuit output is allowed.
 *  LEARN   (发挥 1): FPGA Develop_one sweep + dual-ADC ratio measurement,
 *          filter classification, f0/Q/R/L/C on the screen, << 2 min.
 *  EMULATE (发挥 2): input analysis + harmonic synthesis through the
 *          learned model, DAC output within the 5 s budget, re-locked
 *          every ~2 s so the scope traces do not drift.
 *  CALIB   (基本要求 2 / signal source page): FPGA Basic_two codes
 *          (100 Hz..3 kHz step 100 Hz, 1 MHz, 2 MHz, ~3.5 Vpp) plus the
 *          STM32 aux output for arbitrary amplitude up to 200 kHz.
 */

typedef enum {
    APP_OP_NONE = 0,
    APP_OP_BASIC,
    APP_OP_LEARN,
    APP_OP_EMULATE,
    APP_OP_CALIB,
} AppOp;

/* GB2312 texts for the filter types and waves */
#define T_CN_LOWPASS   "\xB5\xCD\xCD\xA8 LPF"   /* 低通 */
#define T_CN_HIGHPASS  "\xB8\xDF\xCD\xA8 HPF"   /* 高通 */
#define T_CN_BANDPASS  "\xB4\xF8\xCD\xA8 BPF"   /* 带通 */
#define T_CN_BANDSTOP  "\xB4\xF8\xD7\xE8 BSF"   /* 带阻 */
#define T_CN_SINE      "\xD5\xFD\xCF\xD2"       /* 正弦 */
#define T_CN_SQUARE    "\xBE\xD8\xD0\xCE"       /* 矩形 */
#define T_CN_TRIANGLE  "\xC8\xFD\xBD\xC7"       /* 三角 */
#define T_CN_OTHER     "\xC6\xE4\xCB\xFB"       /* 其他 */

#define T_MODEL_NOT_LEARNED   "MODEL:NOT LEARNED"

/* aux output ceiling for direct DAC synthesis */
#define AUX_MAX_FREQ_HZ  200000u

enum {
    ERR_BAD_CMD      = 0x1001,
    ERR_HMI_BASE     = 0x1100,
};

static HmiParser s_hmi_parser;
static HmiScreen s_screen;
static GAppIo s_io;
static AppOp s_op;
static uint32_t s_op_start_ms;

/* RX ring: bytes land here from the UART IRQ and are parsed in the main
 * loop, so screen refreshes never run (and never block) in IRQ context. */
#define RX_RING_LEN 64u
static volatile uint8_t s_rx_ring[RX_RING_LEN];
static volatile uint8_t s_rx_head;
static volatile uint8_t s_rx_tail;

/* basic mode context */
static uint32_t s_basic_freq_hz;
static uint16_t s_basic_vpp10;
static uint16_t s_basic_target_mVpp;
static uint8_t  s_basic_started;

/* UI throttles */
static uint8_t  s_learn_percent_shown;
static uint32_t s_ui_ms;
static uint32_t s_led_ms;
static uint8_t  s_led_state;
static uint8_t  s_key_last;
static uint32_t s_key_ms;
static uint8_t  s_emu_shown_state;
static uint16_t s_hmi_err_count;   /* framing glitches survived */

static void hmi_write_adapter(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    if (s_io.hmi_write != 0) {
        s_io.hmi_write(data, len);
    }
}

static uint32_t now_ms(void)
{
    return (s_io.get_ms != 0) ? s_io.get_ms() : 0u;
}

static const char *type_text_cn(uint8_t type)
{
    switch (type) {
    case RLC_TYPE_LOWPASS:  return T_CN_LOWPASS;
    case RLC_TYPE_HIGHPASS: return T_CN_HIGHPASS;
    case RLC_TYPE_BANDPASS: return T_CN_BANDPASS;
    case RLC_TYPE_BANDSTOP: return T_CN_BANDSTOP;
    default:                return "?";
    }
}

static const char *wave_text_cn(uint8_t wave)
{
    switch (wave) {
    case SIG_WAVE_SINE:     return T_CN_SINE;
    case SIG_WAVE_SQUARE:   return T_CN_SQUARE;
    case SIG_WAVE_TRIANGLE: return T_CN_TRIANGLE;
    case SIG_WAVE_OTHER:    return T_CN_OTHER;
    default:                return "--";
    }
}

static void stop_outputs(void)
{
    WaveOut_Enable(0u);
    Learn_Abort();
    Emu_Abort();
    (void)FpgaCtrl_SendStopHint(s_io.fpga_write);
}

static void reset_local_task(void)
{
    s_op = APP_OP_NONE;
    s_op_start_ms = 0u;
}

static void enter_error(uint16_t code, const char *msg)
{
    stop_outputs();
    reset_local_task();
    HmiScreen_ShowError(&s_screen, code, msg);
}

static void start_local_task(AppOp op)
{
    s_op = op;
    s_op_start_ms = now_ms();
}

/* ------------------------------------------------------------------ */
/* mode entries                                                        */
/* ------------------------------------------------------------------ */

static void basic_show(uint32_t freq_hz, uint16_t vpp10, uint16_t vin_mVpp)
{
    HmiScreen_Goto(&s_screen, HMI_PAGE_BASIC);
    HmiScreen_SetMode(&s_screen, HMI_MODE_BASIC);
    HmiScreen_ShowBasic(&s_screen, freq_hz, vpp10, vin_mVpp);
}

static void do_set_basic(const HmiEvent *ev)
{
    KnownModelResult km = KnownModel_Calc(ev->freq_hz, ev->target_vpp10);
    s_basic_freq_hz = ev->freq_hz;
    s_basic_vpp10 = ev->target_vpp10;
    basic_show(ev->freq_hz, ev->target_vpp10, km.input_mVpp);
    HmiScreen_SetRunState(&s_screen, HMI_RUN_IDLE);
    reset_local_task();
}

static void do_start_basic(uint32_t freq_hz, uint16_t vpp10)
{
    KnownModelResult km = KnownModel_Calc(freq_hz, vpp10);
    FpgaCtrlResult fr;

    Learn_Abort();
    Emu_Abort();
    WaveOut_Enable(0u); /* explicit: never rebuild a table mid-stream */

    s_basic_freq_hz = freq_hz;
    s_basic_vpp10 = vpp10;
    s_basic_target_mVpp = (uint16_t)(vpp10 * 100u);
    s_basic_started = 1u;

    basic_show(freq_hz, vpp10, km.input_mVpp);
    HmiScreen_SetRunState(&s_screen, HMI_RUN_RUNNING);

    /* primary path: FPGA Basic_four table (device key must sit on state 2) */
    fr = FpgaCtrl_SendBasic4(s_io.fpga_write, freq_hz, vpp10);

    /* aux path: exact same sine on J_OUT - covers the 1700 Hz FPGA bug   */
    if (km.input_mVpp <= CAL_DAC_MAX_MVPP) {
        WaveOut_BuildSine(km.input_mVpp);
        WaveOut_SetFreqMilliHz((uint64_t)freq_hz * 1000u);
        WaveOut_Enable(1u);
    } else {
        WaveOut_Enable(0u);
    }

    if (fr.sent && !fr.code_ok) {
        /* FPGA table bug at this point - tell the operator to use J_OUT  */
        HmiScreen_ShowBasicNote(&s_screen, "1700Hz:USE J_OUT");
    } else {
        HmiScreen_ShowBasicNote(&s_screen, "FPGA+J_OUT");
    }

    start_local_task(APP_OP_BASIC);
}

static void do_start_learn(void)
{
    WaveOut_Enable(0u);
    Emu_Abort();
    s_learn_percent_shown = 0xFFu;
    HmiScreen_Goto(&s_screen, HMI_PAGE_LEARN);
    HmiScreen_SetMode(&s_screen, HMI_MODE_LEARN);
    HmiScreen_ShowLearnProgress(&s_screen, 0u, "SWEEP 1k-50kHz");
    HmiScreen_SetRunState(&s_screen, HMI_RUN_RUNNING);
    Learn_Start(now_ms());
    start_local_task(APP_OP_LEARN);
}

static void do_start_emulate(uint8_t wave_hint)
{
    const RlcModel *model = Learn_Model();

    WaveOut_Enable(0u);
    Learn_Abort();

    s_emu_shown_state = 0xFFu;
    HmiScreen_Goto(&s_screen, HMI_PAGE_EMULATE);
    HmiScreen_SetMode(&s_screen, HMI_MODE_EMULATE);
    HmiScreen_SetWave(&s_screen, wave_hint);
    HmiScreen_SetRunState(&s_screen, HMI_RUN_RUNNING);

    Emu_Start((model != 0 && model->valid) ? model : 0, wave_hint, now_ms());
    start_local_task(APP_OP_EMULATE);
}

static void do_calib_output(const HmiEvent *ev)
{
    uint16_t aux_mVpp;

    Learn_Abort();
    Emu_Abort();
    WaveOut_Enable(0u); /* explicit: never rebuild a table mid-stream */

    HmiScreen_Goto(&s_screen, HMI_PAGE_CALIB);
    HmiScreen_SetMode(&s_screen, HMI_MODE_CALIB);
    HmiScreen_SetWave(&s_screen, ev->wave);
    HmiScreen_SetRunState(&s_screen, HMI_RUN_RUNNING);

    /* FPGA Basic_two path: exact grid only, fixed ~3.5 Vpp amplitude */
    if (FpgaCtrl_Basic2Supported(ev->freq_hz)) {
        (void)FpgaCtrl_SendBasic2Freq(s_io.fpga_write, ev->freq_hz);
    }

    /* STM32 aux path: any amplitude, up to AUX_MAX_FREQ_HZ */
    aux_mVpp = (ev->output_mVpp > CAL_DAC_MAX_MVPP) ? CAL_DAC_MAX_MVPP
                                                    : ev->output_mVpp;
    if (ev->freq_hz <= AUX_MAX_FREQ_HZ && aux_mVpp > 0u) {
        if (ev->wave == HMI_WAVE_SQUARE) {
            WaveOut_BuildSquare(aux_mVpp, 500u);
            HmiScreen_ShowCalibDuty(&s_screen, "50.0 %");
        } else if (ev->wave == HMI_WAVE_OTHER) {
            /* triangle stands in for "other" (FPGA ROM parity) */
            static float tri[64];
            uint32_t i;
            for (i = 0u; i < 64u; ++i) {
                float ph = (float)i / 64.0f;
                float v = (ph < 0.5f) ? (4.0f * ph - 1.0f) : (3.0f - 4.0f * ph);
                tri[i] = 0.5f * (float)aux_mVpp * v;
            }
            WaveOut_LoadWave(tri, 64u);
            HmiScreen_ShowCalibDuty(&s_screen, "TRI");
        } else {
            WaveOut_BuildSine(aux_mVpp);
            HmiScreen_ShowCalibDuty(&s_screen, "SINE");
        }
        WaveOut_SetFreqMilliHz((uint64_t)ev->freq_hz * 1000u);
        WaveOut_Enable(1u);
        HmiScreen_ShowCalibMeasure(&s_screen, aux_mVpp);
    } else if (FpgaCtrl_Basic2Supported(ev->freq_hz)) {
        /* above aux range but on the FPGA grid (1 MHz / 2 MHz points) */
        WaveOut_Enable(0u);
        HmiScreen_ShowCalibMeasure(&s_screen, 3500u);
        HmiScreen_ShowCalibDuty(&s_screen, "FPGA OUT");
    } else {
        /* neither output path can generate this point - say so */
        WaveOut_Enable(0u);
        HmiScreen_ShowCalibMeasure(&s_screen, 0u);
        HmiScreen_ShowCalibDuty(&s_screen, "OUT OF RANGE");
    }

    start_local_task(APP_OP_CALIB);
}

static void do_stop(void)
{
    stop_outputs();
    HmiScreen_SetRunState(&s_screen, HMI_RUN_IDLE);
    reset_local_task();
}

/* ------------------------------------------------------------------ */
/* HMI event dispatch                                                  */
/* ------------------------------------------------------------------ */

static void handle_hmi_event(const HmiEvent *ev)
{
    switch (ev->cmd) {
    case HMI_CMD_SET_BASIC:
        do_set_basic(ev);
        break;
    case HMI_CMD_START_BASIC:
        do_start_basic(ev->freq_hz, ev->target_vpp10);
        break;
    case HMI_CMD_START_LEARN:
        do_start_learn();
        break;
    case HMI_CMD_START_EMULATE:
        do_start_emulate(ev->wave);
        break;
    case HMI_CMD_CALIB_OUTPUT:
        do_calib_output(ev);
        break;
    case HMI_CMD_STOP:
        do_stop();
        break;
    case HMI_CMD_CLEAR_ERROR:
        stop_outputs();
        HmiScreen_Goto(&s_screen, HMI_PAGE_HOME);
        HmiScreen_SetMode(&s_screen, HMI_MODE_HOME);
        HmiScreen_SetRunState(&s_screen, HMI_RUN_IDLE);
        reset_local_task();
        break;
    default:
        enter_error(ERR_BAD_CMD, "BAD CMD");
        break;
    }
}

void GApp_Init(const GAppIo *io)
{
    if (io != 0) {
        s_io = *io;
    } else {
        s_io.hmi_write = 0;
        s_io.fpga_write = 0;
        s_io.get_ms = 0;
        s_io.key_read = 0;
        s_io.led_write = 0;
    }

    reset_local_task();
    s_basic_started = 0u;
    s_basic_target_mVpp = 0u;
    s_learn_percent_shown = 0xFFu;
    s_emu_shown_state = 0xFFu;
    s_key_last = 0u;

    HmiProtocol_Init(&s_hmi_parser);
    HmiScreen_Init(&s_screen, hmi_write_adapter, 0);
    Learn_Init(s_io.fpga_write);
    Emu_Init();

    HmiScreen_Goto(&s_screen, HMI_PAGE_HOME);
    HmiScreen_SetMode(&s_screen, HMI_MODE_HOME);
    HmiScreen_SetRunState(&s_screen, HMI_RUN_IDLE);
}

void GApp_OnHmiRxByte(uint8_t byte)
{
    /* IRQ context: only enqueue */
    uint8_t next = (uint8_t)((s_rx_head + 1u) % RX_RING_LEN);
    if (next != s_rx_tail) {
        s_rx_ring[s_rx_head] = byte;
        s_rx_head = next;
    }
}

static void drain_hmi_rx(void)
{
    while (s_rx_tail != s_rx_head) {
        HmiEvent ev;
        uint8_t byte = s_rx_ring[s_rx_tail];
        HmiParseResult r;

        s_rx_tail = (uint8_t)((s_rx_tail + 1u) % RX_RING_LEN);
        r = HmiProtocol_PushByte(&s_hmi_parser, byte, &ev);
        if (r == HMI_PARSE_FRAME_OK) {
            handle_hmi_event(&ev);
        } else if (r == HMI_PARSE_ERROR) {
            /* A corrupted screen frame must never abort a judged run:
             * the parser has already resynced itself, so while an
             * operation is active we just count the glitch and move on.
             * Only surface the error page when the device is idle.      */
            ++s_hmi_err_count;
            if (s_op == APP_OP_NONE) {
                enter_error((uint16_t)(ERR_HMI_BASE +
                                       HmiProtocol_LastError(&s_hmi_parser)),
                            "HMI FRAME");
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* pollers                                                             */
/* ------------------------------------------------------------------ */

static void poll_keys(uint32_t t)
{
    uint8_t keys, pressed;

    if (s_io.key_read == 0) {
        return;
    }
    if ((uint32_t)(t - s_key_ms) < 30u) { /* debounce window */
        return;
    }
    s_key_ms = t;

    keys = s_io.key_read();
    pressed = (uint8_t)(keys & (uint8_t)~s_key_last);
    s_key_last = keys;

    if (pressed & GAPP_KEY_STOP) {
        do_stop();
    } else if (pressed & GAPP_KEY_LEARN) {
        do_start_learn();               /* the single "learn key" */
    } else if (pressed & GAPP_KEY_START) {
        /* one-key start of the last configured basic setting */
        if (s_op == APP_OP_NONE && s_basic_freq_hz != 0u) {
            do_start_basic(s_basic_freq_hz, s_basic_vpp10);
        }
    }
}

static void poll_learn(uint32_t t)
{
    LearnState st;
    uint8_t pct;

    Learn_Poll(t);
    st = Learn_State();

    if (st == LEARN_RUNNING) {
        pct = Learn_Percent();
        if (pct != s_learn_percent_shown &&
            (uint32_t)(t - s_ui_ms) >= 150u) {
            s_ui_ms = t;
            s_learn_percent_shown = pct;
            HmiScreen_ShowLearnLive(&s_screen, pct, Learn_CurrentFreqHz());
        }
    } else if (st == LEARN_DONE) {
        const RlcModel *m = Learn_Model();
        HmiScreen_ShowLearnResult(&s_screen, type_text_cn(m->type),
                                  (uint16_t)(m->r_ohm + 0.5f),
                                  (uint16_t)(m->l_mh * 1000.0f + 0.5f),
                                  (uint32_t)(m->c_nf * 1000.0f + 0.5f),
                                  0u);
        HmiScreen_SetRunState(&s_screen, HMI_RUN_DONE);
        reset_local_task();
    } else if (st == LEARN_FAILED) {
        enter_error(Learn_ErrCode(),
                    (Learn_ErrCode() == LEARN_ERR_NOT_RESET)
                        ? "RESET FPGA (Develop1)" : "LEARN FAIL");
    }
}

static void poll_emulate(uint32_t t)
{
    EmuState st;

    Emu_Poll(t);
    st = Emu_State();

    if (st == EMU_FAILED) {
        enter_error(Emu_ErrCode(), "EMU INPUT");
        return;
    }

    if ((uint32_t)(t - s_ui_ms) < 400u && st == s_emu_shown_state) {
        return;
    }
    if (st == EMU_RUNNING || st == EMU_NO_MODEL) {
        const SigStats *in = Emu_Input();
        uint32_t f_show = Emu_SnappedFreqHz();
        if (f_show == 0u) {
            f_show = (uint32_t)(in->freq_hz + 0.5f);
        }
        HmiScreen_ShowEmulateInput(&s_screen, f_show,
                                   wave_text_cn(in->wave),
                                   (uint16_t)in->vpp_mv,
                                   SigMeas_SnapDuty5(in->duty_pct10));
        if (st == EMU_RUNNING) {
            const RlcModel *m = Learn_Model();
            HmiScreen_ShowEmulateMeasure(&s_screen, type_text_cn(m->type),
                                         Emu_OutputVpp_mV(), 0u);
            if (s_emu_shown_state != (uint8_t)st) {
                HmiScreen_SetRunState(&s_screen, HMI_RUN_RUNNING);
            }
        } else {
            HmiScreen_ShowEmulateMeasure(&s_screen, T_MODEL_NOT_LEARNED,
                                         0u, 0u);
        }
        s_emu_shown_state = (uint8_t)st;
        s_ui_ms = t;
    }
}

void GApp_Poll(void)
{
    uint32_t t = now_ms();

    drain_hmi_rx();

    /* heartbeat LED */
    if (s_io.led_write != 0 && (uint32_t)(t - s_led_ms) >= 250u) {
        s_led_ms = t;
        s_led_state ^= 1u;
        s_io.led_write(s_led_state);
    }

    poll_keys(t);

    switch (s_op) {
    case APP_OP_BASIC:
        /* open loop by rule: settled state is reported after 1 s */
        if (s_basic_started && (uint32_t)(t - s_op_start_ms) >= 1000u) {
            s_basic_started = 0u;
            HmiScreen_ShowBasicMeasure(&s_screen, s_basic_target_mVpp);
            HmiScreen_SetRunState(&s_screen, HMI_RUN_DONE);
            /* output keeps running until STOP; task bookkeeping ends */
            reset_local_task();
        }
        break;

    case APP_OP_LEARN:
        poll_learn(t);
        break;

    case APP_OP_EMULATE:
        poll_emulate(t);
        break;

    case APP_OP_CALIB:
        if ((uint32_t)(t - s_op_start_ms) >= 300u) {
            HmiScreen_SetRunState(&s_screen, HMI_RUN_DONE);
            reset_local_task();
        }
        break;

    default:
        break;
    }
}
