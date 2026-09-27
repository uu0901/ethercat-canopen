#include "FOC.h"

#include "FOC_Math.h"
#include "UserData_Function.h"
#include "UserData_Motor.h"

#define MT6701_CPR            16384
#define MT6701_HALF_CPR       (MT6701_CPR / 2)
#define MT6701_RAD_PER_COUNT  (Value_2PI / (float)MT6701_CPR)
#define ENCODER_SAMPLE_DT     0.00005f

static int16_t encoderCorrectionTable[ENCODER_CORRECTION_POINTS];
static uint16_t encoderCorrectionBaseRaw = 0U;
static int8_t encoderCorrectionDirection = 1;
static uint8_t encoderCorrectionValid = 0U;

static int32_t Encoder_WrapCount(int32_t count)
{
    while (count >= MT6701_CPR)
    {
        count -= MT6701_CPR;
    }
    while (count < 0)
    {
        count += MT6701_CPR;
    }
    return count;
}

void encoderCorrectionDisable(void)
{
    encoderCorrectionValid = 0U;
    encoderCorrectionBaseRaw = 0U;
    encoderCorrectionDirection = 1;
    FOC.encoder.correctionEnabled = 0U;
    FOC.encoder.correctionCalibrated = 0U;
    FOC.encoder.correctionDirection = 1;
    FOC.encoder.correctionCount = 0;
    FOC.encoder.correctionBaseRaw = 0U;
}

uint8_t encoderCorrectionConfigure(uint16_t baseRaw,
                                   int8_t direction,
                                   const int16_t *correctionTable,
                                   uint16_t tableLength)
{
    uint16_t index;

    if ((correctionTable == 0) ||
        (tableLength != ENCODER_CORRECTION_POINTS) ||
        ((direction != 1) && (direction != -1)))
    {
        encoderCorrectionDisable();
        return 0U;
    }

    for (index = 0U; index < ENCODER_CORRECTION_POINTS; index++)
    {
        encoderCorrectionTable[index] = correctionTable[index];
    }

    encoderCorrectionBaseRaw = (uint16_t)Encoder_WrapCount((int32_t)baseRaw);
    encoderCorrectionDirection = direction;
    encoderCorrectionValid = 1U;
    FOC.encoder.correctionEnabled = 1U;
    FOC.encoder.correctionCalibrated = 1U;
    FOC.encoder.correctionDirection = direction;
    FOC.encoder.correctionBaseRaw = encoderCorrectionBaseRaw;
    return 1U;
}

uint16_t encoderCorrectionApply(uint16_t rawAngle, int16_t *appliedCorrection)
{
    uint32_t phase;
    uint32_t tableIndex;
    uint32_t tableRemainder;
    uint32_t nextIndex;
    int32_t correction;
    int32_t correctedPhase;
    int32_t correctedRaw;

    if (encoderCorrectionValid == 0U)
    {
        if (appliedCorrection != 0)
        {
            *appliedCorrection = 0;
        }
        return rawAngle;
    }

    if (encoderCorrectionDirection > 0)
    {
        phase = (uint32_t)Encoder_WrapCount((int32_t)rawAngle -
                                            (int32_t)encoderCorrectionBaseRaw);
    }
    else
    {
        phase = (uint32_t)Encoder_WrapCount((int32_t)encoderCorrectionBaseRaw -
                                            (int32_t)rawAngle);
    }

    /* 16384/256=64：使用低6位在相邻校正点之间线性插值。 */
    tableIndex = phase >> 6U;
    tableRemainder = phase & 0x3FU;
    nextIndex = (tableIndex + 1U) & (ENCODER_CORRECTION_POINTS - 1U);
    correction = (int32_t)encoderCorrectionTable[tableIndex] +
                 (((int32_t)encoderCorrectionTable[nextIndex] -
                   (int32_t)encoderCorrectionTable[tableIndex]) *
                  (int32_t)tableRemainder) / 64;

    correctedPhase = Encoder_WrapCount((int32_t)phase + correction);
    if (encoderCorrectionDirection > 0)
    {
        correctedRaw = Encoder_WrapCount((int32_t)encoderCorrectionBaseRaw +
                                         correctedPhase);
    }
    else
    {
        correctedRaw = Encoder_WrapCount((int32_t)encoderCorrectionBaseRaw -
                                         correctedPhase);
    }

    if (appliedCorrection != 0)
    {
        *appliedCorrection = (int16_t)correction;
    }
    return (uint16_t)correctedRaw;
}

void encoderObserverInit(void)
{
    PLL_Init(&FOC.transfer.PLL_encoder);
    LPF_Init(&FOC.transfer.LPF_encoder);
    FOC.encoder.pllAngle = 0.0f;
    FOC.encoder.pllVel = 0.0f;
    FOC.encoder.pllError = 0.0f;
    FOC.encoder.lowSpeedVel = 0.0f;
    FOC.encoder.speedBlend = 0.0f;
    FOC.encoder.filterVel = 0.0f;
}

void encoderObserverReset(void)
{
    encoderObserverInit();
}

static int32_t Encoder_PositionToTurns(int64_t rawPosition)
{
    if (rawPosition >= 0)
    {
        return (int32_t)(rawPosition / MT6701_CPR);
    }

    return -(int32_t)(((-rawPosition) + MT6701_CPR - 1) / MT6701_CPR);
}

static void Encoder_InitFirstSample(MOTOR_ENCODER_STRUCT *encoder)
{
    encoder->sampleReady = 1U;
    encoder->rawAnglePre = encoder->rawAngleCur;
    encoder->angleWithoutTrackPre = encoder->angleWithoutTrackCur;

    encoder->rawPosition = (int64_t)encoder->rawAngleCur;
    encoder->angleCur = (float)encoder->rawPosition * MT6701_RAD_PER_COUNT;
    encoder->anglePre = encoder->angleCur;
    encoder->fullRotationsCur = 0;
    encoder->rawAngleDelta = 0;

    encoder->vel = 0.0f;
    encoder->windowVel = 0.0f;
    encoder->lowSpeedVel = 0.0f;
    encoder->speedBlend = 0.0f;
    encoder->filterVel = 0.0f;
    encoder->speedPositionHistory[0] = encoder->rawPosition;
    encoder->speedHistoryIndex = 1U;
    encoder->speedHistoryCount = 1U;

    encoder->pllAngle = FOC.transfer.PLL_encoder.go.OutRe;
    encoder->pllVel = FOC.transfer.PLL_encoder.go.OutWe;
    encoder->pllError = FOC.transfer.PLL_encoder.go.Error;
}

void encoderUpdate(void)
{
    MOTOR_ENCODER_STRUCT *encoder = &FOC.encoder;
    int32_t rawDelta;
    uint8_t positionMode;
    float observerAngle;
    float pllFilteredVel;
    float speedAbs;
    float blend;
    uint8_t speedWindowReady = 0U;

    encoder->rawAngleOriginal = User_Encoder_ReadRaw();
    if (encoder->errorFlag != 0U)
    {
        return;
    }
    encoder->rawAngleCur = encoderCorrectionApply(encoder->rawAngleOriginal,
                                                   &encoder->correctionCount);

    encoder->angleWithoutTrackCur =
        (float)encoder->rawAngleCur * MT6701_RAD_PER_COUNT;

    if (encoder->sampleReady == 0U)
    {
        Encoder_InitFirstSample(encoder);
        return;
    }

    rawDelta = (int32_t)encoder->rawAngleCur -
               (int32_t)encoder->rawAnglePre;
    if (rawDelta > MT6701_HALF_CPR)
    {
        rawDelta -= MT6701_CPR;
    }
    else if (rawDelta < -MT6701_HALF_CPR)
    {
        rawDelta += MT6701_CPR;
    }

    encoder->rawAngleDelta = (int16_t)rawDelta;
    encoder->anglePre = encoder->angleCur;
    encoder->rawPosition += (int64_t)rawDelta;
    encoder->angleCur = (float)encoder->rawPosition * MT6701_RAD_PER_COUNT;
    encoder->fullRotationsCur = Encoder_PositionToTurns(encoder->rawPosition);
    encoder->vel = (float)rawDelta * MT6701_RAD_PER_COUNT /
                   ENCODER_SAMPLE_DT;

    if (encoder->speedHistoryCount < ENCODER_SPEED_WINDOW_SIZE)
    {
        encoder->speedPositionHistory[encoder->speedHistoryIndex] =
            encoder->rawPosition;
        encoder->speedHistoryIndex++;
        if (encoder->speedHistoryIndex >= ENCODER_SPEED_WINDOW_SIZE)
        {
            encoder->speedHistoryIndex = 0U;
        }
        encoder->speedHistoryCount++;
        encoder->windowVel = 0.0f;
    }
    else
    {
        int64_t windowDelta = encoder->rawPosition -
            encoder->speedPositionHistory[encoder->speedHistoryIndex];

        encoder->speedPositionHistory[encoder->speedHistoryIndex] =
            encoder->rawPosition;
        encoder->speedHistoryIndex++;
        if (encoder->speedHistoryIndex >= ENCODER_SPEED_WINDOW_SIZE)
        {
            encoder->speedHistoryIndex = 0U;
        }

        encoder->windowVel = (float)windowDelta * MT6701_RAD_PER_COUNT /
            (ENCODER_SAMPLE_DT * (float)ENCODER_SPEED_WINDOW_SIZE);
        speedWindowReady = 1U;
    }

    positionMode = (FOC.mode == Position_Speed_Current_MODE) ? 1U : 0U;
    observerAngle = (encoder->angleWithoutTrackCur - encoder->Pos_offset) *
                    (float)encoder->dir;
    Transfer_PLL_Loop(&FOC.transfer.PLL_encoder,
                      positionMode,
                      1U,
                      observerAngle);
    Transfer_LPF_Loop(&FOC.transfer.LPF_encoder,
                      FOC.transfer.PLL_encoder.go.OutWe);

    encoder->pllError = FOC.transfer.PLL_encoder.go.Error;
    encoder->pllVel = FOC.transfer.PLL_encoder.go.OutWe;
    encoder->pllAngle = FOC.transfer.PLL_encoder.go.OutRe;
    pllFilteredVel = FOC.transfer.LPF_encoder.filter.Output;

    /*
     * 10 ms窗口把2 rad/s时的量化分辨率从约0.383 rad/s提高到
     * 约0.038 rad/s。窗口尚未填满时暂用PLL，避免启动阶段反馈为零。
     */
    if (speedWindowReady == 0U)
    {
        encoder->lowSpeedVel = pllFilteredVel;
        encoder->speedBlend = 1.0f;
        encoder->filterVel = pllFilteredVel;
    }
    else
    {
        encoder->lowSpeedVel = (float)encoder->dir * encoder->windowVel;
        speedAbs = Value_fabsf(encoder->lowSpeedVel);

        if (speedAbs <= FOC_SPEED_ESTIMATOR_LOW_MAX_RAD_S)
        {
            blend = 0.0f;
        }
        else if (speedAbs >= FOC_SPEED_ESTIMATOR_PLL_MIN_RAD_S)
        {
            blend = 1.0f;
        }
        else
        {
            blend = (speedAbs - FOC_SPEED_ESTIMATOR_LOW_MAX_RAD_S) /
                    (FOC_SPEED_ESTIMATOR_PLL_MIN_RAD_S -
                     FOC_SPEED_ESTIMATOR_LOW_MAX_RAD_S);
        }

        encoder->speedBlend = blend;
        encoder->filterVel = encoder->lowSpeedVel * (1.0f - blend) +
                             pllFilteredVel * blend;
    }

    encoder->rawAnglePre = encoder->rawAngleCur;
    encoder->angleWithoutTrackPre = encoder->angleWithoutTrackCur;
}
