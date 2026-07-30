/*
 * File: fpga_ctrl_table.h
 * Role: Legacy 2025 FPGA Basic_four lookup-table declarations.
 * Scope: Retained source data; not part of the 2026 G packet protocol.
 */
#ifndef FPGA_CTRL_TABLE_H
#define FPGA_CTRL_TABLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Basic_four two-byte command table, auto-extracted from the FPGA project
 * (project.srcs/sources_1/new/DDS_AD9767.v, state Basic_four).
 *
 * The FPGA receives bytes over UART into a shift register:
 *   Rx_Data_0 = newest byte, Rx_Data_1 = previous byte,
 * and decodes case({Rx_Data_1, Rx_Data_0}).  To select an entry the STM32
 * therefore sends: hi first, then lo.
 *
 * code_ok == 0 marks the 10 entries (1700 Hz, 1.1 .. 2.0 Vpp) that carry a
 * copy-paste bug inside the FPGA table: the FPGA sets the 1600 Hz frequency
 * word for them.  The FPGA must not be modified, so for those settings the
 * firmware falls back to the STM32 DAC auxiliary output (exact frequency and
 * amplitude) and reports the substitution on the HMI.
 */

#define FPGA_B4_NFREQ 30u  /* 100 Hz .. 3000 Hz, 100 Hz step */
#define FPGA_B4_NVPP  11u  /* 1.0 Vpp .. 2.0 Vpp, 0.1 V step */

typedef struct {
    uint8_t hi;      /* first byte to send (matches Rx_Data_1) */
    uint8_t lo;      /* second byte to send (matches Rx_Data_0) */
    uint8_t code_ok; /* 1 = FPGA output correct, 0 = FPGA-side table bug */
} FpgaBasic4Code;

extern const FpgaBasic4Code g_fpga_basic4[FPGA_B4_NFREQ][FPGA_B4_NVPP];

#ifdef __cplusplus
}
#endif

#endif
