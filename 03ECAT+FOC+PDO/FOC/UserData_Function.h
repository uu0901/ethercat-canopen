#ifndef __USERDATA_FUNCTION_H
#define __USERDATA_FUNCTION_H

#include <stdint.h>
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "spi.h"
#include "adc.h"
#include "FOC.h"
#include "MT6701.h"

extern char Serial_RxPacket[2][1024];
extern uint8_t rxBufIndex;
//extern volatile uint16_t ADC_LargeBuffer[ADC1_REGULAR_CH_NUM];
#define MT6701_CS_Enable()  HAL_GPIO_WritePin(MT6701_CS_GPIO_Port, MT6701_CS_Pin, GPIO_PIN_RESET)
#define MT6701_CS_Disable() HAL_GPIO_WritePin(MT6701_CS_GPIO_Port, MT6701_CS_Pin, GPIO_PIN_SET)

static inline void User_PwmDuty_Set(unsigned short int Duty_u, unsigned short int Duty_v, unsigned short int Duty_w)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, Duty_u);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, Duty_v);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, Duty_w);
}

static inline void User_InitialInit(void)
{
	/* MT6701 uses a complete 24-bit SSI frame. Prime the DMA pipeline before
	 * starting the 20 kHz motor-control time base. */
	MT6701_Init();
	MT6701_TriggerRead();
	/* TIM1 CH4/OC4REF在公共低侧导通窗口内触发ADC注入序列；功率PWM仍保持关闭。 */
	HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
	HAL_ADCEx_InjectedStart_IT(&hadc1);
	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);  // 启动内部OC4REF，同时启动TIM1计数器
//	__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 4200 / 2);//50			50
//	__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 4200 / 3);//66.6		33.4
//	__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 4200 / 4);//75			25
	
//	User_PwmDuty_Set(4200 / 2, 4200 / 3, 4200 / 4);

	HAL_UARTEx_ReceiveToIdle_IT(&huart3, (uint8_t *)Serial_RxPacket[rxBufIndex], sizeof(Serial_RxPacket[rxBufIndex]));
}

static inline void User_Enable_Motor(void)
{
	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
	
	__HAL_TIM_MOE_ENABLE(&htim1);
}
static inline void User_Disable_Motor(void)
{
    User_PwmDuty_Set(0, 0, 0);
	__HAL_TIM_MOE_DISABLE_UNCONDITIONALLY(&htim1);
}
static inline void User_CorrespondSet(unsigned char *ch, unsigned short int size)
{
    HAL_UART_Transmit(&huart3, ch, size, 0xFFFF);
}
static inline void User_Delay(unsigned int ms)
{
    HAL_Delay(ms);
}
static inline uint16_t User_Encoder_ReadRaw(void)
{
    FOC.encoder.errorFlag = (MT6701_IsDataValid() != 0U) ? 0U : 1U;
    return MT6701_GetRawAngle();
}

static inline uint16_t User_Encoder_ReadRad(void)
{
    return User_Encoder_ReadRaw();
}

#endif  // USERDATA_FUNCTION_H
