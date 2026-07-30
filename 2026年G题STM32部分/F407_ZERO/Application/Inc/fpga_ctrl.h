/*
 * File: fpga_ctrl.h
 * Role: Legacy 2025 FPGA DDS control-table interface.
 * Scope: Retained for reference; the 2026 G Target uses fpga_link instead.
 */
#ifndef FPGA_CTRL_H
#define FPGA_CTRL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Downlink driver for the (unmodifiable) FPGA DDS project.
 *
 * The FPGA selects its working state with the on-board key (LED coded):
 *   state 0 Basic_two   : 1-byte frequency code, fixed ~3.5 Vpp sine
 *   state 1 Basic_three : fixed 1 kHz, amplitude pre-set for the 2 Vpp target
 *   state 2 Basic_four  : 2-byte (freq, target) code, see fpga_ctrl_table.h
 *   state 3 Develop_one : every received byte steps the 1 k..50 kHz sweep
 *   state 4 Develop_two : DAC A muxed to an undriven net - do not use
 *
 * The FPGA has no TX line back to the STM32; every send is fire-and-forget
 * and must never block the HMI flow.
 */

typedef void (*FpgaCtrlWriteFn)(const uint8_t *data, uint16_t len);

typedef struct {
    uint8_t sent;      /* 1 = bytes were written to the UART            */
    uint8_t code_ok;   /* 1 = the FPGA-side table entry is trustworthy  */
} FpgaCtrlResult;

/* Basic_two: 100..3000 Hz in 100 Hz steps, plus 1 MHz and 2 MHz.        */
uint8_t FpgaCtrl_Basic2Supported(uint32_t freq_hz);
uint8_t FpgaCtrl_SendBasic2Freq(FpgaCtrlWriteFn write, uint32_t freq_hz);

/* Basic_four: freq 100..3000 Hz step 100, target 1.0..2.0 Vpp step 0.1.
 * Sends a 0xFF flush byte first so the two-byte pair can never alias
 * against a stale byte in the FPGA shift register.                      */
FpgaCtrlResult FpgaCtrl_SendBasic4(FpgaCtrlWriteFn write,
                                   uint32_t freq_hz, uint16_t target_vpp10);

/* Develop_one: advance the sweep by one 100 Hz step (any byte value).   */
uint8_t FpgaCtrl_SendLearnStep(FpgaCtrlWriteFn write);

/* Legacy hook kept for the HMI STOP flow.  The FPGA has no stop opcode:
 * this deliberately writes nothing (a stray byte would corrupt the
 * Develop_one sweep counter or retune Basic_two to 100 Hz).             */
uint8_t FpgaCtrl_SendStopHint(FpgaCtrlWriteFn write);

#ifdef __cplusplus
}
#endif

#endif
