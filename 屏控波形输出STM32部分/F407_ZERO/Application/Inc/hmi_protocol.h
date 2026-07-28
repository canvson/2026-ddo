#ifndef HMI_PROTOCOL_H
#define HMI_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_FRAME_SOF             0xAAu
#define HMI_FRAME_EOF             0x55u
#define HMI_CMD_OUTPUT_CONFIG     0x21u
#define HMI_OUTPUT_CONFIG_LEN     0x16u
#define HMI_MAX_DATA              HMI_OUTPUT_CONFIG_LEN

typedef enum {
    HMI_WAVE_SINE = 0u,
    HMI_WAVE_SQUARE = 1u,
    HMI_WAVE_TRIANGLE = 2u,
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
    uint8_t wave;
    uint32_t freq_hz;
    uint16_t amp_mVpp;
    uint16_t duty_pct10;
} WaveChannelConfig;

typedef struct {
    uint8_t proto_ver;
    uint8_t flags;
    WaveChannelConfig ch_a;
    WaveChannelConfig ch_b;
    int16_t phase_b_rel_a_deg;
} DualWaveOutputConfig;

typedef struct {
    uint8_t cmd;
    DualWaveOutputConfig output;
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
bool HmiProtocol_ValidateConfig(const DualWaveOutputConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif
