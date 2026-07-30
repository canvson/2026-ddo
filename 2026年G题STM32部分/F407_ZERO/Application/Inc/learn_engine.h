/*
 * File: learn_engine.h
 * Role: Legacy 2025 unknown-RLC learning engine public interface.
 * Scope: Retained for reference; not compiled into the 2026 G Target.
 */
#ifndef LEARN_ENGINE_H
#define LEARN_ENGINE_H

#include <stdint.h>
#include "rlc_model.h"
#include "fpga_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Advanced part (1): self-learning of the unknown RLC circuit.
 *
 * Prerequisites (operating procedure, see docs):
 *   - FPGA keyed into Develop_one (LED3 on) and RESET just before learning
 *     so its sweep counter starts at 1 kHz;
 *   - J_REF on the unknown-circuit INPUT (= FPGA drive),
 *     J_MEAS on the unknown-circuit OUTPUT.
 *
 * The engine steps the FPGA sweep byte-by-byte (1 k..50 kHz, 100 Hz грид,
 * 491 points), measures out/in RMS ratio per point, then classifies the
 * filter and fits f0/Q/R/L/C.  Runtime ~10 s, hard budget 110 s.
 */

typedef enum {
    LEARN_IDLE = 0,
    LEARN_RUNNING,
    LEARN_DONE,
    LEARN_FAILED,
} LearnState;

enum {
    LEARN_ERR_NONE        = 0,
    LEARN_ERR_NO_STIMULUS = 0x2101, /* no drive seen on J_REF/J_MEAS      */
    LEARN_ERR_NOT_RESET   = 0x2102, /* sweep does not start at 1 kHz      */
    LEARN_ERR_TIMEOUT     = 0x2103,
    LEARN_ERR_FIT         = 0x2104, /* could not classify the response    */
};

void Learn_Init(FpgaCtrlWriteFn fpga_write);
void Learn_Start(uint32_t now_ms);
void Learn_Abort(void);
void Learn_Poll(uint32_t now_ms);

LearnState  Learn_State(void);
uint8_t     Learn_Percent(void);
uint16_t    Learn_ErrCode(void);
uint32_t    Learn_CurrentFreqHz(void);
const RlcModel *Learn_Model(void);

#ifdef __cplusplus
}
#endif

#endif
