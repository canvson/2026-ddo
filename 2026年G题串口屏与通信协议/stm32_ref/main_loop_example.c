#include "fpga_packet.h"
#include "hmi_protocol.h"
#include "hmi_screen.h"
#include "measure_model.h"

/*
 * Integration-oriented pseudo C. Replace uart_write_hmi(), uart_write_fpga(),
 * millis(), and RX polling hooks with the local HAL/BSP in the real project.
 *
 * Final timing:
 *   1. FPGA keeps sampling and uses its board keys to choose UA/UB/UB+uJ.
 *   2. FPGA periodically sends FEATURE packets to STM32.
 *   3. HMI only changes local page and 1/3-period waveform window.
 */

static HmiParser hmi_rx;
static FpgaPacketParser fpga_rx;
static HmiScreen screen;
static MeasureModel model;

static HmiView view = HMI_VIEW_PARAM;
static uint8_t period = 1u;
static uint8_t fpga_seq = 0u;
static uint32_t last_feature_ms = 0u;
static uint32_t last_ping_ms = 0u;

static uint16_t uart_write_hmi(const uint8_t *data, uint16_t len, void *user)
{
    (void)data;
    (void)len;
    (void)user;
    return len;
}

static void uart_write_fpga(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
}

static uint32_t millis(void)
{
    return 0u;
}

static HmiPage page_for_view(HmiView v)
{
    if (v == HMI_VIEW_WAVE) {
        return HMI_PAGE_WAVE;
    }
    if (v == HMI_VIEW_SPEC) {
        return HMI_PAGE_SPEC;
    }
    return HMI_PAGE_HOME;
}

static void send_ping(void)
{
    uint8_t frame[16];
    size_t n = FpgaPacket_BuildFrame(FPGA_CMD_PING, fpga_seq++, 0, 0u,
                                     frame, sizeof(frame));
    if (n != 0u) {
        uart_write_fpga(frame, (uint16_t)n);
    }
}

static void refresh_current_page(const MeasureFeature *feature)
{
    if (feature == 0 || feature->valid == 0u) {
        HmiScreen_ShowStatusText(&screen, "WAIT DATA", 62951u);
        return;
    }

    if (feature->status == (uint8_t)MEASURE_STATUS_VALID) {
        HmiScreen_ShowStatusText(&screen, "LIVE", 11564u);
    } else if (feature->status == (uint8_t)MEASURE_STATUS_HOLD) {
        HmiScreen_ShowStatusText(&screen, "HOLD", 62951u);
    } else {
        HmiScreen_ShowStatusText(&screen, "FPGA ERR", 55882u);
    }

    if (view == HMI_VIEW_WAVE) {
        HmiScreen_ClearWave(&screen);
        HmiScreen_ShowWaveSummary(&screen, feature, period);
        /*
         * Draw a synthesized waveform here:
         *   y(t)=sum(component[i].amp_peak_mv *
         *            sin(2*pi*component[i].freq_hz/f1_hz*t + phase))
         * Map t across either 1 or 3 periods according to period.
         */
    } else if (view == HMI_VIEW_SPEC) {
        HmiScreen_ClearSpectrum(&screen);
        HmiScreen_ShowSpectrumComponents(&screen, feature);
        /*
         * Draw only line spectrum sticks:
         *   x = 60 + freq_hz / 500000 * 646
         *   height follows amp_peak_mv relative to the largest component.
         */
    } else {
        HmiScreen_ShowFeatureHome(&screen, feature);
    }
}

static void handle_hmi_event(const HmiEvent *ev)
{
    switch (ev->cmd) {
    case HMI_CMD_SET_PERIOD:
        period = ev->period_mode;
        HmiScreen_SetPeriod(&screen, period);
        if (view == HMI_VIEW_WAVE) {
            refresh_current_page(&model.feature);
        }
        break;
    case HMI_CMD_SET_VIEW:
        view = ev->view;
        HmiScreen_Goto(&screen, page_for_view(view));
        refresh_current_page(&model.feature);
        break;
    default:
        HmiScreen_ShowStatusText(&screen, "BAD HMI CMD", 55882u);
        break;
    }
}

static void handle_fpga_packet(const FpgaPacket *packet)
{
    if (packet->type == FPGA_PKT_FEATURE) {
        if (MeasureModel_UpdateFeaturePayload(&model, packet->payload, packet->len)) {
            last_feature_ms = millis();
            refresh_current_page(&model.feature);
        } else {
            HmiScreen_ShowStatusText(&screen, "BAD FEATURE", 55882u);
        }
    } else if (packet->type == FPGA_PKT_STATUS) {
        (void)MeasureModel_UpdateStatusPayload(&model, packet->payload, packet->len);
    }
}

void App_Init(void)
{
    HmiProtocol_Init(&hmi_rx);
    FpgaPacket_Init(&fpga_rx);
    HmiScreen_Init(&screen, uart_write_hmi, 0);
    MeasureModel_Init(&model);
    HmiScreen_Goto(&screen, HMI_PAGE_HOME);
    HmiScreen_ShowStatusText(&screen, "WAIT DATA", 62951u);
}

void App_OnHmiByte(uint8_t byte)
{
    HmiEvent ev;
    if (HmiProtocol_PushByte(&hmi_rx, byte, &ev) == HMI_PARSE_FRAME_OK) {
        handle_hmi_event(&ev);
    }
}

void App_OnFpgaByte(uint8_t byte)
{
    FpgaPacket packet;
    FpgaParseResult res = FpgaPacket_PushByte(&fpga_rx, byte, &packet);
    if (res == FPGA_PARSE_FRAME_OK) {
        handle_fpga_packet(&packet);
    } else if (res == FPGA_PARSE_ERROR && fpga_rx.last_error == FPGA_PACKET_ERR_BAD_CRC) {
        HmiScreen_ShowStatusText(&screen, "CRC ERR", 55882u);
    }
}

void App_Poll(void)
{
    uint32_t now = millis();

    if ((uint32_t)(now - last_ping_ms) >= 500u) {
        last_ping_ms = now;
        send_ping();
    }

    if (model.feature.valid == 0u) {
        HmiScreen_ShowStatusText(&screen, "WAIT DATA", 62951u);
    } else if ((uint32_t)(now - last_feature_ms) > 5000u) {
        HmiScreen_ShowStatusText(&screen, "COMM ERR", 55882u);
        FpgaPacket_Init(&fpga_rx);
    } else if ((uint32_t)(now - last_feature_ms) > 2000u) {
        HmiScreen_ShowStatusText(&screen, "HOLD", 62951u);
    }
}
