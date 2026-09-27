#include "FOC_PID.h"

#include "FOC_Math.h"

void PID_Init(PID_STRUCT *pid)
{
    double temp0 = (double)pid->T * (double)pid->Ki / 2.0;
    double temp1 = (double)pid->T * (double)pid->Wc;
    double temp2 = (double)pid->Kd * (double)pid->Wc;
    double den = -2.0 + temp1;

    pid->run.I_num = (float)temp0;
    pid->run.D_num = (float)((2.0 * temp2) / den);
    pid->run.D_den = (float)((2.0 + temp1) / den);

    pid->run.i[0] = 0.0f;
    pid->run.i[1] = 0.0f;
    pid->run.Io = 0.0f;
    pid->run.Do = 0.0f;
    pid->run.Ref = 0.0f;
    pid->run.Fbk = 0.0f;
    pid->run.Output = 0.0f;
    pid->run.IntegralFrozen_flag = 0U;
}

void PID_Loop(PID_STRUCT *pid)
{
    float integralCandidate;
    float outputCandidate;

    pid->run.i[0] = pid->run.Ref - pid->run.Fbk;

    integralCandidate = pid->run.Io;

    if (pid->Ki != 0.0f)
    {
        integralCandidate += pid->run.I_num *
                             (pid->run.i[0] + pid->run.i[1]);

        if (integralCandidate > pid->IntMax)
        {
            integralCandidate = pid->IntMax;
        }
        else if (integralCandidate < pid->IntMin)
        {
            integralCandidate = pid->IntMin;
        }
    }

    if (pid->Kd != 0.0f)
    {
        pid->run.Do = pid->run.D_num *
                      (pid->run.i[0] + pid->run.i[1]) -
                      pid->run.D_den * pid->run.Do;
    }

    outputCandidate = pid->run.i[0] * pid->Kp +
                      integralCandidate + pid->run.Do;

    /*
     * 条件积分抗饱和：输出已在上/下限且当前误差还在继续推向饱和时，
     * 拒绝本周期的新积分；误差反向时立即允许积分回退。
     */
    if ((outputCandidate > pid->OutMax) && (pid->run.i[0] > 0.0f))
    {
        pid->run.Output = Value_Limit(pid->run.i[0] * pid->Kp +
                                      pid->run.Io + pid->run.Do,
                                      pid->OutMax,
                                      pid->OutMin);
        pid->run.IntegralFrozen_flag = 1U;
    }
    else if ((outputCandidate < pid->OutMin) && (pid->run.i[0] < 0.0f))
    {
        pid->run.Output = Value_Limit(pid->run.i[0] * pid->Kp +
                                      pid->run.Io + pid->run.Do,
                                      pid->OutMax,
                                      pid->OutMin);
        pid->run.IntegralFrozen_flag = 1U;
    }
    else
    {
        pid->run.Io = integralCandidate;
        pid->run.Output = Value_Limit(outputCandidate,
                                      pid->OutMax,
                                      pid->OutMin);
        pid->run.IntegralFrozen_flag = 0U;
    }
    pid->run.i[1] = pid->run.i[0];
}

void PID_Clear(PID_STRUCT *pid)
{
    pid->run.Io = 0.0f;
    pid->run.Do = 0.0f;
    PID_Init(pid);
}

void Transfer_PID_Loop(PID_STRUCT *pid, float ref, float fbk)
{
    pid->run.Ref = ref;
    pid->run.Fbk = fbk;
    PID_Loop(pid);
}
