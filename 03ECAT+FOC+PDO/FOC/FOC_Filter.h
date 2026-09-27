#ifndef FOC_FILTER_H
#define FOC_FILTER_H

typedef struct
{
    float i[2];
    float o[2];
    float Input;
    float Output;
    float num[2];
    float den[2];
} LPF_GO_STRUCT;

typedef struct
{
    LPF_GO_STRUCT filter;
    float T;
    float Wc;
} LPF_STRUCT;

void LPF_Init(LPF_STRUCT *lpf);
void LPF_Loop(LPF_STRUCT *lpf);
void Transfer_LPF_Loop(LPF_STRUCT *lpf, float input);

#endif /* FOC_FILTER_H */
