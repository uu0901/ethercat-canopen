#ifndef _FOC_H
#define _FOC_H

#include "FOC_Serial.h"
#include "FOC_PID.h"
#include "FOC_PLL.h"
#include "FOC_Filter.h"
#include "FOC_Cogging.h"

#define ADC1_CH_NUM          3
#define ADC1_OFFSET_SAMPLES  1024

#define ADC2_CH_NUM          2
/* 20 kHz下200点等于10 ms，低速速度分辨率约0.038 rad/s。 */
#define ENCODER_SPEED_WINDOW_SIZE 200U
#define ENCODER_CORRECTION_POINTS 256U

#define Voltage_FORCE_MODE   0x00   // 电压强托VF
#define Voltage_OPEN_MODE    0x01   // 电压开环
#define Current_SINGLE_MODE  0x02  //  电流单闭环(力矩控制)
#define Speed_Current_MODE   0x03  // 速度-电流串级闭环控制
#define Position_Speed_Current_MODE 0x04  // 位置-速度-电流多环

// MOTOR_SAFE_STRUCT.errorFlag 位定义，可同时记录多个故障。
#define MOTOR_ERROR_NONE              ((uint8_t)0x00U)
#define MOTOR_ERROR_DISABLED          ((uint8_t)(1U << 0))	//1
#define MOTOR_ERROR_OVERVOLTAGE       ((uint8_t)(1U << 1))	//2
#define MOTOR_ERROR_UNDERVOLTAGE      ((uint8_t)(1U << 2))	//4
#define MOTOR_ERROR_OVERTEMPERATURE   ((uint8_t)(1U << 3))	//8
#define MOTOR_ERROR_UNDERTEMPERATURE  ((uint8_t)(1U << 4))	//16
#define MOTOR_ERROR_OVERCURRENT       ((uint8_t)(1U << 5))	//32
#define MOTOR_ERROR_ENCODER           ((uint8_t)(1U << 6))	//64

typedef struct
{
    uint8_t Poles;  // (电机实体参数)电机极对极数
    float VBUS;     // (电机实体参数)母线电压

    int8_t Motor_Dir;  // (参数设计)电机的运行方向设计
    int8_t PWM_Dir;    // (参数设计)PWM占空比高低对应
    uint16_t Duty;     // (参数设计)PWM满占空比

    int8_t Encoder_Dir;  // (参数设计)编码器的方向设置

    int8_t Current_Dir0;     // (参数设计)电流采样方向0
    int8_t Current_Dir1;     // (参数设计)电流采样方向1
    int8_t Current_Dir2;     // (参数设计)C相电流采样方向
    uint8_t Current_Num;     // (参数设计)电流通道0->AB相，1->AC相，2->BC相
    uint32_t ADC_Precision;  // (参数设计)ADC采样精度,如12位精度为4096
    float Amplifier;         // (参数设计)运放的放大倍数
    float MCU_Voltage;       // (参数设计)DSP/单片机的ADC基准电压
    float Sampling_Rs;       // (参数设计)采样电阻的阻值大小

    float Divide_Ratio;  // (参数设计)电压分压比设计,如22.276：(100K + 4.7K) / 4.7K

    float Electrical_Angle;  // (数据)电机的电角度
    float Electrical_Speed;  // (数据)电机的电角速度
	
	float Ld;              // D轴电感
    float Lq;              // Q轴电感
    float Rs;              // 相电阻
	float Flux;            // 永磁磁链
	float Rotor_Inertia;   // 电机转子惯量, kg*m^2
	float Torque_Constant; // 转矩常数, N*m/A
	float Speed_Bandwidth; // 速度闭环设计带宽, Hz
	float Speed_Damping;   // 速度闭环设计阻尼比
	float Position_Bandwidth; // 位置闭环设计带宽, Hz
    uint8_t AutoIdentify;  // 1 = 上电自动辨识, 辨识完成后自动清零
} MOTOR_QUANTIZE_STRUCT;

typedef struct
{
    float Target_Speed;  // (期望速度)机械角速度或位置模式最大速度, rad/s
    float Target_Pos;    // (期望角度)多圈机械目标位置, rad
    float Target_Id;     // (期望电流)期望D轴电流
    float Target_Iq;     // (期望电流)期望Q轴电流
    float Target_Uq;     // (期望电压)期望Q轴电压
    float Target_VF_Speed;  
	
    float Ud_in;  // (输入值)D轴物理电压输入, V
    float Uq_in;  // (输入值)Q轴物理电压输入, V

    uint16_t Duty_u;  // (输入值)U相占空比输入0~pwmMAX
    uint16_t Duty_v;  // (输入值)V相占空比输入0~pwmMAX
    uint16_t Duty_w;  // (输入值)W相占空比输入0~pwmMAX

    float Du;  // (数据)U相占空比输入0~1
    float Dv;  // (数据)V相占空比输入0~1
    float Dw;  // (数据)W相占空比输入0~1

    float sine;    // (数据)sine临时保存的正弦值
    float cosine;  // (数据)cosine临时保存的余弦值

    float Real_VBUS;  // (数据)Real实际的电机母线电压
	float Real_Temp;  // (数据)Temp实际的驱动器物理温度
    float Real_MCUTemp;  // (数据)Temp实际的驱动器物理温度
} MOTOR_FOC_STRUCT;

/**
 * 编码器角度、速度和多圈位置数据。
 * 类型跟随参考工程统一放在FOC.h，由FOC.encoder持有。
 */
typedef struct
{
    uint8_t sampleReady;
    uint8_t errorFlag;
    int dir;

    uint16_t rawAngleOriginal;
    uint16_t rawAngleCur;
    uint16_t rawAnglePre;
    int16_t rawAngleDelta;

    uint8_t correctionEnabled;
    uint8_t correctionCalibrated;
    int8_t correctionDirection;
    int16_t correctionCount;
    uint16_t correctionBaseRaw;

    float angleWithoutTrackCur;
    float angleWithoutTrackPre;

    int64_t rawPosition;
    int32_t fullRotationsCur;
    float angleCur;
    float anglePre;

    float vel;
    float windowVel;
    float lowSpeedVel;
    float speedBlend;
    float filterVel;

    int64_t speedPositionHistory[ENCODER_SPEED_WINDOW_SIZE];
    uint16_t speedHistoryIndex;
    uint16_t speedHistoryCount;

    float pllAngle;
    float pllVel;
    float pllError;
    float Pos_offset;
} MOTOR_ENCODER_STRUCT;

/** 参考工程的传递函数层：算法状态不混入编码器数据结构。 */
typedef struct
{
    LPF_STRUCT LPF_encoder;
    PLL_STRUCT PLL_encoder;
} MOTOR_TRANSFER_STRUCT;

typedef struct
{
    volatile uint16_t ADC_InjectedValues[ADC1_CH_NUM];
    volatile int32_t InjectedValuesOffsetSum[ADC1_CH_NUM];
	
	volatile uint8_t adcOffsetReady;
	volatile uint32_t offsetCnt;
	
	int32_t Current_offset0;  // (Current电流偏置)offset偏置位
    int32_t Current_offset1;  // (Current电流偏置)offset偏置位
	int32_t Current_offset2;  // (Current电流偏置)offset偏置位
	
    float Real_Id;  // (Current电流)Real实际D轴电流
    float Real_Iq;  // (Current电流)Real实际Q轴电流

    float Real_Ia;  // (Current相电流)A相电流
    float Real_Ib;  // (Current相电流)B相电流
    float Real_Ic;  // (Current相电流)C相电流

    float Real_Ialpha;  // (Current中间量电流)alpha轴电流
    float Real_Ibeta;   // (Current中间量电流)beta轴电流

    float Final_Gain;         // (ADC增益)最终的ADC电流采样增益

    /* 当前PWM周期中被重构的物理相：0=A、1=B、2=C。 */
    uint8_t reconstructedPhase;


} MOTOR_CURRENT_STRUCT;

typedef struct
{
    volatile uint16_t ADC2_Values[ADC2_CH_NUM];
} MOTOR_ADC2_STRUCT;

typedef struct
{
    uint8_t PWM_Calc;            // PWM计算“标志位”
    uint8_t PWM_watchdog_limit;  // PWM错误限幅(uint8_t够用，PWM计算不能错误太多次)

    uint8_t in_PWM_Calc_ISR;  // (互斥锁)标记是否在PWM计算中断中

    /* 速度T型规划和堵转释放状态，保留在FOC中便于在线观察。 */
    uint8_t speedStallActive;
    uint8_t speedRecoveryActive;
    uint8_t lowSpeedAssistEnabled;
    uint16_t speedStallCounter;
    float speedProfileRef;
    float lowSpeedAssistGain;
    float speedFrictionIq;

} MOTOR_FLAG_STRUCT;

typedef struct
{
    PID_STRUCT pos;
    PID_STRUCT spd;
    PID_STRUCT id;  // D轴电流环
    PID_STRUCT iq;  // Q轴电流环
} MOTOR_PID_STRUCT;
typedef struct
{
    float VBUS_MAX;                // (参数设计)母线电压值波动MAX阈值
    float VBUS_MIM;                // (参数设计)母线电压值波动MIN阈值
    uint32_t VBUS_watchdog_limit;  // (参数设计)电压异常的警告周期


    float Temp_MAX;                // (参数设计)驱动器允许最大温度
    float Temp_MIN;                // (参数设计)驱动器允许最小温度
    uint32_t Temp_watchdog_limit;  // (参数设计)温度异常的警告周期


    float Dcur_MAX;                 // (参数设计)电机最大电流D轴限制
    float Qcur_MAX;                 // (参数设计)电机最大电流Q轴限制
    uint32_t DQcur_watchdog_limit;  // (参数设计)过流保护的警告周期


    float Current_limit;   // (参数设计)电流正负区间设计，电流状态机判断
    float Speed_limit;     // (参数设计)速度正负区间设计，速度状态机判断
    float Position_limit;  // (参数设计)位置正负区间设计，位置状态机判断

    uint32_t count;  // (数据)状态机计数器

    uint32_t Encoder_watchdog_limit;   // (参数设计)编码器错误的警告周期
    uint32_t DISABLED_watchdog_limit;  //(参数设计)电机DISABLED状态机进待机模式的延时周期
	
	uint8_t errorFlag;  // 故障位集合，使用MOTOR_ERROR_xxx按位判断

    /* 自动恢复不会清除的最近一次故障快照，便于调试瞬态停机原因。 */
    uint8_t lastFaultStatus;
    float faultVBUS;
    float faultId;
    float faultIq;
    float faultSpeed;
} MOTOR_SAFE_STRUCT;


typedef struct
{
	uint8_t status;  // 【数据】status存储电机运行状态
	int8_t mode;    		 // 【有参数设计】mode选择电机的运行模式
    PRINTF_STRUCT TXdata;  // 【数据】data串口或CAN发送的信息
	MOTOR_TRANSFER_STRUCT transfer; // 【有参数设计】transfer传递函数
	MOTOR_ENCODER_STRUCT encoder;   // 【数据】encoder电机角速度和角度信息
	MOTOR_COGGING_STRUCT cogging;   // 【数据】低速机械角度齿槽转矩学习与补偿
	MOTOR_QUANTIZE_STRUCT motor;   // 【有参数设计】motor电机参数量化
	MOTOR_FOC_STRUCT foc;          // 【有参数设计】foc控制的参数输入“缓存”
	MOTOR_CURRENT_STRUCT current;  // 【数据】电机电流采样信息“缓存”
	MOTOR_FLAG_STRUCT flag;  // 【有参数设计】flag电机运行标志位
	MOTOR_SAFE_STRUCT safe;        // 【有参数设计】safe电机安全设置
	MOTOR_ADC2_STRUCT adc2;
	MOTOR_PID_STRUCT pid_Current;
	MOTOR_PID_STRUCT pid_VelCur;
	MOTOR_PID_STRUCT pid_PosVelCur;
} FOC_System_STRUCT;

extern FOC_System_STRUCT FOC;

void FOC_Init(void);
//void Main_Loop(void);
void Volatge_Force(void);
void High_Loop(void);
void Printf_Normal_Loop(FOC_System_STRUCT *FOC);
void Low_Loop(void);
void encoderUpdate(void);
void encoderObserverInit(void);
void encoderObserverReset(void);
void encoderCorrectionDisable(void);
uint8_t encoderCorrectionConfigure(uint16_t baseRaw,
                                   int8_t direction,
                                   const int16_t *correctionTable,
                                   uint16_t tableLength);
uint16_t encoderCorrectionApply(uint16_t rawAngle, int16_t *appliedCorrection);
#endif /* FOC_H */
