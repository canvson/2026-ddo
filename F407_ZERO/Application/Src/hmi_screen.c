#include "hmi_screen.h"

#include <stdio.h>
#include <string.h>

#define C_BLACK  0u
#define C_WHITE  65535u
#define C_PANEL  6471u
#define C_PANEL2 8617u
#define C_BLUE   11263u
#define C_CYAN   7671u
#define C_GREEN  11564u
#define C_YELLOW 62951u
#define C_RED    55882u

#define T_HOME          "\xCA\xD7\xD2\xB3"
#define T_BASIC         "\xBB\xF9\xB1\xBE"
#define T_LEARN         "\xD1\xA7\xCF\xB0"
#define T_EMULATE       "\xB5\xC8\xD0\xA7"
#define T_WAVE          "\xB2\xA8\xD0\xCE"
#define T_RESULT        "\xBD\xE1\xB9\xFB"
#define T_BASIC_CTRL    "\xBB\xF9\xB1\xBE\xBF\xD8\xD6\xC6"
#define T_LEARN_MODEL   "\xD1\xA7\xCF\xB0\xBD\xA8\xC4\xA3"
#define T_EMUL_OUT      "\xB5\xC8\xD0\xA7\xCA\xE4\xB3\xF6"
#define T_WAVE_SETTING  "\xB2\xA8\xD0\xCE\xC9\xE8\xB6\xA8"
#define T_SINE          "\xD5\xFD\xCF\xD2"
#define T_SQUARE        "\xBE\xD8\xD0\xCE"
#define T_OTHER         "\xC6\xE4\xCB\xFB"
#define T_IDLE          "\xB4\xFD\xBB\xFA"
#define T_RUNNING       "\xD4\xCB\xD0\xD0\xD6\xD0"
#define T_DONE          "\xCD\xEA\xB3\xC9"
#define T_ERROR         "\xB4\xED\xCE\xF3"
#define T_PROGRESS      "\xBD\xF8\xB6\xC8"
#define T_MODEL_UNKNOWN "\xC4\xA3\xD0\xCD:\xCE\xB4\xD6\xAA"
#define T_OUTPUT        "\xCA\xE4\xB3\xF6"
#define T_ERR_DELTA     "\xCE\xF3\xB2\xEE"
#define T_LOCAL_VALUE   "LOCAL"
#define T_UNKNOWN_ERROR "\xCE\xB4\xD6\xAA\xB4\xED\xCE\xF3"
#define T_CLEAR_ERROR   "\xC7\xE5\xB4\xED"
#define T_CURRENT       "\xB5\xB1\xC7\xB0"
#define T_STATUS        "\xD7\xB4\xCC\xAC"

static void send_cmd(HmiScreen *screen, const char *cmd)
{
    static const uint8_t tail[3] = {0xFFu, 0xFFu, 0xFFu};
    if (screen == 0 || screen->write == 0 || cmd == 0) {
        return;
    }
    screen->write((const uint8_t *)cmd, (uint16_t)strlen(cmd), screen->user);
    screen->write(tail, 3u, screen->user);
}

static void fill_box(HmiScreen *screen, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     uint16_t color)
{
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "fill %u,%u,%u,%u,%u", x, y, w, h, color);
    send_cmd(screen, buf);
}

static void draw_label(HmiScreen *screen, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       uint16_t pco, uint16_t bco, const char *text)
{
    char buf[160];
    (void)snprintf(buf, sizeof(buf), "xstr %u,%u,%u,%u,1,%u,%u,1,1,1,\"%s\"",
                   x, y, w, h, pco, bco, text);
    send_cmd(screen, buf);
}

static void draw_button(HmiScreen *screen, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const char *text, uint8_t active, uint16_t active_color)
{
    uint16_t bco = active ? active_color : C_PANEL2;
    fill_box(screen, x, y, w, h, bco);
    draw_label(screen, x, y, w, h, C_WHITE, bco, text);
}

static void redraw_footer_modes(HmiScreen *screen)
{
    static const uint16_t x0[6] = {32u, 192u, 352u, 512u, 672u, 832u};
    static const char *names[6] = {T_HOME, T_BASIC, T_LEARN, T_EMULATE, T_WAVE, T_RESULT};
    for (uint8_t i = 0u; i < 6u; ++i) {
        draw_button(screen, x0[i], 528u, 140u, 52u, names[i], screen->mode == i, C_BLUE);
    }
}

void HmiScreen_Init(HmiScreen *screen, HmiScreenWriteFn write, void *user)
{
    if (screen == 0) {
        return;
    }
    screen->write = write;
    screen->user = user;
    screen->page = HMI_PAGE_HOME;
    screen->mode = HMI_MODE_HOME;
    screen->wave = 0u;
    screen->run_state = HMI_RUN_IDLE;
}

void HmiScreen_SendRaw(HmiScreen *screen, const char *cmd)
{
    send_cmd(screen, cmd);
}

void HmiScreen_Goto(HmiScreen *screen, HmiPage page)
{
    char buf[24];
    if (screen == 0) {
        return;
    }
    screen->page = (uint8_t)page;
    (void)snprintf(buf, sizeof(buf), "page %u", (uint8_t)page);
    send_cmd(screen, buf);
}

void HmiScreen_SetMode(HmiScreen *screen, HmiMode mode)
{
    if (screen == 0) {
        return;
    }
    screen->mode = (uint8_t)mode;

    if (screen->page == HMI_PAGE_HOME) {
        draw_button(screen, 84u, 172u, 364u, 106u, T_BASIC_CTRL, mode == HMI_MODE_BASIC, C_BLUE);
        draw_button(screen, 576u, 172u, 364u, 106u, T_LEARN_MODEL, mode == HMI_MODE_LEARN, C_CYAN);
        draw_button(screen, 84u, 326u, 364u, 106u, T_EMUL_OUT, mode == HMI_MODE_EMULATE, C_GREEN);
        draw_button(screen, 576u, 326u, 364u, 106u, T_WAVE_SETTING, mode == HMI_MODE_CALIB, C_YELLOW);
    }
    redraw_footer_modes(screen);
}

void HmiScreen_SetWave(HmiScreen *screen, uint8_t wave)
{
    if (screen == 0) {
        return;
    }
    if (wave > 2u) {
        wave = 0u;
    }
    screen->wave = wave;

    if (screen->page == HMI_PAGE_EMULATE) {
        draw_button(screen, 78u, 170u, 112u, 70u, T_SINE, wave == 0u, C_BLUE);
        draw_button(screen, 214u, 170u, 112u, 70u, T_SQUARE, wave == 1u, C_CYAN);
        draw_button(screen, 350u, 170u, 112u, 70u, T_OTHER, wave == 2u, C_YELLOW);
    } else if (screen->page == HMI_PAGE_CALIB) {
        draw_button(screen, 84u, 166u, 146u, 62u, T_SINE, wave == 0u, C_BLUE);
        draw_button(screen, 254u, 166u, 146u, 62u, T_SQUARE, wave == 1u, C_CYAN);
        draw_button(screen, 424u, 166u, 146u, 62u, T_OTHER, wave == 2u, C_YELLOW);
    }
}

void HmiScreen_SetRunState(HmiScreen *screen, HmiRunState state)
{
    static const char *texts[4] = {T_IDLE, T_RUNNING, T_DONE, T_ERROR};
    static const uint16_t colors[4] = {C_PANEL2, C_GREEN, C_BLUE, C_RED};
    char buf[80];
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;

    if (screen == 0) {
        return;
    }
    if (state > HMI_RUN_ERROR) {
        state = HMI_RUN_ERROR;
    }
    screen->run_state = (uint8_t)state;

    if (screen->page == HMI_PAGE_HOME) {
        x = 84u;
        y = 458u;
        w = 856u;
        h = 36u;
        (void)snprintf(buf, sizeof(buf), T_CURRENT ":" T_HOME "    " T_STATUS ":%s", texts[state]);
        fill_box(screen, x, y, w, h, C_PANEL2);
        draw_label(screen, x, y, w, h, C_WHITE, C_PANEL2, buf);
        return;
    } else if (screen->page == HMI_PAGE_BASIC) {
        x = 680u;
        y = 272u;
        w = 262u;
        h = 70u;
    } else if (screen->page == HMI_PAGE_LEARN) {
        x = 744u;
        y = 326u;
        w = 174u;
        h = 38u;
    } else if (screen->page == HMI_PAGE_EMULATE) {
        x = 530u;
        y = 250u;
        w = 388u;
        h = 70u;
    } else if (screen->page == HMI_PAGE_CALIB) {
        x = 704u;
        y = 350u;
        w = 238u;
        h = 66u;
    } else {
        return;
    }

    fill_box(screen, x, y, w, h, colors[state]);
    draw_label(screen, x, y, w, h, C_WHITE, colors[state], texts[state]);
}

void HmiScreen_ShowBasic(HmiScreen *screen, uint32_t freq_hz,
                         uint16_t target_vpp10, uint16_t vin_mVpp)
{
    char buf[64];
    if (screen == 0) {
        return;
    }
    (void)snprintf(buf, sizeof(buf), "%lu Hz", (unsigned long)freq_hz);
    draw_label(screen, 272u, 174u, 276u, 56u, C_WHITE, C_BLACK, buf);

    (void)snprintf(buf, sizeof(buf), "%u.%u Vpp",
                   (unsigned)(target_vpp10 / 10u), (unsigned)(target_vpp10 % 10u));
    draw_label(screen, 272u, 258u, 276u, 56u, C_WHITE, C_BLACK, buf);

    (void)snprintf(buf, sizeof(buf), "%u.%03u Vpp",
                   (unsigned)(vin_mVpp / 1000u), (unsigned)(vin_mVpp % 1000u));
    draw_label(screen, 272u, 342u, 276u, 56u, C_YELLOW, C_PANEL2, buf);
}

void HmiScreen_ShowBasicMeasure(HmiScreen *screen, uint16_t vout_mVpp)
{
    char buf[64];
    if (screen == 0) {
        return;
    }
    (void)snprintf(buf, sizeof(buf), "%u.%03u Vpp",
                   (unsigned)(vout_mVpp / 1000u), (unsigned)(vout_mVpp % 1000u));
    draw_label(screen, 272u, 426u, 276u, 56u, C_WHITE, C_PANEL2, buf);
}

void HmiScreen_ShowLearnProgress(HmiScreen *screen, uint8_t percent, const char *type_text)
{
    char buf[64];
    uint16_t bar_w;
    if (screen == 0) {
        return;
    }
    if (percent > 100u) {
        percent = 100u;
    }
    bar_w = (uint16_t)((388u * percent) / 100u);
    fill_box(screen, 530u, 260u, 388u, 44u, C_BLACK);
    if (bar_w > 0u) {
        fill_box(screen, 530u, 260u, bar_w, 44u, C_GREEN);
    }

    (void)snprintf(buf, sizeof(buf), T_PROGRESS ":%u%%", (unsigned)percent);
    draw_label(screen, 530u, 326u, 180u, 38u, C_WHITE, C_PANEL, buf);
    if (type_text != 0) {
        draw_label(screen, 530u, 380u, 330u, 38u, C_YELLOW, C_PANEL, type_text);
    }
}

void HmiScreen_ShowLearnResult(HmiScreen *screen, const char *type_text,
                               uint16_t r_ohm, uint16_t l_uH, uint32_t c_pF,
                               uint16_t err_code)
{
    char buf[96];
    if (screen == 0) {
        return;
    }
    HmiScreen_ShowLearnProgress(screen, 100u, type_text);

    (void)snprintf(buf, sizeof(buf), "R:%u Ohm  L:%u uH",
                   (unsigned)r_ohm, (unsigned)l_uH);
    draw_label(screen, 530u, 424u, 370u, 34u, C_WHITE, C_PANEL, buf);

    (void)snprintf(buf, sizeof(buf), "C:%lu pF  ERR:%u",
                   (unsigned long)c_pF, (unsigned)err_code);
    draw_label(screen, 530u, 462u, 370u, 34u, C_WHITE, C_PANEL, buf);
}

void HmiScreen_ShowEmulateMeasure(HmiScreen *screen, const char *model_text,
                                  uint16_t output_mVpp, uint16_t err_mVpp)
{
    char buf[96];
    if (screen == 0) {
        return;
    }
    if (model_text == 0) {
        model_text = T_MODEL_UNKNOWN;
    }
    draw_label(screen, 600u, 358u, 250u, 34u, C_YELLOW, C_PANEL, model_text);

    (void)snprintf(buf, sizeof(buf), T_OUTPUT ":%u.%03u Vpp",
                   (unsigned)(output_mVpp / 1000u), (unsigned)(output_mVpp % 1000u));
    draw_label(screen, 600u, 400u, 250u, 34u, C_WHITE, C_PANEL, buf);

    (void)snprintf(buf, sizeof(buf), T_ERR_DELTA ":%u mV", (unsigned)err_mVpp);
    draw_label(screen, 600u, 442u, 250u, 34u, C_WHITE, C_PANEL, buf);
}

void HmiScreen_ShowCalibMeasure(HmiScreen *screen, uint16_t output_mVpp)
{
    char buf[64];
    if (screen == 0) {
        return;
    }
    (void)snprintf(buf, sizeof(buf), T_LOCAL_VALUE ":%u.%03u Vpp",
                   (unsigned)(output_mVpp / 1000u), (unsigned)(output_mVpp % 1000u));
    draw_label(screen, 704u, 420u, 238u, 66u, C_WHITE, C_BLACK, buf);
}

void HmiScreen_ShowBasicNote(HmiScreen *screen, const char *text)
{
    if (screen == 0 || text == 0) {
        return;
    }
    /* note strip under the run-state block on page1 (680,386,262,40) */
    fill_box(screen, 680u, 386u, 262u, 40u, C_PANEL2);
    draw_label(screen, 680u, 386u, 262u, 40u, C_YELLOW, C_PANEL2, text);
}

void HmiScreen_ShowEmulateInput(HmiScreen *screen, uint32_t freq_hz,
                                const char *wave_text, uint16_t vpp_mV,
                                uint16_t duty_pct10)
{
    char buf[80];
    if (screen == 0) {
        return;
    }
    if (wave_text == 0) {
        wave_text = "--";
    }
    (void)snprintf(buf, sizeof(buf), "%lu Hz %s",
                   (unsigned long)freq_hz, wave_text);
    fill_box(screen, 210u, 284u, 202u, 56u, C_BLACK);
    draw_label(screen, 210u, 284u, 202u, 56u, C_WHITE, C_BLACK, buf);

    (void)snprintf(buf, sizeof(buf), "%u.%03u Vpp",
                   (unsigned)(vpp_mV / 1000u), (unsigned)(vpp_mV % 1000u));
    fill_box(screen, 210u, 350u, 202u, 56u, C_BLACK);
    draw_label(screen, 210u, 350u, 202u, 56u, C_WHITE, C_BLACK, buf);

    (void)snprintf(buf, sizeof(buf), "%u.%u %%",
                   (unsigned)(duty_pct10 / 10u), (unsigned)(duty_pct10 % 10u));
    fill_box(screen, 210u, 416u, 202u, 56u, C_BLACK);
    draw_label(screen, 210u, 416u, 202u, 56u, C_WHITE, C_BLACK, buf);
}

void HmiScreen_ShowLearnLive(HmiScreen *screen, uint8_t percent,
                             uint32_t freq_hz)
{
    char buf[48];
    (void)snprintf(buf, sizeof(buf), "f=%lu.%lukHz",
                   (unsigned long)(freq_hz / 1000u),
                   (unsigned long)((freq_hz % 1000u) / 100u));
    HmiScreen_ShowLearnProgress(screen, percent, buf);
}

void HmiScreen_ShowCalibDuty(HmiScreen *screen, const char *text)
{
    if (screen == 0 || text == 0) {
        return;
    }
    fill_box(screen, 276u, 434u, 284u, 56u, C_BLACK);
    draw_label(screen, 276u, 434u, 284u, 56u, C_WHITE, C_BLACK, text);
}

void HmiScreen_ShowResult(HmiScreen *screen, const char *line1, const char *line2,
                          const char *line3)
{
    if (screen == 0) {
        return;
    }
    if (line1 != 0) {
        draw_label(screen, 740u, 172u, 200u, 34u, C_WHITE, C_PANEL, line1);
    }
    if (line2 != 0) {
        draw_label(screen, 740u, 226u, 200u, 34u, C_WHITE, C_PANEL, line2);
    }
    if (line3 != 0) {
        draw_label(screen, 740u, 334u, 200u, 34u, C_YELLOW, C_PANEL, line3);
    }
}

void HmiScreen_ShowError(HmiScreen *screen, uint16_t code, const char *message)
{
    char buf[96];
    if (screen == 0) {
        return;
    }
    HmiScreen_Goto(screen, HMI_PAGE_ERROR);
    screen->run_state = HMI_RUN_ERROR;

    (void)snprintf(buf, sizeof(buf), "ERR 0x%04X", (unsigned)code);
    draw_label(screen, 146u, 248u, 480u, 48u, C_YELLOW, 0x4210u, buf);

    if (message == 0) {
        message = T_UNKNOWN_ERROR;
    }
    draw_label(screen, 146u, 314u, 600u, 42u, C_WHITE, 0x4210u, message);
    draw_button(screen, 710u, 408u, 160u, 60u, T_CLEAR_ERROR, 1u, C_GREEN);
}
