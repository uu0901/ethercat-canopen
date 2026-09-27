#include "FOC_Cogging.h"
#include "FOC_Math.h"
#include "UserData_Motor.h"

/*
 * 正反转分表：同一机械位置在两个方向上的摩擦项符号不同，分表可避免
 * 正反转学习互相抵消。表项单位为A，表示附加到速度PI输出上的Iq。
 */
static float coggingPositiveTable[FOC_COGGING_TABLE_SIZE];
static float coggingNegativeTable[FOC_COGGING_TABLE_SIZE];

static void Cogging_ClearTable(float *table)
{
    uint16_t index;

    for (index = 0U; index < FOC_COGGING_TABLE_SIZE; index++)
    {
        table[index] = 0.0f;
    }
}

void Cogging_Clear(MOTOR_COGGING_STRUCT *cogging)
{
    Cogging_ClearTable(coggingPositiveTable);
    Cogging_ClearTable(coggingNegativeTable);

    cogging->activeDirection = 0;
    cogging->learningActive = 0U;
    cogging->tableIndex = 0U;
    cogging->tableFraction = 0.0f;
    cogging->speedError = 0.0f;
    cogging->iqCompensation = 0.0f;
    cogging->applyGain = 0.0f;
    cogging->learningUpdates = 0U;
}

void Cogging_Init(MOTOR_COGGING_STRUCT *cogging)
{
    cogging->enabled = (uint8_t)FOC_COGGING_ENABLE;
    cogging->learningEnabled = (uint8_t)FOC_COGGING_LEARNING_ENABLE;
    Cogging_Clear(cogging);
}

float Cogging_Update(MOTOR_COGGING_STRUCT *cogging,
                     float mechanicalAngle,
                     float speedReference,
                     float speedFeedback,
                     uint8_t allowLearning)
{
    float *table;
    float normalizedAngle;
    float scaledIndex;
    float fraction;
    float speedAbs;
    float applyGain;
    float speedError;
    float learningDelta;
    float compensation;
    uint16_t index;
    uint16_t nextIndex;

    cogging->learningActive = 0U;

    if ((cogging->enabled == 0U) ||
        (Value_fabsf(speedReference) < FOC_COGGING_MIN_REFERENCE_RAD_S))
    {
        cogging->activeDirection = 0;
        cogging->speedError = 0.0f;
        cogging->iqCompensation = 0.0f;
        cogging->applyGain = 0.0f;
        return 0.0f;
    }

    normalizedAngle = Value_normalize(mechanicalAngle);
    scaledIndex = normalizedAngle *
                  ((float)FOC_COGGING_TABLE_SIZE / Value_2PI);
    index = (uint16_t)scaledIndex;
    if (index >= FOC_COGGING_TABLE_SIZE)
    {
        index = 0U;
        fraction = 0.0f;
    }
    else
    {
        fraction = scaledIndex - (float)index;
    }
    nextIndex = (uint16_t)(index + 1U);
    if (nextIndex >= FOC_COGGING_TABLE_SIZE)
    {
        nextIndex = 0U;
    }

    if (speedReference >= 0.0f)
    {
        table = coggingPositiveTable;
        cogging->activeDirection = 1;
    }
    else
    {
        table = coggingNegativeTable;
        cogging->activeDirection = -1;
    }

    speedAbs = Value_fabsf(speedReference);
    if (speedAbs <= FOC_COGGING_APPLY_FULL_SPEED_RAD_S)
    {
        applyGain = 1.0f;
    }
    else if (speedAbs >= FOC_COGGING_APPLY_ZERO_SPEED_RAD_S)
    {
        applyGain = 0.0f;
    }
    else
    {
        applyGain = (FOC_COGGING_APPLY_ZERO_SPEED_RAD_S - speedAbs) /
                    (FOC_COGGING_APPLY_ZERO_SPEED_RAD_S -
                     FOC_COGGING_APPLY_FULL_SPEED_RAD_S);
    }

    /* 相邻表点线性插值，避免跨表格边界时产生转矩阶跃。 */
    compensation = table[index] * (1.0f - fraction) +
                   table[nextIndex] * fraction;
    compensation *= applyGain;

    speedError = Value_Limit(speedReference - speedFeedback,
                             FOC_COGGING_LEARN_ERROR_LIMIT_RAD_S,
                             -FOC_COGGING_LEARN_ERROR_LIMIT_RAD_S);

    if ((allowLearning != 0U) &&
        (cogging->learningEnabled != 0U) &&
        (speedAbs <= FOC_COGGING_LEARN_MAX_SPEED_RAD_S))
    {
        /*
         * 重复学习控制：把当前机械角度处的周期性速度误差积累到Iq表中。
         * 按插值权重同时更新两个相邻表点，学习后仍逐项限幅。
         */
        learningDelta = FOC_COGGING_LEARNING_GAIN_A_PER_RAD_S * speedError;
        table[index] = Value_Limit(table[index] +
                                   learningDelta * (1.0f - fraction),
                                   FOC_COGGING_MAX_IQ_A,
                                   -FOC_COGGING_MAX_IQ_A);
        table[nextIndex] = Value_Limit(table[nextIndex] +
                                       learningDelta * fraction,
                                       FOC_COGGING_MAX_IQ_A,
                                       -FOC_COGGING_MAX_IQ_A);
        cogging->learningActive = 1U;
        cogging->learningUpdates++;
    }

    cogging->tableIndex = index;
    cogging->tableFraction = fraction;
    cogging->speedError = speedError;
    cogging->applyGain = applyGain;
    cogging->iqCompensation = compensation;

    return compensation;
}
