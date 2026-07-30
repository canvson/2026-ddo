/*
 * File: bsp_board.h
 * Role: Board-level key and LED abstraction used by the application layer.
 * Scope: Converts GPIO state into GApp key bits and writes the run LED.
 */
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Physical keys / LED glue (pins in board_pins.h). */
uint8_t BspBoard_KeyRead(void);   /* GAPP_KEY_* bitmask, pressed = 1 */
void    BspBoard_LedWrite(uint8_t on);

#ifdef __cplusplus
}
#endif

#endif
