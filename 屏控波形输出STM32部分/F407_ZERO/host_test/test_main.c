#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fpga_ctrl.h"
#include "g_app.h"
#include "hmi_protocol.h"

static int g_fail;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        ++g_fail; \
        printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

static uint32_t s_fake_ms;
static uint32_t fake_ms(void)
{
    return s_fake_ms;
}

static uint8_t s_fpga_bytes[256];
static uint16_t s_fpga_len;
static void fpga_write(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0u; i < len && s_fpga_len < sizeof(s_fpga_bytes); ++i) {
        s_fpga_bytes[s_fpga_len++] = data[i];
    }
}

static void hmi_write(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
}

static uint16_t rd_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void feed_frame(const uint8_t *frame, size_t len, HmiParser *parser,
                       HmiEvent *ev, HmiParseResult *last)
{
    size_t i;

    *last = HMI_PARSE_NONE;
    for (i = 0u; i < len; ++i) {
        HmiParseResult r = HmiProtocol_PushByte(parser, frame[i], ev);
        if (r != HMI_PARSE_NONE) {
            *last = r;
        }
    }
}

static void test_hmi_protocol(void)
{
    static const uint8_t default_frame[] = {
        0xAA,0x21,0x16,0x01,0x03,0x00,0xE8,0x03,0x00,0x00,
        0xE8,0x03,0xF4,0x01,0x00,0xE8,0x03,0x00,0x00,0xE8,
        0x03,0xF4,0x01,0x00,0x00,0x7B,0x55
    };
    static uint8_t bad_checksum[] = {
        0xAA,0x21,0x16,0x01,0x03,0x00,0xE8,0x03,0x00,0x00,
        0xE8,0x03,0xF4,0x01,0x00,0xE8,0x03,0x00,0x00,0xE8,
        0x03,0xF4,0x01,0x00,0x00,0x00,0x55
    };
    static uint8_t out_of_range[] = {
        0xAA,0x21,0x16,0x01,0x03,0x03,0xE8,0x03,0x00,0x00,
        0xE8,0x03,0xF4,0x01,0x00,0xE8,0x03,0x00,0x00,0xE8,
        0x03,0xF4,0x01,0x00,0x00,0x7E,0x55
    };
    HmiParser parser;
    HmiEvent ev;
    HmiParseResult r;

    HmiProtocol_Init(&parser);
    feed_frame(default_frame, sizeof(default_frame), &parser, &ev, &r);
    CHECK(r == HMI_PARSE_FRAME_OK, "default HMI frame parses");
    CHECK(ev.cmd == HMI_CMD_OUTPUT_CONFIG, "command is OUTPUT_CONFIG");
    CHECK(ev.output.ch_a.freq_hz == 1000u, "A freq is 1000 Hz");
    CHECK(ev.output.ch_a.amp_mVpp == 1000u, "A amplitude is 1000 mVpp");
    CHECK(ev.output.ch_b.duty_pct10 == 500u, "B duty is 50.0 percent");

    HmiProtocol_Init(&parser);
    feed_frame(bad_checksum, sizeof(bad_checksum), &parser, &ev, &r);
    CHECK(r == HMI_PARSE_ERROR, "bad checksum is rejected");
    CHECK(HmiProtocol_LastError(&parser) == HMI_ERR_BAD_CHECKSUM,
          "bad checksum error code");

    HmiProtocol_Init(&parser);
    feed_frame(out_of_range, sizeof(out_of_range), &parser, &ev, &r);
    CHECK(r == HMI_PARSE_ERROR, "bad wave is rejected");
    CHECK(HmiProtocol_LastError(&parser) == HMI_ERR_OUT_OF_RANGE,
          "out-of-range error code");

    printf("hmi_protocol: parse and validation OK\n");
}

static void test_fpga_frame(void)
{
    DualWaveOutputConfig cfg;
    uint8_t frame[FPGA_DUAL_WAVE_FRAME_LEN];
    size_t len;
    const uint8_t *data = &frame[3];

    cfg.proto_ver = 1u;
    cfg.flags = 0x03u;
    cfg.ch_a.wave = HMI_WAVE_SINE;
    cfg.ch_a.freq_hz = 1000u;
    cfg.ch_a.amp_mVpp = 1000u;
    cfg.ch_a.duty_pct10 = 500u;
    cfg.ch_b.wave = HMI_WAVE_SQUARE;
    cfg.ch_b.freq_hz = 20000000u;
    cfg.ch_b.amp_mVpp = 5000u;
    cfg.ch_b.duty_pct10 = 300u;
    cfg.phase_b_rel_a_deg = -90;

    len = FpgaCtrl_BuildDualWaveFrame(&cfg, frame, sizeof(frame));
    CHECK(len == FPGA_DUAL_WAVE_FRAME_LEN, "FPGA frame length");
    CHECK(frame[0] == FPGA_FRAME_SOF && frame[1] == FPGA_CMD_DUAL_WAVE_CONFIG &&
          frame[2] == FPGA_DUAL_WAVE_DATA_LEN, "FPGA frame header");
    CHECK(frame[sizeof(frame) - 1u] == FPGA_FRAME_EOF, "FPGA frame tail");
    CHECK(frame[sizeof(frame) - 2u] ==
          FpgaCtrl_Checksum(FPGA_CMD_DUAL_WAVE_CONFIG, data, FPGA_DUAL_WAVE_DATA_LEN),
          "FPGA frame checksum");
    CHECK(rd_u32_le(&data[3]) == FpgaCtrl_FwordFromHz(1000u), "A fword");
    CHECK(rd_u16_le(&data[7]) == 1638u, "A amplitude q13");
    CHECK(rd_u32_le(&data[9]) == 0x80000000u, "A duty q32");
    CHECK(rd_u32_le(&data[14]) == FpgaCtrl_FwordFromHz(20000000u), "B fword");
    CHECK(rd_u16_le(&data[18]) == FPGA_AMP_Q13_FULL, "B amplitude q13 full scale");
    CHECK(rd_u32_le(&data[20]) == FpgaCtrl_DutyToQ32(300u), "B duty q32");
    CHECK(rd_u32_le(&data[24]) == 0xC0000000u, "phase -90 deg q32");

    printf("fpga_ctrl: DDS frame packing OK\n");
}

static void test_g_app_flow(void)
{
    static const uint8_t default_frame[] = {
        0xAA,0x21,0x16,0x01,0x03,0x00,0xE8,0x03,0x00,0x00,
        0xE8,0x03,0xF4,0x01,0x00,0xE8,0x03,0x00,0x00,0xE8,
        0x03,0xF4,0x01,0x00,0x00,0x7B,0x55
    };
    GAppIo io = { hmi_write, fpga_write, fake_ms, 0, 0 };
    size_t i;

    s_fpga_len = 0u;
    GApp_Init(&io);
    for (i = 0u; i < sizeof(default_frame); ++i) {
        GApp_OnHmiRxByte(default_frame[i]);
    }
    GApp_Poll();

    CHECK(s_fpga_len == FPGA_DUAL_WAVE_FRAME_LEN, "GApp emits one FPGA frame");
    CHECK(s_fpga_bytes[0] == FPGA_FRAME_SOF, "GApp FPGA frame SOF");
    CHECK(s_fpga_bytes[1] == FPGA_CMD_DUAL_WAVE_CONFIG, "GApp FPGA command");

    printf("g_app: HMI frame forwards to FPGA OK\n");
}

int main(void)
{
    test_hmi_protocol();
    test_fpga_frame();
    test_g_app_flow();

    if (g_fail != 0) {
        printf("\n== %d FAILURE(S) ==\n", g_fail);
        return 1;
    }
    printf("\n== ALL TESTS PASSED ==\n");
    return 0;
}
