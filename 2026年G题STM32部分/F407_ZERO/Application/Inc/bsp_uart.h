/*
 * File: bsp_uart.h
 * Role: Board UART service interface for HMI and FPGA communication.
 * Scope: USART1 screen I/O plus USART2 FPGA TX and circular DMA RX access.
 */
#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void BspUart_Start(void);
void BspUart_Poll(void);  /* call each main-loop turn: self-healing IRQ enable */
uint16_t BspUart_HmiWrite(const uint8_t *data, uint16_t len);
void BspUart_FpgaWrite(const uint8_t *data, uint16_t len);
uint16_t BspUart_FpgaRead(uint8_t *data, uint16_t cap);
uint32_t BspUart_FpgaOverflowCount(void);
void BspUart_USART1_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif
