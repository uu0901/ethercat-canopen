#ifndef FOC_PID_H
#define FOC_PID_H

#include <stdint.h>

typedef struct
{
    float i[2];
    float Io;
    float Do;

    float Ref;
    float Fbk;
    float Output;

    float I_num;
    float D_num;
    float D_den;

    uint8_t IntegralFrozen_flag;
} PID_RUN_STRUCT;

typedef struct
{
    PID_RUN_STRUCT run;

    float T;
    float Wc;
    float Kp;
    float Ki;
    float Kd;

    float OutMax;
    float OutMin;
    float IntMax;
    float IntMin;
} PID_STRUCT;

void PID_Init(PID_STRUCT *pid);
void PID_Loop(PID_STRUCT *pid);
void PID_Clear(PID_STRUCT *pid);
void Transfer_PID_Loop(PID_STRUCT *pid, float ref, float fbk);
#endif /* FOC_PID_H */
