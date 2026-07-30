/*
 * File: bsp_board.c
 * Role: Implements board key scanning and run LED output.
 * Scope: Maps the active-low period key into the GApp key bit.
 */
#include "bsp_board.h"
#include "board_pins.h"
#include "g_app.h"
#include "main.h"

uint8_t BspBoard_KeyRead(void)
{
    uint8_t keys = 0u;

    if (HAL_GPIO_ReadPin(KEY_LEARN_GPIO_Port, KEY_LEARN_Pin) == GPIO_PIN_RESET) {
        keys |= GAPP_KEY_PERIOD;
    }
    return keys;
}

void BspBoard_LedWrite(uint8_t on)
{
    /* LED_RUN is active low */
    HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
