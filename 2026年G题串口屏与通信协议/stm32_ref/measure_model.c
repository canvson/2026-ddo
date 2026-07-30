#include "measure_model.h"

#include <string.h>

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

void MeasureModel_Init(MeasureModel *model)
{
    if (model != 0) {
        memset(model, 0, sizeof(*model));
    }
}

bool MeasureModel_UpdateFeaturePayload(MeasureModel *model, const uint8_t *payload,
                                       uint16_t len)
{
    uint8_t mode;
    uint8_t status;
    uint8_t count;
    uint8_t flags;
    uint8_t phase_valid;

    if (model == 0 || payload == 0 || len != MEASURE_FEATURE_PAYLOAD_LEN) {
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

    model->feature.valid = 1u;
    model->feature.frame_id = rd_u32(&payload[0]);
    model->feature.mode = mode;
    model->feature.status = status;
    model->feature.component_count = count;
    model->feature.flags = flags;
    model->feature.vpp_mv = (float)rd_i32(&payload[8]) / 1000.0f;
    model->feature.urms_mv = (float)rd_i32(&payload[12]) / 1000.0f;
    model->feature.f1_hz = rd_u32(&payload[16]);

    for (uint8_t i = 0u; i < MEASURE_COMPONENT_MAX; ++i) {
        uint16_t off = (uint16_t)(24u + (uint16_t)i * 12u);
        if (i < count) {
            model->feature.comp[i].freq_hz = rd_u32(&payload[off]);
            model->feature.comp[i].amp_peak_mv =
                (float)rd_i32(&payload[off + 4u]) / 1000.0f;
            model->feature.comp[i].phase_deg10 =
                phase_valid ? rd_i16(&payload[off + 8u]) : 0;
        } else {
            model->feature.comp[i].freq_hz = 0u;
            model->feature.comp[i].amp_peak_mv = 0.0f;
            model->feature.comp[i].phase_deg10 = 0;
        }
    }
    return true;
}

bool MeasureModel_UpdateStatusPayload(MeasureModel *model, const uint8_t *payload,
                                      uint16_t len)
{
    if (model == 0 || payload == 0 || len != 12u) {
        return false;
    }
    model->status.valid = 1u;
    model->status.frame_id = rd_u32(&payload[0]);
    model->status.fpga_state = payload[4];
    model->status.fpga_error = payload[5];
    model->status.rx_crc_errors = rd_u16(&payload[6]);
    model->status.tx_drops = rd_u16(&payload[8]);
    return true;
}
