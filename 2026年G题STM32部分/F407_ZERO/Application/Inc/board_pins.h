/*
 * File: board_pins.h
 * Role: Central board wiring constants for the 2026 G STM32 firmware.
 * Scope: UART baud rates, active-low keys, LED pin and calibration notes.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/*
 * Central pin / wiring map for the 2026 G periodic-signal measurement device.
 * STM32 handles display, user input and the FPGA result link only. Local
 * ADC/DAC/RLC-emulation peripherals from the 2025 firmware are left unused.
 *
 * Serial links
 *   USART1  PA9  TX / PA10 RX   115200 8N1    TJC/Nextion HMI
 *   USART2  PA2  TX / PA3  RX   921600 8N1    FPGA packet link
 *                                               DMA1_Stream5 circular RX
 *
 * Keys (active low, internal pull-up) and LED
 *   KEY_LEARN -> PE3   period select: 1-period / 3-period waveform window
 *   KEY_START -> PE4   reserved; FPGA board keys own algorithm control
 *   KEY_STOP  -> PE2   reserved; FPGA board keys own algorithm control
 *   LED_RUN   -> PF9   heartbeat / run indicator (active low)
 */

#define FPGA_UART_BAUDRATE     921600u
#define HMI_UART_BAUDRATE      115200u

#define KEY_LEARN_GPIO_Port  GPIOE
#define KEY_LEARN_Pin        GPIO_PIN_3
#define KEY_START_GPIO_Port  GPIOE
#define KEY_START_Pin        GPIO_PIN_4
#define KEY_STOP_GPIO_Port   GPIOE
#define KEY_STOP_Pin         GPIO_PIN_2
#define LED_RUN_GPIO_Port    GPIOF
#define LED_RUN_Pin          GPIO_PIN_9

/* Default display calibration is implemented in calibration.c:
 * voltage_gain = 1.0, spectrum_gain = 1.0, freq_offset_hz = 0.
 */

#endif /* BOARD_PINS_H */
