#include "g_app.h"

#include "fpga_ctrl.h"
#include "hmi_protocol.h"

typedef enum {
    APP_IDLE = 0,
    APP_OUTPUT_ACTIVE,
} AppState;

#define RX_RING_LEN 128u

static HmiParser s_hmi_parser;
static GAppIo s_io;
static volatile uint8_t s_rx_ring[RX_RING_LEN];
static volatile uint8_t s_rx_head;
static volatile uint8_t s_rx_tail;
static DualWaveOutputConfig s_last_cfg;
static uint8_t s_has_last_cfg;
static AppState s_state;
static uint32_t s_led_ms;
static uint32_t s_key_ms;
static uint8_t s_led_state;
static uint8_t s_key_last;
static uint16_t s_parse_errors;
static uint16_t s_frames_ok;

static uint32_t now_ms(void)
{
    return (s_io.get_ms != 0) ? s_io.get_ms() : 0u;
}

static void set_led(uint8_t on)
{
    if (s_io.led_write != 0) {
        s_io.led_write(on);
    }
}

static void apply_config(const DualWaveOutputConfig *cfg)
{
    if (FpgaCtrl_SendDualWaveConfig(s_io.fpga_write, cfg) != 0u) {
        s_last_cfg = *cfg;
        s_has_last_cfg = 1u;
        s_state = ((cfg->flags & 0x03u) != 0u) ? APP_OUTPUT_ACTIVE : APP_IDLE;
        ++s_frames_ok;
    }
}

static void stop_output(void)
{
    if (FpgaCtrl_SendDisabled(s_io.fpga_write, s_has_last_cfg ? &s_last_cfg : 0) != 0u) {
        s_state = APP_IDLE;
    }
}

static void poll_keys(uint32_t t)
{
    uint8_t keys;
    uint8_t pressed;

    if (s_io.key_read == 0 || (uint32_t)(t - s_key_ms) < 30u) {
        return;
    }
    s_key_ms = t;

    keys = s_io.key_read();
    pressed = (uint8_t)(keys & (uint8_t)~s_key_last);
    s_key_last = keys;

    if ((pressed & GAPP_KEY_STOP) != 0u) {
        stop_output();
    } else if ((pressed & GAPP_KEY_START) != 0u && s_has_last_cfg != 0u) {
        apply_config(&s_last_cfg);
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
            apply_config(&ev.output);
        } else if (r == HMI_PARSE_ERROR) {
            ++s_parse_errors;
        }
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

    (void)s_io.hmi_write;
    HmiProtocol_Init(&s_hmi_parser);
    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_has_last_cfg = 0u;
    s_state = APP_IDLE;
    s_led_ms = now_ms();
    s_key_ms = s_led_ms;
    s_led_state = 0u;
    s_key_last = 0u;
    s_parse_errors = 0u;
    s_frames_ok = 0u;
    set_led(0u);
}

void GApp_OnHmiRxByte(uint8_t byte)
{
    uint8_t next = (uint8_t)((s_rx_head + 1u) % RX_RING_LEN);

    if (next != s_rx_tail) {
        s_rx_ring[s_rx_head] = byte;
        s_rx_head = next;
    } else {
        ++s_parse_errors;
    }
}

void GApp_Poll(void)
{
    uint32_t t = now_ms();

    drain_hmi_rx();
    poll_keys(t);

    if ((uint32_t)(t - s_led_ms) >= ((s_state == APP_OUTPUT_ACTIVE) ? 100u : 500u)) {
        s_led_ms = t;
        s_led_state ^= 1u;
        set_led(s_led_state);
    }
}
