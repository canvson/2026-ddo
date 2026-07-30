/*
 * File: g_app.h
 * Role: Top-level bare-metal application interface for the 2026 G firmware.
 * Scope: IO callbacks, period key bit, HMI RX entry and polling loop.
 */
#ifndef G_APP_H
#define G_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t (*GAppHmiWriteFn)(const uint8_t *data, uint16_t len);
typedef void (*GAppFpgaWriteFn)(const uint8_t *data, uint16_t len);
typedef uint16_t (*GAppReadFn)(uint8_t *data, uint16_t cap);
typedef uint32_t (*GAppGetMsFn)(void);
typedef uint32_t (*GAppGetCounterFn)(void);
typedef uint8_t (*GAppKeyReadFn)(void);
typedef void (*GAppLedFn)(uint8_t on);

#define GAPP_KEY_PERIOD 0x01u

typedef struct {
    GAppHmiWriteFn hmi_write;
    GAppFpgaWriteFn fpga_write;
    GAppReadFn fpga_read;
    GAppGetCounterFn fpga_overflows;
    GAppGetMsFn get_ms;
    GAppKeyReadFn key_read;
    GAppLedFn led_write;
} GAppIo;

void GApp_Init(const GAppIo *io);
void GApp_OnHmiRxByte(uint8_t byte);
void GApp_Poll(void);

#ifdef __cplusplus
}
#endif

#endif
