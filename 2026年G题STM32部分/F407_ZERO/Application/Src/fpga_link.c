/*
 * File: fpga_link.c
 * Role: Implements the 2026 G FPGA packet-link runtime service.
 * Scope: Dispatches FEATURE/STATUS packets and tracks timeout recovery.
 */
#include "fpga_link.h"

#include "measure_model.h"

#include <string.h>

static uint32_t now_from_link(const FpgaLink *link)
{
    return (link != 0 && link->get_ms != 0) ? link->get_ms() : 0u;
}

static void send_cmd(FpgaLink *link, uint8_t type,
                     const uint8_t *payload, uint16_t len)
{
    uint8_t frame[32];
    size_t n;

    if (link == 0 || link->write == 0) {
        return;
    }
    n = FpgaPacket_BuildFrame(type, link->tx_seq++, payload, len,
                              frame, sizeof(frame));
    if (n != 0u) {
        link->write(frame, (uint16_t)n);
    }
}

static void track_seq(FpgaLink *link, const FpgaPacket *pkt)
{
    uint8_t delta;

    if (link->seq_valid != 0u) {
        delta = (uint8_t)(pkt->seq - link->last_seq);
        if (delta > 1u) {
            link->stats.seq_lost += (uint32_t)(delta - 1u);
        }
    }
    link->last_seq = pkt->seq;
    link->seq_valid = 1u;
}

static void handle_packet(FpgaLink *link, const FpgaPacket *pkt)
{
    uint32_t t = now_from_link(link);
    uint8_t accepted = 0u;
    uint8_t feature = 0u;

    switch (pkt->type) {
    case FPGA_PKT_FEATURE:
        accepted = MeasureModel_UpdateFeaturePayload(pkt->payload, pkt->len, t) ? 1u : 0u;
        if (accepted != 0u) {
            link->stats.feature_packets++;
            feature = 1u;
        }
        break;

    case FPGA_PKT_STATUS:
        accepted = MeasureModel_UpdateStatusPayload(pkt->payload, pkt->len, t) ? 1u : 0u;
        if (accepted != 0u) {
            const MeasureFpgaStatus *st = MeasureModel_FpgaStatus();
            link->stats.status_packets++;
            link->stats.fpga_state = st->fpga_state;
            link->stats.fpga_error = st->fpga_error;
        }
        break;

    default:
        break;
    }

    if (accepted == 0u) {
        link->stats.rejected_packets++;
        return;
    }

    track_seq(link, pkt);
    link->stats.last_packet_ms = t;
    link->last_rx_activity_ms = t;
    link->rx_seen = 1u;
    link->stats.online = 1u;
    link->stats.crc_error_active = 0u;
    if (feature != 0u) {
        link->stats.last_feature_ms = t;
        link->feature_seen = 1u;
        link->stats.feature_online = 1u;
    }
    link->stats.packets_ok++;
}

void FpgaLink_Init(FpgaLink *link, FpgaLinkWriteFn write, FpgaLinkGetMsFn get_ms)
{
    if (link == 0) {
        return;
    }
    memset(link, 0, sizeof(*link));
    FpgaPacket_Init(&link->parser);
    link->write = write;
    link->get_ms = get_ms;
    link->init_ms = now_from_link(link);
    link->last_rx_activity_ms = link->init_ms;
    link->last_ping_ms = link->init_ms;
    link->last_recover_ms = link->init_ms;
}

void FpgaLink_ResetParser(FpgaLink *link)
{
    if (link == 0) {
        return;
    }
    FpgaPacket_Init(&link->parser);
    link->stats.recoveries++;
}

void FpgaLink_ProcessBytes(FpgaLink *link, const uint8_t *data, uint16_t len)
{
    FpgaPacket pkt;

    if (link == 0 || data == 0) {
        return;
    }

    for (uint16_t i = 0u; i < len; ++i) {
        uint32_t crc_before = link->parser.crc_errors;
        uint32_t len_before = link->parser.len_errors;
        FpgaParseResult r = FpgaPacket_PushByte(&link->parser, data[i], &pkt);

        if (link->parser.crc_errors != crc_before) {
            link->stats.crc_errors += link->parser.crc_errors - crc_before;
            link->stats.crc_error_active = 1u;
        }
        if (link->parser.len_errors != len_before) {
            link->stats.len_errors += link->parser.len_errors - len_before;
        }
        if (r == FPGA_PARSE_FRAME_OK) {
            handle_packet(link, &pkt);
        }
    }
}

void FpgaLink_Poll(FpgaLink *link, uint32_t now_ms)
{
    if (link == 0) {
        return;
    }

    if ((uint32_t)(now_ms - link->last_ping_ms) >= 500u) {
        link->last_ping_ms = now_ms;
        FpgaLink_SendPing(link);
    }

    link->stats.online =
        (link->rx_seen != 0u &&
         (uint32_t)(now_ms - link->stats.last_packet_ms) <= 5000u) ? 1u : 0u;
    link->stats.feature_online =
        (link->feature_seen != 0u &&
         (uint32_t)(now_ms - link->stats.last_feature_ms) <= 2000u) ? 1u : 0u;

    if ((uint32_t)(now_ms - link->last_rx_activity_ms) >= 5000u &&
        (uint32_t)(now_ms - link->last_recover_ms) >= 5000u) {
        link->last_recover_ms = now_ms;
        FpgaLink_ResetParser(link);
    }
}

const FpgaLinkStats *FpgaLink_GetStats(const FpgaLink *link)
{
    return (link != 0) ? &link->stats : 0;
}

void FpgaLink_SendPing(FpgaLink *link)
{
    send_cmd(link, FPGA_CMD_PING, 0, 0u);
}
