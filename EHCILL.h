/*****< ehcill.h >*************************************************************/
/*      Copyright 2010 - 2012 Stonestreet One.                                */
/*      All Rights Reserved.                                                  */
/*                                                                            */
/*  EHCILL - Stonestreet One EHCILL Non-Threaded implementation.              */
/*                                                                            */
/*  Author:  Tim Cook                                                         */
/*                                                                            */
/*** MODIFICATION HISTORY *****************************************************/
/*                                                                            */
/*   mm/dd/yy  F. Lastname    Description of Modification                     */
/*   --------  -----------    ------------------------------------------------*/
/*   08/11/10  T. Cook        Initial creation.                               */
/******************************************************************************/
#ifndef __EHCILLH__
#define __EHCILLH__

#include "BTTypes.h"            /* Bluetooth Type Definitions/Constants.      */

   /* HCILL Configuration defines.                                      */
#define EHCILL_CTS_PULSE_WITDH_US              (0xFF)
#define EHCILL_DEFAULT_INACTIVITY_TIMEOUT_S    (3000)
#define EHCILL_DEFAULT_RETRANSMIT_TIMEOUT_S    (1000)

   /* HCILL Command Opcodes.                                            */
#define HCILL_GO_TO_SLEEP_IND                  (0x30)
#define HCILL_GO_TO_SLEEP_ACK                  (0x31)
#define HCILL_WAKE_UP_IND                      (0x32)
#define HCILL_WAKE_UP_ACK                      (0x33)

   /* This enum contains the different values that the HCILL state      */
   /* machine may be in.                                                */
typedef enum 
{ 
   hsAwake=0,
   hsWaitSendSleepAck,
   hsHostInitWakeup,
   hsControllerInitWakeup,
   hsSleep
} HCILL_State_t;

   /* This enum provides a way for the HCILL state machine to request   */
   /* that the caller perform some action.                              */
typedef enum
{
   haNone,
   haSendSleepAck,
   haSendWakeupAck
} HCILL_Action_Request_t;

   /* This function is called to initialize the HCILL state machine.  It*/
   /* should only be called when the UART is configured.                */
void HCILL_Init(void);

   /* The following function is called to free resources allocated by   */
   /* this module.                                                      */
void HCILL_DeInit(void);

   /* This function exists to Enable or Disable HCILL Sleep Mode.  The  */
   /* first parameter is the BluetoothStackID to configure the eHCILL   */
   /* parameters for.  The second is the InactivityTimeout on the Uart  */
   /* in ms.  If no traffic on Uart lines after this time the Controller*/
   /* sends Sleep Indication.  The third is the RetransmitTimeout (ms)  */
   /* for the Sleep Ind if no Sleep Ack is received.  The fourth is the */
   /* Controller RTS pulse width during Controller wakeup (us) and the  */
   /* last is whether or not to enable the deep sleep mode in the       */
   /* controller.  Returns 0 or negative error code.                    */
int HCILL_Configure(unsigned int BluetoothStackID, Word_t InactivityTimeout, Word_t RetransmitTimeout, Boolean_t DeepSleepEnable);

   /* The following function exists to get a lock to prevent HCILL entry*/
   /* into Processor Low Power mode.                                    */
void HCILL_Power_Lock(void);

   /* The following function exists to release the HCILL Power lock.    */
void HCILL_Power_Unlock(void);

   /* The following function is provided to allow a mechanism of        */
   /* decrementing the HCILL Power Lock by a specified Count.  This     */
   /* function has the same affect as calling HCILL_Power_Unlock Count  */
   /* times.                                                            */
void HCILL_Decrement_Power_Lock(unsigned int Count);

   /* The following exists to get the current HCILL_Lock count.         */
int HCILL_Get_Power_Lock_Count(void);

   /* The following function exists to get the state of the HCILL state */
   /* machine.                                                          */
HCILL_State_t HCILL_GetState(void);

   /* The following functions exists to put the state machine in a      */
   /* Controller initiated wake-up.                                     */
int HCILL_ControllerInitWakeup(void);

   /* The following exists to initiate a host wakeup.  Returns 1 if Host*/
   /* should Send HCILL_WAKEUP_IND followed by lowering RTS line.       */
int HCILL_HostInitWakeup(void);

   /* The following function exists to process HCILL Commands and Acks. */
HCILL_Action_Request_t HCILL_Process_Characters(unsigned char *Buffer,int Count,unsigned int *Processed);

   /* The following function exists to flag the state machine that an   */
   /* action requested in return value from HCILL_Process_Characters has*/
   /* been taken.                                                       */
int HCILL_ActionTaken(HCILL_Action_Request_t Action);

#endif /*EHCILL_H_*/
