/*
 * File: measure_model.c
 * Role: Implements the 2026 G shared measurement data model.
 * Scope: Decodes FEATURE/STATUS payloads from the FPGA packet link.
 */
#include "measure_model.h"

#include "calibration.h"

#include <string.h>

static MeasureFeature s_feature;
static MeasureFpgaStatus s_status;

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int16_t rd_i16(const uint8_t *p)
{
    return (int16_t)rd_u16(p);
}

static int32_t rd_i32(const uint8_t *p)
{
    return (int32_t)rd_u32(p);
}

static uint32_t apply_freq_offset(uint32_t hz)
{
    float adjusted = (float)hz + Calibration_Get()->freq_offset_hz;
    if (adjusted < 0.0f) {
        adjusted = 0.0f;
    }
    return (uint32_t)(adjusted + 0.5f);
}

void MeasureModel_Init(void)
{
    memset(&s_feature, 0, sizeof(s_feature));
    memset(&s_status, 0, sizeof(s_status));
}

bool MeasureModel_UpdateFeaturePayload(const uint8_t *payload, uint16_t len,
                                       uint32_t now_ms)
{
    uint8_t mode;
    uint8_t status;
    uint8_t count;
    uint8_t flags;
    uint8_t phase_valid;

    if (payload == 0 || len != MEASURE_FEATURE_PAYLOAD_LEN) {
        return false;
    }

    mode = payload[4];
    status = payload[5];
    count = payload[6];
    flags = payload[7];
    if (mode > (uint8_t)MEASURE_MODE_UB_J ||
        status > (uint8_t)MEASURE_STATUS_LINK_OR_ALGO_ERROR ||
        count == 0u || count > MEASURE_COMPONENT_MAX) {
        return false;
    }

    phase_valid = (uint8_t)((flags & MEASURE_FEATURE_FLAG_PHASE_VALID) != 0u);

    s_feature.valid = 1u;
    s_feature.frame_id = rd_u32(&payload[0]);
    s_feature.mode = mode;
    s_feature.status = status;
    s_feature.component_count = count;
    s_feature.flags = flags;
    s_feature.vpp_mv =
        Calibration_ApplyVoltageMv((float)rd_i32(&payload[8]) / 1000.0f);
    s_feature.urms_mv =
        Calibration_ApplyVoltageMv((float)rd_i32(&payload[12]) / 1000.0f);
    s_feature.f1_hz = apply_freq_offset(rd_u32(&payload[16]));

    for (uint8_t i = 0u; i < MEASURE_COMPONENT_MAX; ++i) {
        uint16_t off = (uint16_t)(24u + (uint16_t)i * 12u);
        if (i < count) {
            s_feature.comp[i].freq_hz = apply_freq_offset(rd_u32(&payload[off]));
            s_feature.comp[i].amp_peak_mv =
                Calibration_ApplySpectrumMv((float)rd_i32(&payload[off + 4u]) /
                                            1000.0f);
            s_feature.comp[i].phase_deg10 =
                phase_valid ? rd_i16(&payload[off + 8u]) : 0;
        } else {
            s_feature.comp[i].freq_hz = 0u;
            s_feature.comp[i].amp_peak_mv = 0.0f;
            s_feature.comp[i].phase_deg10 = 0;
        }
    }
    s_feature.version++;
    s_feature.last_update_ms = now_ms;
    return true;
}

bool MeasureModel_UpdateStatusPayload(const uint8_t *payload, uint16_t len,
                                      uint32_t now_ms)
{
    if (payload == 0 || len != 12u) {
        return false;
    }
    s_status.valid = 1u;
    s_status.frame_id = rd_u32(&payload[0]);
    s_status.fpga_state = payload[4];
    s_status.fpga_error = payload[5];
    s_status.rx_crc_errors = rd_u16(&payload[6]);
    s_status.tx_drops = rd_u16(&payload[8]);
    s_status.version++;
    s_status.last_update_ms = now_ms;
    return true;
}

const MeasureFeature *MeasureModel_Feature(void)
{
    return &s_feature;
}

const MeasureFpgaStatus *MeasureModel_FpgaStatus(void)
{
    return &s_status;
}
