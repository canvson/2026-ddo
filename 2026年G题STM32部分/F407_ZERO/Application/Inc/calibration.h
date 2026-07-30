/*
 * File: calibration.h
 * Role: Display calibration data model for measured values.
 * Scope: Default gains, frequency offset and reserved CRC-backed storage API.
 */
#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CALIBRATION_MAGIC 0x47323032u

typedef struct {
    uint32_t magic;
    float voltage_gain;
    float spectrum_gain;
    float freq_offset_hz;
    uint32_t crc;
} CalibrationData;

void Calibration_Init(void);
void Calibration_ResetDefaults(void);
const CalibrationData *Calibration_Get(void);
float Calibration_ApplyVoltageMv(float mv);
float Calibration_ApplySpectrumMv(float mv);
uint32_t Calibration_CalcCrc(const CalibrationData *cal);

#ifdef __cplusplus
}
#endif

#endif
