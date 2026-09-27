#ifndef __USERDATA_USERCONTROL_H
#define __USERDATA_USERCONTROL_H

#include <stdint.h>
#include <stdio.h>
#include "FOC.h"
#include "UserData_Function.h"

extern float P, I, D;

static inline void analyzeReceivedData(uint16_t Size)
{
    char *buf = Serial_RxPacket[rxBufIndex];

    if (Size >= sizeof(Serial_RxPacket[0]))
        Size = sizeof(Serial_RxPacket[0]) - 1;

    buf[Size] = '\0';

    for (char *p = buf; *p; p++)
    {
        if (*p == '\r' || *p == '\n')
        {
            *p = '\0';
            break;
        }
    }

    if (sscanf(buf, "P:%f,I:%f,D:%f", &P, &I, &D) == 3)
    {
    }
    else if (sscanf(buf, "P:%f", &P) == 1)
    {
    }
    else if (sscanf(buf, "I:%f", &I) == 1)
    {
    }
    else if (sscanf(buf, "D:%f", &D) == 1)
    {
    }
	

    rxBufIndex ^= 1;
}

static inline void User_UserTX(void)
{
    // VOFA速度环跟踪专用通道。CH0/CH2用于直接观察目标与实际速度。
//    FOC.TXdata.fdata[0] = FOC.foc.Target_Speed;
//    FOC.TXdata.fdata[1] = FOC.pid_VelCur.spd.run.Ref;
//    FOC.TXdata.fdata[2] = FOC.pid_VelCur.spd.run.Fbk;
//    FOC.TXdata.fdata[3] = FOC.encoder.pllVel;
//    FOC.TXdata.fdata[4] = FOC.pid_VelCur.spd.run.Ref - FOC.pid_VelCur.spd.run.Fbk;
//    FOC.TXdata.fdata[5] = FOC.pid_VelCur.spd.run.Output;
//    FOC.TXdata.fdata[6] = FOC.current.Real_Iq;
//    FOC.TXdata.fdata[7] = FOC.foc.Target_Iq;
//    FOC.TXdata.fdata[8] = FOC.current.Real_Id;
//    FOC.TXdata.fdata[9] = FOC.foc.Target_Id;
//    FOC.TXdata.fdata[10] = FOC.foc.Uq_in;
//    FOC.TXdata.fdata[11] = FOC.foc.Ud_in;
//    FOC.TXdata.fdata[12] = FOC.encoder.windowVel;
//    FOC.TXdata.fdata[15] = FOC.encoder.pllError;
//    FOC.TXdata.fdata[16] = FOC.current.Real_Ia;
//    FOC.TXdata.fdata[17] = FOC.current.Real_Ib;
//    FOC.TXdata.fdata[18] = FOC.current.Real_Ic;
//    FOC.TXdata.fdata[19] = FOC.foc.Duty_u;
//    FOC.TXdata.fdata[20] = FOC.foc.Duty_v;
//    FOC.TXdata.fdata[21] = FOC.foc.Duty_w;
//    FOC.TXdata.fdata[22] = FOC.foc.Real_MCUTemp;  // VOFA CH22：MCU温度(℃)
//    FOC.TXdata.fdata[23] = FOC.foc.Real_VBUS;     // VOFA CH23：母线电压(V)

    FOC.TXdata.fdata[0] = FOC.current.ADC_InjectedValues[0];
    FOC.TXdata.fdata[1] = FOC.current.ADC_InjectedValues[1];
    FOC.TXdata.fdata[2] = FOC.current.ADC_InjectedValues[2];
	
    FOC.TXdata.fdata[3] = FOC.foc.Duty_u;
    FOC.TXdata.fdata[4] = FOC.foc.Duty_v;
    FOC.TXdata.fdata[5] = FOC.foc.Duty_w;
	
    FOC.TXdata.fdata[6] = FOC.current.Real_Ia;
    FOC.TXdata.fdata[7] = FOC.current.Real_Ib;
    FOC.TXdata.fdata[8] = FOC.current.Real_Ic;
	
    FOC.TXdata.fdata[9] =  FOC.encoder.angleWithoutTrackCur;
    FOC.TXdata.fdata[10] = FOC.encoder.angleCur;
	
    FOC.TXdata.fdata[11] = FOC.current.Real_Id;
    FOC.TXdata.fdata[12] = FOC.current.Real_Iq;
    FOC.TXdata.fdata[13] = 0;
    FOC.TXdata.fdata[14] = 0;
    FOC.TXdata.fdata[15] = 0;
}


#endif  // USERDATA_USERCONTROL_H
