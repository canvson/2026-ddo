/*
 * File: signal_capture.h
 * Role: Legacy 2025 dual-ADC capture service contract.
 * Scope: Retained for reference; 2026 G measurement data comes from FPGA.
 */
#ifndef SIGNAL_CAPTURE_H
#define SIGNAL_CAPTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Capture service contract.
 * Implemented by Core/Src/adc.c on target (ADC1+ADC2 dual simultaneous,
 * TIM2 TRGO trigger, DMA2 Stream0); implemented by a stub in host tests.
 *
 *   MAIN = ADC1 / PA1 / J_MEAS
 *   REF  = ADC2 / PB0 / J_REF
 */

#define CAP_MAX_SAMPLES 4096u
#define CAP_MIN_FS_HZ   2000u
#define CAP_MAX_FS_HZ   700000u

/* Start a capture; returns 1 on success (0 while busy / bad args). */
uint8_t  Cap_Start(uint32_t fs_hz, uint32_t n_samples);
uint8_t  Cap_Busy(void);
uint32_t Cap_Fs(void);        /* actual sample rate of the last capture   */
uint32_t Cap_Count(void);     /* samples per channel of the last capture  */
const uint16_t *Cap_Main(void);
const uint16_t *Cap_Ref(void);

#ifdef __cplusplus
}
#endif

#endif
