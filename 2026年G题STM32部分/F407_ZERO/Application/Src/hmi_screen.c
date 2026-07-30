/*
 * File: hmi_screen.c
 * Role: Implements TJC/Nextion screen drawing for the 2026 G UI.
 * Scope: Passive feature text, synthesized waveform and line spectrum.
 */
#include "hmi_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define C_BLACK  0u
#define C_WHITE  65535u
#define C_PANEL  6471u
#define C_FIELD  8617u
#define C_GRID   16904u
#define C_BLUE   11263u
#define C_CYAN   7671u
#define C_GREEN  11564u
#define C_YELLOW 62951u
#define C_RED    55882u

#define HMI_FONT_DEFAULT 1u
#define HMI_FONT_COMPACT 2u
#define HMI_FONT_TINY    3u

#define WAVE_X 60u
#define WAVE_Y 166u
#define WAVE_W 646u
#define WAVE_H 286u

#define SPEC_X 60u
#define SPEC_Y 166u
#define SPEC_W 646u
#define SPEC_H 286u

#define RIGHT_X 760u
#define RIGHT_W 202u

#define G_PI 3.14159265358979323846f

static uint16_t queue_free(const HmiScreen *screen)
{
    if (screen->tx_head >= screen->tx_tail) {
        return (uint16_t)(HMI_SCREEN_TX_QUEUE_LEN -
                          (screen->tx_head - screen->tx_tail) - 1u);
    }
    return (uint16_t)(screen->tx_tail - screen->tx_head - 1u);
}

static void queue_byte(HmiScreen *screen, uint8_t byte)
{
    screen->tx_queue[screen->tx_head] = byte;
    screen->tx_head =
        (uint16_t)((screen->tx_head + 1u) % HMI_SCREEN_TX_QUEUE_LEN);
}

static void send_cmd(HmiScreen *screen, const char *cmd)
{
    uint16_t len;

    if (screen == 0 || screen->write == 0 || cmd == 0) {
        return;
    }
    len = (uint16_t)strlen(cmd);
    if (queue_free(screen) < (uint16_t)(len + 3u)) {
        screen->queue_overflows++;
        return;
    }
    for (uint16_t i = 0u; i < len; ++i) {
        queue_byte(screen, (uint8_t)cmd[i]);
    }
    queue_byte(screen, 0xFFu);
    queue_byte(screen, 0xFFu);
    queue_byte(screen, 0xFFu);
}

static void fill_box(HmiScreen *screen, uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h, uint16_t color)
{
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "fill %u,%u,%u,%u,%u", x, y, w, h, color);
    send_cmd(screen, buf);
}

static void draw_line(HmiScreen *screen, uint16_t x1, uint16_t y1,
                      uint16_t x2, uint16_t y2, uint16_t color)
{
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "line %u,%u,%u,%u,%u",
                   x1, y1, x2, y2, color);
    send_cmd(screen, buf);
}

static void draw_label_font(HmiScreen *screen, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h, uint8_t font_id,
                            uint16_t pco, uint16_t bco, const char *text)
{
    char buf[192];
    (void)snprintf(buf, sizeof(buf),
                   "xstr %u,%u,%u,%u,%u,%u,%u,1,1,1,\"%s\"",
                   x, y, w, h, font_id, pco, bco,
                   (text != 0) ? text : "");
    send_cmd(screen, buf);
}

static void draw_label(HmiScreen *screen, uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h, uint16_t pco,
                       uint16_t bco, const char *text)
{
    draw_label_font(screen, x, y, w, h, HMI_FONT_DEFAULT,
                    pco, bco, text);
}

static void draw_button(HmiScreen *screen, uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h, const char *text,
                        uint8_t active)
{
    uint16_t bco = active ? C_BLUE : C_PANEL;
    fill_box(screen, x, y, w, h, bco);
    draw_label(screen, x, y, w, h, C_WHITE, bco, text);
}

static const char *mode_text(const MeasureFeature *feature)
{
    if (feature == 0 || feature->valid == 0u) {
        return "--";
    }
    if (feature->mode == (uint8_t)MEASURE_MODE_UA) {
        return "UA";
    }
    if (feature->mode == (uint8_t)MEASURE_MODE_UB) {
        return "UB";
    }
    return "UB+uJ";
}

static const char *runtime_state_text(HmiRuntimeState state)
{
    switch (state) {
    case HMI_RUNTIME_LIVE:
        return "LIVE";
    case HMI_RUNTIME_HOLD:
        return "HOLD";
    case HMI_RUNTIME_COMM_ERR:
        return "COMM ERR";
    case HMI_RUNTIME_CRC_ERR:
        return "CRC ERR";
    case HMI_RUNTIME_OVER_RANGE:
        return "OVER RANGE";
    case HMI_RUNTIME_ALGO_ERR:
        return "ALGO ERR";
    case HMI_RUNTIME_WAIT:
    default:
        return "WAIT DATA";
    }
}

static uint16_t runtime_state_color(HmiRuntimeState state)
{
    if (state == HMI_RUNTIME_LIVE) {
        return C_GREEN;
    }
    if (state == HMI_RUNTIME_COMM_ERR ||
        state == HMI_RUNTIME_CRC_ERR ||
        state == HMI_RUNTIME_OVER_RANGE ||
        state == HMI_RUNTIME_ALGO_ERR) {
        return C_RED;
    }
    return C_YELLOW;
}

static uint8_t runtime_state_holds_values(HmiRuntimeState state)
{
    return (state == HMI_RUNTIME_HOLD ||
            state == HMI_RUNTIME_COMM_ERR ||
            state == HMI_RUNTIME_CRC_ERR) ? 1u : 0u;
}

static void draw_grid(HmiScreen *screen, uint16_t x, uint16_t y,
                      uint16_t w, uint16_t h)
{
    fill_box(screen, x, y, w, h, C_BLACK);
    for (uint8_t i = 0u; i <= 10u; ++i) {
        uint16_t gx = (uint16_t)(x + ((uint32_t)w * i) / 10u);
        draw_line(screen, gx, y, gx, (uint16_t)(y + h), C_GRID);
    }
    for (uint8_t i = 0u; i <= 6u; ++i) {
        uint16_t gy = (uint16_t)(y + ((uint32_t)h * i) / 6u);
        draw_line(screen, x, gy, (uint16_t)(x + w), gy, C_GRID);
    }
    draw_line(screen, x, (uint16_t)(y + h / 2u),
              (uint16_t)(x + w), (uint16_t)(y + h / 2u), C_CYAN);
}

static float abs_f(float v)
{
    return (v < 0.0f) ? -v : v;
}

static uint16_t clamp_i32_u16(int32_t v)
{
    if (v < 0) {
        return 0u;
    }
    if (v > 65535) {
        return 65535u;
    }
    return (uint16_t)v;
}

static uint16_t wave_y_from_mv(float mv, float scale_mv)
{
    float n;
    int32_t y;

    if (scale_mv < 1.0f) {
        scale_mv = 1.0f;
    }
    n = mv / scale_mv;
    if (n > 1.0f) {
        n = 1.0f;
    } else if (n < -1.0f) {
        n = -1.0f;
    }
    y = (int32_t)WAVE_Y + (int32_t)(WAVE_H / 2u) -
        (int32_t)(n * (float)(WAVE_H / 2u - 8u));
    return clamp_i32_u16(y);
}

static void draw_status_box(HmiScreen *screen, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h,
                            HmiRuntimeState state)
{
    uint16_t color = runtime_state_color(state);
    fill_box(screen, x, y, w, h, C_FIELD);
    draw_label(screen, x, y, w, h, color, C_FIELD,
               runtime_state_text(state));
}

static void format_metric(char *buf, size_t cap, const char *name,
                          float value, const char *unit)
{
    const char *label = (name != 0) ? name : "";

    if (abs_f(value) >= 1000.0f) {
        if (label[0] == '\0') {
            (void)snprintf(buf, cap, "%.2f %s", value / 1000.0f,
                           (unit[0] == 'm') ? "V" : unit);
        } else {
            (void)snprintf(buf, cap, "%s %.2f %s", label, value / 1000.0f,
                           (unit[0] == 'm') ? "V" : unit);
        }
    } else {
        if (label[0] == '\0') {
            (void)snprintf(buf, cap, "%.1f %s", value, unit);
        } else {
            (void)snprintf(buf, cap, "%s %.1f %s", label, value, unit);
        }
    }
}

static void draw_feature_summary(HmiScreen *screen, const MeasureFeature *feature,
                                 HmiRuntimeState state)
{
    char buf[96];
    uint16_t color =
        runtime_state_holds_values(state) ? C_YELLOW : C_WHITE;

    if (feature == 0 || feature->valid == 0u) {
        draw_label(screen, RIGHT_X, 194u, RIGHT_W, 54u, C_YELLOW, C_FIELD, "Vpp --");
        draw_label(screen, RIGHT_X, 260u, RIGHT_W, 54u, C_YELLOW, C_FIELD, "Urms --");
        draw_label(screen, RIGHT_X, 326u, RIGHT_W, 54u, C_YELLOW, C_FIELD, "f1 --");
        return;
    }

    format_metric(buf, sizeof(buf), "Vpp", feature->vpp_mv, "mV");
    draw_label(screen, RIGHT_X, 194u, RIGHT_W, 54u, color, C_FIELD, buf);
    format_metric(buf, sizeof(buf), "Urms", feature->urms_mv, "mV");
    draw_label(screen, RIGHT_X, 260u, RIGHT_W, 54u, color, C_FIELD, buf);
    (void)snprintf(buf, sizeof(buf), "f1 %.3f kHz",
                   (float)feature->f1_hz / 1000.0f);
    draw_label(screen, RIGHT_X, 326u, RIGHT_W, 54u, C_YELLOW, C_FIELD, buf);
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
    screen->tx_head = 0u;
    screen->tx_tail = 0u;
    screen->queue_overflows = 0u;
}

void HmiScreen_Poll(HmiScreen *screen, uint16_t budget)
{
    while (screen != 0 && screen->write != 0 &&
           screen->tx_tail != screen->tx_head && budget != 0u) {
        uint16_t contiguous;
        uint16_t written;

        if (screen->tx_head > screen->tx_tail) {
            contiguous = (uint16_t)(screen->tx_head - screen->tx_tail);
        } else {
            contiguous = (uint16_t)(HMI_SCREEN_TX_QUEUE_LEN -
                                    screen->tx_tail);
        }
        if (contiguous > budget) {
            contiguous = budget;
        }
        written = screen->write(&screen->tx_queue[screen->tx_tail],
                                contiguous, screen->user);
        if (written == 0u) {
            break;
        }
        if (written > contiguous) {
            written = contiguous;
        }
        screen->tx_tail =
            (uint16_t)((screen->tx_tail + written) %
                       HMI_SCREEN_TX_QUEUE_LEN);
        budget = (uint16_t)(budget - written);
    }
}

uint8_t HmiScreen_Busy(const HmiScreen *screen)
{
    return (screen != 0 && screen->tx_head != screen->tx_tail) ? 1u : 0u;
}

uint32_t HmiScreen_QueueOverflows(const HmiScreen *screen)
{
    return (screen != 0) ? screen->queue_overflows : 0u;
}

void HmiScreen_SendRaw(HmiScreen *screen, const char *cmd)
{
    send_cmd(screen, cmd);
}

void HmiScreen_DrawLayout(HmiScreen *screen)
{
    if (screen == 0) {
        return;
    }
    if (screen->page == HMI_PAGE_WAVE) {
        HmiScreen_SetPeriod(screen, screen->period_mode);
    }
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
    HmiScreen_DrawLayout(screen);
}

void HmiScreen_SetPeriod(HmiScreen *screen, uint8_t period_mode)
{
    char buf[32];

    if (screen == 0) {
        return;
    }
    screen->period_mode = (period_mode == 3u) ? 3u : 1u;
    if (screen->page == HMI_PAGE_WAVE) {
        draw_button(screen, RIGHT_X, 116u, 104u, 48u, "1P",
                    screen->period_mode == 1u);
        draw_button(screen, 878u, 116u, 84u, 48u, "3P",
                    screen->period_mode == 3u);
        (void)snprintf(buf, sizeof(buf), "Period %u", (unsigned)screen->period_mode);
        draw_label(screen, RIGHT_X, 402u, RIGHT_W, 50u, C_WHITE, C_FIELD, buf);
    }
}

void HmiScreen_ShowHome(HmiScreen *screen, const MeasureFeature *feature,
                        HmiRuntimeState state)
{
    char buf[128];
    uint16_t value_color =
        runtime_state_holds_values(state) ? C_YELLOW : C_WHITE;

    if (screen == 0 || screen->page != HMI_PAGE_HOME) {
        return;
    }

    (void)snprintf(buf, sizeof(buf), "%s", mode_text(feature));
    draw_label(screen, 224u, 112u, 168u, 60u, C_YELLOW, C_FIELD, buf);
    draw_status_box(screen, 584u, 112u, 328u, 60u, state);

    if (feature != 0 && feature->valid != 0u) {
        format_metric(buf, sizeof(buf), "", feature->vpp_mv, "mV");
        draw_label(screen, 154u, 274u, 202u, 56u, value_color, C_FIELD, buf);
        format_metric(buf, sizeof(buf), "", feature->urms_mv, "mV");
        draw_label(screen, 154u, 352u, 202u, 56u, value_color, C_FIELD, buf);
        (void)snprintf(buf, sizeof(buf), "%.3f kHz",
                       (float)feature->f1_hz / 1000.0f);
        draw_label(screen, 154u, 430u, 202u, 56u, C_YELLOW, C_FIELD, buf);

        for (uint8_t i = 0u; i < MEASURE_COMPONENT_MAX; ++i) {
            uint16_t y = (uint16_t)(274u + (uint16_t)i * 78u);
            if (i < feature->component_count) {
                (void)snprintf(buf, sizeof(buf), "%.3fkHz      %.1fmV",
                               (float)feature->comp[i].freq_hz / 1000.0f,
                               feature->comp[i].amp_peak_mv);
            } else {
                (void)snprintf(buf, sizeof(buf), "-- kHz        -- mV");
            }
            draw_label(screen, 520u, y, 424u, 56u, value_color, C_FIELD, buf);
        }

        if (feature->mode == (uint8_t)MEASURE_MODE_UB_J &&
            (feature->flags & MEASURE_FEATURE_FLAG_INTERFERENCE_SUPPRESSED) != 0u) {
            draw_label(screen, 512u, 488u, 430u, 24u, C_GREEN, C_FIELD,
                       "uJ suppressed, UB shown");
        } else {
            draw_label(screen, 512u, 488u, 430u, 24u, C_CYAN, C_FIELD,
                       "FPGA selects algorithm");
        }
    } else {
        draw_label(screen, 154u, 274u, 202u, 56u, C_YELLOW, C_FIELD, "-- mV");
        draw_label(screen, 154u, 352u, 202u, 56u, C_YELLOW, C_FIELD, "-- mV");
        draw_label(screen, 154u, 430u, 202u, 56u, C_YELLOW, C_FIELD, "-- kHz");
        draw_label(screen, 520u, 274u, 424u, 56u, C_YELLOW, C_FIELD, "-- kHz        -- mV");
        draw_label(screen, 520u, 352u, 424u, 56u, C_YELLOW, C_FIELD, "-- kHz        -- mV");
        draw_label(screen, 520u, 430u, 424u, 56u, C_YELLOW, C_FIELD, "-- kHz        -- mV");
        draw_label(screen, 512u, 488u, 430u, 24u, C_CYAN, C_FIELD,
                   "FPGA selects algorithm");
    }
}

void HmiScreen_DrawWave(HmiScreen *screen, const MeasureFeature *feature,
                        uint8_t period_mode, HmiRuntimeState state)
{
    enum {
        MIN_INTERVALS = 120u,
        SAMPLES_PER_HIGHEST_CYCLE = 4u
    };
    uint16_t prev_x = 0u;
    uint16_t prev_y = 0u;
    uint16_t intervals;
    float scale_mv = 0.0f;
    uint32_t max_freq_hz = 0u;
    uint8_t periods;

    if (screen == 0 || screen->page != HMI_PAGE_WAVE) {
        return;
    }

    HmiScreen_SetPeriod(screen, period_mode);
    draw_grid(screen, WAVE_X, WAVE_Y, WAVE_W, WAVE_H);
    draw_feature_summary(screen, feature, state);
    draw_status_box(screen, RIGHT_X, 462u, RIGHT_W, 44u, state);

    if (feature == 0 || feature->valid == 0u ||
        feature->component_count == 0u || feature->f1_hz == 0u) {
        draw_label(screen, (uint16_t)(WAVE_X + 220u), (uint16_t)(WAVE_Y + 140u),
                   220u, 42u, C_YELLOW, C_BLACK, "WAIT FEATURE");
        return;
    }

    if (feature->component_count > 1u &&
        (feature->flags & MEASURE_FEATURE_FLAG_PHASE_VALID) == 0u) {
        draw_label(screen, (uint16_t)(WAVE_X + 198u), (uint16_t)(WAVE_Y + 140u),
                   250u, 42u, C_YELLOW, C_BLACK, "PHASE REQUIRED");
        return;
    }

    for (uint8_t c = 0u; c < feature->component_count; ++c) {
        scale_mv += abs_f(feature->comp[c].amp_peak_mv);
        if (feature->comp[c].freq_hz > max_freq_hz) {
            max_freq_hz = feature->comp[c].freq_hz;
        }
    }
    if (scale_mv < abs_f(feature->vpp_mv) * 0.5f) {
        scale_mv = abs_f(feature->vpp_mv) * 0.5f;
    }
    if (scale_mv < 1.0f) {
        scale_mv = 1.0f;
    }

    periods = (period_mode == 3u) ? 3u : 1u;
    {
        uint64_t numerator =
            (uint64_t)SAMPLES_PER_HIGHEST_CYCLE * (uint64_t)periods *
            (uint64_t)max_freq_hz;
        uint64_t needed =
            (numerator + (uint64_t)feature->f1_hz - 1u) /
            (uint64_t)feature->f1_hz;

        if (needed < MIN_INTERVALS) {
            needed = MIN_INTERVALS;
        }
        if (needed > WAVE_W) {
            needed = WAVE_W;
        }
        intervals = (uint16_t)needed;
    }

    for (uint16_t i = 0u; i <= intervals; ++i) {
        float cycles = ((float)periods * (float)i) / (float)intervals;
        float sample_mv = 0.0f;
        uint16_t x = (uint16_t)(WAVE_X + ((uint32_t)WAVE_W * i) /
                                (uint32_t)intervals);
        uint16_t y;

        for (uint8_t c = 0u; c < feature->component_count; ++c) {
            float ratio = (float)feature->comp[c].freq_hz / (float)feature->f1_hz;
            float phase_rad =
                ((float)feature->comp[c].phase_deg10 * G_PI) / 1800.0f;
            float angle = (2.0f * G_PI * ratio * cycles) + phase_rad;
            sample_mv += feature->comp[c].amp_peak_mv * sinf(angle);
        }

        y = wave_y_from_mv(sample_mv, scale_mv);
        if (i != 0u) {
            draw_line(screen, prev_x, prev_y, x, y, C_GREEN);
        }
        prev_x = x;
        prev_y = y;
    }
}

void HmiScreen_DrawSpectrum(HmiScreen *screen, const MeasureFeature *feature,
                            HmiRuntimeState state)
{
    char freq_buf[48];
    char amp_buf[48];
    float max_amp = 1.0f;
    uint16_t value_color =
        runtime_state_holds_values(state) ? C_YELLOW : C_WHITE;

    if (screen == 0 || screen->page != HMI_PAGE_SPEC) {
        return;
    }

    draw_grid(screen, SPEC_X, SPEC_Y, SPEC_W, SPEC_H);
    if (feature == 0 || feature->valid == 0u || feature->component_count == 0u) {
        draw_label(screen, (uint16_t)(SPEC_X + 220u), (uint16_t)(SPEC_Y + 140u),
                   220u, 42u, C_YELLOW, C_BLACK, "WAIT FEATURE");
    } else {
        for (uint8_t i = 0u; i < feature->component_count; ++i) {
            float amp = abs_f(feature->comp[i].amp_peak_mv);
            if (amp > max_amp) {
                max_amp = amp;
            }
        }
        for (uint8_t i = 0u; i < feature->component_count; ++i) {
            uint32_t f = feature->comp[i].freq_hz;
            float amp = abs_f(feature->comp[i].amp_peak_mv);
            uint16_t x;
            uint16_t h;
            uint16_t top;

            if (f > 500000u) {
                f = 500000u;
            }
            x = (uint16_t)(SPEC_X + ((uint32_t)SPEC_W * f) / 500000u);
            h = (uint16_t)((amp * (float)(SPEC_H - 12u)) / max_amp);
            top = (uint16_t)(SPEC_Y + SPEC_H - h);
            draw_line(screen, x, (uint16_t)(SPEC_Y + SPEC_H), x, top, C_YELLOW);
            if (x > SPEC_X) {
                draw_line(screen, (uint16_t)(x - 1u), (uint16_t)(SPEC_Y + SPEC_H),
                          (uint16_t)(x - 1u), top, C_YELLOW);
            }
        }
    }

    for (uint8_t i = 0u; i < MEASURE_COMPONENT_MAX; ++i) {
        uint16_t y = (uint16_t)(172u + (uint16_t)i * 80u);
        if (feature != 0 && feature->valid != 0u && i < feature->component_count) {
            (void)snprintf(freq_buf, sizeof(freq_buf), "%.3f kHz",
                           (float)feature->comp[i].freq_hz / 1000.0f);
            (void)snprintf(amp_buf, sizeof(amp_buf), "%.1f mV",
                           feature->comp[i].amp_peak_mv);
        } else {
            (void)snprintf(freq_buf, sizeof(freq_buf), "-- kHz");
            (void)snprintf(amp_buf, sizeof(amp_buf), "-- mV");
        }
        draw_label_font(screen, 804u, y, 158u, 31u, HMI_FONT_COMPACT,
                        value_color, C_FIELD, freq_buf);
        draw_label_font(screen, 804u, (uint16_t)(y + 31u), 158u, 31u,
                        HMI_FONT_COMPACT, value_color, C_FIELD, amp_buf);
    }
    draw_status_box(screen, RIGHT_X, 452u, RIGHT_W, 54u, state);
}
