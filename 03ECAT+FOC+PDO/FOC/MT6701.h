#ifndef FOC_MT6701_H
#define FOC_MT6701_H

#include <stdint.h>

typedef struct
{
    volatile uint32_t rawFrame;
    volatile uint32_t validFrameCount;
    volatile uint32_t crcErrorCount;
    volatile uint32_t transferErrorCount;
    volatile uint32_t busySkipCount;
    volatile uint16_t consecutiveErrorCount;
    volatile uint16_t rawAngle;
    volatile uint8_t magneticStatus;
    volatile uint8_t crcValid;
    volatile uint8_t dataValid;
} MT6701_DiagnosticsTypeDef;

extern MT6701_DiagnosticsTypeDef mt6701Diag;

void MT6701_Init(void);
void MT6701_TriggerRead(void);
void MT6701_ProcessDMA(void);
void MT6701_TransferError(void);
uint16_t MT6701_GetRawAngle(void);
uint8_t MT6701_IsDataValid(void);

#endif /* FOC_MT6701_H */
