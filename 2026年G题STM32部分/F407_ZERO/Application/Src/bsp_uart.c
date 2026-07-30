/*
 * File: bsp_uart.c
 * Role: Implements USART services for HMI and FPGA communication.
 * Scope: USART1 interrupt-driven screen I/O and USART2 circular DMA RX scan.
 */
#include "bsp_uart.h"

#include "g_app.h"
#include "uart_ring_math.h"
#include "usart.h"

#define HMI_TX_RING_LEN 4096u
#define FPGA_RX_DMA_LEN 4096u

static volatile uint8_t s_hmi_tx_ring[HMI_TX_RING_LEN];
static volatile uint16_t s_hmi_tx_head;
static volatile uint16_t s_hmi_tx_tail;
static uint8_t s_fpga_rx_dma[FPGA_RX_DMA_LEN];
static volatile uint32_t s_fpga_rx_wraps;
static uint32_t s_fpga_rx_cons_total;
static volatile uint32_t s_fpga_rx_overflow_count;
static uint8_t s_fpga_rx_started;

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

static void clear_fpga_rx_flags(void)
{
    volatile uint32_t tmp;

    tmp = huart2.Instance->SR;
    tmp = huart2.Instance->DR;
    (void)tmp;
}

static void hmi_tx_kick(void)
{
    huart1.Instance->CR1 |= USART_CR1_TXEIE;
}

static void fpga_rx_start(void)
{
    if (s_fpga_rx_started != 0u) {
        return;
    }
    clear_fpga_rx_flags();
    if (HAL_UART_Receive_DMA(&huart2, s_fpga_rx_dma, FPGA_RX_DMA_LEN) == HAL_OK) {
        s_fpga_rx_wraps = 0u;
        s_fpga_rx_cons_total = 0u;
        s_fpga_rx_started = 1u;
    }
}

static uint32_t fpga_rx_producer_total(void)
{
    uint32_t wraps_before;
    uint32_t wraps_after;
    uint16_t pos;

    do {
        wraps_before = s_fpga_rx_wraps;
        pos = (uint16_t)(FPGA_RX_DMA_LEN -
                         __HAL_DMA_GET_COUNTER(huart2.hdmarx));
        if (pos >= FPGA_RX_DMA_LEN) {
            pos = 0u;
        }
        wraps_after = s_fpga_rx_wraps;
    } while (wraps_before != wraps_after);

    return wraps_before * FPGA_RX_DMA_LEN + pos;
}

void BspUart_Start(void)
{
    /* Keep HMI RX armed through RXNE, independent of blocking screen writes. */
    clear_hmi_rx_flags();
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_ERR);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    fpga_rx_start();
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
    if (s_fpga_rx_started == 0u ||
        (huart2.Instance->CR3 & USART_CR3_DMAR) == 0u) {
        s_fpga_rx_started = 0u;
        (void)HAL_UART_DMAStop(&huart2);
        fpga_rx_start();
    }
}

uint16_t BspUart_HmiWrite(const uint8_t *data, uint16_t len)
{
    uint16_t written = 0u;

    if (data != 0 && len > 0u) {
        while (written < len) {
            uint16_t next = hmi_tx_next(s_hmi_tx_head);
            if (next == s_hmi_tx_tail) {
                break;
            }
            s_hmi_tx_ring[s_hmi_tx_head] = data[written++];
            s_hmi_tx_head = next;
        }
        hmi_tx_kick();
    }
    return written;
}

void BspUart_FpgaWrite(const uint8_t *data, uint16_t len)
{
    if (data != 0 && len > 0u) {
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)data, len, 100u);
    }
}

uint16_t BspUart_FpgaRead(uint8_t *data, uint16_t cap)
{
    uint16_t n = 0u;
    uint32_t producer;
    uint8_t overflowed;

    if (data == 0 || cap == 0u || s_fpga_rx_started == 0u || huart2.hdmarx == 0) {
        return 0u;
    }

    producer = fpga_rx_producer_total();
    s_fpga_rx_cons_total =
        UartRing_RetainedConsumer(producer, s_fpga_rx_cons_total,
                                  FPGA_RX_DMA_LEN, &overflowed);
    if (overflowed != 0u) {
        s_fpga_rx_overflow_count++;
    }

    while (s_fpga_rx_cons_total != producer && n < cap) {
        data[n++] =
            s_fpga_rx_dma[s_fpga_rx_cons_total % FPGA_RX_DMA_LEN];
        s_fpga_rx_cons_total++;
    }
    return n;
}

uint32_t BspUart_FpgaOverflowCount(void)
{
    return s_fpga_rx_overflow_count;
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
    if (huart != 0 && huart->Instance == USART2) {
        s_fpga_rx_started = 0u;
        (void)HAL_UART_DMAStop(&huart2);
        clear_fpga_rx_flags();
    }
}

void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    (void)huart;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != 0 && huart->Instance == USART2) {
        s_fpga_rx_wraps++;
    }
}
