#ifndef WAVE_OUT_H
#define WAVE_OUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Software-DDS waveform engine feeding the on-chip DAC through a circular
 * DMA buffer (TIM6 trigger, fixed sample rate WAVEOUT_FS_HZ).
 *
 * The DMA half/complete interrupts call WaveOut_FillBlock() to render the
 * next half buffer from a 4096-entry period table with a 32-bit phase
 * accumulator, so the generated frequency has ~0.25 uHz resolution and no
 * cumulative drift against the measured input frequency.
 *
 * The engine itself is portable (no HAL); Core/Src/dac.c owns the DMA.
 */

#define WAVEOUT_TABLE_LEN 4096u

/* Load one period given in jack-millivolts (arbitrary length, resampled). */
void WaveOut_LoadWave(const float *period_mv, uint32_t n);

/* Convenience builders (amplitude in mVpp at the jack, dc in mV).        */
void WaveOut_BuildSine(uint16_t amp_mvpp);
void WaveOut_BuildSquare(uint16_t amp_mvpp, uint16_t duty_pct10);

/* Frequency control, millihertz.  Safe to call while running.            */
void WaveOut_SetFreqMilliHz(uint64_t freq_mHz);
uint64_t WaveOut_GetFreqMilliHz(void);   /* actual DDS frequency         */

/* Rendering state. */
void    WaveOut_Enable(uint8_t on);       /* on=0 renders mid-rail silence */
uint8_t WaveOut_IsEnabled(void);

/* Called from the DAC DMA ISR context.                                   */
void WaveOut_FillBlock(uint16_t *dst, uint32_t n);

/* Peak-peak of the currently loaded table, jack mV (for the UI).         */
uint16_t WaveOut_TableVpp(void);

#ifdef __cplusplus
}
#endif

#endif
