#include "bsp_uart.h"

#include "g_app.h"
#include "usart.h"

static uint8_t s_hmi_rx;

static void start_hmi_rx(void)
{
    (void)HAL_UART_Receive_IT(&huart1, &s_hmi_rx, 1u);
}

void BspUart_Start(void)
{
    start_hmi_rx();
}

void BspUart_Poll(void)
{
    /* Self-healing RX re-arm.
     *
     * The 1-byte HAL_UART_Receive_IT is normally re-armed from the RxCplt
     * callback (IRQ context).  If that re-arm ever collides with the brief
     * __HAL_LOCK window of a HAL_UART_Transmit running in the main loop,
     * it returns HAL_BUSY and the receive chain would silently die.
     * Re-arming here whenever RxState is READY closes that hole (and any
     * other path that drops the reception, e.g. error recovery).         */
    if (huart1.RxState == HAL_UART_STATE_READY) {
        start_hmi_rx();
    }
}

void BspUart_HmiWrite(const uint8_t *data, uint16_t len)
{
    if (data != 0 && len > 0u) {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 100u);
    }
}

void BspUart_FpgaWrite(const uint8_t *data, uint16_t len)
{
    if (data != 0 && len > 0u) {
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)data, len, 100u);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) {
        GApp_OnHmiRxByte(s_hmi_rx);
        start_hmi_rx();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) {
        start_hmi_rx();
    }
}
