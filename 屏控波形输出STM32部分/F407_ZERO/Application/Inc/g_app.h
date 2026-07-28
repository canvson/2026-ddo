#ifndef G_APP_H
#define G_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GAppWriteFn)(const uint8_t *data, uint16_t len);
typedef uint32_t (*GAppGetMsFn)(void);
typedef uint8_t (*GAppKeyReadFn)(void);
typedef void (*GAppLedFn)(uint8_t on);

#define GAPP_KEY_LEARN  0x01u
#define GAPP_KEY_START  0x02u
#define GAPP_KEY_STOP   0x04u

typedef struct {
    GAppWriteFn hmi_write;
    GAppWriteFn fpga_write;
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
