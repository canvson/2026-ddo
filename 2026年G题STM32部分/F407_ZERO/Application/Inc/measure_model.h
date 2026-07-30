/*
 * File: measure_model.h
 * Role: Shared 2026 G measurement data model and access API.
 * Scope: Compact FPGA FEATURE payload and optional link status.
 */
#ifndef MEASURE_MODEL_H
#define MEASURE_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEASURE_COMPONENT_MAX 3u
#define MEASURE_FEATURE_PAYLOAD_LEN 60u

#define MEASURE_FEATURE_FLAG_INTERFERENCE_SUPPRESSED 0x01u
#define MEASURE_FEATURE_FLAG_PHASE_VALID             0x02u

typedef enum {
    MEASURE_MODE_UA = 0u,
    MEASURE_MODE_UB = 1u,
    MEASURE_MODE_UB_J = 2u,
} MeasureMode;

typedef enum {
    MEASURE_STATUS_WAIT = 0u,
    MEASURE_STATUS_VALID = 1u,
    MEASURE_STATUS_HOLD = 2u,
    MEASURE_STATUS_OVER_RANGE = 3u,
    MEASURE_STATUS_LINK_OR_ALGO_ERROR = 4u,
} MeasureStatus;

typedef struct {
    uint32_t freq_hz;
    float amp_peak_mv;
    int16_t phase_deg10;
} MeasureComponent;

typedef struct {
    uint8_t valid;
    uint32_t frame_id;
    uint8_t mode;
    uint8_t status;
    uint8_t component_count;
    uint8_t flags;
    float vpp_mv;
    float urms_mv;
    uint32_t f1_hz;
    MeasureComponent comp[MEASURE_COMPONENT_MAX];
    uint32_t version;
    uint32_t last_update_ms;
} MeasureFeature;

typedef struct {
    uint8_t valid;
    uint32_t frame_id;
    uint8_t fpga_state;
    uint8_t fpga_error;
    uint16_t rx_crc_errors;
    uint16_t tx_drops;
    uint32_t version;
    uint32_t last_update_ms;
} MeasureFpgaStatus;

void MeasureModel_Init(void);
bool MeasureModel_UpdateFeaturePayload(const uint8_t *payload, uint16_t len,
                                       uint32_t now_ms);
bool MeasureModel_UpdateStatusPayload(const uint8_t *payload, uint16_t len,
                                      uint32_t now_ms);

const MeasureFeature *MeasureModel_Feature(void);
const MeasureFpgaStatus *MeasureModel_FpgaStatus(void);

#ifdef __cplusplus
}
#endif

#endif
