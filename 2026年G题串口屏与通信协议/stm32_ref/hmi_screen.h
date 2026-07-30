#ifndef HMI_SCREEN_H
#define HMI_SCREEN_H

#include "hmi_protocol.h"
#include "measure_model.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t (*HmiScreenWriteFn)(const uint8_t *data, uint16_t len, void *user);

typedef struct {
    HmiScreenWriteFn write;
    void *user;
    HmiPage page;
    uint8_t period_mode;
} HmiScreen;

void HmiScreen_Init(HmiScreen *screen, HmiScreenWriteFn write, void *user);
void HmiScreen_SendRaw(HmiScreen *screen, const char *cmd);
void HmiScreen_Goto(HmiScreen *screen, HmiPage page);
void HmiScreen_SetPeriod(HmiScreen *screen, uint8_t period_mode);
void HmiScreen_ShowStatusText(HmiScreen *screen, const char *text, uint16_t color);
void HmiScreen_ShowFeatureHome(HmiScreen *screen, const MeasureFeature *feature);
void HmiScreen_ShowWaveSummary(HmiScreen *screen, const MeasureFeature *feature,
                               uint8_t period_mode);
void HmiScreen_ShowSpectrumComponents(HmiScreen *screen, const MeasureFeature *feature);
void HmiScreen_ClearWave(HmiScreen *screen);
void HmiScreen_ClearSpectrum(HmiScreen *screen);
void HmiScreen_DrawLine(HmiScreen *screen, uint16_t x0, uint16_t y0,
                        uint16_t x1, uint16_t y1, uint16_t color);

#ifdef __cplusplus
}
#endif

#endif
