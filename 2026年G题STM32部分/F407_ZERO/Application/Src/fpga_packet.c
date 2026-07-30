/*
 * File: fpga_packet.c
 * Role: Implements 2026 G binary frame CRC, builder and streaming parser.
 * Scope: Resynchronizes after bad bytes, bad lengths and CRC failures.
 */
#include "fpga_packet.h"

#include <string.h>

enum {
    RX_WAIT_A5 = 0,
    RX_WAIT_5A,
    RX_TYPE,
    RX_SEQ,
    RX_LEN_L,
    RX_LEN_H,
    RX_PAYLOAD,
    RX_CRC_L,
    RX_CRC_H,
};

static void update_parser_state(FpgaPacketParser *parser)
{
    if (parser->buffered == 0u) {
        parser->state = RX_WAIT_A5;
    } else if (parser->buffered == 1u) {
        parser->state = RX_WAIT_5A;
    } else if (parser->buffered == 2u) {
        parser->state = RX_TYPE;
    } else if (parser->buffered == 3u) {
        parser->state = RX_SEQ;
    } else if (parser->buffered == 4u) {
        parser->state = RX_LEN_L;
    } else if (parser->buffered == 5u) {
        parser->state = RX_LEN_H;
    } else if (parser->expected == 0u ||
               parser->buffered < (uint16_t)(parser->expected - 2u)) {
        parser->state = RX_PAYLOAD;
    } else if (parser->buffered == (uint16_t)(parser->expected - 2u)) {
        parser->state = RX_CRC_L;
    } else {
        parser->state = RX_CRC_H;
    }
}

static void keep_next_header(FpgaPacketParser *parser)
{
    uint16_t start;
    uint16_t incomplete = parser->buffered;
    uint16_t keep;
    uint16_t len;
    uint16_t total;
    uint16_t got;
    uint16_t calc;

    for (start = 1u; start + 1u < parser->buffered; ++start) {
        if (parser->frame[start] == FPGA_PACKET_HEAD0 &&
            parser->frame[start + 1u] == FPGA_PACKET_HEAD1) {
            keep = (uint16_t)(parser->buffered - start);
            if (keep < 6u) {
                if (incomplete == parser->buffered) {
                    incomplete = start;
                }
                continue;
            }
            len = (uint16_t)parser->frame[start + 4u] |
                  ((uint16_t)parser->frame[start + 5u] << 8);
            if (len > FPGA_PACKET_MAX_PAYLOAD) {
                continue;
            }
            total = (uint16_t)(len + 8u);
            if (keep < total) {
                if (incomplete == parser->buffered) {
                    incomplete = start;
                }
                continue;
            }
            got = (uint16_t)parser->frame[start + total - 2u] |
                  ((uint16_t)parser->frame[start + total - 1u] << 8);
            calc = FpgaPacket_Crc16(&parser->frame[start],
                                    (uint16_t)(total - 2u));
            if (got == calc) {
                break;
            }
        }
    }
    if (start + 1u >= parser->buffered) {
        if (incomplete < parser->buffered) {
            start = incomplete;
            keep = (uint16_t)(parser->buffered - start);
            memmove(parser->frame, &parser->frame[start], keep);
            parser->buffered = keep;
        } else if (parser->buffered != 0u &&
            parser->frame[parser->buffered - 1u] == FPGA_PACKET_HEAD0) {
            parser->frame[0] = FPGA_PACKET_HEAD0;
            parser->buffered = 1u;
        } else {
            parser->buffered = 0u;
        }
    } else {
        keep = (uint16_t)(parser->buffered - start);
        memmove(parser->frame, &parser->frame[start], keep);
        parser->buffered = keep;
    }
    parser->expected = 0u;
    parser->index = parser->buffered;
    update_parser_state(parser);
}

static void copy_frame(const FpgaPacketParser *parser, FpgaPacket *out)
{
    uint16_t i;

    if (out == 0) {
        return;
    }
    out->type = parser->frame[2];
    out->seq = parser->frame[3];
    out->len = (uint16_t)parser->frame[4] |
               ((uint16_t)parser->frame[5] << 8);
    for (i = 0u; i < out->len; ++i) {
        out->payload[i] = parser->frame[6u + i];
    }
}

uint16_t FpgaPacket_Crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;

    if (data == 0 && len != 0u) {
        return crc;
    }

    for (uint16_t i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t b = 0u; b < 8u; ++b) {
            if ((crc & 1u) != 0u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void FpgaPacket_Init(FpgaPacketParser *parser)
{
    if (parser == 0) {
        return;
    }
    parser->state = RX_WAIT_A5;
    parser->crc_l = 0u;
    parser->index = 0u;
    parser->buffered = 0u;
    parser->expected = 0u;
    parser->last_error = FPGA_PACKET_ERR_NONE;
    parser->frames_ok = 0u;
    parser->crc_errors = 0u;
    parser->len_errors = 0u;
    parser->resyncs = 0u;
}

FpgaParseResult FpgaPacket_PushByte(FpgaPacketParser *parser, uint8_t byte,
                                    FpgaPacket *out)
{
    uint16_t len;
    uint16_t got;
    uint16_t calc;

    if (parser == 0) {
        return FPGA_PARSE_ERROR;
    }

    if (parser->buffered == 0u) {
        if (byte == FPGA_PACKET_HEAD0) {
            parser->frame[0] = byte;
            parser->buffered = 1u;
            parser->index = 1u;
            parser->last_error = FPGA_PACKET_ERR_NONE;
            update_parser_state(parser);
        }
        return FPGA_PARSE_NONE;
    }

    if (parser->buffered == 1u) {
        if (byte == FPGA_PACKET_HEAD1) {
            parser->frame[1] = byte;
            parser->buffered = 2u;
            parser->index = 2u;
            update_parser_state(parser);
        } else if (byte == FPGA_PACKET_HEAD0) {
            parser->frame[0] = byte;
            parser->resyncs++;
        } else {
            parser->buffered = 0u;
            parser->index = 0u;
            parser->last_error = FPGA_PACKET_ERR_BAD_HEAD;
            parser->resyncs++;
            update_parser_state(parser);
            return FPGA_PARSE_ERROR;
        }
        return FPGA_PARSE_NONE;
    }

    if (parser->buffered >= FPGA_PACKET_MAX_FRAME) {
        parser->last_error = FPGA_PACKET_ERR_BAD_LEN;
        parser->len_errors++;
        keep_next_header(parser);
        return FPGA_PARSE_ERROR;
    }
    parser->frame[parser->buffered++] = byte;
    parser->index = parser->buffered;

    for (;;) {
        if (parser->buffered < 6u) {
            update_parser_state(parser);
            return FPGA_PARSE_NONE;
        }

        len = (uint16_t)parser->frame[4] |
              ((uint16_t)parser->frame[5] << 8);
        if (len > FPGA_PACKET_MAX_PAYLOAD) {
            parser->last_error = FPGA_PACKET_ERR_BAD_LEN;
            parser->len_errors++;
            keep_next_header(parser);
            if (parser->buffered < 6u) {
                return FPGA_PARSE_ERROR;
            }
            continue;
        }
        parser->expected = (uint16_t)(len + 8u);
        if (parser->buffered < parser->expected) {
            update_parser_state(parser);
            return FPGA_PARSE_NONE;
        }

        parser->crc_l = parser->frame[parser->expected - 2u];
        got = (uint16_t)parser->crc_l |
              ((uint16_t)parser->frame[parser->expected - 1u] << 8);
        calc = FpgaPacket_Crc16(parser->frame,
                                (uint16_t)(parser->expected - 2u));
        if (got != calc) {
            parser->last_error = FPGA_PACKET_ERR_BAD_CRC;
            parser->crc_errors++;
            keep_next_header(parser);
            if (parser->buffered < 6u) {
                return FPGA_PARSE_ERROR;
            }
            continue;
        }

        copy_frame(parser, out);
        parser->last_error = FPGA_PACKET_ERR_NONE;
        parser->frames_ok++;
        parser->buffered = 0u;
        parser->expected = 0u;
        parser->index = 0u;
        update_parser_state(parser);
        return FPGA_PARSE_FRAME_OK;
    }
}

size_t FpgaPacket_BuildFrame(uint8_t type, uint8_t seq,
                             const uint8_t *payload, uint16_t len,
                             uint8_t *out, size_t out_cap)
{
    uint16_t crc;
    size_t total = (size_t)len + 8u;

    if (out == 0 || len > FPGA_PACKET_MAX_PAYLOAD || out_cap < total) {
        return 0u;
    }
    if (payload == 0 && len != 0u) {
        return 0u;
    }

    out[0] = FPGA_PACKET_HEAD0;
    out[1] = FPGA_PACKET_HEAD1;
    out[2] = type;
    out[3] = seq;
    out[4] = (uint8_t)(len & 0xFFu);
    out[5] = (uint8_t)(len >> 8);
    for (uint16_t i = 0u; i < len; ++i) {
        out[6u + i] = payload[i];
    }

    crc = FpgaPacket_Crc16(out, (uint16_t)(6u + len));
    out[6u + len] = (uint8_t)(crc & 0xFFu);
    out[7u + len] = (uint8_t)(crc >> 8);
    return total;
}
