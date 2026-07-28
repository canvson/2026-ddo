#ifndef FPGA_CTRL_H
#define FPGA_CTRL_H

#include "hmi_protocol.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FPGA_FRAME_SOF             0xA5u
#define FPGA_FRAME_EOF             0x5Au
#define FPGA_CMD_DUAL_WAVE_CONFIG  0x41u
#define FPGA_DUAL_WAVE_DATA_LEN    28u
#define FPGA_DUAL_WAVE_FRAME_LEN   (FPGA_DUAL_WAVE_DATA_LEN + 5u)
#define FPGA_DDS_CLK_HZ            125000000u
#define FPGA_AMP_Q13_FULL          8192u

typedef void (*FpgaCtrlWriteFn)(const uint8_t *data, uint16_t len);

uint32_t FpgaCtrl_FwordFromHz(uint32_t freq_hz);
uint16_t FpgaCtrl_AmpToQ13(uint16_t amp_mVpp);
uint32_t FpgaCtrl_DutyToQ32(uint16_t duty_pct10);
uint32_t FpgaCtrl_PhaseDegToQ32(int16_t phase_deg);
uint8_t FpgaCtrl_Checksum(uint8_t cmd, const uint8_t *data, uint8_t len);
size_t FpgaCtrl_BuildDualWaveFrame(const DualWaveOutputConfig *cfg,
                                   uint8_t *out, size_t out_cap);
uint8_t FpgaCtrl_SendDualWaveConfig(FpgaCtrlWriteFn write,
                                    const DualWaveOutputConfig *cfg);
uint8_t FpgaCtrl_SendDisabled(FpgaCtrlWriteFn write,
                              const DualWaveOutputConfig *last_cfg);

#ifdef __cplusplus
}
#endif

#endif
