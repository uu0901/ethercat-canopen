#ifndef FOC_PLL_H
#define FOC_PLL_H

#include <stdint.h>

typedef struct
{
    float We_i;
    float Re_i;
    float X_num[2];
    float Y_num;
    float Error;
    float OutWe;
    float OutRe;
} PLL_GO_STRUCT;

typedef struct
{
    PLL_GO_STRUCT go;
    float T;
    float Kp;
    float Ki;
    uint8_t is_position_mode;
} PLL_STRUCT;

void PLL_Init(PLL_STRUCT *pll);
void PLL_Loop(PLL_STRUCT *pll);
void Transfer_PLL_Loop(PLL_STRUCT *pll,
                       uint8_t positionMode,
                       uint8_t poles,
                       float inputRad);

#endif /* FOC_PLL_H */
