/*
 * File: emulate_engine.h
 * Role: Legacy 2025 equivalent-output engine public interface.
 * Scope: Retained for reference; not compiled into the 2026 G Target.
 */
#ifndef EMULATE_ENGINE_H
#define EMULATE_ENGINE_H

#include <stdint.h>
#include "rlc_model.h"
#include "signal_meas.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Advanced part (2): real-time equivalent output.
 *
 * The signal generator feeds J_MEAS (input impedance >= 100k front-end).
 * The engine measures frequency / waveform / duty, folds one period,
 * pushes its harmonics through the learned model H(jkw) and streams the
 * synthesized "unknown circuit output" on J_OUT via the software DDS.
 * First output appears well inside the 5 s budget; afterwards the input
 * is re-measured every ~2 s and the DDS frequency is re-locked so the
 * two traces stay drift-free on the scope.
 */

typedef enum {
    EMU_IDLE = 0,
    EMU_MEASURING,
    EMU_RUNNING,        /* DAC streaming */
    EMU_NO_MODEL,       /* input analyzed, but nothing learned yet */
    EMU_FAILED,
} EmuState;

enum {
    EMU_ERR_NONE      = 0,
    EMU_ERR_NO_INPUT  = 0x2201, /* no periodic signal on J_MEAS */
    EMU_ERR_FREQ_LOW  = 0x2202, /* fundamental below 800 Hz     */
    EMU_ERR_FREQ_HIGH = 0x2203, /* fundamental above 52 kHz     */
};

void Emu_Init(void);
void Emu_Start(const RlcModel *model, uint8_t wave_hint, uint32_t now_ms);
void Emu_Abort(void);
void Emu_Poll(uint32_t now_ms);

EmuState  Emu_State(void);
uint16_t  Emu_ErrCode(void);
const SigStats *Emu_Input(void);      /* last analyzed input        */
uint32_t  Emu_SnappedFreqHz(void);    /* 0 if off the 200 Hz grid   */
uint16_t  Emu_OutputVpp_mV(void);

#ifdef __cplusplus
}
#endif

#endif
