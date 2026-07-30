/*
 * File: g_app.c
 * Role: Top-level 2026 G bare-metal application scheduler.
 * Scope: Passive HMI events, FPGA feature polling, page refresh and heartbeat.
 */
#include "g_app.h"

#include "calibration.h"
#include "fpga_link.h"
#include "hmi_protocol.h"
#include "hmi_screen.h"
#include "measure_model.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define RX_RING_LEN 256u
#define FPGA_READ_CHUNK 128u

static GAppIo s_io;
static HmiParser s_hmi_parser;
static HmiScreen s_screen;
static FpgaLink s_fpga;

static volatile uint8_t s_rx_ring[RX_RING_LEN];
static volatile uint8_t s_rx_head;
static volatile uint8_t s_rx_tail;
static volatile uint8_t s_rx_overflow_pending;
static uint32_t s_rx_overflow_count;

static HmiPage s_page;
static uint8_t s_period_mode;
static HmiRuntimeState s_runtime_state;
static uint8_t s_runtime_state_valid;
static uint8_t s_key_last;
static uint32_t s_key_ms;
static uint32_t s_led_ms;
static uint8_t s_led_state;
static uint32_t s_app_init_ms;
static uint32_t s_fpga_poll_ms;
static uint32_t s_refresh_ms;
static uint32_t s_last_feature_version;
static uint32_t s_last_fpga_overflows;

static uint16_t hmi_write_adapter(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    if (s_io.hmi_write != 0) {
        return s_io.hmi_write(data, len);
    }
    return 0u;
}

static uint32_t now_ms(void)
{
    return (s_io.get_ms != 0) ? s_io.get_ms() : 0u;
}

static HmiPage page_for_view(HmiView view)
{
    if (view == HMI_VIEW_WAVE) {
        return HMI_PAGE_WAVE;
    }
    if (view == HMI_VIEW_SPEC) {
        return HMI_PAGE_SPEC;
    }
    return HMI_PAGE_HOME;
}

static void show_page_status(const char *text, uint16_t color)
{
    char cmd[96];
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;

    if (s_page == HMI_PAGE_WAVE) {
        x = 760u;
        y = 462u;
        w = 202u;
        h = 44u;
    } else if (s_page == HMI_PAGE_SPEC) {
        x = 760u;
        y = 452u;
        w = 202u;
        h = 54u;
    } else {
        x = 584u;
        y = 112u;
        w = 328u;
        h = 60u;
    }

    (void)snprintf(cmd, sizeof(cmd),
                   "xstr %u,%u,%u,%u,1,%u,8617,1,1,1,\"%s\"",
                   x, y, w, h, color, (text != 0) ? text : "");
    HmiScreen_SendRaw(&s_screen, cmd);
}

static void set_view(HmiView view)
{
    s_page = page_for_view(view);
    HmiScreen_Goto(&s_screen, s_page);
    s_refresh_ms = 0u;
    s_last_feature_version = 0u;
    s_runtime_state_valid = 0u;
}

static void set_period(uint8_t period)
{
    s_period_mode = (period == 3u) ? 3u : 1u;
    HmiScreen_SetPeriod(&s_screen, s_period_mode);
    s_refresh_ms = 0u;
    s_last_feature_version = 0u;
}

static void toggle_period(void)
{
    set_period((s_period_mode == 1u) ? 3u : 1u);
}

static void handle_hmi_event(const HmiEvent *ev)
{
    if (ev == 0) {
        return;
    }

    switch (ev->cmd) {
    case HMI_CMD_SET_PERIOD:
        set_period(ev->period_mode);
        break;

    case HMI_CMD_SET_VIEW:
        set_view(ev->view);
        break;

    default:
        show_page_status("BAD HMI CMD", 55882u);
        break;
    }
}

static void drain_hmi_rx(void)
{
    if (s_rx_overflow_pending != 0u) {
        s_rx_tail = s_rx_head;
        s_rx_overflow_pending = 0u;
        s_rx_overflow_count++;
        HmiProtocol_Init(&s_hmi_parser);
        show_page_status("HMI RX FULL", 55882u);
    }

    while (s_rx_tail != s_rx_head) {
        HmiEvent ev;
        uint8_t byte = s_rx_ring[s_rx_tail];
        HmiParseResult r;

        s_rx_tail = (uint8_t)((s_rx_tail + 1u) % RX_RING_LEN);
        r = HmiProtocol_PushByte(&s_hmi_parser, byte, &ev);
        if (r == HMI_PARSE_FRAME_OK) {
            handle_hmi_event(&ev);
        } else if (r == HMI_PARSE_ERROR) {
            show_page_status("HMI FRAME ERR", 55882u);
        }
    }
}

static void poll_fpga_rx(void)
{
    uint8_t buf[FPGA_READ_CHUNK];
    uint16_t n;

    if (s_io.fpga_read == 0) {
        return;
    }
    if (s_io.fpga_overflows != 0) {
        uint32_t overflows = s_io.fpga_overflows();
        if (overflows != s_last_fpga_overflows) {
            s_last_fpga_overflows = overflows;
            FpgaLink_ResetParser(&s_fpga);
            show_page_status("FPGA RX DROP", 55882u);
        }
    }
    do {
        n = s_io.fpga_read(buf, sizeof(buf));
        if (n != 0u) {
            FpgaLink_ProcessBytes(&s_fpga, buf, n);
        }
    } while (n == sizeof(buf));
}

static void poll_keys(uint32_t t)
{
    uint8_t keys;
    uint8_t pressed;

    if (s_io.key_read == 0) {
        return;
    }
    if ((uint32_t)(t - s_key_ms) < 30u) {
        return;
    }
    s_key_ms = t;

    keys = s_io.key_read();
    pressed = (uint8_t)(keys & (uint8_t)~s_key_last);
    s_key_last = keys;

    if ((pressed & GAPP_KEY_PERIOD) != 0u) {
        toggle_period();
    }
}

static HmiRuntimeState resolve_runtime_state(uint32_t t)
{
    const FpgaLinkStats *stats = FpgaLink_GetStats(&s_fpga);
    const MeasureFeature *feature = MeasureModel_Feature();

    if (stats != 0 && stats->online == 0u &&
        (stats->packets_ok != 0u ||
         (uint32_t)(t - s_app_init_ms) > 5000u)) {
        return HMI_RUNTIME_COMM_ERR;
    }
    if (stats != 0 && stats->crc_error_active != 0u &&
        stats->feature_online == 0u) {
        return HMI_RUNTIME_CRC_ERR;
    }
    if (feature->valid == 0u ||
        feature->status == (uint8_t)MEASURE_STATUS_WAIT) {
        return HMI_RUNTIME_WAIT;
    }
    if (feature->status == (uint8_t)MEASURE_STATUS_HOLD ||
        (uint32_t)(t - feature->last_update_ms) > 2000u) {
        return HMI_RUNTIME_HOLD;
    }
    if (feature->status == (uint8_t)MEASURE_STATUS_OVER_RANGE) {
        return HMI_RUNTIME_OVER_RANGE;
    }
    if (feature->status == (uint8_t)MEASURE_STATUS_LINK_OR_ALGO_ERROR) {
        return HMI_RUNTIME_ALGO_ERR;
    }
    return HMI_RUNTIME_LIVE;
}

static void refresh_page(uint32_t t)
{
    const MeasureFeature *feature = MeasureModel_Feature();
    HmiRuntimeState state = resolve_runtime_state(t);

    if (HmiScreen_Busy(&s_screen) != 0u) {
        return;
    }

    if (s_last_feature_version != feature->version ||
        s_runtime_state_valid == 0u ||
        state != s_runtime_state ||
        (uint32_t)(t - s_refresh_ms) >= 500u) {
        s_refresh_ms = t;
        s_last_feature_version = feature->version;
        s_runtime_state = state;
        s_runtime_state_valid = 1u;

        if (s_page == HMI_PAGE_HOME) {
            HmiScreen_ShowHome(&s_screen, feature, state);
        } else if (s_page == HMI_PAGE_WAVE) {
            HmiScreen_DrawWave(&s_screen, feature, s_period_mode, state);
        } else {
            HmiScreen_DrawSpectrum(&s_screen, feature, state);
        }
    }
}

void GApp_Init(const GAppIo *io)
{
    if (io != 0) {
        s_io = *io;
    } else {
        memset(&s_io, 0, sizeof(s_io));
    }

    Calibration_Init();
    MeasureModel_Init();
    HmiProtocol_Init(&s_hmi_parser);
    HmiScreen_Init(&s_screen, hmi_write_adapter, 0);
    FpgaLink_Init(&s_fpga, s_io.fpga_write, s_io.get_ms);

    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_rx_overflow_pending = 0u;
    s_rx_overflow_count = 0u;
    s_page = HMI_PAGE_HOME;
    s_period_mode = 1u;
    s_runtime_state = HMI_RUNTIME_WAIT;
    s_runtime_state_valid = 0u;
    s_key_last = 0u;
    s_led_state = 0u;
    s_key_ms = 0u;
    s_led_ms = 0u;
    s_app_init_ms = now_ms();
    s_fpga_poll_ms = 0u;
    s_refresh_ms = 0u;
    s_last_feature_version = 0u;
    s_last_fpga_overflows =
        (s_io.fpga_overflows != 0) ? s_io.fpga_overflows() : 0u;

    HmiScreen_Goto(&s_screen, HMI_PAGE_HOME);
}

void GApp_OnHmiRxByte(uint8_t byte)
{
    uint8_t next = (uint8_t)((s_rx_head + 1u) % RX_RING_LEN);
    if (next != s_rx_tail) {
        s_rx_ring[s_rx_head] = byte;
        s_rx_head = next;
    } else {
        s_rx_overflow_pending = 1u;
    }
}

void GApp_Poll(void)
{
    uint32_t t = now_ms();

    HmiScreen_Poll(&s_screen, 512u);
    drain_hmi_rx();

    if ((uint32_t)(t - s_fpga_poll_ms) >= 1u) {
        s_fpga_poll_ms = t;
        poll_fpga_rx();
    }
    FpgaLink_Poll(&s_fpga, t);

    if (s_io.led_write != 0 && (uint32_t)(t - s_led_ms) >= 250u) {
        s_led_ms = t;
        s_led_state ^= 1u;
        s_io.led_write(s_led_state);
    }

    poll_keys(t);
    refresh_page(t);
    HmiScreen_Poll(&s_screen, 512u);
}
