/*
 * File: rlc_model.c
 * Role: Legacy 2025 RLC model fitting implementation.
 * Scope: Retained for reference; not compiled into the 2026 G Target.
 */
#include "rlc_model.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * Fit strategy: model competition.
 *
 * All four canonical biquads (LP / HP / BP / BS) are fitted to the measured
 * log-magnitude curve over a (f0, Q) grid with the passband gain k solved
 * analytically per candidate; the candidate with the least squared error
 * wins.  This is robust for both low-Q topologies (series RLC, Q = 0.01..1,
 * broad responses) and high-Q topologies (LC tank / trap, Q = 1..100,
 * sharp peak or notch) that a single-threshold classifier confuses.
 */

static float sweep_freq(uint32_t i)
{
    return (float)(RLC_SWEEP_F0_HZ + RLC_SWEEP_DF_HZ * i);
}

/* |H| of a normalized (k=1) biquad of the given type */
static float biquad_mag(uint8_t type, float f, float f0, float q)
{
    float x = f / f0;
    float dr = 1.0f - x * x;
    float di = x / q;
    float den = sqrtf(dr * dr + di * di);
    float num;

    switch (type) {
    case RLC_TYPE_LOWPASS:  num = 1.0f;           break;
    case RLC_TYPE_HIGHPASS: num = x * x;          break;
    case RLC_TYPE_BANDPASS: num = x / q;          break;
    case RLC_TYPE_BANDSTOP:
    default:                num = fabsf(dr);      break;
    }
    if (den < 1.0e-9f) {
        den = 1.0e-9f;
    }
    num /= den;
    if (num < 1.0e-6f) {
        num = 1.0e-6f; /* keep the log defined near a notch */
    }
    return num;
}

#define FIT_DECIM   8u                     /* every 8th sweep point */
#define FIT_NPTS    (1u + (RLC_SWEEP_POINTS - 1u) / FIT_DECIM)  /* 62 */

typedef struct {
    float sse;
    float f0;
    float q;
    float logk;
} FitCand;

/* evaluate one (type, f0, q): optimal log-k is the mean log residual */
static void eval_cand(uint8_t type, float f0, float q,
                      const float *logg, const float *ff, FitCand *out)
{
    float diff[FIT_NPTS];
    float mean = 0.0f, sse = 0.0f;
    uint32_t i;

    for (i = 0u; i < FIT_NPTS; ++i) {
        float lh = logf(biquad_mag(type, ff[i], f0, q));
        diff[i] = logg[i] - lh;
        mean += diff[i];
    }
    mean /= (float)FIT_NPTS;
    for (i = 0u; i < FIT_NPTS; ++i) {
        float d = diff[i] - mean;
        sse += d * d;
    }
    out->sse = sse;
    out->f0 = f0;
    out->q = q;
    out->logk = mean;
}

static void grid_search(uint8_t type, const float *logg, const float *ff,
                        float f0_lo, float f0_hi, uint32_t nf,
                        float q_lo, float q_hi, uint32_t nq,
                        FitCand *best)
{
    uint32_t i, j;
    float lf_lo = logf(f0_lo), lf_hi = logf(f0_hi);
    float lq_lo = logf(q_lo), lq_hi = logf(q_hi);
    FitCand c;

    best->sse = 1.0e30f;
    for (i = 0u; i < nf; ++i) {
        float f0 = expf(lf_lo + (lf_hi - lf_lo) * (float)i / (float)(nf - 1u));
        for (j = 0u; j < nq; ++j) {
            float q = expf(lq_lo + (lq_hi - lq_lo) * (float)j / (float)(nq - 1u));
            eval_cand(type, f0, q, logg, ff, &c);
            if (c.sse < best->sse) {
                *best = c;
            }
        }
    }
}

/* pick a consistent in-range R/L/C triple from (f0, q, type) */
static void solve_rlc(RlcModel *m)
{
    float w0 = 2.0f * (float)M_PI * m->f0_hz;
    float best_score = 0.0f;
    uint8_t found = 0u;
    uint8_t relation_parallel;
    uint8_t pass;
    uint32_t li;

    /* series topologies: Q = sqrt(L/C)/R  (only reachable for Q <= ~1)
     * tank/trap topologies: Q = R*sqrt(C/L)                              */
    relation_parallel = (m->q > 1.05f) ? 1u : 0u;

    m->r_ohm = 0.0f;
    m->l_mh = 0.0f;
    m->c_nf = 0.0f;

    for (pass = 0u; pass < 2u && !found; ++pass) {
        uint8_t par = pass ? (uint8_t)!relation_parallel : relation_parallel;
        for (li = 0u; li <= 96u; ++li) {
            /* 0.95 .. 10.55 mH: 5 % soft margin absorbs fit error at the
             * catalog corners (e.g. a true 10 mH / 10 nF plant)          */
            float l_h = (0.95f + 0.1f * (float)li) * 1.0e-3f;
            float c_f = 1.0f / (w0 * w0 * l_h);
            float zc = sqrtf(l_h / c_f);
            float r_o = par ? (m->q * zc) : (zc / m->q);
            if (c_f < 9.5e-9f || c_f > 105.0e-9f) {
                continue;
            }
            if (r_o < 950.0f || r_o > 10500.0f) {
                continue;
            }
            {
                float sl = fabsf(logf(l_h / 3.16e-3f));
                float sc = fabsf(logf(c_f / 31.6e-9f));
                float sr = fabsf(logf(r_o / 3160.0f));
                float score = -(sl + sc + sr);
                if (!found || score > best_score) {
                    found = 1u;
                    best_score = score;
                    m->l_mh = l_h * 1000.0f;
                    m->c_nf = c_f * 1.0e9f;
                    m->r_ohm = r_o;
                }
            }
        }
    }
    if (!found) {
        /* out of catalog range: report an LC pair matching f0 anyway */
        float l_h = 3.3e-3f;
        float c_f = 1.0f / (w0 * w0 * l_h);
        float zc = sqrtf(l_h / c_f);
        m->l_mh = l_h * 1000.0f;
        m->c_nf = c_f * 1.0e9f;
        m->r_ohm = relation_parallel ? (m->q * zc) : (zc / m->q);
    }
}

uint8_t RlcModel_Fit(RlcModel *m)
{
    static float logg[FIT_NPTS];
    static float ff[FIT_NPTS];
    float g_max = 0.0f;
    uint32_t i;
    FitCand best_all;
    uint8_t best_type = RLC_TYPE_UNKNOWN;
    uint8_t t;

    if (m == 0) {
        return 0u;
    }
    m->valid = 0u;
    m->type = RLC_TYPE_UNKNOWN;

    for (i = 0u; i < RLC_SWEEP_POINTS; ++i) {
        if (m->mag[i] > g_max) {
            g_max = m->mag[i];
        }
    }
    if (g_max <= 1.0e-5f) {
        return 0u; /* dead measurement */
    }

    for (i = 0u; i < FIT_NPTS; ++i) {
        uint32_t src = i * FIT_DECIM;
        float g = m->mag[src];
        if (g < g_max * 1.0e-4f) {
            g = g_max * 1.0e-4f; /* floor for the log */
        }
        logg[i] = logf(g);
        ff[i] = sweep_freq(src);
    }

    best_all.sse = 1.0e30f;
    best_all.f0 = 10000.0f;
    best_all.q = 0.7f;
    best_all.logk = 0.0f;
    for (t = RLC_TYPE_LOWPASS; t <= RLC_TYPE_BANDSTOP; ++t) {
        FitCand c;
        grid_search(t, logg, ff, 2000.0f, 80000.0f, 30u,
                    0.03f, 40.0f, 26u, &c);
        /* local refinement around the winner (two zoom rounds) */
        {
            float span_f = 1.35f, span_q = 1.4f;
            uint8_t r;
            for (r = 0u; r < 2u; ++r) {
                FitCand c2;
                grid_search(t, logg, ff,
                            c.f0 / span_f, c.f0 * span_f, 9u,
                            c.q / span_q, c.q * span_q, 9u, &c2);
                if (c2.sse < c.sse) {
                    c = c2;
                }
                span_f = 1.0f + (span_f - 1.0f) * 0.35f;
                span_q = 1.0f + (span_q - 1.0f) * 0.35f;
            }
        }
        if (c.sse < best_all.sse) {
            best_all = c;
            best_type = t;
        }
    }

    m->type = best_type;
    m->f0_hz = best_all.f0;
    m->q = best_all.q;
    m->k_pass = expf(best_all.logk);

    solve_rlc(m);

    m->valid = 1u;
    return 1u;
}

/* ------------------------------------------------------------------ */
/* response evaluation                                                 */
/* ------------------------------------------------------------------ */

void RlcModel_Response(const RlcModel *m, float freq_hz, float *re, float *im)
{
    float w0, x, dr, di, den, nr, ni;

    if (re == 0 || im == 0) {
        return;
    }
    *re = 0.0f;
    *im = 0.0f;
    if (m == 0 || m->valid == 0u || m->f0_hz <= 0.0f) {
        return;
    }
    if (freq_hz <= 0.0f) {
        /* DC gain of the biquad */
        switch (m->type) {
        case RLC_TYPE_LOWPASS:
        case RLC_TYPE_BANDSTOP:
            *re = m->k_pass;
            break;
        default:
            *re = 0.0f;
            break;
        }
        return;
    }

    w0 = 2.0f * (float)M_PI * m->f0_hz;
    x = (2.0f * (float)M_PI * freq_hz) / w0;

    /* denominator: 1 - x^2 + j*x/Q */
    dr = 1.0f - x * x;
    di = x / m->q;
    den = dr * dr + di * di;
    if (den < 1.0e-12f) {
        den = 1.0e-12f;
    }

    switch (m->type) {
    case RLC_TYPE_LOWPASS:
        nr = 1.0f;
        ni = 0.0f;
        break;
    case RLC_TYPE_HIGHPASS:
        nr = -x * x;
        ni = 0.0f;
        break;
    case RLC_TYPE_BANDPASS:
        nr = 0.0f;
        ni = x / m->q;
        break;
    case RLC_TYPE_BANDSTOP:
    default:
        nr = 1.0f - x * x;
        ni = 0.0f;
        break;
    }

    /* H = k * n / d  ->  k * (n * conj(d)) / |d|^2 */
    *re = m->k_pass * (nr * dr + ni * di) / den;
    *im = m->k_pass * (ni * dr - nr * di) / den;
}

float RlcModel_Mag(const RlcModel *m, float freq_hz)
{
    float re, im;

    if (m == 0 || m->valid == 0u) {
        return 0.0f;
    }
    if (freq_hz >= (float)RLC_SWEEP_F0_HZ &&
        freq_hz <= sweep_freq(RLC_SWEEP_POINTS - 1u)) {
        float pos = (freq_hz - (float)RLC_SWEEP_F0_HZ) / (float)RLC_SWEEP_DF_HZ;
        uint32_t i0 = (uint32_t)pos;
        float frac = pos - (float)i0;
        if (i0 >= RLC_SWEEP_POINTS - 1u) {
            return m->mag[RLC_SWEEP_POINTS - 1u];
        }
        return m->mag[i0] + frac * (m->mag[i0 + 1u] - m->mag[i0]);
    }
    RlcModel_Response(m, freq_hz, &re, &im);
    return sqrtf(re * re + im * im);
}

const char *RlcModel_TypeText(uint8_t type)
{
    switch (type) {
    case RLC_TYPE_LOWPASS:  return "LOW-PASS";
    case RLC_TYPE_HIGHPASS: return "HIGH-PASS";
    case RLC_TYPE_BANDPASS: return "BAND-PASS";
    case RLC_TYPE_BANDSTOP: return "BAND-STOP";
    default:                return "UNKNOWN";
    }
}
