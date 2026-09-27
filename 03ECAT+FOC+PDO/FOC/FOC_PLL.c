#include "FOC_PLL.h"

#include "FOC_Math.h"

static void PLL_CorrectError(float *angle, float error)
{
    if (error >= Value_PI)
    {
        *angle -= Value_2PI;
    }
    if (error <= -Value_PI)
    {
        *angle += Value_2PI;
    }
}

void PLL_Init(PLL_STRUCT *pll)
{
    double temp0 = (double)pll->T * (double)pll->Ki;

    pll->go.X_num[0] = (float)((2.0 * (double)pll->Kp + temp0) / 2.0);
    pll->go.X_num[1] = (float)((-2.0 * (double)pll->Kp + temp0) / 2.0);
    pll->go.Y_num = (float)((double)pll->T / 2.0);

    pll->is_position_mode = 0U;
    pll->go.We_i = 0.0f;
    pll->go.Re_i = 0.0f;
    pll->go.OutWe = 0.0f;
    pll->go.OutRe = 0.0f;
    pll->go.Error = 0.0f;
}

void PLL_Loop(PLL_STRUCT *pll)
{
    pll->go.OutWe += pll->go.X_num[0] * pll->go.Error +
                     pll->go.X_num[1] * pll->go.We_i;

    pll->go.OutRe += pll->go.Y_num * (pll->go.OutWe + pll->go.Re_i);
    if (pll->is_position_mode == 0U)
    {
        pll->go.OutRe = Value_normalize(pll->go.OutRe);
    }

    pll->go.We_i = pll->go.Error;
    pll->go.Re_i = pll->go.OutWe;
}

void Transfer_PLL_Loop(PLL_STRUCT *pll,
                       uint8_t positionMode,
                       uint8_t poles,
                       float inputRad)
{
    pll->is_position_mode = positionMode;
    if (positionMode != 0U)
    {
        pll->go.Error = inputRad - Value_normalize(pll->go.OutRe * poles);
    }
    else
    {
        pll->go.Error = inputRad - pll->go.OutRe * poles;
    }

    PLL_CorrectError(&pll->go.Error, pll->go.Error);
    PLL_Loop(pll);
}
