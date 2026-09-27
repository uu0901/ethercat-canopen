#ifndef _FOC_SERIAL_H_
#define _FOC_SERIAL_H_

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stdio.h"
#include "usart.h"

#define CH_COUNT 16  // 与参考工程一致：JustFloat通道数

#define FOC_PRINT_MODE_JUSTFLOAT 0
#define FOC_PRINT_MODE_STRING    1

#ifndef FOC_PRINT_MODE
#define FOC_PRINT_MODE FOC_PRINT_MODE_JUSTFLOAT
#endif

typedef struct
{
    float fdata[CH_COUNT];
    uint8_t tail[4];
} PRINTF_STRUCT;

void Printf_Init(PRINTF_STRUCT *str);
void Printf_Loop(PRINTF_STRUCT *str);
void serialPrintf(const char *format, ...);
#endif
