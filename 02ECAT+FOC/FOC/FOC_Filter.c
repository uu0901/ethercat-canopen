#include "FOC_Filter.h"

#define VALUE_2_SQRT2 2.8284271247461903f

void LPF_Init(LPF_STRUCT *lpf)
{
    double temp1 = (double)lpf->T * (double)lpf->Wc *
                   (double)VALUE_2_SQRT2;
    double temp2 = (double)lpf->T * (double)lpf->T *
                   (double)lpf->Wc * (double)lpf->Wc;
    double den = temp2 + temp1 + 4.0;

    lpf->filter.num[0] = (float)(temp2 / den);
    lpf->filter.num[1] = (float)((2.0 * temp2) / den);
    lpf->filter.den[0] = (float)((-8.0 + 2.0 * temp2) / den);
    lpf->filter.den[1] = (float)((temp2 - temp1 + 4.0) / den);

    lpf->filter.i[0] = 0.0f;
    lpf->filter.i[1] = 0.0f;
    lpf->filter.o[0] = 0.0f;
    lpf->filter.o[1] = 0.0f;
    lpf->filter.Input = 0.0f;
    lpf->filter.Output = 0.0f;
}

void LPF_Loop(LPF_STRUCT *lpf)
{
    lpf->filter.Output =
        lpf->filter.num[0] * (lpf->filter.Input + lpf->filter.i[1]) +
        lpf->filter.num[1] * lpf->filter.i[0] -
        lpf->filter.den[0] * lpf->filter.o[0] -
        lpf->filter.den[1] * lpf->filter.o[1];

    lpf->filter.i[1] = lpf->filter.i[0];
    lpf->filter.i[0] = lpf->filter.Input;
    lpf->filter.o[1] = lpf->filter.o[0];
    lpf->filter.o[0] = lpf->filter.Output;
}

void Transfer_LPF_Loop(LPF_STRUCT *lpf, float input)
{
    lpf->filter.Input = input;
    LPF_Loop(lpf);
}
