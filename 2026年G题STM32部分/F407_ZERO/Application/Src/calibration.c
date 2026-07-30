/*
 * File: calibration.c
 * Role: Provides default display calibration and value scaling helpers.
 * Scope: Voltage/spectrum gains, frequency offset and CRC calculation.
 */
#include "calibration.h"

#include <string.h>

static CalibrationData s_cal;

uint32_t Calibration_CalcCrc(const CalibrationData *cal)
{
    const uint8_t *p = (const uint8_t *)cal;
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t n;

    if (cal == 0) {
        return 0u;
    }

    n = (uint32_t)(sizeof(CalibrationData) - sizeof(cal->crc));
    for (uint32_t i = 0u; i < n; ++i) {
        crc ^= p[i];
        for (uint8_t b = 0u; b < 8u; ++b) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

void Calibration_ResetDefaults(void)
{
    memset(&s_cal, 0, sizeof(s_cal));
    s_cal.magic = CALIBRATION_MAGIC;
    s_cal.voltage_gain = 1.0f;
    s_cal.spectrum_gain = 1.0f;
    s_cal.freq_offset_hz = 0.0f;
    s_cal.crc = Calibration_CalcCrc(&s_cal);
}

void Calibration_Init(void)
{
    Calibration_ResetDefaults();
}

const CalibrationData *Calibration_Get(void)
{
    return &s_cal;
}

float Calibration_ApplyVoltageMv(float mv)
{
    return mv * s_cal.voltage_gain;
}

float Calibration_ApplySpectrumMv(float mv)
{
    return mv * s_cal.spectrum_gain;
}
