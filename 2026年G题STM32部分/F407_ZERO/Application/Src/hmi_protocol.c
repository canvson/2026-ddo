/*
 * File: hmi_protocol.c
 * Role: Implements AA..55 HMI event-frame parsing and frame building.
 * Scope: Period-window and page-view commands from the touch screen.
 */
#include "hmi_protocol.h"

enum {
    RX_WAIT_SOF = 0,
    RX_CMD,
    RX_LEN,
    RX_DATA,
    RX_CHECK,
    RX_EOF,
};

static bool valid_view(uint8_t view)
{
    return view <= (uint8_t)HMI_VIEW_SPEC;
}

static bool valid_period(uint8_t period)
{
    return period == 1u || period == 3u;
}

static void restart_from_byte(HmiParser *parser, uint8_t byte)
{
    if (byte == HMI_FRAME_SOF) {
        parser->cmd = 0u;
        parser->len = 0u;
        parser->index = 0u;
        parser->checksum = HMI_FRAME_SOF;
        parser->state = RX_CMD;
    } else {
        parser->state = RX_WAIT_SOF;
    }
}

static bool decode_frame(uint8_t cmd, const uint8_t *data, uint8_t len,
                         HmiEvent *event, HmiProtocolError *err)
{
    if (event == 0 || err == 0) {
        return false;
    }

    event->cmd = (HmiCommand)cmd;
    event->view = HMI_VIEW_PARAM;
    event->period_mode = 1u;
    *err = HMI_ERR_NONE;

    switch ((HmiCommand)cmd) {
    case HMI_CMD_SET_PERIOD:
        if (len != 1u) {
            *err = HMI_ERR_BAD_LEN;
            return false;
        }
        if (!valid_period(data[0])) {
            *err = HMI_ERR_OUT_OF_RANGE;
            return false;
        }
        event->period_mode = data[0];
        return true;

    case HMI_CMD_SET_VIEW:
        if (len != 1u) {
            *err = HMI_ERR_BAD_LEN;
            return false;
        }
        if (!valid_view(data[0])) {
            *err = HMI_ERR_OUT_OF_RANGE;
            return false;
        }
        event->view = (HmiView)data[0];
        return true;

    default:
        *err = HMI_ERR_BAD_CMD;
        return false;
    }
}

void HmiProtocol_Init(HmiParser *parser)
{
    if (parser == 0) {
        return;
    }
    parser->state = RX_WAIT_SOF;
    parser->cmd = 0u;
    parser->len = 0u;
    parser->index = 0u;
    parser->checksum = 0u;
    parser->last_error = HMI_ERR_NONE;
}

HmiParseResult HmiProtocol_PushByte(HmiParser *parser, uint8_t byte, HmiEvent *event)
{
    if (parser == 0) {
        return HMI_PARSE_ERROR;
    }

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
            restart_from_byte(parser, byte);
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
            restart_from_byte(parser, byte);
            return HMI_PARSE_ERROR;
        }
        parser->state = RX_EOF;
        return HMI_PARSE_NONE;

    case RX_EOF:
        if (byte != HMI_FRAME_EOF) {
            parser->last_error = HMI_ERR_BAD_EOF;
            restart_from_byte(parser, byte);
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
    return (parser != 0) ? parser->last_error : HMI_ERR_BAD_CMD;
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
    if (len > HMI_MAX_DATA || out == 0 || out_cap < (size_t)len + 5u) {
        return 0u;
    }
    if (data == 0 && len != 0u) {
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
