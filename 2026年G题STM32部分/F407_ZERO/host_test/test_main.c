/*
 * Host verification harness for the 2026 G STM32 application layer.
 *
 * It compiles only portable modules and verifies:
 *   - FPGA CRC16/Modbus, frame builder and streaming parser recovery
 *   - FEATURE/STATUS decoding and missing-phase waveform rejection
 *   - passive FPGA link behavior with optional ping only
 *   - HMI AA..55 period/view parsing and g_app passive display refresh
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "calibration.h"
#include "fpga_link.h"
#include "fpga_packet.h"
#include "g_app.h"
#include "hmi_protocol.h"
#include "hmi_screen.h"
#include "measure_model.h"
#include "uart_ring_math.h"

static int g_fail;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        g_fail++; \
        printf("FAIL %s:%d  ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static uint32_t s_now_ms;
static uint8_t s_fpga_tx[65536];
static uint32_t s_fpga_tx_n;
static uint8_t s_fpga_rx[700000];
static uint32_t s_fpga_rx_n;
static uint32_t s_fpga_rx_pos;
static char s_hmi_text[262144];
static uint32_t s_hmi_n;
static uint8_t s_hmi_backpressure;
static uint16_t s_hmi_accept_budget;
static uint32_t s_fpga_overflow_fake;
static uint8_t s_keys;
static uint8_t s_led;

static uint32_t fake_ms(void)
{
    return s_now_ms;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static void put_i16(uint8_t *p, int16_t v)
{
    put_u16(p, (uint16_t)v);
}

static void put_i32(uint8_t *p, int32_t v)
{
    put_u32(p, (uint32_t)v);
}

static int near_f(float a, float b, float tol)
{
    float d = a - b;
    if (d < 0.0f) {
        d = -d;
    }
    return d <= tol;
}

static uint32_t count_text(const char *text, const char *needle)
{
    uint32_t count = 0u;
    size_t needle_len = strlen(needle);

    if (needle_len == 0u) {
        return 0u;
    }
    while ((text = strstr(text, needle)) != 0) {
        count++;
        text += needle_len;
    }
    return count;
}

static void reset_io(void)
{
    s_fpga_tx_n = 0u;
    s_fpga_rx_n = 0u;
    s_fpga_rx_pos = 0u;
    s_hmi_n = 0u;
    s_hmi_text[0] = '\0';
    s_hmi_backpressure = 0u;
    s_hmi_accept_budget = 0u;
    s_fpga_overflow_fake = 0u;
    s_keys = 0u;
    s_led = 0u;
}

static void fpga_write(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0u; i < len && s_fpga_tx_n < sizeof(s_fpga_tx); ++i) {
        s_fpga_tx[s_fpga_tx_n++] = data[i];
    }
}

static uint16_t fpga_read(uint8_t *data, uint16_t cap)
{
    uint16_t n = 0u;

    while (n < cap && s_fpga_rx_pos < s_fpga_rx_n) {
        data[n++] = s_fpga_rx[s_fpga_rx_pos++];
    }
    return n;
}

static uint16_t hmi_write(const uint8_t *data, uint16_t len)
{
    uint16_t accepted = len;

    if (s_hmi_backpressure != 0u && accepted > s_hmi_accept_budget) {
        accepted = s_hmi_accept_budget;
    }

    for (uint16_t i = 0u; i < accepted && s_hmi_n < sizeof(s_hmi_text) - 1u; ++i) {
        s_hmi_text[s_hmi_n++] = (char)((data[i] == 0xFFu) ? '|' : data[i]);
    }
    s_hmi_text[s_hmi_n] = '\0';
    if (s_hmi_backpressure != 0u) {
        s_hmi_accept_budget = (uint16_t)(s_hmi_accept_budget - accepted);
    }
    return accepted;
}

static uint32_t fpga_overflows(void)
{
    return s_fpga_overflow_fake;
}

static uint16_t screen_write(const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    return hmi_write(data, len);
}

static uint8_t key_read(void)
{
    return s_keys;
}

static void led_write(uint8_t on)
{
    s_led = on;
}

static size_t parse_fpga_frames(const uint8_t *data, uint32_t len,
                                FpgaPacket *frames, size_t cap)
{
    FpgaPacketParser parser;
    FpgaPacket pkt;
    size_t count = 0u;

    FpgaPacket_Init(&parser);
    for (uint32_t i = 0u; i < len; ++i) {
        if (FpgaPacket_PushByte(&parser, data[i], &pkt) == FPGA_PARSE_FRAME_OK) {
            if (count < cap) {
                frames[count] = pkt;
            }
            count++;
        }
    }
    return count;
}

static void feed_hmi_frame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t frame[16];
    size_t n = HmiProtocol_BuildFrame(cmd, data, len, frame, sizeof(frame));

    CHECK(n != 0u, "build hmi frame cmd=0x%02X", cmd);
    for (size_t i = 0u; i < n; ++i) {
        GApp_OnHmiRxByte(frame[i]);
    }
}

static void append_rx_frame(uint8_t type, uint8_t seq,
                            const uint8_t *payload, uint16_t len,
                            uint8_t corrupt_crc)
{
    uint8_t frame[FPGA_PACKET_MAX_FRAME];
    size_t n = FpgaPacket_BuildFrame(type, seq, payload, len, frame, sizeof(frame));

    CHECK(n != 0u, "build fpga frame type=0x%02X len=%u", type, len);
    if (corrupt_crc != 0u && n > 0u) {
        frame[n - 1u] ^= 0x55u;
    }
    for (size_t i = 0u; i < n && s_fpga_rx_n < sizeof(s_fpga_rx); ++i) {
        s_fpga_rx[s_fpga_rx_n++] = frame[i];
    }
}

static void fill_feature_payload(uint8_t *p, uint32_t id)
{
    memset(p, 0, MEASURE_FEATURE_PAYLOAD_LEN);
    put_u32(&p[0], id);
    p[4] = (uint8_t)MEASURE_MODE_UB_J;
    p[5] = (uint8_t)MEASURE_STATUS_VALID;
    p[6] = 3u;
    p[7] = MEASURE_FEATURE_FLAG_INTERFERENCE_SUPPRESSED |
           MEASURE_FEATURE_FLAG_PHASE_VALID;
    put_i32(&p[8], 210000);
    put_i32(&p[12], 74246);
    put_u32(&p[16], 50000u);
    put_u32(&p[24], 50000u);
    put_i32(&p[28], 90000);
    put_i16(&p[32], 0);
    put_u32(&p[36], 100000u);
    put_i32(&p[40], 28000);
    put_i16(&p[44], 900);
    put_u32(&p[48], 150000u);
    put_i32(&p[52], 12000);
    put_i16(&p[56], -450);
}

static void fill_status_payload(uint8_t *p, uint32_t id)
{
    memset(p, 0, 12u);
    put_u32(&p[0], id);
    p[4] = 1u;
    p[5] = 0u;
    put_u16(&p[6], 7u);
    put_u16(&p[8], 9u);
}

static void test_crc_and_parser(void)
{
    static const uint8_t text[] = "123456789";
    uint8_t payload[3] = { 1u, 2u, 3u };
    uint8_t frame[32];
    uint8_t bad_len[6] = {
        FPGA_PACKET_HEAD0, FPGA_PACKET_HEAD1, FPGA_PKT_FEATURE, 0u,
        (uint8_t)((FPGA_PACKET_MAX_PAYLOAD + 1u) & 0xFFu),
        (uint8_t)((FPGA_PACKET_MAX_PAYLOAD + 1u) >> 8)
    };
    FpgaPacketParser parser;
    FpgaPacket out;
    FpgaParseResult r = FPGA_PARSE_NONE;
    size_t n;

    CHECK(FpgaPacket_Crc16(text, 9u) == 0x4B37u, "modbus crc known vector");

    n = FpgaPacket_BuildFrame(FPGA_PKT_STATUS, 0x12u, payload, 3u,
                              frame, sizeof(frame));
    CHECK(n == 11u, "frame length %u", (unsigned)n);
    CHECK(frame[0] == FPGA_PACKET_HEAD0 && frame[1] == FPGA_PACKET_HEAD1,
          "frame header");
    CHECK(((uint16_t)frame[n - 2u] | ((uint16_t)frame[n - 1u] << 8)) ==
          FpgaPacket_Crc16(frame, (uint16_t)(n - 2u)), "frame crc");

    FpgaPacket_Init(&parser);
    (void)FpgaPacket_PushByte(&parser, 0x00u, &out);
    (void)FpgaPacket_PushByte(&parser, FPGA_PACKET_HEAD0, &out);
    r = FpgaPacket_PushByte(&parser, 0x00u, &out);
    CHECK(r == FPGA_PARSE_ERROR, "bad head reports error");
    for (size_t i = 0u; i < n; ++i) {
        r = FpgaPacket_PushByte(&parser, frame[i], &out);
    }
    CHECK(r == FPGA_PARSE_FRAME_OK, "valid frame parsed after noise");
    CHECK(out.type == FPGA_PKT_STATUS && out.seq == 0x12u && out.len == 3u,
          "parsed frame fields");

    FpgaPacket_Init(&parser);
    for (size_t i = 0u; i < sizeof(bad_len); ++i) {
        r = FpgaPacket_PushByte(&parser, bad_len[i], &out);
    }
    CHECK(r == FPGA_PARSE_ERROR && parser.len_errors == 1u,
          "oversized payload rejected");

    frame[n - 1u] ^= 0x10u;
    FpgaPacket_Init(&parser);
    for (size_t i = 0u; i < n; ++i) {
        r = FpgaPacket_PushByte(&parser, frame[i], &out);
    }
    CHECK(r == FPGA_PARSE_ERROR && parser.crc_errors == 1u,
          "crc error detected");

    printf("fpga_packet: CRC, builder, parser recovery OK\n");
}

static void test_uart_ring_math(void)
{
    uint8_t overflowed;
    uint32_t consumer;

    consumer = UartRing_RetainedConsumer(4096u, 0u, 4096u, &overflowed);
    CHECK(consumer == 0u && overflowed == 0u,
          "exactly one DMA window retained");
    consumer = UartRing_RetainedConsumer(8192u, 4096u, 4096u, &overflowed);
    CHECK(consumer == 4096u && overflowed == 0u,
          "second DMA window retained");
    consumer = UartRing_RetainedConsumer(9000u, 0u, 4096u, &overflowed);
    CHECK(consumer == 4904u && overflowed == 1u,
          "multi-wrap overflow keeps newest DMA window");

    printf("uart_ring: wrap and overflow window math OK\n");
}

static void test_hmi_backpressure_queue(void)
{
    static HmiScreen screen;
    MeasureFeature feature;

    reset_io();
    Calibration_Init();
    MeasureModel_Init();
    memset(&feature, 0, sizeof(feature));
    feature.valid = 1u;
    feature.mode = (uint8_t)MEASURE_MODE_UB_J;
    feature.status = (uint8_t)MEASURE_STATUS_VALID;
    feature.component_count = 3u;
    feature.flags = MEASURE_FEATURE_FLAG_PHASE_VALID;
    feature.vpp_mv = 210.0f;
    feature.urms_mv = 74.2f;
    feature.f1_hz = 50000u;
    feature.comp[0].freq_hz = 50000u;
    feature.comp[0].amp_peak_mv = 90.0f;
    feature.comp[1].freq_hz = 100000u;
    feature.comp[1].amp_peak_mv = 28.0f;
    feature.comp[1].phase_deg10 = 900;
    feature.comp[2].freq_hz = 150000u;
    feature.comp[2].amp_peak_mv = 12.0f;
    feature.comp[2].phase_deg10 = -450;

    HmiScreen_Init(&screen, screen_write, 0);
    screen.page = HMI_PAGE_WAVE;
    HmiScreen_DrawWave(&screen, &feature, 3u, HMI_RUNTIME_LIVE);
    CHECK(HmiScreen_Busy(&screen) != 0u &&
          HmiScreen_QueueOverflows(&screen) == 0u,
          "synthesized wave fits nonblocking HMI queue");

    s_hmi_backpressure = 1u;
    for (uint16_t turn = 0u; turn < 1000u &&
                            HmiScreen_Busy(&screen) != 0u; ++turn) {
        s_hmi_accept_budget = 23u;
        HmiScreen_Poll(&screen, 128u);
    }
    CHECK(HmiScreen_Busy(&screen) == 0u,
          "HMI queue drains under repeated partial writes");
    CHECK(strstr(s_hmi_text, "line ") != 0 && strstr(s_hmi_text, "|||") != 0,
          "queued drawing commands remain framed");

    reset_io();
    HmiScreen_Init(&screen, screen_write, 0);
    screen.page = HMI_PAGE_SPEC;
    HmiScreen_DrawSpectrum(&screen, &feature, HMI_RUNTIME_LIVE);
    HmiScreen_Poll(&screen, 20000u);
    CHECK(strstr(s_hmi_text, "xstr 804,172,158,31,2,") != 0 &&
          strstr(s_hmi_text, "50.000 kHz") != 0 &&
          strstr(s_hmi_text, "90.0 mV") != 0 &&
          strstr(s_hmi_text, "C1 50.000") == 0 &&
          strstr(s_hmi_text, "line ") != 0,
          "line spectrum uses ID2 two-line component fields");

    reset_io();
    HmiScreen_Init(&screen, screen_write, 0);
    screen.page = HMI_PAGE_WAVE;
    feature.flags = 0u;
    HmiScreen_DrawWave(&screen, &feature, 3u, HMI_RUNTIME_LIVE);
    HmiScreen_Poll(&screen, 32767u);
    CHECK(strstr(s_hmi_text, "PHASE REQUIRED") != 0 &&
          count_text(s_hmi_text, ",11564|||") == 0u,
          "multi-component wave is not fabricated without valid phase");

    reset_io();
    HmiScreen_Init(&screen, screen_write, 0);
    screen.page = HMI_PAGE_WAVE;
    feature.component_count = 1u;
    HmiScreen_DrawWave(&screen, &feature, 3u, HMI_RUNTIME_LIVE);
    HmiScreen_Poll(&screen, 32767u);
    CHECK(strstr(s_hmi_text, "PHASE REQUIRED") == 0 &&
          count_text(s_hmi_text, ",11564|||") == 120u,
          "single-component wave permits a zero-degree time reference");

    reset_io();
    HmiScreen_Init(&screen, screen_write, 0);
    screen.page = HMI_PAGE_WAVE;
    feature.component_count = 3u;
    feature.flags = MEASURE_FEATURE_FLAG_PHASE_VALID;
    feature.f1_hz = 10000u;
    feature.comp[0].freq_hz = 10000u;
    feature.comp[1].freq_hz = 500000u;
    feature.comp[2].freq_hz = 30000u;
    HmiScreen_DrawWave(&screen, &feature, 3u, HMI_RUNTIME_LIVE);
    CHECK(HmiScreen_QueueOverflows(&screen) == 0u,
          "10k/500kHz three-period wave fits HMI queue");
    HmiScreen_Poll(&screen, 32767u);
    CHECK(HmiScreen_Busy(&screen) == 0u &&
          count_text(s_hmi_text, ",11564|||") == 600u,
          "highest legal harmonic uses 600 adaptive line intervals");
    CHECK(s_hmi_n <= 23040u,
          "worst wave command stream fits 2s at 115200 baud: %lu bytes",
          (unsigned long)s_hmi_n);

    printf("hmi_queue: adaptive wave, phase gate and spectrum drawing OK\n");
}

static void test_measure_model(void)
{
    uint8_t feature[MEASURE_FEATURE_PAYLOAD_LEN];
    uint8_t status[12];
    const MeasureFeature *f;
    const MeasureFpgaStatus *st;
    uint32_t version;

    Calibration_Init();
    MeasureModel_Init();
    fill_feature_payload(feature, 7u);
    CHECK(MeasureModel_UpdateFeaturePayload(feature, sizeof(feature), 100u),
          "feature payload accepted");
    f = MeasureModel_Feature();
    CHECK(f->valid && f->frame_id == 7u, "feature id");
    CHECK(f->mode == (uint8_t)MEASURE_MODE_UB_J &&
          f->status == (uint8_t)MEASURE_STATUS_VALID &&
          f->component_count == 3u, "feature metadata");
    CHECK(near_f(f->vpp_mv, 210.0f, 0.01f), "vpp %.3f", f->vpp_mv);
    CHECK(near_f(f->urms_mv, 74.246f, 0.01f), "urms %.3f", f->urms_mv);
    CHECK(f->f1_hz == 50000u && f->comp[1].freq_hz == 100000u,
          "frequency decode");
    CHECK(near_f(f->comp[0].amp_peak_mv, 90.0f, 0.01f) &&
          f->comp[1].phase_deg10 == 900 &&
          f->comp[2].phase_deg10 == -450, "component peak and phase");

    version = f->version;
    feature[7] &= (uint8_t)~MEASURE_FEATURE_FLAG_PHASE_VALID;
    CHECK(MeasureModel_UpdateFeaturePayload(feature, sizeof(feature), 120u),
          "missing phase flag accepted");
    CHECK(MeasureModel_Feature()->version == version + 1u &&
          MeasureModel_Feature()->comp[1].phase_deg10 == 0,
          "missing phase is retained in flags; phase fields are sanitized");

    fill_feature_payload(feature, 8u);
    feature[4] = 3u;
    CHECK(!MeasureModel_UpdateFeaturePayload(feature, sizeof(feature), 130u),
          "bad mode rejected");
    fill_feature_payload(feature, 8u);
    feature[6] = 0u;
    CHECK(!MeasureModel_UpdateFeaturePayload(feature, sizeof(feature), 131u),
          "zero component count rejected");
    fill_feature_payload(feature, 8u);
    feature[6] = 4u;
    CHECK(!MeasureModel_UpdateFeaturePayload(feature, sizeof(feature), 132u),
          "too many components rejected");
    CHECK(!MeasureModel_UpdateFeaturePayload(feature,
                                            MEASURE_FEATURE_PAYLOAD_LEN - 1u,
                                            133u),
          "bad feature length rejected");

    fill_status_payload(status, 13u);
    CHECK(MeasureModel_UpdateStatusPayload(status, sizeof(status), 160u),
          "status payload accepted");
    st = MeasureModel_FpgaStatus();
    CHECK(st->valid && st->fpga_state == 1u && st->fpga_error == 0u &&
          st->rx_crc_errors == 7u && st->tx_drops == 9u,
          "status decode");

    printf("measure_model: FEATURE/STATUS decode OK\n");
}

static void test_fpga_link(void)
{
    FpgaLink link;
    uint8_t payload[MEASURE_FEATURE_PAYLOAD_LEN];
    uint8_t status[12];
    uint8_t frame[FPGA_PACKET_MAX_FRAME];
    FpgaPacket frames[4];
    size_t n;
    const FpgaLinkStats *stats;

    reset_io();
    s_now_ms = 100u;
    Calibration_Init();
    MeasureModel_Init();
    FpgaLink_Init(&link, fpga_write, fake_ms);

    fill_feature_payload(payload, 21u);
    n = FpgaPacket_BuildFrame(FPGA_PKT_FEATURE, 0u, payload,
                              MEASURE_FEATURE_PAYLOAD_LEN,
                              frame, sizeof(frame));
    FpgaLink_ProcessBytes(&link, frame, (uint16_t)n);
    fill_feature_payload(payload, 22u);
    n = FpgaPacket_BuildFrame(FPGA_PKT_FEATURE, 3u, payload,
                              MEASURE_FEATURE_PAYLOAD_LEN,
                              frame, sizeof(frame));
    FpgaLink_ProcessBytes(&link, frame, (uint16_t)n);
    stats = FpgaLink_GetStats(&link);
    CHECK(stats->packets_ok == 2u && stats->feature_packets == 2u,
          "link feature stats");
    CHECK(stats->seq_lost == 2u, "global seq gap counted");
    CHECK(MeasureModel_Feature()->frame_id == 22u, "link updates feature model");

    fill_feature_payload(payload, 23u);
    n = FpgaPacket_BuildFrame(FPGA_PKT_FEATURE, 4u, payload,
                              MEASURE_FEATURE_PAYLOAD_LEN,
                              frame, sizeof(frame));
    frame[n - 1u] ^= 0x01u;
    FpgaLink_ProcessBytes(&link, frame, (uint16_t)n);
    CHECK(FpgaLink_GetStats(&link)->crc_error_active == 1u,
          "CRC error remains active until a valid frame");

    fill_status_payload(status, 24u);
    n = FpgaPacket_BuildFrame(FPGA_PKT_STATUS, 4u, status, sizeof(status),
                              frame, sizeof(frame));
    FpgaLink_ProcessBytes(&link, frame, (uint16_t)n);
    CHECK(FpgaLink_GetStats(&link)->crc_error_active == 0u,
          "valid STATUS clears recovered CRC state");

    reset_io();
    FpgaLink_SendPing(&link);
    n = parse_fpga_frames(s_fpga_tx, s_fpga_tx_n, frames, 4u);
    CHECK(n == 1u && frames[0].type == FPGA_CMD_PING && frames[0].len == 0u,
          "only ping command generated");

    s_now_ms = 2099u;
    FpgaLink_Poll(&link, s_now_ms);
    CHECK(FpgaLink_GetStats(&link)->online == 1u &&
          FpgaLink_GetStats(&link)->feature_online == 1u,
          "1999ms keeps link and feature live");
    s_now_ms = 2101u;
    FpgaLink_Poll(&link, s_now_ms);
    CHECK(FpgaLink_GetStats(&link)->online == 1u &&
          FpgaLink_GetStats(&link)->feature_online == 0u,
          "2001ms holds feature while link stays online");
    s_now_ms = 5099u;
    FpgaLink_Poll(&link, s_now_ms);
    CHECK(FpgaLink_GetStats(&link)->online == 1u &&
          FpgaLink_GetStats(&link)->feature_online == 0u &&
          FpgaLink_GetStats(&link)->recoveries == 0u,
          "4999ms keeps link online");
    s_now_ms = 5101u;
    FpgaLink_Poll(&link, s_now_ms);
    CHECK(FpgaLink_GetStats(&link)->online == 0u &&
          FpgaLink_GetStats(&link)->feature_online == 0u &&
          FpgaLink_GetStats(&link)->recoveries == 1u,
          "5001ms marks link offline and recovers parser");

    printf("fpga_link: feature dispatch and ping-only behavior OK\n");
}

static void test_hmi_protocol(void)
{
    HmiParser parser;
    HmiEvent ev;
    HmiParseResult r = HMI_PARSE_NONE;
    uint8_t frame[16];
    uint8_t data;
    size_t n;

    HmiProtocol_Init(&parser);
    data = HMI_VIEW_SPEC;
    n = HmiProtocol_BuildFrame(HMI_CMD_SET_VIEW, &data, 1u, frame, sizeof(frame));
    for (size_t i = 0u; i < n; ++i) {
        r = HmiProtocol_PushByte(&parser, frame[i], &ev);
    }
    CHECK(r == HMI_PARSE_FRAME_OK && ev.cmd == HMI_CMD_SET_VIEW &&
          ev.view == HMI_VIEW_SPEC, "view event decode");

    HmiProtocol_Init(&parser);
    data = 3u;
    n = HmiProtocol_BuildFrame(HMI_CMD_SET_PERIOD, &data, 1u, frame, sizeof(frame));
    for (size_t i = 0u; i < n; ++i) {
        r = HmiProtocol_PushByte(&parser, frame[i], &ev);
    }
    CHECK(r == HMI_PARSE_FRAME_OK && ev.cmd == HMI_CMD_SET_PERIOD &&
          ev.period_mode == 3u, "period event decode");

    HmiProtocol_Init(&parser);
    data = 2u;
    n = HmiProtocol_BuildFrame(HMI_CMD_SET_PERIOD, &data, 1u, frame, sizeof(frame));
    for (size_t i = 0u; i < n; ++i) {
        r = HmiProtocol_PushByte(&parser, frame[i], &ev);
    }
    CHECK(r == HMI_PARSE_ERROR &&
          HmiProtocol_LastError(&parser) == HMI_ERR_OUT_OF_RANGE,
          "bad period rejected");

    HmiProtocol_Init(&parser);
    data = 0u;
    n = HmiProtocol_BuildFrame(0x30u, &data, 1u, frame, sizeof(frame));
    for (size_t i = 0u; i < n; ++i) {
        r = HmiProtocol_PushByte(&parser, frame[i], &ev);
    }
    CHECK(r == HMI_PARSE_ERROR &&
          HmiProtocol_LastError(&parser) == HMI_ERR_BAD_CMD,
          "legacy scene-like command rejected");

    HmiProtocol_Init(&parser);
    n = HmiProtocol_BuildFrame(0x34u, 0, 0u, frame, sizeof(frame));
    for (size_t i = 0u; i < n; ++i) {
        r = HmiProtocol_PushByte(&parser, frame[i], &ev);
    }
    CHECK(r == HMI_PARSE_ERROR &&
          HmiProtocol_LastError(&parser) == HMI_ERR_BAD_CMD,
          "legacy stop-like command rejected");

    printf("hmi_protocol: period/view-only events OK\n");
}

static void pump_app(uint16_t turns)
{
    for (uint16_t i = 0u; i < turns; ++i) {
        GApp_Poll();
    }
}

static void test_g_app_flow(void)
{
    GAppIo io = {
        hmi_write,
        fpga_write,
        fpga_read,
        fpga_overflows,
        fake_ms,
        key_read,
        led_write
    };
    FpgaPacket frames[32];
    size_t n;
    uint8_t data;
    uint8_t payload[MEASURE_FEATURE_PAYLOAD_LEN];
    uint8_t status[12];

    reset_io();
    s_now_ms = 0u;
    GApp_Init(&io);
    pump_app(64u);
    CHECK(s_fpga_tx_n == 0u, "boot does not control FPGA");
    CHECK(strstr(s_hmi_text, "page 0") != 0, "boot HMI home page");

    reset_io();
    data = HMI_VIEW_SPEC;
    feed_hmi_frame(HMI_CMD_SET_VIEW, &data, 1u);
    pump_app(64u);
    CHECK(s_fpga_tx_n == 0u, "HMI view change remains local");
    CHECK(strstr(s_hmi_text, "page 2") != 0, "HMI page spec command");

    reset_io();
    data = 3u;
    feed_hmi_frame(HMI_CMD_SET_PERIOD, &data, 1u);
    pump_app(8u);
    CHECK(s_fpga_tx_n == 0u, "HMI period change remains local");

    reset_io();
    s_now_ms = 500u;
    pump_app(1u);
    n = parse_fpga_frames(s_fpga_tx, s_fpga_tx_n, frames, 32u);
    CHECK(n == 1u && frames[0].type == FPGA_CMD_PING,
          "periodic ping is the only FPGA uplink");

    reset_io();
    data = HMI_VIEW_PARAM;
    feed_hmi_frame(HMI_CMD_SET_VIEW, &data, 1u);
    pump_app(32u);

    reset_io();
    fill_feature_payload(payload, 31u);
    append_rx_frame(FPGA_PKT_FEATURE, 1u, payload,
                    MEASURE_FEATURE_PAYLOAD_LEN, 0u);
    s_now_ms += 20u;
    pump_app(64u);
    CHECK(strstr(s_hmi_text, "210.0 mV") != 0 &&
          strstr(s_hmi_text, "50.000kHz") != 0,
          "FPGA feature closes loop back to HMI");

    reset_io();
    data = HMI_VIEW_WAVE;
    feed_hmi_frame(HMI_CMD_SET_VIEW, &data, 1u);
    pump_app(80u);
    CHECK(strstr(s_hmi_text, "page 1") != 0 &&
          strstr(s_hmi_text, "line ") != 0, "wave page synthesizes from feature");

    reset_io();
    s_keys = GAPP_KEY_PERIOD;
    s_now_ms += 40u;
    pump_app(8u);
    s_keys = 0u;
    CHECK(s_fpga_tx_n == 0u && strstr(s_hmi_text, "Period 1") != 0,
          "physical period key stays local");

    reset_io();
    s_now_ms = MeasureModel_Feature()->last_update_ms + 1999u;
    pump_app(64u);
    CHECK(strstr(s_hmi_text, "LIVE") != 0 &&
          strstr(s_hmi_text, "HOLD") == 0 &&
          strstr(s_hmi_text, "COMM ERR") == 0,
          "1999ms feature age remains live");

    reset_io();
    s_now_ms = MeasureModel_Feature()->last_update_ms + 2001u;
    pump_app(64u);
    CHECK(strstr(s_hmi_text, "HOLD") != 0 &&
          strstr(s_hmi_text, "COMM ERR") == 0,
          "2001ms feature age becomes hold");

    reset_io();
    s_now_ms = MeasureModel_Feature()->last_update_ms + 4999u;
    pump_app(64u);
    CHECK(strstr(s_hmi_text, "HOLD") != 0 &&
          strstr(s_hmi_text, "COMM ERR") == 0,
          "4999ms silence remains hold");

    reset_io();
    s_now_ms = MeasureModel_Feature()->last_update_ms + 5001u;
    pump_app(64u);
    CHECK(strstr(s_hmi_text, "COMM ERR") != 0,
          "5001ms complete silence becomes communication error");

    reset_io();
    s_now_ms += 100u;
    fill_feature_payload(payload, 32u);
    append_rx_frame(FPGA_PKT_FEATURE, 2u, payload,
                    MEASURE_FEATURE_PAYLOAD_LEN, 0u);
    pump_app(80u);
    CHECK(strstr(s_hmi_text, "LIVE") != 0,
          "fresh feature recovers communication state");

    reset_io();
    s_now_ms = MeasureModel_Feature()->last_update_ms + 2001u;
    fill_status_payload(status, 33u);
    append_rx_frame(FPGA_PKT_STATUS, 3u, status, sizeof(status), 0u);
    pump_app(80u);
    CHECK(strstr(s_hmi_text, "HOLD") != 0 &&
          strstr(s_hmi_text, "COMM ERR") == 0,
          "STATUS-only traffic keeps link online while feature holds");

    for (data = HMI_VIEW_PARAM; data <= HMI_VIEW_SPEC; ++data) {
        reset_io();
        feed_hmi_frame(HMI_CMD_SET_VIEW, &data, 1u);
        pump_app(100u);
        CHECK(strstr(s_hmi_text, "HOLD") != 0 &&
              strstr(s_hmi_text, "COMM ERR") == 0,
              "page %u shows unified hold state", (unsigned)data);
    }

    s_now_ms += 5001u;
    for (data = HMI_VIEW_PARAM; data <= HMI_VIEW_SPEC; ++data) {
        reset_io();
        feed_hmi_frame(HMI_CMD_SET_VIEW, &data, 1u);
        pump_app(100u);
        CHECK(strstr(s_hmi_text, "COMM ERR") != 0,
              "page %u shows unified communication error",
              (unsigned)data);
    }

    reset_io();
    s_now_ms += 100u;
    fill_feature_payload(payload, 34u);
    append_rx_frame(FPGA_PKT_FEATURE, 4u, payload,
                    MEASURE_FEATURE_PAYLOAD_LEN, 0u);
    pump_app(100u);
    CHECK(strstr(s_hmi_text, "LIVE") != 0,
          "feature recovery returns all pages to live state");

    reset_io();
    s_now_ms = MeasureModel_Feature()->last_update_ms + 2001u;
    fill_feature_payload(payload, 35u);
    append_rx_frame(FPGA_PKT_FEATURE, 5u, payload,
                    MEASURE_FEATURE_PAYLOAD_LEN, 1u);
    pump_app(100u);
    CHECK(strstr(s_hmi_text, "CRC ERR") != 0,
          "recent bad CRC is shown after feature becomes stale");

    reset_io();
    s_now_ms += 1u;
    fill_status_payload(status, 36u);
    append_rx_frame(FPGA_PKT_STATUS, 6u, status, sizeof(status), 0u);
    pump_app(100u);
    CHECK(strstr(s_hmi_text, "HOLD") != 0 &&
          strstr(s_hmi_text, "CRC ERR") == 0,
          "valid STATUS clears CRC state and preserves feature hold");

    reset_io();
    for (uint16_t i = 0u; i < 300u; ++i) {
        GApp_OnHmiRxByte(0x00u);
    }
    pump_app(16u);
    CHECK(strstr(s_hmi_text, "HMI RX FULL") != 0,
          "HMI RX overflow is reported on current page");

    CHECK(s_led == 0u || s_led == 1u, "LED callback exercised");

    printf("g_app: passive HMI and feature refresh OK\n");
}

static void test_mixed_stream_recovery(void)
{
    FpgaLink link;
    uint8_t payload[MEASURE_FEATURE_PAYLOAD_LEN];
    uint32_t bad = 0u;
    const FpgaLinkStats *stats;

    reset_io();
    Calibration_Init();
    MeasureModel_Init();
    FpgaLink_Init(&link, 0, fake_ms);

    for (uint32_t i = 0u; i < 10000u; ++i) {
        uint8_t corrupt = ((i % 37u) == 0u) ? 1u : 0u;

        if ((i % 101u) == 0u) {
            s_fpga_rx[s_fpga_rx_n++] = 0x00u;
            s_fpga_rx[s_fpga_rx_n++] = FPGA_PACKET_HEAD0;
            s_fpga_rx[s_fpga_rx_n++] = 0x13u;
        }
        fill_feature_payload(payload, i + 1u);
        if ((i % 11u) == 0u) {
            payload[7] &= (uint8_t)~MEASURE_FEATURE_FLAG_PHASE_VALID;
        }
        if (corrupt != 0u) {
            bad++;
        }
        append_rx_frame(FPGA_PKT_FEATURE, (uint8_t)i, payload,
                        MEASURE_FEATURE_PAYLOAD_LEN, corrupt);
    }

    s_fpga_rx_pos = 0u;
    while (s_fpga_rx_pos < s_fpga_rx_n) {
        uint8_t chunk[97];
        uint16_t n = fpga_read(chunk, sizeof(chunk));
        s_now_ms++;
        FpgaLink_ProcessBytes(&link, chunk, n);
    }

    stats = FpgaLink_GetStats(&link);
    CHECK(stats->packets_ok == 10000u - bad,
          "mixed packets ok %lu bad %lu",
          (unsigned long)stats->packets_ok, (unsigned long)bad);
    CHECK(stats->crc_errors == bad, "mixed crc errors %lu",
          (unsigned long)stats->crc_errors);
    CHECK(stats->feature_packets == stats->packets_ok,
          "all accepted data frames are features");
    CHECK(MeasureModel_Feature()->valid, "mixed stream updates feature model");

    printf("mixed_stream: 10000 FEATURE frames with CRC faults/misalignment recovered\n");
}

int main(void)
{
    test_crc_and_parser();
    test_uart_ring_math();
    test_hmi_backpressure_queue();
    test_measure_model();
    test_fpga_link();
    test_hmi_protocol();
    test_g_app_flow();
    test_mixed_stream_recovery();

    if (g_fail != 0) {
        printf("\n== %d FAILURE(S) ==\n", g_fail);
        return 1;
    }

    printf("\n== ALL TESTS PASSED ==\n");
    return 0;
}
