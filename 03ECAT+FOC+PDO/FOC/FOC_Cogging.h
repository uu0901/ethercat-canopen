#ifndef __FOC_COGGING_H
#define __FOC_COGGING_H

#include <stdint.h>

#define FOC_COGGING_TABLE_SIZE 256U

/*
 * 低速齿槽转矩补偿的运行状态。
 * 两张实际查找表保存在FOC_Cogging.c中，避免把大数组塞进FOC调试结构体。
 */
typedef struct
{
    uint8_t enabled;
    uint8_t learningEnabled;
    int8_t activeDirection;
    uint8_t learningActive;

    uint16_t tableIndex;
    float tableFraction;
    float speedError;
    float iqCompensation;
    float applyGain;
    uint32_t learningUpdates;
} MOTOR_COGGING_STRUCT;

void Cogging_Init(MOTOR_COGGING_STRUCT *cogging);
void Cogging_Clear(MOTOR_COGGING_STRUCT *cogging);
float Cogging_Update(MOTOR_COGGING_STRUCT *cogging,
                     float mechanicalAngle,
                     float speedReference,
                     float speedFeedback,
                     uint8_t allowLearning);

#endif /* __FOC_COGGING_H */
