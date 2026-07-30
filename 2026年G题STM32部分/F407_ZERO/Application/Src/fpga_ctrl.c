/*
 * File: fpga_ctrl.c
 * Role: Legacy 2025 FPGA DDS control-byte sender.
 * Scope: Retained for reference; 2026 G control packets use fpga_link.c.
 */
#include "fpga_ctrl.h"
#include "fpga_ctrl_table.h"

/*
 * Byte codes verified against DDS_AD9767.v (FPGA project is read-only):
 *
 * Basic_two, case(Rx_Data_0):
 *   n = freq/100 - 1 (0..29) written as "decimal digits read as hex":
 *   100..1000 Hz -> 0x00..0x09, 1100..2000 -> 0x10..0x19, 2100..3000 -> 0x20..0x29
 *   1 MHz -> 0x30, 2 MHz -> 0x31, anything else -> default 100 Hz.
 *
 * Basic_four, case({Rx_Data_1, Rx_Data_0}): see fpga_ctrl_table.c.
 *   The case has NO default branch, so unmatched pairs simply hold the
 *   previous output.  A leading 0xFF flush byte therefore guarantees the
 *   {flush, hi} intermediate pair never matches a real entry
 *   (no entry uses hi = 0xFF, and every real hi is 0x01/0x02).
 *
 * Develop_one: fre_cnt increments on every received byte (saturates at 490,
 *   only a hardware reset rewinds it) - one byte == one 100 Hz sweep step.
 */

static uint8_t send_bytes(FpgaCtrlWriteFn write, const uint8_t *bytes, uint16_t len)
{
    if (write == 0) {
        return 0u;
    }
    write(bytes, len);
    return 1u;
}

uint8_t FpgaCtrl_Basic2Supported(uint32_t freq_hz)
{
    if (freq_hz == 1000000u || freq_hz == 2000000u) {
        return 1u;
    }
    return (freq_hz >= 100u && freq_hz <= 3000u && (freq_hz % 100u) == 0u) ? 1u : 0u;
}

uint8_t FpgaCtrl_SendBasic2Freq(FpgaCtrlWriteFn write, uint32_t freq_hz)
{
    uint8_t code;
    uint32_t n;

    if (freq_hz == 1000000u) {
        code = 0x30u;
    } else if (freq_hz == 2000000u) {
        code = 0x31u;
    } else if (freq_hz >= 100u && freq_hz <= 3000u && (freq_hz % 100u) == 0u) {
        n = (freq_hz / 100u) - 1u;
        code = (uint8_t)(((n / 10u) << 4) | (n % 10u));
    } else {
        return 0u;
    }
    return send_bytes(write, &code, 1u);
}

FpgaCtrlResult FpgaCtrl_SendBasic4(FpgaCtrlWriteFn write,
                                   uint32_t freq_hz, uint16_t target_vpp10)
{
    FpgaCtrlResult res = {0u, 0u};
    uint32_t fi;
    uint32_t vi;
    uint8_t frame[3];

    if (freq_hz < 100u || freq_hz > 3000u || (freq_hz % 100u) != 0u) {
        return res;
    }
    if (target_vpp10 < 10u || target_vpp10 > 20u) {
        return res;
    }

    fi = (freq_hz / 100u) - 1u;
    vi = (uint32_t)target_vpp10 - 10u;

    frame[0] = 0xFFu; /* flush: {stale, 0xFF} and {0xFF, hi} match nothing */
    frame[1] = g_fpga_basic4[fi][vi].hi;
    frame[2] = g_fpga_basic4[fi][vi].lo;

    res.sent = send_bytes(write, frame, 3u);
    res.code_ok = g_fpga_basic4[fi][vi].code_ok;
    return res;
}

uint8_t FpgaCtrl_SendLearnStep(FpgaCtrlWriteFn write)
{
    static const uint8_t step = 0x5Au; /* value is ignored by the FPGA */
    return send_bytes(write, &step, 1u);
}

uint8_t FpgaCtrl_SendStopHint(FpgaCtrlWriteFn write)
{
    (void)write; /* intentionally silent - see header */
    return 1u;
}
