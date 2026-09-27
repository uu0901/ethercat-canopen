/**
 * \addtogroup LAN9252_SPI_SLAVE_IO LAN9252_SPI_SLAVE_IO
 * @{
 */

/**
\file LAN9252_SPI_SLAVE_IO.h
\brief LAN9252_SPI_SLAVE_IO function prototypes and defines

\version 1.0.0.11
 */

 
 #ifndef _LAN9252__SPI__SLAVE__IO_H_
#define _LAN9252__SPI__SLAVE__IO_H_


/*-----------------------------------------------------------------------------------------
------
------    Includes
------
-----------------------------------------------------------------------------------------*/
#include "ecat_def.h"

#include "ecatappl.h"

/*-----------------------------------------------------------------------------------------
------
------    Defines and Types
------
 -----------------------------------------------------------------------------------------*/

/* Stage 1 PDO bridge.  Rx commands are observable but are deliberately not
 * connected to the FOC set-points until the safety/watchdog stage is added. */
typedef struct
{
    UINT16 controlWord;
    UINT16 commandCounter;
    INT32 targetVelocity;       /* 0.001 rad/s */
    UINT16 reserved1;
    UINT16 reserved2;
    UINT32 reserved3;
} ECAT_MOTOR_RX_PDO;

typedef struct
{
    UINT16 statusWord;
    UINT16 motorStatus;
    INT32 actualVelocity;       /* 0.001 rad/s */
    INT16 actualIq;             /* 0.01 A */
    INT16 actualId;             /* 0.01 A */
    UINT16 errorCode;
    UINT16 busVoltage;          /* 0.01 V */
} ECAT_MOTOR_TX_PDO;

#define ECAT_STATUS_OP_ACTIVE       ((UINT16)(1U << 0))
#define ECAT_STATUS_FOC_READY       ((UINT16)(1U << 1))
#define ECAT_STATUS_STAGE1_INHIBIT  ((UINT16)(1U << 2))
#define ECAT_STATUS_FAULT           ((UINT16)(1U << 3))
#define ECAT_STATUS_ENCODER_VALID   ((UINT16)(1U << 4))

#endif //_LAN9252__SPI__SLAVE__IO_H_

//include custom application object dictionary 
#include "LAN9252_SPI_SLAVE_IOObjects.h"


#if defined(_LAN9252__SPI__SLAVE__IO_) && (_LAN9252__SPI__SLAVE__IO_ == 1)
    #define PROTO
#else
    #define PROTO extern
#endif


PROTO void APPL_Application(void);
#if EXPLICIT_DEVICE_ID
PROTO UINT16 APPL_GetDeviceID(void);
#endif

PROTO void   APPL_AckErrorInd(UINT16 stateTrans);
PROTO UINT16 APPL_StartMailboxHandler(void);
PROTO UINT16 APPL_StopMailboxHandler(void);
PROTO UINT16 APPL_StartInputHandler(UINT16 *pIntMask);
PROTO UINT16 APPL_StopInputHandler(void);
PROTO UINT16 APPL_StartOutputHandler(void);
PROTO UINT16 APPL_StopOutputHandler(void);

PROTO UINT16 APPL_GenerateMapping(UINT16 *pInputSize,UINT16 *pOutputSize);
PROTO void APPL_InputMapping(UINT16* pData);
PROTO void APPL_OutputMapping(UINT16* pData);

/* Keil Watch/debug visibility for the isolated Stage 1 PDO bridge. */
PROTO volatile ECAT_MOTOR_RX_PDO gEcatMotorRxPdo;
PROTO volatile ECAT_MOTOR_TX_PDO gEcatMotorTxPdo;
PROTO volatile UINT32 gEcatRxPdoCount;
PROTO volatile UINT8 gEcatOutputActive;

#undef PROTO
/** @}*/

