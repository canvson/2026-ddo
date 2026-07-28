#include "fpga_ctrl.h"

static void wr_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

uint32_t FpgaCtrl_FwordFromHz(uint32_t freq_hz)
{
    uint64_t n = ((uint64_t)freq_hz << 32) + (FPGA_DDS_CLK_HZ / 2u);
    return (uint32_t)(n / FPGA_DDS_CLK_HZ);
}

uint16_t FpgaCtrl_AmpToQ13(uint16_t amp_mVpp)
{
    uint32_t q = ((uint32_t)amp_mVpp * FPGA_AMP_Q13_FULL + 2500u) / 5000u;
    return (q > FPGA_AMP_Q13_FULL) ? FPGA_AMP_Q13_FULL : (uint16_t)q;
}

uint32_t FpgaCtrl_DutyToQ32(uint16_t duty_pct10)
{
    uint64_t q = ((uint64_t)duty_pct10 << 32) + 500u;
    return (uint32_t)(q / 1000u);
}

uint32_t FpgaCtrl_PhaseDegToQ32(int16_t phase_deg)
{
    uint16_t deg = (phase_deg < 0) ? (uint16_t)(phase_deg + 360) : (uint16_t)phase_deg;
    uint64_t q = ((uint64_t)deg << 32) + 180u;
    return (uint32_t)(q / 360u);
}

uint8_t FpgaCtrl_Checksum(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t sum = (uint8_t)(FPGA_FRAME_SOF + cmd + len);
    uint8_t i;

    for (i = 0u; i < len; ++i) {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

static void pack_channel(uint8_t *dst, const WaveChannelConfig *ch)
{
    dst[0] = ch->wave;
    wr_u32_le(&dst[1], FpgaCtrl_FwordFromHz(ch->freq_hz));
    wr_u16_le(&dst[5], FpgaCtrl_AmpToQ13(ch->amp_mVpp));
    wr_u32_le(&dst[7], FpgaCtrl_DutyToQ32(ch->duty_pct10));
}

size_t FpgaCtrl_BuildDualWaveFrame(const DualWaveOutputConfig *cfg,
                                   uint8_t *out, size_t out_cap)
{
    uint8_t *data;

    if (!HmiProtocol_ValidateConfig(cfg) || out == 0 || out_cap < FPGA_DUAL_WAVE_FRAME_LEN) {
        return 0u;
    }

    out[0] = FPGA_FRAME_SOF;
    out[1] = FPGA_CMD_DUAL_WAVE_CONFIG;
    out[2] = FPGA_DUAL_WAVE_DATA_LEN;
    data = &out[3];

    data[0] = cfg->proto_ver;
    data[1] = cfg->flags;
    pack_channel(&data[2], &cfg->ch_a);
    pack_channel(&data[13], &cfg->ch_b);
    wr_u32_le(&data[24], FpgaCtrl_PhaseDegToQ32(cfg->phase_b_rel_a_deg));

    out[3u + FPGA_DUAL_WAVE_DATA_LEN] =
        FpgaCtrl_Checksum(FPGA_CMD_DUAL_WAVE_CONFIG, data, FPGA_DUAL_WAVE_DATA_LEN);
    out[4u + FPGA_DUAL_WAVE_DATA_LEN] = FPGA_FRAME_EOF;
    return FPGA_DUAL_WAVE_FRAME_LEN;
}

uint8_t FpgaCtrl_SendDualWaveConfig(FpgaCtrlWriteFn write,
                                    const DualWaveOutputConfig *cfg)
{
    uint8_t frame[FPGA_DUAL_WAVE_FRAME_LEN];
    size_t len;

    if (write == 0) {
        return 0u;
    }
    len = FpgaCtrl_BuildDualWaveFrame(cfg, frame, sizeof(frame));
    if (len == 0u) {
        return 0u;
    }
    write(frame, (uint16_t)len);
    return 1u;
}

uint8_t FpgaCtrl_SendDisabled(FpgaCtrlWriteFn write,
                              const DualWaveOutputConfig *last_cfg)
{
    DualWaveOutputConfig cfg;

    if (last_cfg != 0 && HmiProtocol_ValidateConfig(last_cfg)) {
        cfg = *last_cfg;
    } else {
        cfg.proto_ver = 1u;
        cfg.flags = 0u;
        cfg.ch_a.wave = HMI_WAVE_SINE;
        cfg.ch_a.freq_hz = 1000u;
        cfg.ch_a.amp_mVpp = 0u;
        cfg.ch_a.duty_pct10 = 500u;
        cfg.ch_b = cfg.ch_a;
        cfg.phase_b_rel_a_deg = 0;
    }
    cfg.flags = 0u;
    return FpgaCtrl_SendDualWaveConfig(write, &cfg);
}
