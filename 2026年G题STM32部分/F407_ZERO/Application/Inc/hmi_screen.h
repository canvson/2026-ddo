/*
 * File: hmi_screen.h
 * Role: TJC/Nextion screen drawing interface for measurement pages.
 * Scope: Passive parameter, waveform and spectrum refresh APIs.
 */
#ifndef HMI_SCREEN_H
#define HMI_SCREEN_H

#include "hmi_protocol.h"
#include "measure_model.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HMI_SCREEN_TX_QUEUE_LEN 32768u

typedef uint16_t (*HmiScreenWriteFn)(const uint8_t *data, uint16_t len,
                                     void *user);

typedef enum {
    HMI_RUNTIME_WAIT = 0u,
    HMI_RUNTIME_LIVE,
    HMI_RUNTIME_HOLD,
    HMI_RUNTIME_COMM_ERR,
    HMI_RUNTIME_CRC_ERR,
    HMI_RUNTIME_OVER_RANGE,
    HMI_RUNTIME_ALGO_ERR,
} HmiRuntimeState;

typedef struct {
    HmiScreenWriteFn write;
    void *user;
    HmiPage page;
    uint8_t period_mode;
    uint16_t tx_head;
    uint16_t tx_tail;
    uint32_t queue_overflows;
    uint8_t tx_queue[HMI_SCREEN_TX_QUEUE_LEN];
} HmiScreen;

void HmiScreen_Init(HmiScreen *screen, HmiScreenWriteFn write, void *user);
void HmiScreen_Poll(HmiScreen *screen, uint16_t budget);
uint8_t HmiScreen_Busy(const HmiScreen *screen);
uint32_t HmiScreen_QueueOverflows(const HmiScreen *screen);
void HmiScreen_SendRaw(HmiScreen *screen, const char *cmd);
void HmiScreen_Goto(HmiScreen *screen, HmiPage page);
void HmiScreen_DrawLayout(HmiScreen *screen);
void HmiScreen_SetPeriod(HmiScreen *screen, uint8_t period_mode);
void HmiScreen_ShowHome(HmiScreen *screen, const MeasureFeature *feature,
                        HmiRuntimeState state);
void HmiScreen_DrawWave(HmiScreen *screen, const MeasureFeature *feature,
                        uint8_t period_mode, HmiRuntimeState state);
void HmiScreen_DrawSpectrum(HmiScreen *screen, const MeasureFeature *feature,
                            HmiRuntimeState state);

#ifdef __cplusplus
}
#endif

#endif
