#include "hmi_screen.h"

#include <stdio.h>
#include <string.h>

#define C_BLACK  0u
#define C_WHITE  65535u
#define C_PANEL  8617u
#define C_BLUE   11263u
#define C_GREEN  11564u
#define C_YELLOW 62951u
#define C_RED    55882u

static void send_cmd(HmiScreen *screen, const char *cmd)
{
    static const uint8_t tail[3] = {0xFFu, 0xFFu, 0xFFu};
    if (screen == 0 || screen->write == 0 || cmd == 0) {
        return;
    }
    screen->write((const uint8_t *)cmd, (uint16_t)strlen(cmd), screen->user);
    screen->write(tail, 3u, screen->user);
}

static void fill_box(HmiScreen *screen, uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h, uint16_t color)
{
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "fill %u,%u,%u,%u,%u", x, y, w, h, color);
    send_cmd(screen, buf);
}

static void draw_label(HmiScreen *screen, uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h, uint16_t pco,
                       uint16_t bco, const char *text)
{
    char buf[192];
    (void)snprintf(buf, sizeof(buf), "xstr %u,%u,%u,%u,1,%u,%u,1,1,1,\"%s\"",
                   x, y, w, h, pco, bco, (text != 0) ? text : "");
    send_cmd(screen, buf);
}

static const char *mode_text(uint8_t mode)
{
    if (mode == (uint8_t)MEASURE_MODE_UA) {
        return "UA";
    }
    if (mode == (uint8_t)MEASURE_MODE_UB) {
        return "UB";
    }
    return "UB+uJ";
}

static void format_mv(char *buf, size_t cap, const char *label, float mv)
{
    if (label != 0 && label[0] != '\0') {
        (void)snprintf(buf, cap, "%s %.1f mV", label, mv);
    } else {
        (void)snprintf(buf, cap, "%.1f mV", mv);
    }
}

static void format_khz(char *buf, size_t cap, const char *label, uint32_t hz)
{
    if (label != 0 && label[0] != '\0') {
        (void)snprintf(buf, cap, "%s %.3f kHz", label, (float)hz / 1000.0f);
    } else {
        (void)snprintf(buf, cap, "%.3f kHz", (float)hz / 1000.0f);
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
    screen->period_mode = 1u;
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
    if (page > HMI_PAGE_SPEC) {
        page = HMI_PAGE_HOME;
    }
    screen->page = page;
    (void)snprintf(buf, sizeof(buf), "page %u", (unsigned)page);
    send_cmd(screen, buf);
}

void HmiScreen_SetPeriod(HmiScreen *screen, uint8_t period_mode)
{
    char buf[32];
    if (screen == 0) {
        return;
    }
    screen->period_mode = (period_mode == 3u) ? 3u : 1u;
    if (screen->page == HMI_PAGE_WAVE) {
        (void)snprintf(buf, sizeof(buf), "Period %u", (unsigned)screen->period_mode);
        draw_label(screen, 760u, 402u, 202u, 50u, C_WHITE, C_PANEL, buf);
    }
}

void HmiScreen_ShowStatusText(HmiScreen *screen, const char *text, uint16_t color)
{
    uint16_t x = (screen != 0 && screen->page == HMI_PAGE_HOME) ? 584u : 760u;
    uint16_t y = (screen != 0 && screen->page == HMI_PAGE_HOME) ? 112u : 462u;
    uint16_t w = (screen != 0 && screen->page == HMI_PAGE_HOME) ? 328u : 202u;
    uint16_t h = (screen != 0 && screen->page == HMI_PAGE_HOME) ? 60u : 44u;
    draw_label(screen, x, y, w, h, color, C_PANEL, text);
}

void HmiScreen_ShowFeatureHome(HmiScreen *screen, const MeasureFeature *feature)
{
    char buf[96];
    if (screen == 0 || feature == 0 || feature->valid == 0u) {
        return;
    }
    (void)snprintf(buf, sizeof(buf), "%s", mode_text(feature->mode));
    draw_label(screen, 224u, 112u, 168u, 60u, C_YELLOW, C_PANEL, buf);
    format_mv(buf, sizeof(buf), "", feature->vpp_mv);
    draw_label(screen, 154u, 274u, 202u, 56u, C_WHITE, C_PANEL, buf);
    format_mv(buf, sizeof(buf), "", feature->urms_mv);
    draw_label(screen, 154u, 352u, 202u, 56u, C_WHITE, C_PANEL, buf);
    format_khz(buf, sizeof(buf), "", feature->f1_hz);
    draw_label(screen, 154u, 430u, 202u, 56u, C_YELLOW, C_PANEL, buf);

    for (uint8_t i = 0u; i < MEASURE_COMPONENT_MAX; ++i) {
        uint16_t y = (uint16_t)(274u + (uint16_t)i * 78u);
        if (i < feature->component_count) {
            (void)snprintf(buf, sizeof(buf), "%.3fkHz %.1fmV",
                           (float)feature->comp[i].freq_hz / 1000.0f,
                           feature->comp[i].amp_peak_mv);
        } else {
            (void)snprintf(buf, sizeof(buf), "-- kHz -- mV");
        }
        draw_label(screen, 520u, y, 424u, 56u, C_WHITE, C_PANEL, buf);
    }
}

void HmiScreen_ShowWaveSummary(HmiScreen *screen, const MeasureFeature *feature,
                               uint8_t period_mode)
{
    char buf[80];
    if (screen == 0 || feature == 0 || feature->valid == 0u) {
        return;
    }
    format_mv(buf, sizeof(buf), "Vpp", feature->vpp_mv);
    draw_label(screen, 760u, 194u, 202u, 54u, C_WHITE, C_PANEL, buf);
    format_mv(buf, sizeof(buf), "Urms", feature->urms_mv);
    draw_label(screen, 760u, 260u, 202u, 54u, C_WHITE, C_PANEL, buf);
    format_khz(buf, sizeof(buf), "f1", feature->f1_hz);
    draw_label(screen, 760u, 326u, 202u, 54u, C_YELLOW, C_PANEL, buf);
    HmiScreen_SetPeriod(screen, period_mode);
}

void HmiScreen_ShowSpectrumComponents(HmiScreen *screen, const MeasureFeature *feature)
{
    char buf[80];
    if (screen == 0 || feature == 0 || feature->valid == 0u) {
        return;
    }
    for (uint8_t i = 0u; i < MEASURE_COMPONENT_MAX; ++i) {
        uint16_t y = (uint16_t)(142u + (uint16_t)i * 76u);
        if (i < feature->component_count) {
            (void)snprintf(buf, sizeof(buf), "%.3fk %.1fmV",
                           (float)feature->comp[i].freq_hz / 1000.0f,
                           feature->comp[i].amp_peak_mv);
        } else {
            (void)snprintf(buf, sizeof(buf), "--");
        }
        draw_label(screen, 804u, y, 158u, 62u, C_WHITE, C_PANEL, buf);
    }
}

void HmiScreen_ClearWave(HmiScreen *screen)
{
    fill_box(screen, 60u, 166u, 646u, 286u, C_BLACK);
}

void HmiScreen_ClearSpectrum(HmiScreen *screen)
{
    fill_box(screen, 60u, 166u, 646u, 286u, C_BLACK);
}

void HmiScreen_DrawLine(HmiScreen *screen, uint16_t x0, uint16_t y0,
                        uint16_t x1, uint16_t y1, uint16_t color)
{
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "line %u,%u,%u,%u,%u", x0, y0, x1, y1, color);
    send_cmd(screen, buf);
}
