/*
 * File: hmi_protocol.h
 * Role: Touch-screen upstream event-frame protocol definition.
 * Scope: Passive-display HMI events: period window and page view only.
 */
#ifndef HMI_PROTOCOL_H
#define HMI_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_FRAME_SOF 0xAAu
#define HMI_FRAME_EOF 0x55u
#define HMI_MAX_DATA  1u

typedef enum {
    HMI_PAGE_HOME = 0u,
    HMI_PAGE_WAVE = 1u,
    HMI_PAGE_SPEC = 2u,
} HmiPage;

typedef enum {
    HMI_VIEW_PARAM = 0u,
    HMI_VIEW_WAVE  = 1u,
    HMI_VIEW_SPEC  = 2u,
} HmiView;

typedef enum {
    HMI_CMD_SET_PERIOD = 0x31u,
    HMI_CMD_SET_VIEW   = 0x32u,
} HmiCommand;

typedef enum {
    HMI_PARSE_NONE = 0,
    HMI_PARSE_FRAME_OK,
    HMI_PARSE_ERROR,
} HmiParseResult;

typedef enum {
    HMI_ERR_NONE = 0,
    HMI_ERR_BAD_LEN,
    HMI_ERR_BAD_CHECKSUM,
    HMI_ERR_BAD_EOF,
    HMI_ERR_BAD_CMD,
    HMI_ERR_OUT_OF_RANGE,
} HmiProtocolError;

typedef struct {
    HmiCommand cmd;
    HmiView view;
    uint8_t period_mode;
} HmiEvent;

typedef struct {
    uint8_t state;
    uint8_t cmd;
    uint8_t len;
    uint8_t index;
    uint8_t checksum;
    uint8_t data[HMI_MAX_DATA];
    HmiProtocolError last_error;
} HmiParser;

void HmiProtocol_Init(HmiParser *parser);
HmiParseResult HmiProtocol_PushByte(HmiParser *parser, uint8_t byte, HmiEvent *event);
HmiProtocolError HmiProtocol_LastError(const HmiParser *parser);
uint8_t HmiProtocol_Checksum(uint8_t cmd, const uint8_t *data, uint8_t len);
size_t HmiProtocol_BuildFrame(uint8_t cmd, const uint8_t *data, uint8_t len,
                              uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif
