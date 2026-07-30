/*
 * File: fpga_link.h
 * Role: Runtime FPGA link service for the 2026 G STM32 firmware.
 * Scope: Feature-packet dispatch, optional ping, timeout and recovery stats.
 */
#ifndef FPGA_LINK_H
#define FPGA_LINK_H

#include "fpga_packet.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FpgaLinkWriteFn)(const uint8_t *data, uint16_t len);
typedef uint32_t (*FpgaLinkGetMsFn)(void);

typedef struct {
    uint32_t packets_ok;
    uint32_t feature_packets;
    uint32_t status_packets;
    uint32_t rejected_packets;
    uint32_t crc_errors;
    uint32_t len_errors;
    uint32_t seq_lost;
    uint32_t recoveries;
    uint32_t last_packet_ms;
    uint32_t last_feature_ms;
    uint8_t online;
    uint8_t feature_online;
    uint8_t crc_error_active;
    uint8_t fpga_state;
    uint8_t fpga_error;
} FpgaLinkStats;

typedef struct {
    FpgaPacketParser parser;
    FpgaLinkWriteFn write;
    FpgaLinkGetMsFn get_ms;
    FpgaLinkStats stats;
    uint8_t tx_seq;
    uint8_t last_seq;
    uint8_t seq_valid;
    uint8_t rx_seen;
    uint8_t feature_seen;
    uint32_t last_ping_ms;
    uint32_t last_recover_ms;
    uint32_t init_ms;
    uint32_t last_rx_activity_ms;
} FpgaLink;

void FpgaLink_Init(FpgaLink *link, FpgaLinkWriteFn write, FpgaLinkGetMsFn get_ms);
void FpgaLink_ProcessBytes(FpgaLink *link, const uint8_t *data, uint16_t len);
void FpgaLink_Poll(FpgaLink *link, uint32_t now_ms);
const FpgaLinkStats *FpgaLink_GetStats(const FpgaLink *link);
void FpgaLink_ResetParser(FpgaLink *link);
void FpgaLink_SendPing(FpgaLink *link);

#ifdef __cplusplus
}
#endif

#endif
