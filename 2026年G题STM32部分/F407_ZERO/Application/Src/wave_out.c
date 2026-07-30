/*
 * File: wave_out.c
 * Role: Legacy 2025 software DDS and DAC output implementation.
 * Scope: Retained for reference; DAC streaming is not started by 2026 G code.
 */
#include "wave_out.h"
#include "board_pins.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint16_t s_table[WAVEOUT_TABLE_LEN]; /* DAC codes, one period      */
static volatile uint32_t s_phase;           /* Q32 phase accumulator      */
static volatile uint32_t s_step;            /* Q32 phase step per sample  */
static volatile uint8_t  s_enabled;
static uint16_t s_table_vpp_mv;

#define DAC_MID_CODE 2048

static uint16_t mv_to_code(float mv)
{
    float c = 2048.0f + mv / (float)CAL_DAC_MV_PER_LSB;
    if (c < 0.0f) {
        c = 0.0f;
    }
    if (c > 4095.0f) {
        c = 4095.0f;
    }
    return (uint16_t)(c + 0.5f);
}

void WaveOut_LoadWave(const float *period_mv, uint32_t n)
{
    uint32_t i;
    float vmin = 0.0f, vmax = 0.0f;

    if (period_mv == 0 || n < 2u) {
        return;
    }

    vmin = vmax = period_mv[0];
    for (i = 1u; i < n; ++i) {
        if (period_mv[i] < vmin) vmin = period_mv[i];
        if (period_mv[i] > vmax) vmax = period_mv[i];
    }
    {
        float span = vmax - vmin;
        s_table_vpp_mv = (uint16_t)((span < 0.0f) ? 0.0f :
                         (span > 65535.0f) ? 65535.0f : span);
    }

    /* linear resample n -> WAVEOUT_TABLE_LEN */
    for (i = 0u; i < WAVEOUT_TABLE_LEN; ++i) {
        float pos = (float)i * (float)n / (float)WAVEOUT_TABLE_LEN;
        uint32_t i0 = (uint32_t)pos;
        float frac = pos - (float)i0;
        uint32_t i1 = (i0 + 1u) % n;
        float v = period_mv[i0] + frac * (period_mv[i1] - period_mv[i0]);
        s_table[i] = mv_to_code(v);
    }
}

void WaveOut_BuildSine(uint16_t amp_mvpp)
{
    uint32_t i;
    float a = 0.5f * (float)amp_mvpp;
    for (i = 0u; i < WAVEOUT_TABLE_LEN; ++i) {
        float v = a * sinf(2.0f * (float)M_PI * (float)i / (float)WAVEOUT_TABLE_LEN);
        s_table[i] = mv_to_code(v);
    }
    s_table_vpp_mv = amp_mvpp;
}

void WaveOut_BuildSquare(uint16_t amp_mvpp, uint16_t duty_pct10)
{
    uint32_t i;
    uint32_t high = ((uint32_t)duty_pct10 * WAVEOUT_TABLE_LEN) / 1000u;
    float a = 0.5f * (float)amp_mvpp;
    if (high == 0u) {
        high = 1u;
    }
    if (high >= WAVEOUT_TABLE_LEN) {
        high = WAVEOUT_TABLE_LEN - 1u;
    }
    for (i = 0u; i < WAVEOUT_TABLE_LEN; ++i) {
        s_table[i] = mv_to_code((i < high) ? a : -a);
    }
    s_table_vpp_mv = amp_mvpp;
}

void WaveOut_SetFreqMilliHz(uint64_t freq_mHz)
{
    /* step = f * 2^32 / fs, computed in millihertz for resolution */
    uint64_t num = freq_mHz << 32;
    uint64_t den = (uint64_t)WAVEOUT_FS_HZ * 1000u;
    s_step = (uint32_t)(num / den);
}

uint64_t WaveOut_GetFreqMilliHz(void)
{
    /* f = step * fs / 2^32, back in millihertz */
    return ((uint64_t)s_step * (uint64_t)WAVEOUT_FS_HZ * 1000u) >> 32;
}

void WaveOut_Enable(uint8_t on)
{
    if (on) {
        s_phase = 0u;
    }
    s_enabled = on ? 1u : 0u;
}

uint8_t WaveOut_IsEnabled(void)
{
    return s_enabled;
}

uint16_t WaveOut_TableVpp(void)
{
    return s_table_vpp_mv;
}

void WaveOut_FillBlock(uint16_t *dst, uint32_t n)
{
    uint32_t i;
    uint32_t ph = s_phase;
    uint32_t st = s_step;

    if (dst == 0) {
        return;
    }
    if (!s_enabled || st == 0u) {
        for (i = 0u; i < n; ++i) {
            dst[i] = DAC_MID_CODE;
        }
        s_phase = 0u;
        return;
    }
    for (i = 0u; i < n; ++i) {
        dst[i] = s_table[ph >> 20]; /* 2^32 / 4096 = 2^20 */
        ph += st;
    }
    s_phase = ph;
}
