#include "bsp_board.h"
#include "board_pins.h"
#include "g_app.h"
#include "main.h"

uint8_t BspBoard_KeyRead(void)
{
    uint8_t keys = 0u;

    if (HAL_GPIO_ReadPin(KEY_LEARN_GPIO_Port, KEY_LEARN_Pin) == GPIO_PIN_RESET) {
        keys |= GAPP_KEY_LEARN;
    }
    if (HAL_GPIO_ReadPin(KEY_START_GPIO_Port, KEY_START_Pin) == GPIO_PIN_RESET) {
        keys |= GAPP_KEY_START;
    }
    if (HAL_GPIO_ReadPin(KEY_STOP_GPIO_Port, KEY_STOP_Pin) == GPIO_PIN_RESET) {
        keys |= GAPP_KEY_STOP;
    }
    return keys;
}

void BspBoard_LedWrite(uint8_t on)
{
    /* LED_RUN is active low */
    HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
