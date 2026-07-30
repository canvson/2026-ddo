#include "../stm32_ref/fpga_packet.h"
#include "../stm32_ref/hmi_protocol.h"
#include "../stm32_ref/measure_model.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

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

static HmiParseResult feed_hmi(const uint8_t *data, size_t len, HmiEvent *event,
                               HmiProtocolError *err)
{
    HmiParser parser;
    HmiProtocol_Init(&parser);
    for (size_t i = 0u; i < len; ++i) {
        HmiParseResult result = HmiProtocol_PushByte(&parser, data[i], event);
        if (result != HMI_PARSE_NONE) {
            *err = HmiProtocol_LastError(&parser);
            return result;
        }
    }
    *err = HmiProtocol_LastError(&parser);
    return HMI_PARSE_NONE;
}

static FpgaParseResult feed_fpga(const uint8_t *data, size_t len, FpgaPacket *packet,
                                 FpgaPacketParser *parser)
{
    FpgaPacket_Init(parser);
    for (size_t i = 0u; i < len; ++i) {
        FpgaParseResult result = FpgaPacket_PushByte(parser, data[i], packet);
        if (result != FPGA_PARSE_NONE) {
            return result;
        }
    }
    return FPGA_PARSE_NONE;
}

static void fill_feature(uint8_t *p)
{
    memset(p, 0, MEASURE_FEATURE_PAYLOAD_LEN);
    put_u32(&p[0], 9u);
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

static int test_hmi_valid_frames(void)
{
    const uint8_t period[] = {0xAAu, 0x31u, 0x01u, 0x03u, 0xDFu, 0x55u};
    const uint8_t view[] = {0xAAu, 0x32u, 0x01u, 0x02u, 0xDFu, 0x55u};
    HmiEvent event;
    HmiProtocolError err;

    ASSERT_TRUE(feed_hmi(period, sizeof(period), &event, &err) == HMI_PARSE_FRAME_OK);
    ASSERT_TRUE(err == HMI_ERR_NONE);
    ASSERT_TRUE(event.cmd == HMI_CMD_SET_PERIOD);
    ASSERT_TRUE(event.period_mode == 3u);
    ASSERT_TRUE(feed_hmi(view, sizeof(view), &event, &err) == HMI_PARSE_FRAME_OK);
    ASSERT_TRUE(event.cmd == HMI_CMD_SET_VIEW);
    ASSERT_TRUE(event.view == HMI_VIEW_SPEC);
    return 0;
}

static int test_hmi_rejects_bad_frames(void)
{
    const uint8_t bad_checksum[] = {0xAAu, 0x32u, 0x01u, 0x02u, 0xDEu, 0x55u};
    const uint8_t bad_period[] = {0xAAu, 0x31u, 0x01u, 0x02u, 0xDEu, 0x55u};
    const uint8_t old_command[] = {0xAAu, 0x30u, 0x01u, 0x00u, 0xDBu, 0x55u};
    HmiEvent event;
    HmiProtocolError err;

    ASSERT_TRUE(feed_hmi(bad_checksum, sizeof(bad_checksum), &event, &err) ==
                HMI_PARSE_ERROR);
    ASSERT_TRUE(err == HMI_ERR_BAD_CHECKSUM);
    ASSERT_TRUE(feed_hmi(bad_period, sizeof(bad_period), &event, &err) ==
                HMI_PARSE_ERROR);
    ASSERT_TRUE(err == HMI_ERR_OUT_OF_RANGE);
    ASSERT_TRUE(feed_hmi(old_command, sizeof(old_command), &event, &err) ==
                HMI_PARSE_ERROR);
    ASSERT_TRUE(err == HMI_ERR_BAD_CMD);
    return 0;
}

static int test_fpga_valid_feature(void)
{
    uint8_t payload[MEASURE_FEATURE_PAYLOAD_LEN];
    uint8_t bytes[FPGA_PACKET_MAX_FRAME];
    FpgaPacket packet;
    FpgaPacketParser parser;
    MeasureModel model;
    size_t len;

    fill_feature(payload);
    len = FpgaPacket_BuildFrame(FPGA_PKT_FEATURE, 7u, payload, sizeof(payload),
                                bytes, sizeof(bytes));
    ASSERT_TRUE(len == MEASURE_FEATURE_PAYLOAD_LEN + 8u);
    ASSERT_TRUE(feed_fpga(bytes, len, &packet, &parser) == FPGA_PARSE_FRAME_OK);
    ASSERT_TRUE(parser.last_error == FPGA_PACKET_ERR_NONE);
    ASSERT_TRUE(packet.type == FPGA_PKT_FEATURE);
    ASSERT_TRUE(packet.seq == 7u);
    ASSERT_TRUE(packet.len == MEASURE_FEATURE_PAYLOAD_LEN);

    MeasureModel_Init(&model);
    ASSERT_TRUE(MeasureModel_UpdateFeaturePayload(&model, packet.payload, packet.len));
    ASSERT_TRUE(model.feature.mode == (uint8_t)MEASURE_MODE_UB_J);
    ASSERT_TRUE(model.feature.component_count == 3u);
    ASSERT_TRUE(model.feature.comp[1].phase_deg10 == 900);
    return 0;
}

static int test_fpga_rejects_bad_crc(void)
{
    uint8_t payload[MEASURE_FEATURE_PAYLOAD_LEN];
    uint8_t bytes[FPGA_PACKET_MAX_FRAME];
    FpgaPacket packet;
    FpgaPacketParser parser;
    size_t len;

    fill_feature(payload);
    len = FpgaPacket_BuildFrame(FPGA_PKT_FEATURE, 1u, payload, sizeof(payload),
                                bytes, sizeof(bytes));
    ASSERT_TRUE(len != 0u);
    bytes[len - 1u] ^= 0x01u;
    ASSERT_TRUE(feed_fpga(bytes, len, &packet, &parser) == FPGA_PARSE_ERROR);
    ASSERT_TRUE(parser.last_error == FPGA_PACKET_ERR_BAD_CRC);
    return 0;
}

static int test_feature_rejects_bad_payload(void)
{
    uint8_t payload[MEASURE_FEATURE_PAYLOAD_LEN];
    MeasureModel model;

    MeasureModel_Init(&model);
    fill_feature(payload);
    payload[4] = 3u;
    ASSERT_TRUE(!MeasureModel_UpdateFeaturePayload(&model, payload, sizeof(payload)));
    fill_feature(payload);
    payload[6] = 0u;
    ASSERT_TRUE(!MeasureModel_UpdateFeaturePayload(&model, payload, sizeof(payload)));
    fill_feature(payload);
    payload[7] &= (uint8_t)~MEASURE_FEATURE_FLAG_PHASE_VALID;
    ASSERT_TRUE(MeasureModel_UpdateFeaturePayload(&model, payload, sizeof(payload)));
    ASSERT_TRUE(model.feature.comp[1].phase_deg10 == 0);
    return 0;
}

int main(void)
{
    ASSERT_TRUE(test_hmi_valid_frames() == 0);
    ASSERT_TRUE(test_hmi_rejects_bad_frames() == 0);
    ASSERT_TRUE(test_fpga_valid_feature() == 0);
    ASSERT_TRUE(test_fpga_rejects_bad_crc() == 0);
    ASSERT_TRUE(test_feature_rejects_bad_payload() == 0);
    printf("protocol_selftest: OK\n");
    return 0;
}
