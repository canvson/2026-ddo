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

static int16_t rd_s16_le(const uint8_t *p)
{
    return (int16_t)rd_u16_le(p);
}

static uint32_t rd_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool valid_wave(uint8_t wave)
{
    return wave <= HMI_WAVE_TRIANGLE;
}

static bool valid_channel(const WaveChannelConfig *ch)
{
    return valid_wave(ch->wave) &&
           ch->freq_hz >= 1u && ch->freq_hz <= 20000000u &&
           ch->amp_mVpp <= 5000u &&
           ch->duty_pct10 >= 100u && ch->duty_pct10 <= 900u;
}

bool HmiProtocol_ValidateConfig(const DualWaveOutputConfig *cfg)
{
    if (cfg == 0) {
        return false;
    }
    if (cfg->proto_ver != 1u || (cfg->flags & (uint8_t)~0x03u) != 0u) {
        return false;
    }
    if (!valid_channel(&cfg->ch_a) || !valid_channel(&cfg->ch_b)) {
        return false;
    }
    return cfg->phase_b_rel_a_deg >= -180 && cfg->phase_b_rel_a_deg <= 180;
}

static void decode_output_config(const uint8_t *data, DualWaveOutputConfig *cfg)
{
    cfg->proto_ver = data[0];
    cfg->flags = data[1];
    cfg->ch_a.wave = data[2];
    cfg->ch_a.freq_hz = rd_u32_le(&data[3]);
    cfg->ch_a.amp_mVpp = rd_u16_le(&data[7]);
    cfg->ch_a.duty_pct10 = rd_u16_le(&data[9]);
    cfg->ch_b.wave = data[11];
    cfg->ch_b.freq_hz = rd_u32_le(&data[12]);
    cfg->ch_b.amp_mVpp = rd_u16_le(&data[16]);
    cfg->ch_b.duty_pct10 = rd_u16_le(&data[18]);
    cfg->phase_b_rel_a_deg = rd_s16_le(&data[20]);
}

static bool decode_frame(uint8_t cmd, const uint8_t *data, uint8_t len, HmiEvent *event,
                         HmiProtocolError *err)
{
    if (cmd != HMI_CMD_OUTPUT_CONFIG) {
        *err = HMI_ERR_BAD_CMD;
        return false;
    }
    if (len != HMI_OUTPUT_CONFIG_LEN) {
        *err = HMI_ERR_BAD_LEN;
        return false;
    }

    event->cmd = cmd;
    decode_output_config(data, &event->output);
    if (!HmiProtocol_ValidateConfig(&event->output)) {
        *err = HMI_ERR_OUT_OF_RANGE;
        return false;
    }
    *err = HMI_ERR_NONE;
    return true;
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
    uint8_t i;

    for (i = 0u; i < len; ++i) {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

size_t HmiProtocol_BuildFrame(uint8_t cmd, const uint8_t *data, uint8_t len,
                              uint8_t *out, size_t out_cap)
{
    uint8_t i;

    if (len > HMI_MAX_DATA || out_cap < (size_t)len + 5u) {
        return 0u;
    }
    out[0] = HMI_FRAME_SOF;
    out[1] = cmd;
    out[2] = len;
    for (i = 0u; i < len; ++i) {
        out[3u + i] = data[i];
    }
    out[3u + len] = HmiProtocol_Checksum(cmd, data, len);
    out[4u + len] = HMI_FRAME_EOF;
    return (size_t)len + 5u;
}
