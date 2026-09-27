#include "FOC_MotorStatus.h"

#define STATUS_HANDLER_COUNT ((uint8_t)(sizeof(status_handlers) / sizeof(status_handlers[0])))

// 函数指针数组（按枚举顺序排列）
static void (*const status_handlers[])(void) = {
    //24个函数指针，对应24个状态，索引是0到23

    // 初始化与运行状态
    MOTOR_STATUS_STANDBY_Loop,
    MOTOR_STATUS_UNINITIALIZED_Loop,
    MOTOR_STATUS_INITIALIZING_Loop,
    MOTOR_STATUS_CALIBRATING_Loop,

    // 运行状态
    MOTOR_STATUS_IDLE_Loop,
    MOTOR_STATUS_TORQUE_INCREASING_Loop,
    MOTOR_STATUS_TORQUE_DECREASING_Loop,
    MOTOR_STATUS_TORQUE_CONTROL_Loop,
    MOTOR_STATUS_ACCELERATING_Loop,
    MOTOR_STATUS_DECELERATING_Loop,
    MOTOR_STATUS_CONST_SPEED_Loop,
    MOTOR_STATUS_POSITION_INCREASING_Loop,
    MOTOR_STATUS_POSITION_DECREASING_Loop,
    MOTOR_STATUS_POSITION_HOLD_Loop,

    // 硬件错误
    MOTOR_STATUS_OVERVOLTAGE_Loop,
    MOTOR_STATUS_UNDERVOLTAGE_Loop,
    MOTOR_STATUS_OVERTEMPERATURE_Loop,
    MOTOR_STATUS_UNDERTEMPERATURE_Loop,
    MOTOR_STATUS_OVERCURRENT_Loop,
    MOTOR_STATUS_ENCODER_ERROR_Loop,
    MOTOR_STATUS_SENSOR_ERROR_Loop,
    MOTOR_STATUS_PWM_CALC_FAULT_Loop,

    // 安全状态
    MOTOR_STATUS_EMERGENCY_STOP_Loop,
    MOTOR_STATUS_DISABLED_Loop

    //...
};

static void MotorStatus_IndicatorHook(uint8_t status)
{
    (void)status;
    /* Reserved 1 kHz status indication interface.
       Add status LED or buzzer handling here after assigning dedicated pins. */
}

// 核心函数MotorStatus_Loop函数
void MotorStatus_Loop(uint8_t *status)
{
    static uint8_t previousStatus = 0xFFU;

    if (*status >= STATUS_HANDLER_COUNT)
    {
        *status = MOTOR_STATUS_STANDBY;
    }

    // 状态处理函数只在进入新状态时执行一次，避免反复清PID或重复上报。
    if (*status != previousStatus)
    {
        previousStatus = *status;
        status_handlers[*status]();
    }

    MotorStatus_IndicatorHook(*status);
}
