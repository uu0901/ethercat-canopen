#include "FOC_Serial.h"
#include "UserData_Function.h"
#include "UserData_UserControl.h"

char Serial_RxPacket[2][1024] = {{0}};
uint8_t rxBufIndex = 0U;

// [初始化]初始化JustFloat数据帧尾
void Printf_Init(PRINTF_STRUCT *str)
{
    str->tail[0] = 0x00;
    str->tail[1] = 0x00;
    str->tail[2] = 0x80;
    str->tail[3] = 0x7f;
    /* JustFloa数据帧尾格式 */
}

// 与参考工程一致：主循环更新通道后，阻塞发送一整帧JustFloat数据。
void Printf_Loop(PRINTF_STRUCT *str)
{
    User_UserTX();
    User_CorrespondSet((uint8_t *)str, sizeof(PRINTF_STRUCT));
}
void serialPrintf(const char *format, ...)
{
    static char txBuf[1024];
    va_list args;
    va_start(args, format);

    int len = vsnprintf(txBuf, sizeof(txBuf), format, args);

    va_end(args);

//    while (huart2.gState != HAL_UART_STATE_READY)
//        ;

//    HAL_UART_Transmit_DMA(&huart2, (uint8_t *)txBuf, strlen(txBuf));
    if (len < 0)
    {
        return;
    }
    if (len >= (int)sizeof(txBuf))
    {
        len = sizeof(txBuf) - 1;
    }

	User_CorrespondSet((uint8_t *)txBuf, (uint16_t)len);
}

