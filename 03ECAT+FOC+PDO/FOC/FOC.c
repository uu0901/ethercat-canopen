#include "FOC.h"
#include "FOC_Serial.h"
#include "UserData_Function.h"
#include "UserData_UserControl.h"
#include "FOC_SVPWM.h"
#include "UserData_Motor.h"
#include "FOC_MotorStatus.h"
#include "FOC_CurrentSense.h"

/*
 * 当前Keil工程长期保持打开时会以缓存的文件列表覆盖uvprojx，导致新增的
 * FOC_Cogging.c没有生成目标文件。这里采用单元编译方式纳入独立模块，
 * 模块代码仍单独维护，但不依赖工程文件列表刷新。
 */
#include "FOC_Cogging.c"

FOC_System_STRUCT FOC = {0};


#define ENCODER_ALIGN_RAMP_STEPS       100U
#define ENCODER_ALIGN_RAMP_DELAY_MS    5U
#define ENCODER_ALIGN_HOLD_MS          1000U
#define ENCODER_OFFSET_SAMPLES         128U
#define ENCODER_OFFSET_SAMPLE_DELAY_MS 1U
#define ENCODER_OFFSET_MAX_SPAN        32
#define ENCODER_OFFSET_MAX_ATTEMPTS    5U
#define MT6701_OFFSET_CPR              16384
#define MT6701_OFFSET_HALF_CPR         (MT6701_OFFSET_CPR / 2)
#define VBUS_VALID_MIN                 1.0f

/* 当前调试阶段先关闭dq解耦/反电动势前馈，便于单独验证PI与方向。 */
#define FOC_CURRENT_FEEDFORWARD_ENABLE 0U

static uint8_t speedLoopInitialized = 0U;
static int8_t speedLoopActiveMode = -1;

static int32_t encoderCalibrationForward[ENCODER_CORRECTION_POINTS + 1U];
static int32_t encoderCalibrationReverse[ENCODER_CORRECTION_POINTS + 1U];
static int32_t encoderCalibrationPhase[ENCODER_CORRECTION_POINTS + 1U];
static int16_t encoderCalibrationTable[ENCODER_CORRECTION_POINTS];

static float SpeedProfile_Next(float reference, float target)
{
    float error = target - reference;
    float step;

    /* 反向或目标幅值降低时先按减速度收回到目标，其余情况按加速度起坡。 */
    if (((reference * target) < 0.0f) ||
        (Value_fabsf(target) < Value_fabsf(reference)))
    {
        step = FOC_SPEED_PROFILE_DECEL_RAD_S2 * FOC_SPEED_PROFILE_DT_S;
    }
    else
    {
        step = FOC_SPEED_PROFILE_ACCEL_RAD_S2 * FOC_SPEED_PROFILE_DT_S;
    }

    if (error > step)
    {
        reference += step;
    }
    else if (error < -step)
    {
        reference -= step;
    }
    else
    {
        reference = target;
    }

    return reference;
}

static float SpeedLoop_LowSpeedAssistGain(float speedReference)
{
    float speedAbs = Value_fabsf(speedReference);

    if (speedAbs <= FOC_LOW_SPEED_ASSIST_FULL_RAD_S)
    {
        return 1.0f;
    }
    if (speedAbs >= FOC_LOW_SPEED_ASSIST_ZERO_RAD_S)
    {
        return 0.0f;
    }

    return (FOC_LOW_SPEED_ASSIST_ZERO_RAD_S - speedAbs) /
           (FOC_LOW_SPEED_ASSIST_ZERO_RAD_S -
            FOC_LOW_SPEED_ASSIST_FULL_RAD_S);
}

static void SpeedLoop_HoldMaximumTorque(PID_STRUCT *pid,
                                        float target,
                                        float feedback)
{
    pid->run.Ref = target;
    pid->run.Fbk = feedback;
    pid->run.i[0] = 0.0f;
    pid->run.i[1] = 0.0f;
    pid->run.Io = 0.0f;
    pid->run.Do = 0.0f;
    pid->run.Output = (target >= 0.0f) ? pid->OutMax : pid->OutMin;
    pid->run.IntegralFrozen_flag = 1U;
}

// 优先使用实时母线电压；工程尚未接入母线采样时回退到配置的名义值。
static float VoltageBus_Get(const FOC_System_STRUCT *FOC)
{
    if (FOC->foc.Real_VBUS >= VBUS_VALID_MIN)
    {
        return FOC->foc.Real_VBUS;
    }

    return FOC->motor.VBUS;
}

// 幅值不变 Clarke/Park 变换下，线性 SVPWM 的归一化基准为 Vdc/sqrt(3)。
static float VoltageVector_GetBase(const FOC_System_STRUCT *FOC)
{
    float vbus = VoltageBus_Get(FOC);

    if (vbus < VBUS_VALID_MIN)
    {
        return 0.0f;
    }

    return vbus * Value_INV_SQRT3;
}

/* 为三电阻低侧采样保留公共导通窗口后的实际电压矢量上限。 */
static float VoltageVector_GetLimit(const FOC_System_STRUCT *FOC)
{
    return VoltageVector_GetBase(FOC);
}

// 对最终 dq 电压矢量限幅，保持电压矢量方向不变。
static float VoltageVector_Limit(const FOC_System_STRUCT *FOC, float *u_d, float *u_q)
{
    float voltageBase = VoltageVector_GetBase(FOC);
    float voltageLimit = VoltageVector_GetLimit(FOC);
    float voltageSquare;
    float limitSquare;

    if (voltageBase <= 0.0f)
    {
        *u_d = 0.0f;
        *u_q = 0.0f;
        return 0.0f;
    }

    voltageSquare = *u_d * *u_d + *u_q * *u_q;
    limitSquare = voltageLimit * voltageLimit;

    if (voltageSquare > limitSquare)
    {
        float scale = voltageLimit / Value_sqrtf(voltageSquare);
        *u_d *= scale;
        *u_q *= scale;
    }

    return voltageBase;
}

// SVPWM电机驱动的马鞍波生成
// d_set/q_set 的单位为伏特；本函数统一完成最终矢量限幅和 SVPWM 归一化。
void SVPWM_Tick(FOC_System_STRUCT *FOC, float sine, float cosine, float d_set, float q_set)
{
    float U_alpha, U_beta;
    float voltageBase = VoltageVector_Limit(FOC, &d_set, &q_set);

    if (voltageBase > 0.0f)
    {
        d_set /= voltageBase;
        q_set /= voltageBase;
    }

    ipark(&U_alpha, &U_beta, d_set, q_set, sine, cosine);

    SVPWM(U_alpha, U_beta, &FOC->foc.Du, &FOC->foc.Dv, &FOC->foc.Dw);
    if (FOC->motor.PWM_Dir == 1)
    {
        FOC->foc.Duty_u = (uint16_t)(FOC->foc.Du * FOC->motor.Duty);
        FOC->foc.Duty_v = (uint16_t)(FOC->foc.Dv * FOC->motor.Duty);
        FOC->foc.Duty_w = (uint16_t)(FOC->foc.Dw * FOC->motor.Duty);
    }
    else if (FOC->motor.PWM_Dir == -1)
    {
        FOC->foc.Duty_u = (uint16_t)((1.0f - FOC->foc.Du) * FOC->motor.Duty);
        FOC->foc.Duty_v = (uint16_t)((1.0f - FOC->foc.Dv) * FOC->motor.Duty);
        FOC->foc.Duty_w = (uint16_t)((1.0f - FOC->foc.Dw) * FOC->motor.Duty);
    }
    if (FOC->motor.Motor_Dir == -1)
    {  // 判断电机方向并修改(原理是AB相序交换)
        uint16_t duty_temp = FOC->foc.Duty_u;
        FOC->foc.Duty_u = FOC->foc.Duty_v;
        FOC->foc.Duty_v = duty_temp;
    }

    User_PwmDuty_Set(FOC->foc.Duty_u, FOC->foc.Duty_v, FOC->foc.Duty_w);
}
//电压强拖
void Volatge_Force(void)
{
	static float openLoopShaftAngle;
	openLoopShaftAngle = Value_normalize(openLoopShaftAngle + FOC.foc.Target_VF_Speed * 5e-5f);  // 通过乘以时间间隔和目标速度来计算需要转动的机械角度，存储在 shaft_angle 变量中。在此之前，还需要对轴角度进行归一化，以确保其值在 0 到 2π 之间，因为计算结果要换算成电角度。 .超过2π重新开始 因为有取余, 机械角度是转子的角度
	FOC.motor.Electrical_Angle = Value_normalize(FOC.motor.Poles * openLoopShaftAngle);	//计算电角度
	fast_sin_cos(FOC.motor.Electrical_Angle, &FOC.foc.sine, &FOC.foc.cosine);           //计算电角度的正弦和余弦值
//    FOC.foc.Uq_in = FOC.foc.Target_Uq;
//	FOC.foc.Ud_in = 0;
//	SVPWM_Tick(&FOC, FOC.foc.sine, FOC.foc.cosine, FOC.foc.Ud_in, FOC.foc.Uq_in);
}
//电机零点对齐(机械角度对齐)
static void Positioning_SetAngle(FOC_System_STRUCT *FOC,
                                 float electricalAngle,
                                 float Ud,
                                 float Uq)
{
    float sine;
    float cosine;

    electricalAngle = Value_normalize(electricalAngle);
    fast_sin_cos(electricalAngle, &sine, &cosine);
    SVPWM_Tick(FOC, sine, cosine, Ud, Uq);
}

static void Positioning_Set(FOC_System_STRUCT *FOC, float Ud, float Uq)
{
    Positioning_SetAngle(FOC, 0.0f, Ud, Uq);
}
//justfloat打印调试
void Printf_Normal_Loop(FOC_System_STRUCT *FOC)
{
    Printf_Loop(&FOC->TXdata);
}
static int32_t Encoder_WrapDelta(int32_t delta)
{
    if (delta > MT6701_OFFSET_HALF_CPR)
    {
        delta -= MT6701_OFFSET_CPR;
    }
    else if (delta < -MT6701_OFFSET_HALF_CPR)
    {
        delta += MT6701_OFFSET_CPR;
    }
    return delta;
}

static uint16_t Encoder_WrapRaw(int32_t raw)
{
    while (raw >= MT6701_OFFSET_CPR)
    {
        raw -= MT6701_OFFSET_CPR;
    }
    while (raw < 0)
    {
        raw += MT6701_OFFSET_CPR;
    }
    return (uint16_t)raw;
}

static uint8_t Encoder_ReadRawAverage(uint16_t *averageRaw)
{
    uint16_t referenceRaw;
    int32_t deltaSum = 0;
    uint16_t sample;

    referenceRaw = User_Encoder_ReadRaw();
    if (FOC.encoder.errorFlag != 0U)
    {
        return 0U;
    }

    for (sample = 0U;
         sample < FOC_ENCODER_CALIBRATION_AVERAGE_SAMPLES;
         sample++)
    {
        uint16_t raw;
        User_Delay(FOC_ENCODER_CALIBRATION_SAMPLE_DELAY_MS);
        raw = User_Encoder_ReadRaw();
        if (FOC.encoder.errorFlag != 0U)
        {
            return 0U;
        }
        deltaSum += Encoder_WrapDelta((int32_t)raw - (int32_t)referenceRaw);
    }

    *averageRaw = Encoder_WrapRaw((int32_t)referenceRaw +
                                  deltaSum /
                                  (int32_t)FOC_ENCODER_CALIBRATION_AVERAGE_SAMPLES);
    return 1U;
}

/*
 * 使用定子D轴磁场作为角度基准，正反各扫一圈并取平均。
 * 生成的256点表修正MT6701磁铁偏心/安装误差造成的一圈周期非线性。
 */
static uint8_t EncoderCorrection_Calibrate(FOC_System_STRUCT *FOC)
{
#if FOC_ENCODER_CORRECTION_ENABLE
    uint16_t raw;
    uint16_t previousRaw;
    int32_t unwrappedRaw;
    int32_t direction;
    int32_t origin;
    int32_t index;
    uint16_t tableIndex;
    uint16_t segment;
    float calibrationUd;

    encoderCorrectionDisable();
    calibrationUd = FOC_ENCODER_CALIBRATION_UD_RATIO * FOC->motor.VBUS;

    /* 正向扫描，包含0和2pi两个端点。 */
    unwrappedRaw = 0;
    previousRaw = 0U;
    for (index = 0; index <= (int32_t)ENCODER_CORRECTION_POINTS; index++)
    {
        float mechanicalAngle = Value_2PI * (float)index /
                                (float)ENCODER_CORRECTION_POINTS;
        Positioning_SetAngle(FOC,
                             mechanicalAngle * (float)FOC->motor.Poles,
                             calibrationUd,
                             0.0f);
        User_Delay(FOC_ENCODER_CALIBRATION_SETTLE_MS);
        if (Encoder_ReadRawAverage(&raw) == 0U)
        {
            encoderCorrectionDisable();
            return 0U;
        }

        if (index == 0)
        {
            unwrappedRaw = (int32_t)raw;
        }
        else
        {
            unwrappedRaw += Encoder_WrapDelta((int32_t)raw -
                                              (int32_t)previousRaw);
        }
        encoderCalibrationForward[index] = unwrappedRaw;
        previousRaw = raw;
    }

    direction = (encoderCalibrationForward[ENCODER_CORRECTION_POINTS] >=
                 encoderCalibrationForward[0]) ? 1 : -1;
    if (Value_fabsf((float)(encoderCalibrationForward[ENCODER_CORRECTION_POINTS] -
                            encoderCalibrationForward[0])) <
        (0.75f * (float)MT6701_OFFSET_CPR))
    {
        encoderCorrectionDisable();
        return 0U;
    }

    /* 反向扫描，展开值从正向终点连续返回起点。 */
    unwrappedRaw = encoderCalibrationForward[ENCODER_CORRECTION_POINTS];
    for (index = (int32_t)ENCODER_CORRECTION_POINTS; index >= 0; index--)
    {
        float mechanicalAngle = Value_2PI * (float)index /
                                (float)ENCODER_CORRECTION_POINTS;
        Positioning_SetAngle(FOC,
                             mechanicalAngle * (float)FOC->motor.Poles,
                             calibrationUd,
                             0.0f);
        User_Delay(FOC_ENCODER_CALIBRATION_SETTLE_MS);
        if (Encoder_ReadRawAverage(&raw) == 0U)
        {
            encoderCorrectionDisable();
            return 0U;
        }

        unwrappedRaw += Encoder_WrapDelta((int32_t)raw -
                                          (int32_t)previousRaw);
        encoderCalibrationReverse[index] = unwrappedRaw;
        previousRaw = raw;
    }

    origin = (encoderCalibrationForward[0] +
              encoderCalibrationReverse[0]) / 2;
    for (index = 0; index <= (int32_t)ENCODER_CORRECTION_POINTS; index++)
    {
        int32_t averageRaw = (encoderCalibrationForward[index] +
                              encoderCalibrationReverse[index]) / 2;
        int32_t phase = direction * (averageRaw - origin);

        encoderCalibrationPhase[index] = phase;
        if ((index > 0) &&
            (phase <= encoderCalibrationPhase[index - 1]))
        {
            encoderCorrectionDisable();
            return 0U;
        }
    }

    if ((encoderCalibrationPhase[ENCODER_CORRECTION_POINTS] <
         (3 * MT6701_OFFSET_CPR) / 4) ||
        (encoderCalibrationPhase[ENCODER_CORRECTION_POINTS] >
         (5 * MT6701_OFFSET_CPR) / 4))
    {
        encoderCorrectionDisable();
        return 0U;
    }

    /* 将非均匀实测节点重采样为按原始角度均匀分布的256点修正表。 */
    segment = 0U;
    for (tableIndex = 0U;
         tableIndex < ENCODER_CORRECTION_POINTS;
         tableIndex++)
    {
        int32_t rawPhase = (int32_t)tableIndex *
                           MT6701_OFFSET_CPR /
                           (int32_t)ENCODER_CORRECTION_POINTS;
        int32_t denominator;
        float fraction;
        float expectedPhase;
        float correctionFloat;
        int32_t correction;

        while ((segment < (ENCODER_CORRECTION_POINTS - 1U)) &&
               (encoderCalibrationPhase[segment + 1U] < rawPhase))
        {
            segment++;
        }

        denominator = encoderCalibrationPhase[segment + 1U] -
                      encoderCalibrationPhase[segment];
        if (denominator <= 0)
        {
            encoderCorrectionDisable();
            return 0U;
        }

        fraction = (float)(rawPhase - encoderCalibrationPhase[segment]) /
                   (float)denominator;
        expectedPhase = ((float)segment + fraction) *
                        ((float)MT6701_OFFSET_CPR /
                         (float)ENCODER_CORRECTION_POINTS);
        correctionFloat = expectedPhase - (float)rawPhase;
        correction = (correctionFloat >= 0.0f) ?
                     (int32_t)(correctionFloat + 0.5f) :
                     (int32_t)(correctionFloat - 0.5f);

        if ((correction > FOC_ENCODER_CALIBRATION_MAX_ERROR_COUNTS) ||
            (correction < -FOC_ENCODER_CALIBRATION_MAX_ERROR_COUNTS))
        {
            encoderCorrectionDisable();
            return 0U;
        }
        encoderCalibrationTable[tableIndex] = (int16_t)correction;
    }

    Positioning_SetAngle(FOC, 0.0f, calibrationUd, 0.0f);
    User_Delay(100U);
    return encoderCorrectionConfigure(Encoder_WrapRaw(origin),
                                      (int8_t)direction,
                                      encoderCalibrationTable,
                                      ENCODER_CORRECTION_POINTS);
#else
    (void)FOC;
    encoderCorrectionDisable();
    return 1U;
#endif
}

static uint16_t Encoder_ReadCorrectedRaw(MOTOR_ENCODER_STRUCT *encoder)
{
    encoder->rawAngleOriginal = User_Encoder_ReadRaw();
    return encoderCorrectionApply(encoder->rawAngleOriginal,
                                  &encoder->correctionCount);
}

// 读取编码器偏置。以第一笔校正后计数为参考展开平均，可正确处理0/16383跨界。
static uint8_t Offset_EncoderRead(MOTOR_ENCODER_STRUCT *encoder)
{
    float averageCount = 0.0f;
    uint8_t calibrated = 0U;

    for (uint8_t attempt = 0U; attempt < ENCODER_OFFSET_MAX_ATTEMPTS; attempt++)
    {
        int64_t deltaSum = 0;
        int32_t deltaMin = MT6701_OFFSET_HALF_CPR;
        int32_t deltaMax = -MT6701_OFFSET_HALF_CPR;
        uint16_t validSamples = 0U;
        uint16_t readAttempts = 0U;
        uint16_t referenceRaw = Encoder_ReadCorrectedRaw(encoder);

        if (encoder->errorFlag != 0U)
        {
            User_Delay(10U);
            continue;
        }

        while ((validSamples < ENCODER_OFFSET_SAMPLES) &&
               (readAttempts < (ENCODER_OFFSET_SAMPLES * 2U)))
        {
            int32_t delta;
            uint16_t raw;

            User_Delay(ENCODER_OFFSET_SAMPLE_DELAY_MS);
            raw = Encoder_ReadCorrectedRaw(encoder);
            readAttempts++;

            if (encoder->errorFlag != 0U)
            {
                continue;
            }

            delta = (int32_t)raw - (int32_t)referenceRaw;
            if (delta > MT6701_OFFSET_HALF_CPR)
            {
                delta -= MT6701_OFFSET_CPR;
            }
            else if (delta < -MT6701_OFFSET_HALF_CPR)
            {
                delta += MT6701_OFFSET_CPR;
            }

            deltaSum += delta;
            if (delta < deltaMin)
            {
                deltaMin = delta;
            }
            if (delta > deltaMax)
            {
                deltaMax = delta;
            }
            validSamples++;
        }

        if (validSamples == ENCODER_OFFSET_SAMPLES)
        {
            averageCount = (float)referenceRaw + (float)deltaSum / (float)validSamples;
            while (averageCount >= (float)MT6701_OFFSET_CPR)
            {
                averageCount -= (float)MT6701_OFFSET_CPR;
            }
            while (averageCount < 0.0f)
            {
                averageCount += (float)MT6701_OFFSET_CPR;
            }

            if ((deltaMax - deltaMin) <= ENCODER_OFFSET_MAX_SPAN)
            {
                calibrated = 1U;
                break;
            }
        }

        User_Delay(100U);
    }

    if (calibrated == 0U)
    {
        encoder->errorFlag = 1U;
        return 0U;
    }

    encoder->Pos_offset = averageCount * (Value_2PI / (float)MT6701_OFFSET_CPR);

    // 校准结束后清空位置差分和PLL动态状态，下一次20kHz采样重新初始化。
    encoder->sampleReady = 0U;
    encoder->fullRotationsCur = 0;
    encoder->rawPosition = 0;
    encoder->rawAngleDelta = 0;
    encoder->vel = 0.0f;
    encoder->windowVel = 0.0f;
    encoder->lowSpeedVel = 0.0f;
    encoder->speedBlend = 0.0f;
    encoder->filterVel = 0.0f;
    encoderObserverReset();
    encoder->errorFlag = 0U;
    return 1U;
}
// Offset读取电流偏置
static void Offset_CurrentRead(FOC_System_STRUCT *FOC)
{
    // 这里的电流偏置读取，已经放在了ADC转换完成的回调函数里，并且有了平均处理
    //这里就只计算电流放大的最终增益
    FOC->current.Final_Gain = FOC->motor.MCU_Voltage / (FOC->motor.ADC_Precision * FOC->motor.Amplifier * FOC->motor.Sampling_Rs);
}
// Current读取当前的电流值并更新3相电流
void Current_ReadIabc(FOC_System_STRUCT *FOC)
{
    float ia;
    float ib;
    float ic;
    float temp;

    /* ADC注入顺序是 C(JDR1)、B(JDR2)、A(JDR3)，偏置必须一一对应。 */
    ic = ((int16_t)FOC->current.ADC_InjectedValues[0] - FOC->current.Current_offset0)
       * FOC->current.Final_Gain * FOC->motor.Current_Dir2;
    ib = ((int16_t)FOC->current.ADC_InjectedValues[1] - FOC->current.Current_offset1)
       * FOC->current.Final_Gain * FOC->motor.Current_Dir1;
    ia = ((int16_t)FOC->current.ADC_InjectedValues[2] - FOC->current.Current_offset2)
       * FOC->current.Final_Gain * FOC->motor.Current_Dir0;

#if FOC_CURRENT_RECONSTRUCTION_ENABLE
    /* 满调制时丢弃最先退出低侧导通区的一相，由另外两相重构。 */
    if ((FOC->foc.Duty_u <= FOC->foc.Duty_v) &&
        (FOC->foc.Duty_u <= FOC->foc.Duty_w))
    {
        ia = -(ib + ic);
        FOC->current.reconstructedPhase = 0U;
    }
    else if ((FOC->foc.Duty_v <= FOC->foc.Duty_u) &&
             (FOC->foc.Duty_v <= FOC->foc.Duty_w))
    {
        ib = -(ia + ic);
        FOC->current.reconstructedPhase = 1U;
    }
    else
    {
        ic = -(ia + ib);
        FOC->current.reconstructedPhase = 2U;
    }
#else
    /* 对比模式：三相全部保留ADC实测值，不执行相电流重构。 */
    FOC->current.reconstructedPhase = 0xFFU;
#endif

    /* 保留原工程Motor_Dir=-1时A/B逻辑交换的行为。 */
    if (FOC->motor.Motor_Dir == -1)
    {
        temp = ia;
        ia = ib;
        ib = temp;
    }

    FOC->current.Real_Ia = ia;
    FOC->current.Real_Ib = ib;
    FOC->current.Real_Ic = ic;
}
// Calculate有传感器角度和电流
static void Calculate_Loop(FOC_System_STRUCT *FOC)
{
    encoderUpdate();                                                                                                                              //角度和速度更新                                                                                                                       //更新编码器数据
    Current_ReadIabc(FOC);                                                                                                                       //更新电流数据
    FOC->motor.Electrical_Angle = Value_normalize(FOC->motor.Poles * FOC->encoder.dir * (FOC->encoder.angleWithoutTrackCur - FOC->encoder.Pos_offset));        //读取电角度，先在机械空间内消除偏置（找准机械相对位置），再整体映射到电气空间
    // 前馈与速度环统一使用稳定PLL速度；补偿后的快速速度先保留用于波形验证。
    FOC->motor.Electrical_Speed = FOC->encoder.pllVel * FOC->motor.Poles;                                                             //计算电角速度
    fast_sin_cos(FOC->motor.Electrical_Angle, &FOC->foc.sine, &FOC->foc.cosine);                                                               //计算电角度的正弦和余弦值
    clarke(&FOC->current.Real_Ialpha, &FOC->current.Real_Ibeta, FOC->current.Real_Ia, FOC->current.Real_Ib);                                  //clarke变换计算出Ialpha和Ibeta
    park(&FOC->current.Real_Id, &FOC->current.Real_Iq, FOC->current.Real_Ialpha, FOC->current.Real_Ibeta, FOC->foc.sine, FOC->foc.cosine);  //park变换计算出Id和Iq
}
// GeneratePWM_Loop计算PID并执行电机控制
static void GeneratePWM_Loop(FOC_System_STRUCT *FOC)
{
    SVPWM_Tick(FOC, FOC->foc.sine, FOC->foc.cosine, FOC->foc.Ud_in, FOC->foc.Uq_in);
}

//FOC初始化
void FOC_Init(void)
{
	//状态：初始化中
    FOC.status = MOTOR_STATUS_INITIALIZING;
	// 用户自定义的电机参数和控制系统参数
	User_MotorSet();
	Cogging_Init(&FOC.cogging);
	User_InitialInit();
	//各种控制系统的初始化
	Printf_Init(&FOC.TXdata);
    //状态：校准中
    FOC.status = MOTOR_STATUS_CALIBRATING;
	//读取电流偏置
	Offset_CurrentRead(&FOC);
	while (FOC.current.adcOffsetReady == 0);  // 等待ADC偏置读取完成

	// 电流零偏完成后才打开功率PWM，避免开关噪声污染零偏。
	User_Enable_Motor();

	// 缓慢增加D轴对齐电压，减少转子过冲和落入不稳定位置的概率。
	for (uint16_t step = 1U; step <= ENCODER_ALIGN_RAMP_STEPS; step++)
	{
		float alignUd = 0.3f * FOC.motor.VBUS * (float)step / (float)ENCODER_ALIGN_RAMP_STEPS;
		Positioning_Set(&FOC, alignUd, 0.0f);
		User_Delay(ENCODER_ALIGN_RAMP_DELAY_MS);
	}
	User_Delay(ENCODER_ALIGN_HOLD_MS);

	/* 正反各扫一圈，建立MT6701一圈非线性校正表。 */
	if (EncoderCorrection_Calibrate(&FOC) == 0U)
	{
		Positioning_Set(&FOC, 0.0f, 0.0f);
		FOC.encoder.errorFlag = 1U;
		FOC.status = MOTOR_STATUS_ENCODER_ERROR;
		return;
	}

	//读取编码器偏置
	if (Offset_EncoderRead(&FOC.encoder) == 0U)
	{
		Positioning_Set(&FOC, 0.0f, 0.0f);
		FOC.status = MOTOR_STATUS_ENCODER_ERROR;
		return;
	}
	// 电机失能并进入正常工作状态
	Positioning_Set(&FOC, 0.0f, 0.0f);
	User_Delay(100U);
	//状态：空闲
    FOC.status = MOTOR_STATUS_IDLE;

}
static void CurrentLoop_20kHz(FOC_System_STRUCT *FOC)
{
    PID_STRUCT *iqPid;
    PID_STRUCT *idPid;
    float iqTarget;
    float idTarget;
    float voltageLimit;
#if FOC_CURRENT_FEEDFORWARD_ENABLE
    float Ud_ff, Uq_ff;
#endif

    switch (FOC->mode)
    {
        case Current_SINGLE_MODE:
            iqPid = &FOC->pid_Current.iq;
            idPid = &FOC->pid_Current.id;
            iqTarget = FOC->foc.Target_Iq;
            idTarget = FOC->foc.Target_Id;
            break;
		case Speed_Current_MODE:  // 级联: 速度环输出 → 电流环输入
            iqPid = &FOC->pid_VelCur.iq;
            idPid = &FOC->pid_VelCur.id;
            iqTarget = Value_Limit(FOC->pid_VelCur.spd.run.Output +
                                   FOC->cogging.iqCompensation +
                                   FOC->flag.speedFrictionIq,
                                   FOC->pid_VelCur.spd.OutMax,
                                   FOC->pid_VelCur.spd.OutMin);
            idTarget = FOC->foc.Target_Id;
			break;
		case Position_Speed_Current_MODE:
			iqPid = &FOC->pid_PosVelCur.iq;
            idPid = &FOC->pid_PosVelCur.id;
            iqTarget = Value_Limit(FOC->pid_PosVelCur.spd.run.Output +
                                   FOC->cogging.iqCompensation,
                                   FOC->pid_PosVelCur.spd.OutMax,
                                   FOC->pid_PosVelCur.spd.OutMin);
            idTarget = FOC->foc.Target_Id;
            break;
        default:
            return;
    }

    // PI 单轴限幅随母线电压更新；最终的圆形矢量限幅在 SVPWM_Tick 中完成。
    voltageLimit = VoltageVector_GetLimit(FOC);
    idPid->OutMax = voltageLimit;
    idPid->OutMin = -voltageLimit;
    iqPid->OutMax = voltageLimit;
    iqPid->OutMin = -voltageLimit;

    // Q轴电流闭环
    Transfer_PID_Loop(iqPid, iqTarget, FOC->current.Real_Iq);

    // D轴电流闭环
    Transfer_PID_Loop(idPid, idTarget, FOC->current.Real_Id);

    // 电流前馈解耦 — 抵消反电动势与dq轴交叉耦合
#if FOC_CURRENT_FEEDFORWARD_ENABLE
    Ud_ff = -FOC->motor.Electrical_Speed * FOC->motor.Lq * FOC->current.Real_Iq;
    Uq_ff = FOC->motor.Electrical_Speed * (FOC->motor.Ld * FOC->current.Real_Id + FOC->motor.Flux);

    FOC->foc.Ud_in = idPid->run.Output + Ud_ff;
    FOC->foc.Uq_in = iqPid->run.Output + Uq_ff;
#else
    FOC->foc.Ud_in = idPid->run.Output;
    FOC->foc.Uq_in = iqPid->run.Output;
#endif
}
// 1kHz 速度环 — Target 来源取决于模式
static void SpeedLoop_1kHz(FOC_System_STRUCT *FOC)
{
    PID_STRUCT *spdPid;
    float spdTarget;
    float savedOutMax;
    float savedOutMin;
    float stallOutputThreshold;
    uint8_t allowCoggingLearning;
    float mechanicalAngle;
    float savedKp;
    float savedINum;
    float assistGain;

    switch (FOC->mode)
    {
        case Speed_Current_MODE:  // 用户直接指定速度
            spdPid = &FOC->pid_VelCur.spd;
            spdTarget = FOC->foc.Target_Speed;
            break;
		case Position_Speed_Current_MODE:
			spdPid = &FOC->pid_PosVelCur.spd;
			spdTarget = FOC->pid_PosVelCur.pos.run.Output;
			break;
        default:
            FOC->flag.lowSpeedAssistGain = 0.0f;
            FOC->flag.speedFrictionIq = 0.0f;
            return;
    }

    if ((speedLoopInitialized == 0U) ||
        (speedLoopActiveMode != FOC->mode))
    {
        /* 进入速度模式或在模式3/4之间切换时，按当前速度无扰重建速度环。 */
        PID_Clear(spdPid);
        FOC->flag.speedProfileRef = FOC->encoder.filterVel;
        FOC->flag.speedStallActive = 0U;
        FOC->flag.speedRecoveryActive = 0U;
        FOC->flag.speedStallCounter = 0U;
        speedLoopActiveMode = FOC->mode;
        speedLoopInitialized = 1U;
    }

    if (FOC->flag.speedStallActive != 0U)
    {
        if (Value_fabsf(spdTarget) < FOC_SPEED_STALL_TARGET_MIN_RAD_S)
        {
            /* 用户撤销速度指令时立即退出堵转保持。 */
            FOC->flag.speedStallActive = 0U;
            FOC->flag.speedRecoveryActive = 0U;
            FOC->flag.speedStallCounter = 0U;
            FOC->flag.speedProfileRef = FOC->encoder.filterVel;
            PID_Clear(spdPid);
        }
        else if (Value_fabsf(FOC->encoder.windowVel) >= FOC_SPEED_RELEASE_RAD_S)
        {
            /* 松手瞬间从当前速度重新起坡，清除堵转期间的积分和最大转矩。 */
            FOC->flag.speedStallActive = 0U;
            FOC->flag.speedRecoveryActive = 1U;
            FOC->flag.speedStallCounter = 0U;
            FOC->flag.speedProfileRef = FOC->encoder.filterVel;
            PID_Clear(spdPid);
        }
        else
        {
            /* 尚未释放：冻结积分并保持目标方向的最大允许转矩。 */
            FOC->flag.speedProfileRef = FOC->encoder.filterVel;
            SpeedLoop_HoldMaximumTorque(spdPid,
                                        spdTarget,
                                        FOC->encoder.filterVel);
            FOC->cogging.iqCompensation = 0.0f;
            FOC->cogging.learningActive = 0U;
            FOC->flag.speedFrictionIq = 0.0f;
            return;
        }
    }

    FOC->flag.speedProfileRef = SpeedProfile_Next(FOC->flag.speedProfileRef,
                                                   spdTarget);

    savedOutMax = spdPid->OutMax;
    savedOutMin = spdPid->OutMin;
    if (FOC->flag.speedRecoveryActive != 0U)
    {
        /* 恢复阶段只限制与目标方向相反的再生制动力矩。 */
        if (spdTarget >= 0.0f)
        {
            spdPid->OutMin = -FOC_SPEED_RECOVERY_BRAKE_IQ_A;
        }
        else
        {
            spdPid->OutMax = FOC_SPEED_RECOVERY_BRAKE_IQ_A;
        }
    }

    savedKp = spdPid->Kp;
    savedINum = spdPid->run.I_num;
    assistGain = 0.0f;
    FOC->flag.speedFrictionIq = 0.0f;

    if ((FOC->flag.lowSpeedAssistEnabled != 0U) &&
        (FOC->mode == Speed_Current_MODE))
    {
        assistGain = SpeedLoop_LowSpeedAssistGain(FOC->flag.speedProfileRef);
        spdPid->Kp = savedKp *
                     (1.0f + assistGain * (FOC_LOW_SPEED_KP_SCALE - 1.0f));
        spdPid->run.I_num = savedINum *
                            (1.0f + assistGain *
                             (FOC_LOW_SPEED_KI_SCALE - 1.0f));

        if (FOC->flag.speedProfileRef >=
            FOC_LOW_SPEED_FRICTION_MIN_REF_RAD_S)
        {
            FOC->flag.speedFrictionIq =
                FOC_LOW_SPEED_FRICTION_IQ_A * assistGain;
        }
        else if (FOC->flag.speedProfileRef <=
                 -FOC_LOW_SPEED_FRICTION_MIN_REF_RAD_S)
        {
            FOC->flag.speedFrictionIq =
                -FOC_LOW_SPEED_FRICTION_IQ_A * assistGain;
        }
    }
    FOC->flag.lowSpeedAssistGain = assistGain;

    Transfer_PID_Loop(spdPid,
                      FOC->flag.speedProfileRef,
                      FOC->encoder.filterVel);
    spdPid->Kp = savedKp;
    spdPid->run.I_num = savedINum;
    spdPid->OutMax = savedOutMax;
    spdPid->OutMin = savedOutMin;

    /*
     * 只在模式3且T型给定已经到达用户目标时学习，避免把加减速惯量、
     * 位置制动和堵转释放过程误学成齿槽转矩。位置模式复用已经学到的表。
     */
    allowCoggingLearning =
        ((FOC->mode == Speed_Current_MODE) &&
         (FOC->flag.speedStallActive == 0U) &&
         (FOC->flag.speedRecoveryActive == 0U) &&
         (Value_fabsf(FOC->flag.speedProfileRef - spdTarget) <=
          FOC_COGGING_STEADY_REFERENCE_RAD_S)) ? 1U : 0U;
    mechanicalAngle = FOC->encoder.dir *
                      (FOC->encoder.angleWithoutTrackCur -
                       FOC->encoder.Pos_offset);
    Cogging_Update(&FOC->cogging,
                   mechanicalAngle,
                   FOC->flag.speedProfileRef,
                   FOC->encoder.filterVel,
                   allowCoggingLearning);

    if ((FOC->flag.speedRecoveryActive != 0U) &&
        (Value_fabsf(FOC->flag.speedProfileRef - spdTarget) <=
         FOC_SPEED_RECOVERY_DONE_ERROR_RAD_S) &&
        (Value_fabsf(FOC->encoder.filterVel - spdTarget) <=
         FOC_SPEED_RECOVERY_DONE_ERROR_RAD_S))
    {
        FOC->flag.speedRecoveryActive = 0U;
    }

    /* 仅在低速且速度环已接近饱和并持续100 ms时确认堵转。 */
    stallOutputThreshold = Value_fabsf(spdPid->OutMax) *
                           FOC_SPEED_STALL_OUTPUT_RATIO;
    if ((Value_fabsf(spdTarget) >= FOC_SPEED_STALL_TARGET_MIN_RAD_S) &&
        (Value_fabsf(FOC->encoder.windowVel) <= FOC_SPEED_STALL_MAX_RAD_S) &&
        (Value_fabsf(spdPid->run.Output) >= stallOutputThreshold))
    {
        if (FOC->flag.speedStallCounter < FOC_SPEED_STALL_DETECT_TICKS)
        {
            FOC->flag.speedStallCounter++;
        }

        if (FOC->flag.speedStallCounter >= FOC_SPEED_STALL_DETECT_TICKS)
        {
            FOC->flag.speedStallActive = 1U;
            FOC->flag.speedRecoveryActive = 0U;
            SpeedLoop_HoldMaximumTorque(spdPid,
                                        spdTarget,
                                        FOC->encoder.filterVel);
            FOC->cogging.iqCompensation = 0.0f;
            FOC->cogging.learningActive = 0U;
            FOC->flag.speedFrictionIq = 0.0f;
        }
    }
    else
    {
        FOC->flag.speedStallCounter = 0U;
    }
    // spdPid->run.Output 由 CurrentLoop_20kHz 读取，不写入Target_Iq
}
// 1kHz 位置环 — 仅 PosVelCur 模式
static void PositionLoop_1kHz(FOC_System_STRUCT *FOC)
{
    float speedLimit;
    float positionFeedback;
    float positionError;
    float brakeSpeedLimit;

    if (FOC->mode != Position_Speed_Current_MODE)
        return;

    PID_STRUCT *posPid = &FOC->pid_PosVelCur.pos;
    speedLimit = Value_fabsf(FOC->foc.Target_Speed);
    positionFeedback = FOC->encoder.dir * FOC->encoder.angleCur;
    positionError = FOC->foc.Target_Pos - positionFeedback;

    posPid->OutMax = speedLimit;  // Target_Speed在位置模式下表示最大机械速度
    posPid->OutMin = -speedLimit;
    Transfer_PID_Loop(posPid,
                      FOC->foc.Target_Pos,
                      positionFeedback);

    if ((Value_fabsf(positionError) <= FOC_POSITION_SETTLE_ERROR_RAD) &&
        (Value_fabsf(FOC->encoder.filterVel) <= FOC_POSITION_SETTLE_SPEED_RAD_S))
    {
        /* 位置和速度同时到位后才给零速，防止高速穿越目标点时误判到位。 */
        posPid->run.Output = 0.0f;
    }
    else
    {
        /* v=sqrt(2*a*s)：按剩余位置提前限制速度，避免到点后才反向制动。 */
        brakeSpeedLimit = Value_sqrtf(2.0f *
                                      FOC_POSITION_BRAKE_DECEL_RAD_S2 *
                                      Value_fabsf(positionError));
        if (brakeSpeedLimit < speedLimit)
        {
            posPid->run.Output = Value_Limit(posPid->run.Output,
                                             brakeSpeedLimit,
                                            -brakeSpeedLimit);
        }
    }
    // posPid->run.Output 由SpeedLoop_1kHz读取，不写入Target_Speed
}
// 20kHz — 电流环
void High_Loop(void)
{
    if (FOC.status >= MOTOR_STATUS_IDLE && FOC.status < MOTOR_STATUS_OVERVOLTAGE)
    {
        Calculate_Loop(&FOC);
		switch (FOC.mode)
        {
            case Voltage_FORCE_MODE:
				Volatge_Force();
				//FOC.foc.Target_VF_Speed, debug给定
			    FOC.foc.Uq_in = FOC.foc.Target_Uq;
				FOC.foc.Ud_in = 0;
                break;
			case Voltage_OPEN_MODE:
			    FOC.foc.Uq_in = FOC.foc.Target_Uq;
				FOC.foc.Ud_in = 0;
                break;
			 case Current_SINGLE_MODE:
				 CurrentLoop_20kHz(&FOC);
				break;
			 case Speed_Current_MODE:
				 CurrentLoop_20kHz(&FOC);
				break;
		     case Position_Speed_Current_MODE:
				CurrentLoop_20kHz(&FOC);
				break;
            default:
                FOC.foc.Uq_in = 0;
                FOC.foc.Ud_in = 0;
                break;
        }
		
        GeneratePWM_Loop(&FOC);
    }

    FOC.flag.PWM_Calc = 0;
}

// Status切换到STANDBY状态，并重置计数器
static void Status_Switch_STANDBY(FOC_System_STRUCT *FOC, uint32_t *count)
{
    FOC->status = MOTOR_STATUS_STANDBY;
    *count = 0;
    // 已失能状态持续“自定义”次控制周期后，自动切换退出已失能状态
}

// Status切换到IDLE状态，并重置计数器
static void Status_Switch_IDLE(FOC_System_STRUCT *FOC, uint32_t *count)
{
    FOC->status = MOTOR_STATUS_IDLE;
    *count = 0;
}

// 周期检查过压是否已经恢复
static void Status_VBUS_OVERVOLTAGE(FOC_System_STRUCT *FOC, uint32_t *count)
{
    if (FOC->foc.Real_VBUS <= FOC->safe.VBUS_MAX)
    {
        FOC->safe.errorFlag &= (uint8_t)(~MOTOR_ERROR_OVERVOLTAGE);
        Status_Switch_STANDBY(FOC, count);
    }
}

// 周期检查欠压是否已经恢复
static void Status_VBUS_UNDERVOLTAGE(FOC_System_STRUCT *FOC, uint32_t *count)
{
    if (FOC->foc.Real_VBUS >= FOC->safe.VBUS_MIM)
    {
        FOC->safe.errorFlag &= (uint8_t)(~MOTOR_ERROR_UNDERVOLTAGE);
        Status_Switch_STANDBY(FOC, count);
    }
}

// 周期检查过温是否已经恢复
static void Status_Temp_OVERTEMPERATURE(FOC_System_STRUCT *FOC, uint32_t *count)
{
    if (FOC->foc.Real_MCUTemp <= FOC->safe.Temp_MAX)
    {
        FOC->safe.errorFlag &= (uint8_t)(~MOTOR_ERROR_OVERTEMPERATURE);
        Status_Switch_STANDBY(FOC, count);
    }
}

// 周期检查低温是否已经恢复
static void Status_Temp_UNDERTEMPERATURE(FOC_System_STRUCT *FOC, uint32_t *count)
{
    if (FOC->foc.Real_MCUTemp >= FOC->safe.Temp_MIN)
    {
        FOC->safe.errorFlag &= (uint8_t)(~MOTOR_ERROR_UNDERTEMPERATURE);
        Status_Switch_STANDBY(FOC, count);
    }
}

// 周期检查过流是否已经恢复
static void Status_Current_OVERCURRENT(FOC_System_STRUCT *FOC, uint32_t *count)
{
    if ((Value_fabsf(FOC->current.Real_Id) <= FOC->safe.Dcur_MAX) &&
        (Value_fabsf(FOC->current.Real_Iq) <= FOC->safe.Qcur_MAX))
    {
        FOC->safe.errorFlag &= (uint8_t)(~MOTOR_ERROR_OVERCURRENT);
        Status_Switch_STANDBY(FOC, count);
    }
}


// 根据当前控制模式更新运行状态。
// error > 0：实际值需要向数值增大的方向追赶目标；error < 0 则相反。
static void Status_OperatingState_Update(FOC_System_STRUCT *FOC)
{
    float error;
    float tolerance;

    switch (FOC->mode)
    {
        case Current_SINGLE_MODE:
            error = FOC->foc.Target_Iq - FOC->current.Real_Iq;
            tolerance = Value_fabsf(FOC->safe.Current_limit);

            if (Value_fabsf(error) <= tolerance)
            {
                FOC->status = MOTOR_STATUS_TORQUE_CONTROL;
            }
            else if (error > 0.0f)
            {
                FOC->status = MOTOR_STATUS_TORQUE_INCREASING;
            }
            else
            {
                FOC->status = MOTOR_STATUS_TORQUE_DECREASING;
            }
            break;

        case Speed_Current_MODE:
            error = FOC->foc.Target_Speed - FOC->encoder.filterVel;
            tolerance = Value_fabsf(FOC->safe.Speed_limit);

            if (Value_fabsf(error) <= tolerance)
            {
                FOC->status = MOTOR_STATUS_CONST_SPEED;
            }
            else if (error > 0.0f)
            {
                FOC->status = MOTOR_STATUS_ACCELERATING;
            }
            else
            {
                FOC->status = MOTOR_STATUS_DECELERATING;
            }
            break;

        case Position_Speed_Current_MODE:
            error = FOC->foc.Target_Pos - FOC->encoder.angleCur;
            tolerance = Value_fabsf(FOC->safe.Position_limit);

            if (Value_fabsf(error) <= tolerance)
            {
                FOC->status = MOTOR_STATUS_POSITION_HOLD;
            }
            else if (error > 0.0f)
            {
                FOC->status = MOTOR_STATUS_POSITION_INCREASING;
            }
            else
            {
                FOC->status = MOTOR_STATUS_POSITION_DECREASING;
            }
            break;

        default:
            break;
    }
}

static void Status_RecordFault(FOC_System_STRUCT *FOC, uint8_t faultStatus)
{
    FOC->safe.lastFaultStatus = faultStatus;
    FOC->safe.faultVBUS = FOC->foc.Real_VBUS;
    FOC->safe.faultId = FOC->current.Real_Id;
    FOC->safe.faultIq = FOC->current.Real_Iq;
    FOC->safe.faultSpeed = FOC->encoder.filterVel;
}

// Status判断并切换状态机
static void Status_Switch_Loop(FOC_System_STRUCT *FOC)
{
    // ====== 安全状态(状态) ======
    if ((FOC->status != MOTOR_STATUS_EMERGENCY_STOP) && MOTOR_STATUS_EMERGENCY_STOP_Signal())  //急停信号
    {
        FOC->status = MOTOR_STATUS_EMERGENCY_STOP;
    }
    if ((FOC->status != MOTOR_STATUS_DISABLED) && MOTOR_STATUS_DISABLED_Signal())  //失能信号
    {
        FOC->status = MOTOR_STATUS_DISABLED;
    }

    // ====== 初始化与运行状态(状态) ======
    if ((FOC->status != MOTOR_STATUS_STANDBY) && MOTOR_STATUS_STANDBY_Signal())  //待机信号
    {
        FOC->status = MOTOR_STATUS_STANDBY;
    }
    if ((FOC->status == MOTOR_STATUS_EMERGENCY_STOP) || (FOC->status == MOTOR_STATUS_DISABLED) || (FOC->status < 4))  //急停和失能 // 紧急停止和失能状态优先级最高, 直接返回不执行后续状态判断
    {
        return;
    }
    if ((FOC->status != MOTOR_STATUS_UNINITIALIZED) && MOTOR_STATUS_UNINITIALIZED_Signal())  //未初始化信号
    {                                                                                         // [重要]接收到开始信号后，切换到未初始化状态...准备初始化
        FOC->status = MOTOR_STATUS_UNINITIALIZED;
    }

    // ====== 硬件相关错误(状态) ======
    if (FOC->foc.Real_VBUS != -9999.0f)  //母线电压采集正常
    {
        if ((FOC->status != MOTOR_STATUS_OVERVOLTAGE) && FOC->foc.Real_VBUS > FOC->safe.VBUS_MAX)  //过压
        {
            Status_RecordFault(FOC, MOTOR_STATUS_OVERVOLTAGE);
            FOC->status = MOTOR_STATUS_OVERVOLTAGE;
        }
        else if ((FOC->status != MOTOR_STATUS_UNDERVOLTAGE) && FOC->foc.Real_VBUS < FOC->safe.VBUS_MIM)  //欠压
        {
            Status_RecordFault(FOC, MOTOR_STATUS_UNDERVOLTAGE);
            FOC->status = MOTOR_STATUS_UNDERVOLTAGE;
        }
    }
    if (FOC->foc.Real_MCUTemp != -9999.0f)  //温度采集
    {
        if ((FOC->status != MOTOR_STATUS_OVERTEMPERATURE) && FOC->foc.Real_MCUTemp > FOC->safe.Temp_MAX)  //过温
        {
            Status_RecordFault(FOC, MOTOR_STATUS_OVERTEMPERATURE);
            FOC->status = MOTOR_STATUS_OVERTEMPERATURE;
        }
        else if ((FOC->status != MOTOR_STATUS_UNDERTEMPERATURE) && FOC->foc.Real_MCUTemp < FOC->safe.Temp_MIN)  //低温
        {
            Status_RecordFault(FOC, MOTOR_STATUS_UNDERTEMPERATURE);
            FOC->status = MOTOR_STATUS_UNDERTEMPERATURE;
        }
    }
    if ((FOC->status != MOTOR_STATUS_OVERCURRENT) &&
        ((Value_fabsf(FOC->current.Real_Id) > FOC->safe.Dcur_MAX) ||
         (Value_fabsf(FOC->current.Real_Iq) > FOC->safe.Qcur_MAX)))  //正反向过流
    {
        Status_RecordFault(FOC, MOTOR_STATUS_OVERCURRENT);
        FOC->status = MOTOR_STATUS_OVERCURRENT;
    }

    if ((FOC->status != MOTOR_STATUS_ENCODER_ERROR) && MOTOR_STATUS_ENCODER_ERROR_Signal())  //编码器错误信号
    {
        Status_RecordFault(FOC, MOTOR_STATUS_ENCODER_ERROR);
        FOC->status = MOTOR_STATUS_ENCODER_ERROR;
    }

    if ((FOC->status != MOTOR_STATUS_SENSOR_ERROR) && MOTOR_STATUS_SENSOR_ERROR_Signal())  //传感器错误触发
    {
        Status_RecordFault(FOC, MOTOR_STATUS_SENSOR_ERROR);
        FOC->status = MOTOR_STATUS_SENSOR_ERROR;
    }

    if (FOC->status >= 14 && FOC->status < 21)  // 硬件相关错误状态优先级高于运行状态, 直接返回不执行后续状态判断
    {
        return;
    }
    Status_OperatingState_Update(FOC);
}

// 看门狗到期判断；limit为0时禁用周期检查并避免除零。
static uint8_t Status_Watchdog_IsDue(uint32_t count, uint32_t limit)
{
    if (limit == 0U)
    {
        return 0U;
    }

    return ((count % limit) == 0U) ? 1U : 0U;
}

// Status定时器中断调用的状态机运行函数
static void Status_RUN_Loop(FOC_System_STRUCT *FOC)
{
    FOC->safe.count++;

    // 急停必须立即关闭PWM，不等待任何看门狗周期。
    if (FOC->status == MOTOR_STATUS_EMERGENCY_STOP)
    {
        User_Disable_Motor();
    }

    // 1.状态机失能DISABLED安全状态处理
    if (FOC->status == MOTOR_STATUS_DISABLED)
    {
        User_Disable_Motor();
        FOC->safe.errorFlag |= MOTOR_ERROR_DISABLED;

        if ((FOC->safe.DISABLED_watchdog_limit != 0U) &&
            (FOC->safe.count > FOC->safe.DISABLED_watchdog_limit))
        {
            Status_Switch_STANDBY(FOC, &FOC->safe.count);
        }
    }

    //过压
    if (FOC->status == MOTOR_STATUS_OVERVOLTAGE)
    {
        User_Disable_Motor();
        FOC->safe.errorFlag |= MOTOR_ERROR_OVERVOLTAGE;

        if (Status_Watchdog_IsDue(FOC->safe.count, FOC->safe.VBUS_watchdog_limit))
        {
            Status_VBUS_OVERVOLTAGE(FOC, &FOC->safe.count);
        }
    }
    //欠压
    if (FOC->status == MOTOR_STATUS_UNDERVOLTAGE)
    {
        User_Disable_Motor();
        FOC->safe.errorFlag |= MOTOR_ERROR_UNDERVOLTAGE;

        if (Status_Watchdog_IsDue(FOC->safe.count, FOC->safe.VBUS_watchdog_limit))
        {
            Status_VBUS_UNDERVOLTAGE(FOC, &FOC->safe.count);
        }
    }
    //过温
    if (FOC->status == MOTOR_STATUS_OVERTEMPERATURE)
    {
        // 保护动作和故障上报立即执行
        User_Disable_Motor();
        FOC->safe.errorFlag |= MOTOR_ERROR_OVERTEMPERATURE;

        if (Status_Watchdog_IsDue(FOC->safe.count, FOC->safe.Temp_watchdog_limit))//看门狗只负责低频检查温度是否恢复，如果恢复，则进入待机
        {
            Status_Temp_OVERTEMPERATURE(FOC, &FOC->safe.count);
        }
    }
    //低温
    if (FOC->status == MOTOR_STATUS_UNDERTEMPERATURE)
    {
        User_Disable_Motor();
        FOC->safe.errorFlag |= MOTOR_ERROR_UNDERTEMPERATURE;

        if (Status_Watchdog_IsDue(FOC->safe.count, FOC->safe.Temp_watchdog_limit))
        {
            Status_Temp_UNDERTEMPERATURE(FOC, &FOC->safe.count);
        }
    }

    //DQ轴电流过流
    if (FOC->status == MOTOR_STATUS_OVERCURRENT)
    {
        User_Disable_Motor();
        FOC->safe.errorFlag |= MOTOR_ERROR_OVERCURRENT;

        if (Status_Watchdog_IsDue(FOC->safe.count, FOC->safe.DQcur_watchdog_limit))
        {
            Status_Current_OVERCURRENT(FOC, &FOC->safe.count);
        }
    }

    //编码器错误
    if (FOC->status == MOTOR_STATUS_ENCODER_ERROR)
    {
        User_Disable_Motor();
        FOC->safe.errorFlag |= MOTOR_ERROR_ENCODER;

        if (Status_Watchdog_IsDue(FOC->safe.count, FOC->safe.Encoder_watchdog_limit))
        {
            if (MOTOR_STATUS_ENCODER_ERROR_Signal() == 0U)
            {
                FOC->safe.errorFlag &= (uint8_t)(~MOTOR_ERROR_ENCODER);
                Status_Switch_STANDBY(FOC, &FOC->safe.count);
            }
        }
    }

    // 传感器/PWM计算故障同样属于立即失能类故障。
    if ((FOC->status == MOTOR_STATUS_SENSOR_ERROR) ||
        (FOC->status == MOTOR_STATUS_PWM_CALC_FAULT))
    {
        User_Disable_Motor();
    }


    //触发欠压和过压状态之后进入standby，检查电压是否恢复正常，如果恢复正常则切换回空闲状态
    //触发过温和低温状态之后进入standby，检查温度是否恢复正常，如果恢复正常则切换回空闲状态
    //触发过流状态之后进入standby，检查电流是否恢复正常，如果恢复正常则切换回空闲状态
    //触发编码器错误状态之后进入standby，检查编码器是否恢复正常，如果恢复正常则切换回空闲状态
    if (FOC->status == MOTOR_STATUS_STANDBY)//待机模式
    {
        if (FOC->safe.count > FOC->safe.VBUS_watchdog_limit * 10)//待机至少10s才能自动恢复
        {
            // 所有条件必须同时满足
            if ((FOC->foc.Real_VBUS <= FOC->safe.VBUS_MAX && FOC->foc.Real_VBUS >= FOC->safe.VBUS_MIM) &&
                (FOC->foc.Real_MCUTemp <= FOC->safe.Temp_MAX && FOC->foc.Real_MCUTemp >= FOC->safe.Temp_MIN) &&
                (Value_fabsf(FOC->current.Real_Id) <= FOC->safe.Dcur_MAX &&
                 Value_fabsf(FOC->current.Real_Iq) <= FOC->safe.Qcur_MAX) &&
                (FOC->safe.errorFlag == MOTOR_ERROR_NONE))
            {
                Status_Switch_IDLE(FOC, &FOC->safe.count);//进入空闲
                User_Enable_Motor();
                FOC->safe.errorFlag = MOTOR_ERROR_NONE;
            }
        }
    }

    MotorStatus_Loop(&FOC->status);  // 根据状态执行不同的任务,可对函数指针进行设置

}

// 1kHz — 位置环 + 速度环 + 状态机 + 保护 + CAN发送
void Low_Loop(void)
{
    if (FOC.flag.in_PWM_Calc_ISR)
        return;

    // 外层环 (1kHz) — 位置环 → 速度环
    if (FOC.status >= MOTOR_STATUS_IDLE && FOC.status < MOTOR_STATUS_OVERVOLTAGE)
    {
        switch (FOC.mode)
        {
            case Speed_Current_MODE:
                SpeedLoop_1kHz(&FOC);  // Speed → Target_Iq
                break;
			case Position_Speed_Current_MODE:
                PositionLoop_1kHz(&FOC);  // Pos → Target_Speed
                SpeedLoop_1kHz(&FOC);     // Speed → Target_Iq
                break;
            default:
                speedLoopInitialized = 0U;
                speedLoopActiveMode = -1;
                FOC.flag.speedStallActive = 0U;
                FOC.flag.speedRecoveryActive = 0U;
                FOC.flag.speedStallCounter = 0U;
                FOC.flag.lowSpeedAssistGain = 0.0f;
                FOC.flag.speedFrictionIq = 0.0f;
                FOC.cogging.iqCompensation = 0.0f;
                FOC.cogging.learningActive = 0U;
                break;
        }
    }
    else
    {
        speedLoopInitialized = 0U;
        speedLoopActiveMode = -1;
        FOC.flag.speedStallActive = 0U;
        FOC.flag.speedRecoveryActive = 0U;
        FOC.flag.speedStallCounter = 0U;
        FOC.flag.lowSpeedAssistGain = 0.0f;
        FOC.flag.speedFrictionIq = 0.0f;
        FOC.cogging.iqCompensation = 0.0f;
        FOC.cogging.learningActive = 0U;
    }

    // 状态检测和保护必须始终运行，不能只在正常控制状态下运行。
    Status_Switch_Loop(&FOC);
    Status_RUN_Loop(&FOC);
}
//主循环
//void Main_Loop(void)
//{
//    Printf_Normal_Loop(&FOC);
//}


