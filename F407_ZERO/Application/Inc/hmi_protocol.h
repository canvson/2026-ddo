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
#define HMI_MAX_DATA  16u

typedef enum {
    HMI_CMD_SET_BASIC    = 0x30u,
    HMI_CMD_START_BASIC  = 0x31u,
    HMI_CMD_START_LEARN  = 0x32u,
    HMI_CMD_START_EMULATE= 0x33u,
    HMI_CMD_STOP         = 0x34u,
    HMI_CMD_CALIB_OUTPUT = 0x35u,
    HMI_CMD_CLEAR_ERROR  = 0x36u,
} HmiCommand;

typedef enum {
    HMI_WAVE_SINE = 0u,
    HMI_WAVE_SQUARE = 1u,
    HMI_WAVE_OTHER = 2u,
} HmiWave;

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
    uint32_t freq_hz;
    uint16_t target_vpp10;
    uint16_t output_mVpp;
    uint8_t wave;
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
