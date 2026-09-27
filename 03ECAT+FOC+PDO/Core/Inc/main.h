/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED1_Pin GPIO_PIN_13
#define LED1_GPIO_Port GPIOC
#define LED2_Pin GPIO_PIN_14
#define LED2_GPIO_Port GPIOC
#define LED3_Pin GPIO_PIN_15
#define LED3_GPIO_Port GPIOC
#define ECAT_CS_Pin GPIO_PIN_3
#define ECAT_CS_GPIO_Port GPIOA
#define ECAT_INT_Pin GPIO_PIN_4
#define ECAT_INT_GPIO_Port GPIOA
#define ECAT_INT_EXTI_IRQn EXTI4_IRQn
#define ECAT_SYNC0_Pin GPIO_PIN_5
#define ECAT_SYNC0_GPIO_Port GPIOA
#define ECAT_SYNC0_EXTI_IRQn EXTI9_5_IRQn
#define ECAT_SYNC1_Pin GPIO_PIN_6
#define ECAT_SYNC1_GPIO_Port GPIOA
#define ECAT_SYNC1_EXTI_IRQn EXTI9_5_IRQn
#define ECAT_RST_Pin GPIO_PIN_4
#define ECAT_RST_GPIO_Port GPIOC
#define KEY1_Pin GPIO_PIN_6
#define KEY1_GPIO_Port GPIOC
#define KEY2_Pin GPIO_PIN_7
#define KEY2_GPIO_Port GPIOC
#define KEY3_Pin GPIO_PIN_8
#define KEY3_GPIO_Port GPIOC
#define KEY4_Pin GPIO_PIN_9
#define KEY4_GPIO_Port GPIOC
#define ECAT_CLK_Pin GPIO_PIN_10
#define ECAT_CLK_GPIO_Port GPIOC
#define ECAT_MISO_Pin GPIO_PIN_11
#define ECAT_MISO_GPIO_Port GPIOC
#define ECAT_MOSI_Pin GPIO_PIN_12
#define ECAT_MOSI_GPIO_Port GPIOC
#define MT6701_CS_Pin GPIO_PIN_2
#define MT6701_CS_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
