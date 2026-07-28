#ifndef RLC_MODEL_H
#define RLC_MODEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Second-order model of the "unknown model circuit" (one R, one L, one C).
 * Fitted from the 1 k..50 kHz magnitude sweep, then evaluated analytically
 * (magnitude AND phase) for waveform synthesis.
 */

typedef enum {
    RLC_TYPE_UNKNOWN = 0,
    RLC_TYPE_LOWPASS,
    RLC_TYPE_HIGHPASS,
    RLC_TYPE_BANDPASS,
    RLC_TYPE_BANDSTOP,
} RlcFilterType;

#define RLC_SWEEP_POINTS 491u   /* 1000 Hz .. 50000 Hz, 100 Hz step */
#define RLC_SWEEP_F0_HZ  1000u
#define RLC_SWEEP_DF_HZ  100u

typedef struct {
    uint8_t  valid;
    uint8_t  type;        /* RlcFilterType */
    float    f0_hz;       /* natural / center / corner frequency */
    float    q;           /* quality factor of the fitted biquad */
    float    k_pass;      /* passband gain (~1 for passive RLC) */
    /* consistent component estimate inside the stated ranges */
    float    r_ohm;       /* 1k .. 10k   (0 = not resolvable) */
    float    l_mh;        /* 1 .. 10 mH */
    float    c_nf;        /* 10 .. 100 nF */
    /* measured magnitude grid |G(f)|, normalized to the drive */
    float    mag[RLC_SWEEP_POINTS];
} RlcModel;

/* Classify + fit from the measured magnitude grid (mag entries already
 * normalized: out_rms / in_rms).  Returns 1 on success.                 */
uint8_t RlcModel_Fit(RlcModel *m);

/* Complex response of the fitted biquad at freq_hz.                     */
void RlcModel_Response(const RlcModel *m, float freq_hz,
                       float *re, float *im);

/* Magnitude helper: prefers the measured grid inside 1k..50k (linear
 * interpolation), falls back to the fitted biquad outside it.           */
float RlcModel_Mag(const RlcModel *m, float freq_hz);

const char *RlcModel_TypeText(uint8_t type); /* "LOW-PASS" ... */

#ifdef __cplusplus
}
#endif

#endif
