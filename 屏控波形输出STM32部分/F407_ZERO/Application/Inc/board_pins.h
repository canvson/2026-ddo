#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/*
 * Central pin / wiring / calibration map for the G-problem explorer device.
 * Change ONLY this file (and the matching .ioc) when the wiring changes.
 *
 * Serial links
 *   USART1  PA9  TX / PA10 RX   115200 8N1   HMI touch screen
 *   USART2  PA2  TX / PA3  RX   115200 8N1   FPGA downlink (TX only used)
 *
 * Analog measurement (front-end: rail-to-rail op-amp follower, AC coupled,
 * 470k/470k bias to VDDA/2 -> input impedance ~235k >= 100k requirement)
 *   J_MEAS  -> PA1  ADC1_IN1  main measure port
 *              learn mode  : unknown-circuit OUTPUT
 *              emulate mode: signal-generator INPUT
 *   J_REF   -> PB0  ADC2_IN8  drive reference tap
 *              learn mode  : unknown-circuit INPUT (= FPGA DAC drive)
 *
 * Analog output (op-amp AC-couple + gain stage, see docs)
 *   J_OUT   -> PA4  DAC_OUT1  emulated output / auxiliary precision sine
 *
 * Keys (active low, internal pull-up) and LED
 *   KEY_LEARN -> PE3   the single "learn key" required by 发挥(1)
 *   KEY_START -> PE4   one-key start of the currently configured mode
 *   KEY_STOP  -> PE2   stop
 *   LED_RUN   -> PF9   heartbeat / run indicator (active low)
 */

#define KEY_LEARN_GPIO_Port  GPIOE
#define KEY_LEARN_Pin        GPIO_PIN_3
#define KEY_START_GPIO_Port  GPIOE
#define KEY_START_Pin        GPIO_PIN_4
#define KEY_STOP_GPIO_Port   GPIOE
#define KEY_STOP_Pin         GPIO_PIN_2
#define LED_RUN_GPIO_Port    GPIOF
#define LED_RUN_Pin          GPIO_PIN_9

/* ---------------------------------------------------------------------- */
/* Calibration constants - adjust once against a scope / DMM.             */
/* ---------------------------------------------------------------------- */

/* ADC: mV at the JACK per ADC LSB (3.3 V / 4096 = 0.8057 mV per LSB at the
 * pin; front-end is unity gain, so jack scale == pin scale by default).   */
#define CAL_ADC_MAIN_MV_PER_LSB   (3300.0f / 4096.0f)
#define CAL_ADC_REF_MV_PER_LSB    (3300.0f / 4096.0f)

/* DAC: mV at J_OUT per DAC LSB.  With the recommended x2 AC output stage
 * one LSB at PA4 (0.8057 mV) becomes ~1.611 mV at the jack.               */
#define CAL_DAC_OUT_GAIN          (2.0f)
#define CAL_DAC_MV_PER_LSB        ((3300.0f / 4096.0f) * CAL_DAC_OUT_GAIN)

/* Maximum peak-peak the DAC stage can produce at the jack (headroom kept
 * off the rails).  6200 mV with the x2 stage.                             */
#define CAL_DAC_MAX_MVPP          (6200u)

/* Learn-mode stimulus fallback: nominal FPGA Develop_one drive when the
 * J_REF channel is not wired (Amp_scale 4106 ~ 2.0 Vpp).                  */
#define CAL_LEARN_DRIVE_MVPP      (2000.0f)

/* Timer clocks (84 MHz core, APB1 x2 = 84 MHz timer clock).               */
#define BOARD_TIMCLK_HZ           (84000000u)

/* Fixed DAC sample rate: 84 MHz / 80 = 1.05 MS/s.                         */
#define WAVEOUT_TIM6_ARR          (79u)
#define WAVEOUT_FS_HZ             (BOARD_TIMCLK_HZ / (WAVEOUT_TIM6_ARR + 1u))

#endif /* BOARD_PINS_H */
