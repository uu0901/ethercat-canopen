#include "MT6701.h"

#include <string.h>

#include "main.h"
#include "spi.h"

#define MT6701_FRAME_SIZE          3U
#define MT6701_ANGLE_SHIFT         9U
#define MT6701_ANGLE_MASK          0x3FFFU
#define MT6701_STATUS_SHIFT        6U
#define MT6701_STATUS_MASK         0x07U
#define MT6701_CRC_MASK            0x3FU
#define MT6701_ERROR_LIMIT         5U

MT6701_DiagnosticsTypeDef mt6701Diag;

static uint8_t mt6701RxBuffer[MT6701_FRAME_SIZE];

static uint8_t MT6701_CalcCRC6(uint32_t data)
{
    uint8_t crc = 0U;
    int8_t bitIndex;

    /* CRC input is D[13:0] followed by Mg[3:0], MSB first. */
    for (bitIndex = 17; bitIndex >= 0; bitIndex--)
    {
        uint8_t inputBit = (uint8_t)((data >> bitIndex) & 1U);
        uint8_t feedback = (uint8_t)(((crc >> 5U) & 1U) ^ inputBit);

        crc = (uint8_t)((crc << 1U) & MT6701_CRC_MASK);
        if (feedback != 0U)
        {
            /* x^6 + x + 1, without the implicit x^6 term. */
            crc ^= 0x03U;
        }
    }

    return crc;
}

void MT6701_Init(void)
{
    memset(&mt6701Diag, 0, sizeof(mt6701Diag));
    memset(mt6701RxBuffer, 0, sizeof(mt6701RxBuffer));
    HAL_GPIO_WritePin(MT6701_CS_GPIO_Port, MT6701_CS_Pin, GPIO_PIN_SET);
}

void MT6701_TriggerRead(void)
{
    HAL_StatusTypeDef status;

    if (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY)
    {
        mt6701Diag.busySkipCount++;
        return;
    }

    HAL_GPIO_WritePin(MT6701_CS_GPIO_Port, MT6701_CS_Pin, GPIO_PIN_RESET);
    status = HAL_SPI_Receive_DMA(&hspi1, mt6701RxBuffer, MT6701_FRAME_SIZE);
    if (status != HAL_OK)
    {
        HAL_GPIO_WritePin(MT6701_CS_GPIO_Port, MT6701_CS_Pin, GPIO_PIN_SET);
        mt6701Diag.transferErrorCount++;
    }
}

void MT6701_ProcessDMA(void)
{
    uint32_t frame;
    uint32_t crcInput;
    uint8_t receivedCRC;

    HAL_GPIO_WritePin(MT6701_CS_GPIO_Port, MT6701_CS_Pin, GPIO_PIN_SET);

    frame = ((uint32_t)mt6701RxBuffer[0] << 16U) |
            ((uint32_t)mt6701RxBuffer[1] << 8U) |
            (uint32_t)mt6701RxBuffer[2];
    mt6701Diag.rawFrame = frame;

    crcInput = (frame >> MT6701_STATUS_SHIFT) & 0x3FFFFU;
    receivedCRC = (uint8_t)(frame & MT6701_CRC_MASK);
    if (frame == 0U)
    {
        mt6701Diag.crcValid = 0U;
        if (mt6701Diag.consecutiveErrorCount < UINT16_MAX)
        {
            mt6701Diag.consecutiveErrorCount++;
        }
        if (mt6701Diag.consecutiveErrorCount >= MT6701_ERROR_LIMIT)
        {
            mt6701Diag.dataValid = 0U;
        }
        return;
    }

    /* Keep CRC as a link-quality diagnostic, matching the proven motor
     * project.  A nonzero SSI frame still supplies its angle; otherwise a
     * CRC convention mismatch would prevent FOC initialization entirely. */
    if (MT6701_CalcCRC6(crcInput) == receivedCRC)
    {
        mt6701Diag.crcValid = 1U;
    }
    else
    {
        mt6701Diag.crcValid = 0U;
        mt6701Diag.crcErrorCount++;
    }

    /* SSI frame: bit 23 is the fixed start/valid bit; the 14-bit angle is
     * in bits [22:9].  Treating [23:10] as the angle forces bit 13 high and
     * produces the observed false pi..2pi sawtooth. */
    mt6701Diag.rawAngle = (uint16_t)((frame >> MT6701_ANGLE_SHIFT) &
                                     MT6701_ANGLE_MASK);
    mt6701Diag.magneticStatus = (uint8_t)((frame >> MT6701_STATUS_SHIFT) &
                                          MT6701_STATUS_MASK);
    mt6701Diag.consecutiveErrorCount = 0U;
    mt6701Diag.dataValid = 1U;
    mt6701Diag.validFrameCount++;
}

void MT6701_TransferError(void)
{
    HAL_GPIO_WritePin(MT6701_CS_GPIO_Port, MT6701_CS_Pin, GPIO_PIN_SET);
    mt6701Diag.transferErrorCount++;
    if (mt6701Diag.consecutiveErrorCount < UINT16_MAX)
    {
        mt6701Diag.consecutiveErrorCount++;
    }
    if (mt6701Diag.consecutiveErrorCount >= MT6701_ERROR_LIMIT)
    {
        mt6701Diag.dataValid = 0U;
    }
}

uint16_t MT6701_GetRawAngle(void)
{
    return mt6701Diag.rawAngle;
}

uint8_t MT6701_IsDataValid(void)
{
    return mt6701Diag.dataValid;
}
