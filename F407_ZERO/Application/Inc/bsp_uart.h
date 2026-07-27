#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void BspUart_Start(void);
void BspUart_Poll(void);  /* call each main-loop turn: self-healing IRQ enable */
void BspUart_HmiWrite(const uint8_t *data, uint16_t len);
void BspUart_FpgaWrite(const uint8_t *data, uint16_t len);
void BspUart_USART1_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif
