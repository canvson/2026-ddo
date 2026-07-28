/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.c
  * @brief   ADC1 + ADC2 dual regular simultaneous capture, TIM2 triggered.
  *
  *          ADC1 / PA1  (IN1) = J_MEAS  main measurement port
  *          ADC2 / PB0  (IN8) = J_REF   drive reference port
  *
  *          DMA2 Stream0 moves the packed common data register (mode 2):
  *          low half-word = ADC1, high half-word = ADC2.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "adc.h"

/* USER CODE BEGIN 0 */
#include "tim.h"
#include "board_pins.h"

static volatile uint8_t  s_cap_busy;
static volatile uint8_t  s_cap_ready;   /* raw buffer holds a finished capture */
static uint8_t  s_deinterleaved;
static uint32_t s_cap_fs;
static uint32_t s_cap_n;

static uint32_t s_dma_buf[CAP_MAX_SAMPLES];
static uint16_t s_main_buf[CAP_MAX_SAMPLES];
static uint16_t s_ref_buf[CAP_MAX_SAMPLES];
/* USER CODE END 0 */

ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc1;

/* ADC1 init function */
void MX_ADC1_Init(void)
{
  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data
  Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_TRGO;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_DUALMODE_REGSIMULT;
  multimode.DMAAccessMode = ADC_DMAACCESSMODE_2;
  multimode.TwoSamplingDelay = ADC_TWOSAMPLINGDELAY_5CYCLES;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
  the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */
}

/* ADC2 init function */
void MX_ADC2_Init(void)
{
  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc2.Init.Resolution = ADC_RESOLUTION_12B;
  hadc2.Init.ScanConvMode = DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  /* multimode slave: no own trigger, follows the master's T2 TRGO */
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DMAContinuousRequests = DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */
}

void HAL_ADC_MspInit(ADC_HandleTypeDef* adcHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(adcHandle->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspInit 0 */

  /* USER CODE END ADC1_MspInit 0 */
    /* ADC1 clock enable */
    __HAL_RCC_ADC1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**ADC1 GPIO Configuration
    PA1     ------> ADC1_IN1
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* ADC1 DMA Init */
    /* ADC1 Init */
    hdma_adc1.Instance = DMA2_Stream0;
    hdma_adc1.Init.Channel = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_adc1.Init.Mode = DMA_NORMAL;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(adcHandle,DMA_Handle,hdma_adc1);

  /* USER CODE BEGIN ADC1_MspInit 1 */

  /* USER CODE END ADC1_MspInit 1 */
  }
  else if(adcHandle->Instance==ADC2)
  {
  /* USER CODE BEGIN ADC2_MspInit 0 */

  /* USER CODE END ADC2_MspInit 0 */
    /* ADC2 clock enable */
    __HAL_RCC_ADC2_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**ADC2 GPIO Configuration
    PB0     ------> ADC2_IN8
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN ADC2_MspInit 1 */

  /* USER CODE END ADC2_MspInit 1 */
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef* adcHandle)
{
  if(adcHandle->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspDeInit 0 */

  /* USER CODE END ADC1_MspDeInit 0 */
    __HAL_RCC_ADC1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1);
    HAL_DMA_DeInit(adcHandle->DMA_Handle);
  /* USER CODE BEGIN ADC1_MspDeInit 1 */

  /* USER CODE END ADC1_MspDeInit 1 */
  }
  else if(adcHandle->Instance==ADC2)
  {
  /* USER CODE BEGIN ADC2_MspDeInit 0 */

  /* USER CODE END ADC2_MspDeInit 0 */
    __HAL_RCC_ADC2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_0);
  /* USER CODE BEGIN ADC2_MspDeInit 1 */

  /* USER CODE END ADC2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* ------------------------------------------------------------------ */
/* Capture service implementation (contract in signal_capture.h)       */
/* ------------------------------------------------------------------ */

uint8_t Cap_Start(uint32_t fs_hz, uint32_t n_samples)
{
    uint32_t arr;

    if (s_cap_busy || n_samples < 32u || n_samples > CAP_MAX_SAMPLES) {
        return 0u;
    }
    if (fs_hz < CAP_MIN_FS_HZ) {
        fs_hz = CAP_MIN_FS_HZ;
    }
    if (fs_hz > CAP_MAX_FS_HZ) {
        fs_hz = CAP_MAX_FS_HZ;
    }

    arr = (BOARD_TIMCLK_HZ + fs_hz / 2u) / fs_hz;
    if (arr < 2u) {
        arr = 2u;
    }
    s_cap_fs = BOARD_TIMCLK_HZ / arr;
    s_cap_n = n_samples;
    s_cap_ready = 0u;
    s_deinterleaved = 0u;

    HAL_TIM_Base_Stop(&htim2);
    __HAL_TIM_SET_AUTORELOAD(&htim2, arr - 1u);
    __HAL_TIM_SET_COUNTER(&htim2, 0u);

    if (HAL_ADC_Start(&hadc2) != HAL_OK) {
        return 0u;
    }
    if (HAL_ADCEx_MultiModeStart_DMA(&hadc1, s_dma_buf, n_samples) != HAL_OK) {
        (void)HAL_ADC_Stop(&hadc2);
        return 0u;
    }

    s_cap_busy = 1u;
    HAL_TIM_Base_Start(&htim2);
    return 1u;
}

uint8_t Cap_Busy(void)
{
    return s_cap_busy;
}

uint32_t Cap_Fs(void)
{
    return s_cap_fs;
}

uint32_t Cap_Count(void)
{
    return s_cap_ready ? s_cap_n : 0u;
}

static void cap_deinterleave(void)
{
    uint32_t i;
    if (s_deinterleaved || !s_cap_ready) {
        return;
    }
    for (i = 0u; i < s_cap_n; ++i) {
        uint32_t v = s_dma_buf[i];
        s_main_buf[i] = (uint16_t)(v & 0x0FFFu);         /* ADC1 */
        s_ref_buf[i]  = (uint16_t)((v >> 16) & 0x0FFFu); /* ADC2 */
    }
    s_deinterleaved = 1u;
}

const uint16_t *Cap_Main(void)
{
    cap_deinterleave();
    return s_main_buf;
}

const uint16_t *Cap_Ref(void)
{
    cap_deinterleave();
    return s_ref_buf;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        HAL_TIM_Base_Stop(&htim2);
        (void)HAL_ADCEx_MultiModeStop_DMA(&hadc1);
        (void)HAL_ADC_Stop(&hadc2);
        s_cap_ready = 1u;
        s_cap_busy = 0u;
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        HAL_TIM_Base_Stop(&htim2);
        (void)HAL_ADCEx_MultiModeStop_DMA(&hadc1);
        (void)HAL_ADC_Stop(&hadc2);
        s_cap_ready = 0u;
        s_cap_busy = 0u;
    }
}

/* USER CODE END 1 */
