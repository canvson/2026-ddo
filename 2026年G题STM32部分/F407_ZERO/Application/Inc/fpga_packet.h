/*
 * File: fpga_packet.h
 * Role: 2026 G binary frame format, CRC and streaming parser API.
 * Scope: A5 5A packet framing for FPGA feature downlink and optional ping.
 */
#ifndef FPGA_PACKET_H
#define FPGA_PACKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FPGA_PACKET_HEAD0 0xA5u
#define FPGA_PACKET_HEAD1 0x5Au
#define FPGA_PACKET_MAX_PAYLOAD 128u
#define FPGA_PACKET_MAX_FRAME (FPGA_PACKET_MAX_PAYLOAD + 8u)

typedef enum {
    FPGA_PKT_STATUS  = 0x04u,
    FPGA_PKT_FEATURE = 0x10u,

    FPGA_CMD_PING = 0x85u,
} FpgaPacketType;

typedef enum {
    FPGA_PARSE_NONE = 0,
    FPGA_PARSE_FRAME_OK,
    FPGA_PARSE_ERROR,
} FpgaParseResult;

typedef enum {
    FPGA_PACKET_ERR_NONE = 0,
    FPGA_PACKET_ERR_BAD_LEN,
    FPGA_PACKET_ERR_BAD_CRC,
    FPGA_PACKET_ERR_BAD_HEAD,
} FpgaPacketError;

typedef struct {
    uint8_t type;
    uint8_t seq;
    uint16_t len;
    uint8_t payload[FPGA_PACKET_MAX_PAYLOAD];
} FpgaPacket;

typedef struct {
    uint8_t state;
    uint8_t crc_l;
    uint16_t index;
    uint16_t buffered;
    uint16_t expected;
    uint8_t frame[FPGA_PACKET_MAX_FRAME];
    FpgaPacketError last_error;
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t len_errors;
    uint32_t resyncs;
} FpgaPacketParser;

void FpgaPacket_Init(FpgaPacketParser *parser);
FpgaParseResult FpgaPacket_PushByte(FpgaPacketParser *parser, uint8_t byte,
                                    FpgaPacket *out);
uint16_t FpgaPacket_Crc16(const uint8_t *data, uint16_t len);
size_t FpgaPacket_BuildFrame(uint8_t type, uint8_t seq,
                             const uint8_t *payload, uint16_t len,
                             uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif
