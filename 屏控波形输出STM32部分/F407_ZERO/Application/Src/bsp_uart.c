#include "bsp_uart.h"

#include "g_app.h"
#include "usart.h"

#define HMI_RX_DMA_LEN 64u

static uint8_t s_hmi_rx_dma[HMI_RX_DMA_LEN];

static void start_hmi_rx_dma(void)
{
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_hmi_rx_dma, sizeof(s_hmi_rx_dma)) == HAL_OK) {
        if (huart1.hdmarx != 0) {
            __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
        }
    }
}

void BspUart_Start(void)
{
    start_hmi_rx_dma();
}

void BspUart_Poll(void)
{
    if (huart1.RxState == HAL_UART_STATE_READY) {
        start_hmi_rx_dma();
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

void BspUart_USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    uint16_t i;

    if (huart == &huart1) {
        for (i = 0u; i < Size && i < sizeof(s_hmi_rx_dma); ++i) {
            GApp_OnHmiRxByte(s_hmi_rx_dma[i]);
        }
        start_hmi_rx_dma();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) {
        start_hmi_rx_dma();
    }
}
