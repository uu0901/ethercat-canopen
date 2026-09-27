#ifndef __USERDATA_MOTOR_H
#define __USERDATA_MOTOR_H

#include "FOC.h"

/* 1 kHz速度环的T型速度规划参数。 */
#define FOC_SPEED_PROFILE_ACCEL_RAD_S2       300.0f
#define FOC_SPEED_PROFILE_DECEL_RAD_S2       300.0f
#define FOC_SPEED_PROFILE_DT_S                0.001f

/* 堵转时保持最大速度环输出；检测到转动后从实测速度重新起坡。 */
#define FOC_SPEED_STALL_TARGET_MIN_RAD_S       5.0f
#define FOC_SPEED_STALL_MAX_RAD_S              0.5f
#define FOC_SPEED_STALL_OUTPUT_RATIO           0.90f
#define FOC_SPEED_STALL_DETECT_TICKS          100U
#define FOC_SPEED_RELEASE_RAD_S                 1.0f

/* 恢复期间限制反向制动力矩，减少母线回灌。 */
#define FOC_SPEED_RECOVERY_BRAKE_IQ_A           0.10f
#define FOC_SPEED_RECOVERY_DONE_ERROR_RAD_S     0.5f

/* 位置模式制动规划：制动减速度小于速度规划减速度，预留跟踪余量。 */
#define FOC_POSITION_BRAKE_DECEL_RAD_S2         20.0f
#define FOC_POSITION_SETTLE_ERROR_RAD             0.01f
#define FOC_POSITION_SETTLE_SPEED_RAD_S           0.5f

/* MT6701一圈非线性校正：上电时由定子磁场带动电机正反各扫一圈。 */
#define FOC_ENCODER_CORRECTION_ENABLE              1U
#define FOC_ENCODER_CALIBRATION_UD_RATIO            0.15f
#define FOC_ENCODER_CALIBRATION_SETTLE_MS           6U
#define FOC_ENCODER_CALIBRATION_AVERAGE_SAMPLES     4U
#define FOC_ENCODER_CALIBRATION_SAMPLE_DELAY_MS     1U
#define FOC_ENCODER_CALIBRATION_MAX_ERROR_COUNTS 2048

/*
 * 双速度估算器切换区间：低速使用10 ms位置差分，高速使用PLL+LPF。
 * 过渡区线性融合，避免切换反馈时速度环产生阶跃。
 */
#define FOC_SPEED_ESTIMATOR_LOW_MAX_RAD_S          8.0f
#define FOC_SPEED_ESTIMATOR_PLL_MIN_RAD_S         15.0f

/*
 * 模式3低速粘滑抑制：提高低速PI响应，并预先补偿一小部分静摩擦。
 * 到15 rad/s平滑退出，保持原有高速速度环参数不变。
 */
#define FOC_LOW_SPEED_ASSIST_ENABLE                 1U
#define FOC_LOW_SPEED_ASSIST_FULL_RAD_S             5.0f
#define FOC_LOW_SPEED_ASSIST_ZERO_RAD_S            15.0f
#define FOC_LOW_SPEED_KP_SCALE                      2.0f
#define FOC_LOW_SPEED_KI_SCALE                      1.5f
#define FOC_LOW_SPEED_FRICTION_MIN_REF_RAD_S        0.30f
#define FOC_LOW_SPEED_FRICTION_IQ_A                 0.08f

/*
 * 低速抗齿槽：按校正后的机械角度在线学习速度误差对应的Iq补偿。
 * 补偿只改变速度外环的Iq请求，不进入20 kHz电流环的计算路径。
 */
#define FOC_COGGING_ENABLE                         1U
#define FOC_COGGING_LEARNING_ENABLE                1U
#define FOC_COGGING_MIN_REFERENCE_RAD_S          0.30f
#define FOC_COGGING_LEARN_MAX_SPEED_RAD_S        5.00f
#define FOC_COGGING_APPLY_FULL_SPEED_RAD_S       3.00f
#define FOC_COGGING_APPLY_ZERO_SPEED_RAD_S       8.00f
#define FOC_COGGING_STEADY_REFERENCE_RAD_S       0.05f
#define FOC_COGGING_LEARN_ERROR_LIMIT_RAD_S      3.00f
#define FOC_COGGING_LEARNING_GAIN_A_PER_RAD_S  0.0005f
#define FOC_COGGING_MAX_IQ_A                     0.35f

// 电机实体参数设置(根据实际需要填写)
static inline void User_MotorSet(void)
{
	float positionOmega;

	//mode
    FOC.mode = Voltage_FORCE_MODE;
	FOC.flag.lowSpeedAssistEnabled = (uint8_t)FOC_LOW_SPEED_ASSIST_ENABLE;
	//motor
    FOC.motor.Poles = 7;    // (uint8_t)极对数
    FOC.motor.VBUS = 24.0f;  // (float)母线电压
    FOC.motor.Motor_Dir = -1;  // (int8_t)电机方向1->正向CW，负1->负向CCW
    FOC.motor.PWM_Dir = -1;   // (int8_t)PWM占空比高低对应1->正向，负1->负向
    FOC.motor.Duty = 4199;    // (uint16_t)PWM满占空比数值
	FOC.motor.Current_Dir0 = -1;       // A相采样极性：保证正Uq对应正Iq
    FOC.motor.Current_Dir1 = -1;       // B相采样极性
    FOC.motor.Current_Dir2 = -1;       // C相采样极性
    FOC.motor.Current_Num = 0;         // (uint8_t)电流通道0->AB相，1->AC相，2->BC相
    FOC.motor.ADC_Precision = 4096;    // (uint32_t)ADC采样精度
    FOC.motor.Amplifier = 50.0f;       // (float)运算放大器增益
    FOC.motor.MCU_Voltage = 3.3f;      // (float)DSP/单片机的ADC电压基准
    FOC.motor.Sampling_Rs = 0.020f;    // (float)采样电阻大小
    FOC.motor.Divide_Ratio = 16.0f;  // (float)电压分压比设计,如22.276：(30K + 2K) / 2K
	FOC.motor.AutoIdentify = 1;  // 1=自动辨识, 0=使用下方手动参数
    FOC.motor.Ld = 0.00075569f;     // D轴电感(H)
    FOC.motor.Lq = 0.00088331f;     // Q轴电感(H)
    FOC.motor.Rs = 5.9842254066f;        // 相电阻(Ω)
    FOC.motor.Flux = 0.0080110757f;    // 永磁磁链(Wb)
	FOC.motor.Rotor_Inertia = 9.33e-5f;  // 铭牌: 933g*cm^2 = 9.33e-5kg*m^2
	FOC.motor.Torque_Constant = 0.98f;   // 铭牌转矩常数(N*m/A)
	FOC.motor.Speed_Bandwidth = 50.0f;   // 速度闭环目标-3dB带宽(Hz)
	FOC.motor.Speed_Damping = 0.707f;    // 二阶闭环阻尼比
	FOC.motor.Position_Bandwidth = 5.0f; // 位置环比速度环低10倍

	//encoder
	FOC.encoder.dir = FOC.motor.Encoder_Dir = 1;  // (int8_t)编码器方向1->正向，负1->负向
    /* Same encoder observer parameters as the reference motor project. */
    FOC.transfer.PLL_encoder.T = 5e-5f;
    FOC.transfer.PLL_encoder.Kp = 848.528f;
    FOC.transfer.PLL_encoder.Ki = 360000.0f;
    FOC.transfer.LPF_encoder.T = 5e-5f;
    /* 低速速度环调试使用较低截止频率，抑制MT6701量化噪声进入速度PI。 */
    FOC.transfer.LPF_encoder.Wc = 200.0f;
    encoderObserverInit();

	//电机安全设计
    FOC.safe.VBUS_MAX = 50.0f;  // (float)母线电压值波动MAX阈值
    FOC.safe.VBUS_MIM = 5.0f;  // (float)母线电压值波动MIN阈值
    FOC.safe.VBUS_watchdog_limit = 1000;

    FOC.safe.Temp_MAX = 60.0f;   // (float)驱动器允许最大温度
    FOC.safe.Temp_MIN = -20.0f;  // (float)驱动器允许最小温度
    FOC.safe.Temp_watchdog_limit = 1000;

    // 运行状态判定容差，只影响状态显示，不参与PID计算。
    FOC.safe.Current_limit = 0.05f;   // A：|目标Iq - 实际Iq|不超过此值视为力矩稳定
    FOC.safe.Speed_limit = 0.5f;      // rad/s：|目标速度 - 实际速度|不超过此值视为恒速
    FOC.safe.Position_limit = 0.01f;  // rad：|目标位置 - 实际位置|不超过此值视为位置保持
	
	FOC.safe.Dcur_MAX = 60.0f;  // (float)电机最大电流D轴限制
    FOC.safe.Qcur_MAX = 60.0f;  // (float)电机最大电流Q轴限制
    FOC.safe.DQcur_watchdog_limit = 1000;

    FOC.safe.Current_limit = 0.5f;   // (float)电机->电流状态机判断的电流范围
    FOC.safe.Speed_limit = 5.0f;     // (float)电机->速度状态机判断的速度范围
    FOC.safe.Position_limit = 0.1f;  // (float)电机->位置状态机判断的位置范围

    FOC.safe.DISABLED_watchdog_limit = 1000;
    FOC.safe.Encoder_watchdog_limit = 1000;
	FOC.safe.lastFaultStatus = 0xFFU;  // 尚未发生故障
	// PID参数设置
    //Current_SINGLE_MODE电流单闭环(力矩控制)的Id/Iq轴PID参数设置
    FOC.pid_Current.id.T = 5e-5f;
    FOC.pid_Current.id.Wc = 100.0f;
    FOC.pid_Current.id.Kp = 3.5f;
    FOC.pid_Current.id.Ki = 2000.0f;
    FOC.pid_Current.id.Kd = 0.0f;
    FOC.pid_Current.id.OutMax = FOC.motor.VBUS;
    FOC.pid_Current.id.OutMin = -FOC.motor.VBUS;
    FOC.pid_Current.id.IntMax = FOC.motor.VBUS;
    FOC.pid_Current.id.IntMin = -FOC.motor.VBUS;
    FOC.pid_Current.iq = FOC.pid_Current.id;

	//VelCur_DOUBLE_MODE速度环：针对当前电机低速运行调整，不能直接照搬2208电机。
    FOC.pid_VelCur.spd.T = 0.001f;
    FOC.pid_VelCur.spd.Wc = 100.0f;
    FOC.pid_VelCur.spd.Kp = 0.012f;
    FOC.pid_VelCur.spd.Ki = 0.35f;
    FOC.pid_VelCur.spd.Kd = 0.0f;
    FOC.pid_VelCur.spd.OutMax = 1.6f;
    FOC.pid_VelCur.spd.OutMin = -1.6f;
    FOC.pid_VelCur.spd.IntMax = FOC.pid_VelCur.spd.OutMax;
    FOC.pid_VelCur.spd.IntMin = FOC.pid_VelCur.spd.OutMin;

    FOC.pid_VelCur.id.T = 5e-5f;
    FOC.pid_VelCur.id.Wc = 100.0f;
    FOC.pid_VelCur.id.Kp = 6.5f;
    FOC.pid_VelCur.id.Ki = 20.0f;
    FOC.pid_VelCur.id.Kd = 0.0f;
    FOC.pid_VelCur.id.OutMax = FOC.motor.VBUS;
    FOC.pid_VelCur.id.OutMin = -FOC.motor.VBUS;
    FOC.pid_VelCur.id.IntMax = FOC.motor.VBUS;
    FOC.pid_VelCur.id.IntMin = -FOC.motor.VBUS;
    FOC.pid_VelCur.iq = FOC.pid_VelCur.id;
	
    //Position_Speed_Current_MODE位置-速度-电流串级闭环
    //位置P环按5Hz设计，输出为机械速度(rad/s)，运行时由Target_Speed限制幅值。
    positionOmega = Value_2PI * FOC.motor.Position_Bandwidth;
	FOC.pid_PosVelCur.pos.T = 0.001f;
	FOC.pid_PosVelCur.pos.Wc = 200.0f;
	FOC.pid_PosVelCur.pos.Kp = positionOmega;
    FOC.pid_PosVelCur.pos.Ki = 0.0f;
    FOC.pid_PosVelCur.pos.Kd = 0.0f;
    FOC.pid_PosVelCur.pos.OutMax = 1.0f;
    FOC.pid_PosVelCur.pos.OutMin = -1.0f;
    FOC.pid_PosVelCur.pos.IntMax = 1.0f;
    FOC.pid_PosVelCur.pos.IntMin = -1.0f;

    //位置模式复用相同的参考速度PID。
    FOC.pid_PosVelCur.spd = FOC.pid_VelCur.spd;

    //位置模式电流环与速度模式使用相同参数，避免切换模式时电流动态改变。
    FOC.pid_PosVelCur.id = FOC.pid_VelCur.id;
    FOC.pid_PosVelCur.iq = FOC.pid_VelCur.iq;

    PID_Init(&FOC.pid_Current.id);
    PID_Init(&FOC.pid_Current.iq);
    PID_Init(&FOC.pid_VelCur.spd);
    PID_Init(&FOC.pid_VelCur.id);
    PID_Init(&FOC.pid_VelCur.iq);
    PID_Init(&FOC.pid_PosVelCur.pos);
    PID_Init(&FOC.pid_PosVelCur.spd);
    PID_Init(&FOC.pid_PosVelCur.id);
    PID_Init(&FOC.pid_PosVelCur.iq);
	
}

#endif  // USERDATA_MOTOR_H
