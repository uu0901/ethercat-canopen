/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "applInterface.h"
#include "FOC.h"
#include "UserData_Function.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI3_Init();
  MX_TIM7_Init();
  MX_ADC1_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  MX_USART3_UART_Init();
  MX_TIM6_Init();
  MX_ADC2_Init();
  /* USER CODE BEGIN 2 */
  /* Keep LAN9252 reset and its interrupts masked during the lengthy FOC
     alignment/calibration.  TwinCAT must not see a slave whose SSC main loop
     is not running yet. */
  HAL_NVIC_DisableIRQ(ECAT_INT_EXTI_IRQn);
  HAL_NVIC_DisableIRQ(EXTI9_5_IRQn);
  HAL_GPIO_WritePin(ECAT_RST_GPIO_Port, ECAT_RST_Pin, GPIO_PIN_RESET);

  HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);

  /* ADC2首笔DMA结果到达前，禁止状态机把BSS初值0V误判为欠压。 */
  FOC.foc.Real_VBUS = -9999.0f;

	if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)FOC.adc2.ADC2_Values, ADC2_CH_NUM) != HAL_OK)
    {
        Error_Handler();
    }

	FOC_Init();

  /* PDO integration stage 1: keep the power stage inhibited while PDO command
     and feedback layout is validated in TwinCAT.  The FOC sampling and
     calculations continue, but EtherCAT cannot enable the motor yet. */
  FOC.foc.Target_Speed = 0.0f;
  FOC.foc.Target_Iq = 0.0f;
  User_Disable_Motor();
  	
	/* TIM6既触发ADC2规则组，也以1 kHz运行速度环/位置环和状态机。 */
	if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK)
	{
		Error_Handler();
	}

  /* FOC is ready; now expose and initialize the EtherCAT slave. */
  HAL_GPIO_WritePin(ECAT_RST_GPIO_Port, ECAT_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);

  if (HW_Init() != 0U)
  {
    Error_Handler();
  }

  if (MainInit() != 0U)
  {
    Error_Handler();
  }

  /* TIM7 supplies the 1 ms SSC time base. */
  if (HAL_TIM_Base_Start_IT(&htim7) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_GPIO_EXTI_CLEAR_IT(ECAT_INT_Pin);
  __HAL_GPIO_EXTI_CLEAR_IT(ECAT_SYNC0_Pin);
  __HAL_GPIO_EXTI_CLEAR_IT(ECAT_SYNC1_Pin);
  HAL_NVIC_EnableIRQ(ECAT_INT_EXTI_IRQn);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    MainLoop();
//	Printf_Normal_Loop(&FOC);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV2;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
