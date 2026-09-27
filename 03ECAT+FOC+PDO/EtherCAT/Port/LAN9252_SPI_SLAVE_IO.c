/**
\addtogroup LAN9252_SPI_SLAVE_IO LAN9252_SPI_SLAVE_IO
@{
*/

/**
\file LAN9252_SPI_SLAVE_IO.c
\brief Implementation

\version 1.0.0.11
*/


/*-----------------------------------------------------------------------------------------
------
------    Includes
------
-----------------------------------------------------------------------------------------*/
#include "ecat_def.h"

#include "applInterface.h"
#include "main.h"
#include "FOC.h"
#include "FOC_MotorStatus.h"

#define _LAN9252__SPI__SLAVE__IO_ 1
#include "LAN9252_SPI_SLAVE_IO.h"
#undef _LAN9252__SPI__SLAVE__IO_
/*--------------------------------------------------------------------------------------
------
------    local types and defines
------
--------------------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------------------
------
------    local variables and constants
------
 -----------------------------------------------------------------------------------------*/

volatile ECAT_MOTOR_RX_PDO gEcatMotorRxPdo = {0};
volatile ECAT_MOTOR_TX_PDO gEcatMotorTxPdo = {0};
volatile UINT32 gEcatRxPdoCount = 0U;
volatile UINT8 gEcatOutputActive = 0U;

static UINT32 PDO_ReadU32(UINT16 **pData)
{
    UINT16 lowWord = SWAPWORD((*pData)[0]);
    UINT16 highWord = SWAPWORD((*pData)[1]);

    *pData += 2;
    return ((UINT32)highWord << 16) | (UINT32)lowWord;
}

static void PDO_WriteU32(UINT16 **pData, UINT32 value)
{
    *(*pData)++ = SWAPWORD((UINT16)(value & 0xFFFFU));
    *(*pData)++ = SWAPWORD((UINT16)(value >> 16));
}

static INT32 PDO_FloatToI32(float value, float scale)
{
    float scaled = value * scale;

    if (scaled >= 2147483520.0f)
    {
        return (INT32)0x7FFFFFFF;
    }
    if (scaled <= -2147483648.0f)
    {
        return (INT32)0x80000000;
    }
    return (INT32)scaled;
}

static INT16 PDO_FloatToI16(float value, float scale)
{
    float scaled = value * scale;

    if (scaled >= 32767.0f)
    {
        return (INT16)32767;
    }
    if (scaled <= -32768.0f)
    {
        return (INT16)-32768;
    }
    return (INT16)scaled;
}

static UINT16 PDO_PositiveFloatToU16(float value, float scale)
{
    float scaled = value * scale;

    if (scaled <= 0.0f)
    {
        return 0U;
    }
    if (scaled >= 65535.0f)
    {
        return 65535U;
    }
    return (UINT16)scaled;
}

/*-----------------------------------------------------------------------------------------
------
------    application specific functions
------
-----------------------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------------------
------
------    generic functions
------
-----------------------------------------------------------------------------------------*/

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \brief    The function is called when an error state was acknowledged by the master

*////////////////////////////////////////////////////////////////////////////////////////

void    APPL_AckErrorInd(UINT16 stateTrans)
{

}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return    AL Status Code (see ecatslv.h ALSTATUSCODE_....)

 \brief    The function is called in the state transition from INIT to PREOP when
             all general settings were checked to start the mailbox handler. This function
             informs the application about the state transition, the application can refuse
             the state transition when returning an AL Status error code.
            The return code NOERROR_INWORK can be used, if the application cannot confirm
            the state transition immediately, in that case this function will be called cyclically
            until a value unequal NOERROR_INWORK is returned

*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StartMailboxHandler(void)
{
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return     0, NOERROR_INWORK

 \brief    The function is called in the state transition from PREEOP to INIT
             to stop the mailbox handler. This functions informs the application
             about the state transition, the application cannot refuse
             the state transition.

*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StopMailboxHandler(void)
{
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \param    pIntMask    pointer to the AL Event Mask which will be written to the AL event Mask
                        register (0x204) when this function is succeeded. The event mask can be adapted
                        in this function
 \return    AL Status Code (see ecatslv.h ALSTATUSCODE_....)

 \brief    The function is called in the state transition from PREOP to SAFEOP when
           all general settings were checked to start the input handler. This function
           informs the application about the state transition, the application can refuse
           the state transition when returning an AL Status error code.
           The return code NOERROR_INWORK can be used, if the application cannot confirm
           the state transition immediately, in that case the application need to be complete 
           the transition by calling ECAT_StateChange.
*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StartInputHandler(UINT16 *pIntMask)
{
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return     0, NOERROR_INWORK

 \brief    The function is called in the state transition from SAFEOP to PREEOP
             to stop the input handler. This functions informs the application
             about the state transition, the application cannot refuse
             the state transition.

*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StopInputHandler(void)
{
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return    AL Status Code (see ecatslv.h ALSTATUSCODE_....)

 \brief    The function is called in the state transition from SAFEOP to OP when
             all general settings were checked to start the output handler. This function
             informs the application about the state transition, the application can refuse
             the state transition when returning an AL Status error code.
           The return code NOERROR_INWORK can be used, if the application cannot confirm
           the state transition immediately, in that case the application need to be complete 
           the transition by calling ECAT_StateChange.
*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StartOutputHandler(void)
{
    gEcatOutputActive = 1U;
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return     0, NOERROR_INWORK

 \brief    The function is called in the state transition from OP to SAFEOP
             to stop the output handler. This functions informs the application
             about the state transition, the application cannot refuse
             the state transition.

*////////////////////////////////////////////////////////////////////////////////////////

UINT16 APPL_StopOutputHandler(void)
{
    gEcatOutputActive = 0U;
    gEcatMotorRxPdo.controlWord = 0U;
    gEcatMotorRxPdo.targetVelocity = 0;
    return ALSTATUSCODE_NOERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
\return     0(ALSTATUSCODE_NOERROR), NOERROR_INWORK
\param      pInputSize  pointer to save the input process data length
\param      pOutputSize  pointer to save the output process data length

\brief    This function calculates the process data sizes from the actual SM-PDO-Assign
            and PDO mapping
*////////////////////////////////////////////////////////////////////////////////////////
UINT16 APPL_GenerateMapping(UINT16 *pInputSize,UINT16 *pOutputSize)
{
    UINT16 result = ALSTATUSCODE_NOERROR;
    UINT16 InputSize = 0;
    UINT16 OutputSize = 0;

#if COE_SUPPORTED
    UINT16 PDOAssignEntryCnt = 0;
    OBJCONST TOBJECT OBJMEM * pPDO = NULL;
    UINT16 PDOSubindex0 = 0;
    UINT32 *pPDOEntry = NULL;
    UINT16 PDOEntryCnt = 0;
   
    /*Scan object 0x1C12 RXPDO assign*/
    for(PDOAssignEntryCnt = 0; PDOAssignEntryCnt < sRxPDOassign.u16SubIndex0; PDOAssignEntryCnt++)
    {
        pPDO = OBJ_GetObjectHandle(sRxPDOassign.aEntries[PDOAssignEntryCnt]);
        if(pPDO != NULL)
        {
            PDOSubindex0 = *((UINT16 *)pPDO->pVarPtr);
            for(PDOEntryCnt = 0; PDOEntryCnt < PDOSubindex0; PDOEntryCnt++)
            {
                pPDOEntry = (UINT32 *)((UINT8 *)pPDO->pVarPtr + (OBJ_GetEntryOffset((PDOEntryCnt+1),pPDO)>>3));    //goto PDO entry
                // we increment the expected output size depending on the mapped Entry
                OutputSize += (UINT16) ((*pPDOEntry) & 0xFF);
            }
        }
        else
        {
            /*assigned PDO was not found in object dictionary. return invalid mapping*/
            OutputSize = 0;
            result = ALSTATUSCODE_INVALIDOUTPUTMAPPING;
            break;
        }
    }

    OutputSize = (OutputSize + 7) >> 3;

    if(result == 0)
    {
        /*Scan Object 0x1C13 TXPDO assign*/
        for(PDOAssignEntryCnt = 0; PDOAssignEntryCnt < sTxPDOassign.u16SubIndex0; PDOAssignEntryCnt++)
        {
            pPDO = OBJ_GetObjectHandle(sTxPDOassign.aEntries[PDOAssignEntryCnt]);
            if(pPDO != NULL)
            {
                PDOSubindex0 = *((UINT16 *)pPDO->pVarPtr);
                for(PDOEntryCnt = 0; PDOEntryCnt < PDOSubindex0; PDOEntryCnt++)
                {
                    pPDOEntry = (UINT32 *)((UINT8 *)pPDO->pVarPtr + (OBJ_GetEntryOffset((PDOEntryCnt+1),pPDO)>>3));    //goto PDO entry
                    // we increment the expected output size depending on the mapped Entry
                    InputSize += (UINT16) ((*pPDOEntry) & 0xFF);
                }
            }
            else
            {
                /*assigned PDO was not found in object dictionary. return invalid mapping*/
                InputSize = 0;
                result = ALSTATUSCODE_INVALIDINPUTMAPPING;
                break;
            }
        }
    }
    InputSize = (InputSize + 7) >> 3;

#else
#if _WIN32
   #pragma message ("Warning: Define 'InputSize' and 'OutputSize'.")
#else
    #warning "Define 'InputSize' and 'OutputSize'."
#endif
#endif

    *pInputSize = InputSize;
    *pOutputSize = OutputSize;
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
\param      pData  pointer to input process data

\brief      This function will copies the inputs from the local memory to the ESC memory
            to the hardware
*////////////////////////////////////////////////////////////////////////////////////////
void APPL_InputMapping(UINT16* pData)
{
    UINT16 *pTmpData = pData;

    gTxPdoInputs0x6000.StatusWord = gEcatMotorTxPdo.statusWord;
    gTxPdoInputs0x6000.MotorStatus = gEcatMotorTxPdo.motorStatus;
    gTxPdoInputs0x6000.ActualVelocity = gEcatMotorTxPdo.actualVelocity;
    gTxPdoInputs0x6000.ActualIq = gEcatMotorTxPdo.actualIq;
    gTxPdoInputs0x6000.ActualId = gEcatMotorTxPdo.actualId;
    gTxPdoInputs0x6000.ErrorCode = gEcatMotorTxPdo.errorCode;
    gTxPdoInputs0x6000.BusVoltage = gEcatMotorTxPdo.busVoltage;

    *pTmpData++ = SWAPWORD(gTxPdoInputs0x6000.StatusWord);
    *pTmpData++ = SWAPWORD(gTxPdoInputs0x6000.MotorStatus);
    PDO_WriteU32(&pTmpData, (UINT32)gTxPdoInputs0x6000.ActualVelocity);
    *pTmpData++ = SWAPWORD((UINT16)gTxPdoInputs0x6000.ActualIq);
    *pTmpData++ = SWAPWORD((UINT16)gTxPdoInputs0x6000.ActualId);
    *pTmpData++ = SWAPWORD(gTxPdoInputs0x6000.ErrorCode);
    *pTmpData++ = SWAPWORD(gTxPdoInputs0x6000.BusVoltage);
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
\param      pData  pointer to output process data

\brief    This function will copies the outputs from the ESC memory to the local memory
            to the hardware
*////////////////////////////////////////////////////////////////////////////////////////
void APPL_OutputMapping(UINT16* pData)
{
    UINT16 *pTmpData = pData;

    gRxPdoOutputs0x7000.ControlWord = SWAPWORD(*pTmpData++);
    gRxPdoOutputs0x7000.CommandCounter = SWAPWORD(*pTmpData++);
    gRxPdoOutputs0x7000.TargetVelocity = (INT32)PDO_ReadU32(&pTmpData);
    gRxPdoOutputs0x7000.Reserved1 = SWAPWORD(*pTmpData++);
    gRxPdoOutputs0x7000.Reserved2 = SWAPWORD(*pTmpData++);
    gRxPdoOutputs0x7000.Reserved3 = PDO_ReadU32(&pTmpData);

    /* Stage 1 isolation: cache the command for PDO verification only. */
    gEcatMotorRxPdo.controlWord = gRxPdoOutputs0x7000.ControlWord;
    gEcatMotorRxPdo.commandCounter = gRxPdoOutputs0x7000.CommandCounter;
    gEcatMotorRxPdo.targetVelocity = gRxPdoOutputs0x7000.TargetVelocity;
    gEcatMotorRxPdo.reserved1 = gRxPdoOutputs0x7000.Reserved1;
    gEcatMotorRxPdo.reserved2 = gRxPdoOutputs0x7000.Reserved2;
    gEcatMotorRxPdo.reserved3 = gRxPdoOutputs0x7000.Reserved3;
    gEcatRxPdoCount++;
}

/////////////////////////////////////////////////////////////////////////////////////////
/**
\brief    This function will called from the synchronisation ISR 
            or from the mainloop if no synchronisation is supported
*////////////////////////////////////////////////////////////////////////////////////////
void APPL_Application(void)
{
    UINT16 statusWord = ECAT_STATUS_STAGE1_INHIBIT;

    if (gEcatOutputActive != 0U)
    {
        statusWord |= ECAT_STATUS_OP_ACTIVE;
    }
    if ((FOC.status >= MOTOR_STATUS_IDLE) &&
        (FOC.status < MOTOR_STATUS_OVERVOLTAGE))
    {
        statusWord |= ECAT_STATUS_FOC_READY;
    }
    if (FOC.safe.errorFlag != MOTOR_ERROR_NONE)
    {
        statusWord |= ECAT_STATUS_FAULT;
    }
    if (FOC.encoder.errorFlag == 0U)
    {
        statusWord |= ECAT_STATUS_ENCODER_VALID;
    }

    gEcatMotorTxPdo.statusWord = statusWord;
    gEcatMotorTxPdo.motorStatus = (UINT16)FOC.status;
    gEcatMotorTxPdo.actualVelocity = PDO_FloatToI32(FOC.encoder.filterVel, 1000.0f);
    gEcatMotorTxPdo.actualIq = PDO_FloatToI16(FOC.current.Real_Iq, 100.0f);
    gEcatMotorTxPdo.actualId = PDO_FloatToI16(FOC.current.Real_Id, 100.0f);
    gEcatMotorTxPdo.errorCode = (UINT16)FOC.safe.errorFlag;
    gEcatMotorTxPdo.busVoltage = PDO_PositiveFloatToU16(FOC.foc.Real_VBUS, 100.0f);
}

#if EXPLICIT_DEVICE_ID
/////////////////////////////////////////////////////////////////////////////////////////
/**
 \return    The Explicit Device ID of the EtherCAT slave

 \brief     Calculate the Explicit Device ID
*////////////////////////////////////////////////////////////////////////////////////////
UINT16 APPL_GetDeviceID(void)
{
#if _WIN32
   #pragma message ("Warning: Implement explicit Device ID latching")
#else
    /* Explicit Device ID latching is not implemented; the sample ID below is fixed. */
#endif
    /* Explicit Device 5 is expected by Explicit Device ID conformance tests*/
    return 0x5;
}
#endif



#if USE_DEFAULT_MAIN
/////////////////////////////////////////////////////////////////////////////////////////
/**

 \brief    This is the main function

*////////////////////////////////////////////////////////////////////////////////////////
#if _PIC24
int main(void)
#else
void main(void)
#endif
{
    /* initialize the Hardware and the EtherCAT Slave Controller */
#if FC1100_HW
    if(HW_Init())
    {
        HW_Release();
        return;
    }
#else
    HW_Init();
#endif
    MainInit();

    bRunApplication = TRUE;
    do
    {
        MainLoop();
        
    } while (bRunApplication == TRUE);

    HW_Release();
#if _PIC24
    return 0;
#endif
}
#endif //#if USE_DEFAULT_MAIN
/** @} */


