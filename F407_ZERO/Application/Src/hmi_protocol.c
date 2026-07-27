#include "hmi_protocol.h"

enum {
    RX_WAIT_SOF = 0,
    RX_CMD,
    RX_LEN,
    RX_DATA,
    RX_CHECK,
    RX_EOF,
};

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

static bool valid_basic_range(uint32_t freq_hz, uint16_t target_vpp10)
{
    if (freq_hz < 100u || freq_hz > 3000u) {
        return false;
    }
    if ((freq_hz % 100u) != 0u) {
        return false;
    }
    return target_vpp10 >= 10u && target_vpp10 <= 20u;
}

static bool valid_wave(uint8_t wave)
{
    return wave <= HMI_WAVE_OTHER;
}

static bool valid_square_duty(uint16_t duty_pct10)
{
    if (duty_pct10 < 100u || duty_pct10 > 500u) {
        return false;
    }
    return (duty_pct10 % 50u) == 0u;
}

static bool decode_frame(uint8_t cmd, const uint8_t *data, uint8_t len, HmiEvent *event,
                         HmiProtocolError *err)
{
    event->cmd = (HmiCommand)cmd;
    event->freq_hz = 0u;
    event->target_vpp10 = 0u;
    event->output_mVpp = 0u;
    event->duty_pct10 = 500u;
    event->wave = HMI_WAVE_SINE;

    switch ((HmiCommand)cmd) {
    case HMI_CMD_SET_BASIC:
    case HMI_CMD_START_BASIC:
        if (len != 6u) {
            *err = HMI_ERR_BAD_LEN;
            return false;
        }
        event->freq_hz = rd_u32_le(&data[0]);
        event->target_vpp10 = rd_u16_le(&data[4]);
        if (!valid_basic_range(event->freq_hz, event->target_vpp10)) {
            *err = HMI_ERR_OUT_OF_RANGE;
            return false;
        }
        return true;

    case HMI_CMD_START_LEARN:
    case HMI_CMD_STOP:
    case HMI_CMD_CLEAR_ERROR:
        if (len != 0u) {
            *err = HMI_ERR_BAD_LEN;
            return false;
        }
        return true;

    case HMI_CMD_START_EMULATE:
        if (len != 1u) {
            *err = HMI_ERR_BAD_LEN;
            return false;
        }
        event->wave = data[0];
        if (!valid_wave(event->wave)) {
            *err = HMI_ERR_OUT_OF_RANGE;
            return false;
        }
        return true;

    case HMI_CMD_CALIB_OUTPUT:
        if (len != 7u && len != 9u) {
            *err = HMI_ERR_BAD_LEN;
            return false;
        }
        event->freq_hz = rd_u32_le(&data[0]);
        event->output_mVpp = rd_u16_le(&data[4]);
        event->wave = data[6];
        if (len == 9u) {
            event->duty_pct10 = rd_u16_le(&data[7]);
        }
        /* 2 MHz upper bound: the FPGA Basic_two table also has a 2 MHz code */
        if (event->freq_hz < 1u || event->freq_hz > 2000000u) {
            *err = HMI_ERR_OUT_OF_RANGE;
            return false;
        }
        if (event->output_mVpp > 5000u || !valid_wave(event->wave) ||
            !valid_square_duty(event->duty_pct10)) {
            *err = HMI_ERR_OUT_OF_RANGE;
            return false;
        }
        return true;

    default:
        *err = HMI_ERR_BAD_CMD;
        return false;
    }
}

void HmiProtocol_Init(HmiParser *parser)
{
    parser->state = RX_WAIT_SOF;
    parser->cmd = 0u;
    parser->len = 0u;
    parser->index = 0u;
    parser->checksum = 0u;
    parser->last_error = HMI_ERR_NONE;
}

HmiParseResult HmiProtocol_PushByte(HmiParser *parser, uint8_t byte, HmiEvent *event)
{
    switch (parser->state) {
    case RX_WAIT_SOF:
        if (byte == HMI_FRAME_SOF) {
            parser->cmd = 0u;
            parser->len = 0u;
            parser->index = 0u;
            parser->checksum = HMI_FRAME_SOF;
            parser->last_error = HMI_ERR_NONE;
            parser->state = RX_CMD;
        }
        return HMI_PARSE_NONE;

    case RX_CMD:
        parser->cmd = byte;
        parser->checksum = (uint8_t)(parser->checksum + byte);
        parser->state = RX_LEN;
        return HMI_PARSE_NONE;

    case RX_LEN:
        parser->len = byte;
        parser->checksum = (uint8_t)(parser->checksum + byte);
        parser->index = 0u;
        if (parser->len > HMI_MAX_DATA) {
            parser->last_error = HMI_ERR_BAD_LEN;
            parser->state = RX_WAIT_SOF;
            return HMI_PARSE_ERROR;
        }
        parser->state = (parser->len == 0u) ? RX_CHECK : RX_DATA;
        return HMI_PARSE_NONE;

    case RX_DATA:
        parser->data[parser->index++] = byte;
        parser->checksum = (uint8_t)(parser->checksum + byte);
        if (parser->index >= parser->len) {
            parser->state = RX_CHECK;
        }
        return HMI_PARSE_NONE;

    case RX_CHECK:
        if (parser->checksum != byte) {
            parser->last_error = HMI_ERR_BAD_CHECKSUM;
            parser->state = RX_WAIT_SOF;
            return HMI_PARSE_ERROR;
        }
        parser->state = RX_EOF;
        return HMI_PARSE_NONE;

    case RX_EOF:
        if (byte != HMI_FRAME_EOF) {
            parser->last_error = HMI_ERR_BAD_EOF;
            parser->state = RX_WAIT_SOF;
            return HMI_PARSE_ERROR;
        }
        {
            HmiProtocolError err = HMI_ERR_NONE;
            bool ok = decode_frame(parser->cmd, parser->data, parser->len, event, &err);
            parser->last_error = err;
            parser->state = RX_WAIT_SOF;
            return ok ? HMI_PARSE_FRAME_OK : HMI_PARSE_ERROR;
        }

    default:
        parser->state = RX_WAIT_SOF;
        parser->last_error = HMI_ERR_BAD_CMD;
        return HMI_PARSE_ERROR;
    }
}

HmiProtocolError HmiProtocol_LastError(const HmiParser *parser)
{
    return parser->last_error;
}

uint8_t HmiProtocol_Checksum(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t sum = (uint8_t)(HMI_FRAME_SOF + cmd + len);
    for (uint8_t i = 0u; i < len; ++i) {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

size_t HmiProtocol_BuildFrame(uint8_t cmd, const uint8_t *data, uint8_t len,
                              uint8_t *out, size_t out_cap)
{
    if (len > HMI_MAX_DATA || out_cap < (size_t)len + 5u) {
        return 0u;
    }
    out[0] = HMI_FRAME_SOF;
    out[1] = cmd;
    out[2] = len;
    for (uint8_t i = 0u; i < len; ++i) {
        out[3u + i] = data[i];
    }
    out[3u + len] = HmiProtocol_Checksum(cmd, data, len);
    out[4u + len] = HMI_FRAME_EOF;
    return (size_t)len + 5u;
}
