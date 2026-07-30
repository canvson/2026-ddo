/*
 * File: signal_meas.h
 * Role: Legacy 2025 signal-analysis helper interface.
 * Scope: Retained for reference; not compiled into the 2026 G Target.
 */
#ifndef SIGNAL_MEAS_H
#define SIGNAL_MEAS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portable signal analysis on raw ADC sample buffers.
 * No HAL dependency - unit-testable on a host PC.
 */

typedef enum {
    SIG_WAVE_NONE = 0,
    SIG_WAVE_SINE,
    SIG_WAVE_SQUARE,
    SIG_WAVE_TRIANGLE,
    SIG_WAVE_OTHER,
} SigWave;

typedef struct {
    float mv_per_lsb;   /* jack millivolts per ADC LSB */
} SigChanCal;

typedef struct {
    float    vmin_mv;
    float    vmax_mv;
    float    vpp_mv;       /* from period-folded composite (robust) */
    float    vmean_mv;
    float    vrms_ac_mv;   /* RMS of AC component */
    float    freq_hz;      /* 0 when not detectable */
    uint16_t duty_pct10;   /* high-time duty, 0.1 % units (500 = 50 %) */
    uint8_t  wave;         /* SigWave */
    uint16_t periods;      /* full periods seen in the buffer */
} SigStats;

/* Full analysis of one captured channel. */
void SigMeas_Analyze(const uint16_t *raw, uint32_t n, uint32_t fs_hz,
                     const SigChanCal *cal, SigStats *out);

/* AC RMS only (fast path for the learn sweep). */
float SigMeas_RmsAc(const uint16_t *raw, uint32_t n, const SigChanCal *cal);

/* Fold the buffer into one period of P bins (mV).  freq_hz must be the
 * measured fundamental.  Returns number of bins actually filled.        */
uint32_t SigMeas_FoldPeriod(const uint16_t *raw, uint32_t n, uint32_t fs_hz,
                            float freq_hz, const SigChanCal *cal,
                            float *period_mv, uint32_t P);

/* DFT of a folded period: k = 0..K.  re/im arrays hold K+1 entries; the
 * k-th pair is the complex coefficient c_k such that
 *   x(t) ~= c0 + sum_k 2*Re{ c_k * exp(j*k*w0*t) }.                     */
void SigMeas_Harmonics(const float *period_mv, uint32_t P, uint32_t K,
                       float *re, float *im);

/* Snap helpers for the generator grids given in the problem statement.  */
uint32_t SigMeas_SnapFreq200(float freq_hz);   /* 1k..50k, 200 Hz grid; 0 = off-grid */
uint16_t SigMeas_SnapDuty5(uint16_t duty_pct10); /* 10..50 %, 5 % grid, in 0.1 % units */

#ifdef __cplusplus
}
#endif

#endif
