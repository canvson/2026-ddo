/*
 * File: signal_meas.c
 * Role: Legacy 2025 local-sampling signal analyzer implementation.
 * Scope: Retained for reference; 2026 G signal results are decoded from FPGA.
 */
#include "signal_meas.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* basic statistics                                                    */
/* ------------------------------------------------------------------ */

static void raw_stats(const uint16_t *raw, uint32_t n, float scale,
                      float *vmin, float *vmax, float *vmean, float *vrms_ac)
{
    uint32_t i;
    uint16_t mn = 0xFFFFu, mx = 0u;
    double sum = 0.0, sum2 = 0.0;
    double mean;

    for (i = 0u; i < n; ++i) {
        uint16_t v = raw[i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += (double)v;
    }
    mean = sum / (double)n;
    for (i = 0u; i < n; ++i) {
        double d = (double)raw[i] - mean;
        sum2 += d * d;
    }
    *vmin = (float)mn * scale;
    *vmax = (float)mx * scale;
    *vmean = (float)(mean * scale);
    *vrms_ac = (float)(sqrt(sum2 / (double)n) * scale);
}

float SigMeas_RmsAc(const uint16_t *raw, uint32_t n, const SigChanCal *cal)
{
    float mn, mx, mean, rms;
    if (raw == 0 || n == 0u) {
        return 0.0f;
    }
    raw_stats(raw, n, cal ? cal->mv_per_lsb : 1.0f, &mn, &mx, &mean, &rms);
    return rms;
}

/* ------------------------------------------------------------------ */
/* frequency: hysteresis rising-edge crossings + linear interpolation  */
/* ------------------------------------------------------------------ */

static float measure_freq(const uint16_t *raw, uint32_t n, uint32_t fs_hz,
                          float mid_lsb, float hyst_lsb, uint32_t *periods)
{
    uint32_t i;
    uint8_t armed = 0u; /* armed after the signal has been below mid-hyst */
    double first_x = -1.0, last_x = -1.0;
    uint32_t count = 0u;
    float lo = mid_lsb - hyst_lsb;
    float hi = mid_lsb + hyst_lsb;

    *periods = 0u;

    for (i = 1u; i < n; ++i) {
        float a = (float)raw[i - 1u];
        float b = (float)raw[i];
        if (a < lo) {
            armed = 1u;
        }
        if (armed && a < hi && b >= hi) {
            /* sub-sample crossing of the "hi" threshold */
            double frac = (double)(hi - a) / (double)(b - a);
            double x = (double)(i - 1u) + frac;
            if (count == 0u) {
                first_x = x;
            }
            last_x = x;
            ++count;
            armed = 0u;
        }
    }
    if (count < 2u || last_x <= first_x) {
        return 0.0f;
    }
    *periods = count - 1u;
    return (float)((double)(count - 1u) * (double)fs_hz / (last_x - first_x));
}

/* ------------------------------------------------------------------ */
/* period folding                                                      */
/* ------------------------------------------------------------------ */

uint32_t SigMeas_FoldPeriod(const uint16_t *raw, uint32_t n, uint32_t fs_hz,
                            float freq_hz, const SigChanCal *cal,
                            float *period_mv, uint32_t P)
{
    uint32_t i, filled = 0u;
    float scale = cal ? cal->mv_per_lsb : 1.0f;
    double phase_per_sample;
    double ph = 0.0;

    /* accumulate mean per phase bin */
    static float acc[1024];
    static uint16_t cnt[1024];

    if (raw == 0 || period_mv == 0 || P == 0u || P > 1024u ||
        freq_hz <= 0.0f || fs_hz == 0u) {
        return 0u;
    }

    for (i = 0u; i < P; ++i) {
        acc[i] = 0.0f;
        cnt[i] = 0u;
    }

    phase_per_sample = (double)freq_hz * (double)P / (double)fs_hz;

    for (i = 0u; i < n; ++i) {
        uint32_t bin = (uint32_t)ph % P;
        acc[bin] += (float)raw[i];
        if (cnt[bin] < 0xFFFFu) {
            ++cnt[bin];
        }
        ph += phase_per_sample;
        if (ph >= 1.0e9) {          /* keep the double well conditioned */
            ph = fmod(ph, (double)P);
        }
    }

    for (i = 0u; i < P; ++i) {
        if (cnt[i] > 0u) {
            period_mv[i] = (acc[i] / (float)cnt[i]) * scale;
            ++filled;
        } else {
            period_mv[i] = (i > 0u) ? period_mv[i - 1u] : 0.0f;
        }
    }
    return filled;
}

/* ------------------------------------------------------------------ */
/* harmonics of a folded period                                        */
/* ------------------------------------------------------------------ */

void SigMeas_Harmonics(const float *period_mv, uint32_t P, uint32_t K,
                       float *re, float *im)
{
    uint32_t k, i;

    if (period_mv == 0 || re == 0 || im == 0 || P == 0u) {
        return;
    }

    for (k = 0u; k <= K; ++k) {
        double sr = 0.0, si = 0.0;
        double w = 2.0 * M_PI * (double)k / (double)P;
        for (i = 0u; i < P; ++i) {
            double ang = w * (double)i;
            sr += (double)period_mv[i] * cos(ang);
            si -= (double)period_mv[i] * sin(ang);
        }
        re[k] = (float)(sr / (double)P);
        im[k] = (float)(si / (double)P);
    }
}

/* ------------------------------------------------------------------ */
/* waveform classification                                             */
/* ------------------------------------------------------------------ */

static uint8_t classify_wave(const float *per, uint32_t P,
                             float vmin, float vmax, float vrms_ac)
{
    uint32_t i;
    float span = vmax - vmin;
    float mid = 0.5f * (vmax + vmin);
    uint32_t near_rail = 0u;
    float peak = 0.0f;
    float crest;

    if (span < 30.0f || vrms_ac < 10.0f) {
        return SIG_WAVE_NONE;
    }

    for (i = 0u; i < P; ++i) {
        float v = per[i];
        float d = v - mid;
        if (d < 0.0f) {
            d = -d;
        }
        if (d > peak) {
            peak = d;
        }
        if (v > vmax - 0.12f * span || v < vmin + 0.12f * span) {
            ++near_rail;
        }
    }

    crest = (vrms_ac > 1.0f) ? (peak / vrms_ac) : 99.0f;

    /* square: nearly all samples sit at one of the two rails */
    if ((float)near_rail > 0.72f * (float)P) {
        return SIG_WAVE_SQUARE;
    }
    /* sine: crest factor ~ sqrt(2) */
    if (crest > 1.25f && crest < 1.58f) {
        return SIG_WAVE_SINE;
    }
    /* triangle: crest factor ~ sqrt(3) */
    if (crest >= 1.58f && crest < 1.90f) {
        return SIG_WAVE_TRIANGLE;
    }
    return SIG_WAVE_OTHER;
}

/* ------------------------------------------------------------------ */
/* full analysis                                                       */
/* ------------------------------------------------------------------ */

void SigMeas_Analyze(const uint16_t *raw, uint32_t n, uint32_t fs_hz,
                     const SigChanCal *cal, SigStats *out)
{
    static float per[1024];
    float scale = cal ? cal->mv_per_lsb : 1.0f;
    float vmin, vmax, vmean, vrms;
    float mid_lsb, hyst_lsb, freq;
    uint32_t periods = 0u;
    uint32_t i, above;
    const uint32_t P = 1024u;

    if (out == 0) {
        return;
    }
    out->vmin_mv = out->vmax_mv = out->vpp_mv = 0.0f;
    out->vmean_mv = out->vrms_ac_mv = 0.0f;
    out->freq_hz = 0.0f;
    out->duty_pct10 = 0u;
    out->wave = SIG_WAVE_NONE;
    out->periods = 0u;

    if (raw == 0 || n < 64u || fs_hz == 0u) {
        return;
    }

    raw_stats(raw, n, scale, &vmin, &vmax, &vmean, &vrms);
    out->vmin_mv = vmin;
    out->vmax_mv = vmax;
    out->vmean_mv = vmean;
    out->vrms_ac_mv = vrms;

    mid_lsb = 0.5f * (vmin + vmax) / scale;
    hyst_lsb = 0.06f * (vmax - vmin) / scale;
    if (hyst_lsb < 4.0f) {
        hyst_lsb = 4.0f;
    }

    freq = measure_freq(raw, n, fs_hz, mid_lsb, hyst_lsb, &periods);
    out->freq_hz = freq;
    out->periods = (uint16_t)((periods > 0xFFFFu) ? 0xFFFFu : periods);

    if (freq <= 0.0f) {
        out->vpp_mv = vmax - vmin;
        return;
    }

    /* fold into one period for a dense composite waveform */
    (void)SigMeas_FoldPeriod(raw, n, fs_hz, freq, cal, per, P);

    {
        float pmin = per[0], pmax = per[0];
        float pmid;
        for (i = 1u; i < P; ++i) {
            if (per[i] < pmin) pmin = per[i];
            if (per[i] > pmax) pmax = per[i];
        }
        out->vpp_mv = pmax - pmin;

        pmid = 0.5f * (pmin + pmax);
        above = 0u;
        for (i = 0u; i < P; ++i) {
            if (per[i] > pmid) {
                ++above;
            }
        }
        out->duty_pct10 = (uint16_t)((above * 1000u) / P);
        out->wave = classify_wave(per, P, pmin, pmax, out->vrms_ac_mv);
    }
}

/* ------------------------------------------------------------------ */
/* grid snapping                                                       */
/* ------------------------------------------------------------------ */

uint32_t SigMeas_SnapFreq200(float freq_hz)
{
    uint32_t snapped;
    float err;

    if (freq_hz < 800.0f || freq_hz > 52000.0f) {
        return 0u;
    }
    snapped = (uint32_t)((freq_hz / 200.0f) + 0.5f) * 200u;
    if (snapped < 1000u || snapped > 50000u) {
        return 0u;
    }
    err = freq_hz - (float)snapped;
    if (err < 0.0f) {
        err = -err;
    }
    /* accept up to 1.5 % deviation from the grid */
    return (err <= 0.015f * (float)snapped) ? snapped : 0u;
}

uint16_t SigMeas_SnapDuty5(uint16_t duty_pct10)
{
    uint16_t snapped = (uint16_t)(((duty_pct10 + 25u) / 50u) * 50u);
    if (snapped < 100u) {
        snapped = 100u;
    }
    if (snapped > 900u) {
        snapped = 900u;
    }
    return snapped;
}
