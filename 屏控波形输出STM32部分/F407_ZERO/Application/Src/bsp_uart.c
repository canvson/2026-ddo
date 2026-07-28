#include "bsp_uart.h"

#include "g_app.h"
#include "usart.h"

#define HMI_TX_RING_LEN 4096u

static volatile uint8_t s_hmi_tx_ring[HMI_TX_RING_LEN];
static volatile uint16_t s_hmi_tx_head;
static volatile uint16_t s_hmi_tx_tail;

static uint16_t hmi_tx_next(uint16_t index)
{
    return (uint16_t)((index + 1u) % HMI_TX_RING_LEN);
}

static void clear_hmi_rx_flags(void)
{
    volatile uint32_t tmp;

    tmp = huart1.Instance->SR;
    tmp = huart1.Instance->DR;
    (void)tmp;
}

static void hmi_tx_kick(void)
{
    huart1.Instance->CR1 |= USART_CR1_TXEIE;
}

void BspUart_Start(void)
{
    /* Keep HMI RX armed through RXNE, independent of blocking screen writes. */
    clear_hmi_rx_flags();
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_ERR);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    if (s_hmi_tx_tail != s_hmi_tx_head) {
        hmi_tx_kick();
    }
}

void BspUart_Poll(void)
{
    if ((huart1.Instance->CR1 & USART_CR1_RXNEIE) == 0u) {
        __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    }
    if ((huart1.Instance->CR3 & USART_CR3_EIE) == 0u) {
        __HAL_UART_ENABLE_IT(&huart1, UART_IT_ERR);
    }
    if (s_hmi_tx_tail != s_hmi_tx_head &&
        (huart1.Instance->CR1 & USART_CR1_TXEIE) == 0u) {
        hmi_tx_kick();
    }
}

void BspUart_HmiWrite(const uint8_t *data, uint16_t len)
{
    if (data != 0 && len > 0u) {
        for (uint16_t i = 0u; i < len; ++i) {
            uint16_t next = hmi_tx_next(s_hmi_tx_head);
            while (next == s_hmi_tx_tail) {
                hmi_tx_kick();
            }
            s_hmi_tx_ring[s_hmi_tx_head] = data[i];
            s_hmi_tx_head = next;
            hmi_tx_kick();
        }
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
    uint32_t sr = huart1.Instance->SR;
    uint32_t cr1 = huart1.Instance->CR1;
    uint32_t cr3 = huart1.Instance->CR3;
    uint32_t err = sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE);
    uint8_t had_rxne = (uint8_t)(((sr & USART_SR_RXNE) != 0u) ? 1u : 0u);

    if (had_rxne != 0u && ((cr1 & USART_CR1_RXNEIE) != 0u)) {
        uint8_t byte = (uint8_t)(huart1.Instance->DR & 0xFFu);
        GApp_OnHmiRxByte(byte);
    }

    if (had_rxne == 0u && err != 0u &&
        (((cr3 & USART_CR3_EIE) != 0u) || ((cr1 & USART_CR1_PEIE) != 0u))) {
        clear_hmi_rx_flags();
    }

    if (((sr & USART_SR_TXE) != 0u) && ((cr1 & USART_CR1_TXEIE) != 0u)) {
        if (s_hmi_tx_tail != s_hmi_tx_head) {
            huart1.Instance->DR = s_hmi_tx_ring[s_hmi_tx_tail];
            s_hmi_tx_tail = hmi_tx_next(s_hmi_tx_tail);
        } else {
            huart1.Instance->CR1 &= (uint32_t)~USART_CR1_TXEIE;
        }
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    (void)huart;
}
