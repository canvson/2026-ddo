#ifndef HMI_SCREEN_H
#define HMI_SCREEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*HmiScreenWriteFn)(const uint8_t *data, uint16_t len, void *user);

typedef enum {
    HMI_PAGE_HOME = 0,
    HMI_PAGE_BASIC = 1,
    HMI_PAGE_LEARN = 2,
    HMI_PAGE_EMULATE = 3,
    HMI_PAGE_CALIB = 4,
    HMI_PAGE_RESULT = 5,
    HMI_PAGE_ERROR = 6,
} HmiPage;

typedef enum {
    HMI_MODE_HOME = 0,
    HMI_MODE_BASIC = 1,
    HMI_MODE_LEARN = 2,
    HMI_MODE_EMULATE = 3,
    HMI_MODE_CALIB = 4,
} HmiMode;

typedef enum {
    HMI_RUN_IDLE = 0,
    HMI_RUN_RUNNING = 1,
    HMI_RUN_DONE = 2,
    HMI_RUN_ERROR = 3,
} HmiRunState;

typedef struct {
    HmiScreenWriteFn write;
    void *user;
    uint8_t page;
    uint8_t mode;
    uint8_t wave;
    uint8_t run_state;
} HmiScreen;

void HmiScreen_Init(HmiScreen *screen, HmiScreenWriteFn write, void *user);
void HmiScreen_SendRaw(HmiScreen *screen, const char *cmd);
void HmiScreen_Goto(HmiScreen *screen, HmiPage page);
void HmiScreen_SetMode(HmiScreen *screen, HmiMode mode);
void HmiScreen_SetWave(HmiScreen *screen, uint8_t wave);
void HmiScreen_SetRunState(HmiScreen *screen, HmiRunState state);
void HmiScreen_ShowBasic(HmiScreen *screen, uint32_t freq_hz,
                         uint16_t target_vpp10, uint16_t vin_mVpp);
void HmiScreen_ShowBasicMeasure(HmiScreen *screen, uint16_t vout_mVpp);
void HmiScreen_ShowLearnProgress(HmiScreen *screen, uint8_t percent, const char *type_text);
void HmiScreen_ShowLearnResult(HmiScreen *screen, const char *type_text,
                               uint16_t r_ohm, uint16_t l_uH, uint32_t c_pF,
                               uint16_t err_code);
void HmiScreen_ShowEmulateMeasure(HmiScreen *screen, const char *model_text,
                                  uint16_t output_mVpp, uint16_t err_mVpp);
/* page1 note strip under the run-state block. */
void HmiScreen_ShowBasicNote(HmiScreen *screen, const char *text);
/* page3 left column: measured input signal (freq / Vpp / duty). */
void HmiScreen_ShowEmulateInput(HmiScreen *screen, uint32_t freq_hz,
                                const char *wave_text, uint16_t vpp_mV,
                                uint16_t duty_pct10);
/* page2 live progress with the current sweep frequency. */
void HmiScreen_ShowLearnLive(HmiScreen *screen, uint8_t percent,
                             uint32_t freq_hz);
/* page4 duty / note line. */
void HmiScreen_ShowCalibDuty(HmiScreen *screen, const char *text);
void HmiScreen_ShowCalibMeasure(HmiScreen *screen, uint16_t output_mVpp);
void HmiScreen_ShowResult(HmiScreen *screen, const char *line1, const char *line2,
                          const char *line3);
void HmiScreen_ShowError(HmiScreen *screen, uint16_t code, const char *message);

#ifdef __cplusplus
}
#endif

#endif
