/*****< ehcill.c >*************************************************************/
/*      Copyright 2000 - 2012 Stonestreet One.                                */
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
#include <msp430.h>
#include "EHCILL.h"

#include "SS1BTPS.h"
#include "BTPSKRNL.h"

   /* eHCILL HCI_VS_HCILL_Parameters opcode.                            */
#define HCI_VS_HCILL_PARAMETERS_OGF             (HCI_COMMAND_CODE_VENDOR_SPECIFIC_DEBUG_OGF)
#define HCI_VS_HCILL_PARAMETERS_OCF             (0x12B)
#define HCI_VS_HCILL_PARAMETERS_SIZE            (5)

   /* eHCILL HCI_VS_Sleep_Mode_Configurations opcode.                   */
#define HCI_VS_SLEEP_MODE_CONFIGURATIONS_OGF    (HCI_COMMAND_CODE_VENDOR_SPECIFIC_DEBUG_OGF)
#define HCI_VS_SLEEP_MODE_CONFIGURATIONS_OCF    (0x10C)
#define HCI_VS_SLEEP_MODE_CONFIGURATIONS_SIZE   (9)

   /* Macros for conversion from milliseconds to Frames (1.25ms).       */
#define MILLISECONDS_TO_FRAMES(_x)  (((float)_x)/((float)1.25))  

   /* The minimum HCILL ReTransmit Timeout we will allow. This must be  */
   /* enough to allow us to process the Sleep Indication and Respond.   */
#define HCILL_MIN_RETRANSMIT_TIMEOUT (10)

   /* The minimum HCILL Inactivity Timeout. Must be large enough to     */
   /* attempt a host wakeup in 1 thread and have another task process   */
   /* the response (the RxThread) and then switch back to write the     */
   /* response.                                                         */
#define HCILL_MIN_INACTIVITY_TIMEOUT (10)

   /* The maximum size needed for return from HCI_Send_Raw.             */
#define RETURN_BUFFER_SIZE           (2)  

   /* The following MACRO is a utility MACRO that is used to safely     */
   /* re-enable interrupts if they were disabled when calling           */
   /* Critical Enter.                                                   */
#define CriticalExit(_Flags)  \
{                             \
   if((_Flags))               \
     __enable_interrupt();    \
}

   /* Local Function Prototypes.                                        */
static int IsHCILLCharacter(char UART_Character);
static void CriticalEnter(int *Flags);

   /* Internal Variables to this Module (Remember that all variables    */
   /* declared static are initialized to 0 automatically by the         */
   /* compiler as part of standard C/C++).                              */
static HCILL_State_t         HCILL_State = hsAwake;
static volatile int          HCILL_Lock;

   /* The following returns a boolean TRUE if the passed in character   */
   /* is an HCILL character and FALSE otherwise.                        */
static int IsHCILLCharacter(char UART_Character)
{
   return (( (UART_Character == HCILL_GO_TO_SLEEP_IND) 
             || (UART_Character == HCILL_GO_TO_SLEEP_ACK)
             || (UART_Character == HCILL_WAKE_UP_IND)
             || (UART_Character == HCILL_WAKE_UP_ACK)
            ));
}

   /* The following function is a utility function used to safely enter */
   /* a critical section without causing interrupts to be prematurely   */
   /* entered if the critical section is occuring with interrupts       */
   /* already disabled.                                                 */
   /* * NOTE * Since this is an internal function no check is done on   */
   /*          the parameters.                                          */
static inline void CriticalEnter(int *Flags)
{
   *Flags = 0;

   if(__get_interrupt_state() & GIE)
   {
      *Flags = 1;
      __disable_interrupt();
      return;
   }
}

   /* This function is called to initialize the HCILL state machine. It */
   /* should only be called when the UART is configured.                */
void HCILL_Init(void)
{
   HCILL_State  = hsAwake;
   HCILL_Lock   = 0;
}

   /* The following function is called to free resources allocated by   */
   /* this module.                                                      */
void HCILL_DeInit(void)
{  
}

   /* This function exists to Enable or Disable HCILL Sleep Mode. The   */
   /* first parameter is the BluetoothStackID to configure the eHCILL   */
   /* parameters for. The second is the InactivityTimeout on the Uart   */
   /* in ms. If no traffic on Uart lines after this time the Controller */
   /* sends Sleep Indication. The third is the RetransmitTimeout (ms)   */
   /* for the Sleep Ind. if no Sleep Ack is received. The fourth is the */
   /* Controller RTS pulse width during Controller wakeup (us) and the  */
   /* last is whether or not to enable the deep sleep mode in the       */
   /* controller. It returns 0 on success or a negative error code.     */
int HCILL_Configure(unsigned int BluetoothStackID, Word_t InactivityTimeout, Word_t RetransmitTimeout, Boolean_t DeepSleepEnable)
{   
   int     ret_val = 0;
   int     ParameterSize;
   Byte_t  Status,Length; 
   Byte_t *ReturnBuffer;
   Byte_t *HCILL_Parameters;
   
   /* Check to make sure we have a valid Bluetooth Stack ID.            */
   if(BluetoothStackID)
   {
      /* Allocate a buffer for the return from HCI_Send_Raw.            */
      if((ReturnBuffer = BTPS_AllocateMemory(RETURN_BUFFER_SIZE)) != NULL)
      {
         ParameterSize = ((HCI_VS_SLEEP_MODE_CONFIGURATIONS_SIZE > HCI_VS_HCILL_PARAMETERS_SIZE)?HCI_VS_SLEEP_MODE_CONFIGURATIONS_SIZE:HCI_VS_HCILL_PARAMETERS_SIZE);
         
         /* Allocate a buffer for HCILL Parameters.                     */
         HCILL_Parameters = BTPS_AllocateMemory(ParameterSize);
         if(HCILL_Parameters != NULL)
         {
            if((InactivityTimeout) && (RetransmitTimeout))
            {
               /* If either Inactivity Timeout  is less than 1 frame we */
               /* will round up.                                        */
               InactivityTimeout = (InactivityTimeout < HCILL_MIN_INACTIVITY_TIMEOUT)?HCILL_MIN_INACTIVITY_TIMEOUT:InactivityTimeout;
               
               /* We will not allow the Retransmit Timeout to be less   */
               /* than the minimum we support.                          */
               RetransmitTimeout = (RetransmitTimeout < HCILL_MIN_RETRANSMIT_TIMEOUT)?HCILL_MIN_RETRANSMIT_TIMEOUT:RetransmitTimeout;
               
               Length = RETURN_BUFFER_SIZE;
               ASSIGN_HOST_WORD_TO_LITTLE_ENDIAN_UNALIGNED_WORD(&(HCILL_Parameters[0]),MILLISECONDS_TO_FRAMES(InactivityTimeout));
               ASSIGN_HOST_WORD_TO_LITTLE_ENDIAN_UNALIGNED_WORD(&(HCILL_Parameters[2]),MILLISECONDS_TO_FRAMES(RetransmitTimeout));
               ASSIGN_HOST_BYTE_TO_LITTLE_ENDIAN_UNALIGNED_BYTE(&(HCILL_Parameters[4]),EHCILL_CTS_PULSE_WITDH_US);    
               
               /* Send out command to set HCILL Mode Parameters.        */
               ret_val = HCI_Send_Raw_Command(BluetoothStackID,HCI_VS_HCILL_PARAMETERS_OGF,HCI_VS_HCILL_PARAMETERS_OCF,HCI_VS_HCILL_PARAMETERS_SIZE,HCILL_Parameters,&Status,&Length,ReturnBuffer,TRUE);
               
               /* Determine if the command was executed successfully.   */
               if(!ret_val)
               {
                  if(Length == 1)
                  {
                     if(ReturnBuffer[0])
                        ret_val = -ReturnBuffer[0];
                  }
                  else
                     ret_val = -1;
               }
            }
            
            /* If we successfully set the HCILL Procol Parameters then  */
            /* we will enable/disable the protocol.                     */
            if(ret_val >= 0)
            {
               Length = RETURN_BUFFER_SIZE;
               
               /* Assign the command to enable HCILL mode.              */
               ASSIGN_HOST_BYTE_TO_LITTLE_ENDIAN_UNALIGNED_BYTE(&(HCILL_Parameters[0]),1);
               ASSIGN_HOST_BYTE_TO_LITTLE_ENDIAN_UNALIGNED_BYTE(&(HCILL_Parameters[1]),((DeepSleepEnable == TRUE)?1:0));
               ASSIGN_HOST_BYTE_TO_LITTLE_ENDIAN_UNALIGNED_BYTE(&(HCILL_Parameters[2]),0);
               ASSIGN_HOST_BYTE_TO_LITTLE_ENDIAN_UNALIGNED_BYTE(&(HCILL_Parameters[3]),0xFF);
               ASSIGN_HOST_BYTE_TO_LITTLE_ENDIAN_UNALIGNED_BYTE(&(HCILL_Parameters[4]),0xFF);
               ASSIGN_HOST_BYTE_TO_LITTLE_ENDIAN_UNALIGNED_BYTE(&(HCILL_Parameters[5]),0xFF);
               ASSIGN_HOST_BYTE_TO_LITTLE_ENDIAN_UNALIGNED_BYTE(&(HCILL_Parameters[6]),0xFF);
               ASSIGN_HOST_WORD_TO_LITTLE_ENDIAN_UNALIGNED_WORD(&(HCILL_Parameters[7]),0x0000);

               /* Send the command.                                     */
               ret_val = HCI_Send_Raw_Command(BluetoothStackID,HCI_VS_SLEEP_MODE_CONFIGURATIONS_OGF,HCI_VS_SLEEP_MODE_CONFIGURATIONS_OCF,HCI_VS_SLEEP_MODE_CONFIGURATIONS_SIZE,HCILL_Parameters,&Status,&Length,ReturnBuffer,TRUE);
               if(!ret_val)
               {
                  if(Length == 1)
                  {
                     if(ReturnBuffer[0])
                        ret_val = -ReturnBuffer[0];
                  }
                  else
                     ret_val = -1;
               }
            }
            
            /* Free Parameter Buffer.                                   */
            BTPS_FreeMemory(HCILL_Parameters);
         }
         /* Free the Return Buffer.                                     */
         BTPS_FreeMemory(ReturnBuffer);
      }
   }
   else
     ret_val = BTPS_ERROR_INVALID_BLUETOOTH_STACK_ID;
   
   return(ret_val);
}

   /* The following function exists to get a lock to prevent HCILL      */
   /* entry into Processor Low Power mode.                              */
void HCILL_Power_Lock(void)
{
   int Flags = 0;
   
   /* Enter a critical section to modify the HCILL Lock Count.          */
   CriticalEnter(&Flags);

   ++HCILL_Lock;

   /* Exit the critical region.                                         */
   CriticalExit(Flags);
}

   /* The following function exists to release the HCILL Power lock.    */
void HCILL_Power_Unlock(void)
{
   int Flags = 0;
   
   /* Enter a critical section to modify the HCILL Lock Count.          */
   CriticalEnter(&Flags);

   if(HCILL_Lock)
      --HCILL_Lock;
   
   /* Exit the critical region.                                         */
   CriticalExit(Flags);
}

   /* The following function is provided to allow a mechanism of        */
   /* decrementing the HCILL Power Lock by a specified Count.  This     */
   /* function has the same affect as calling HCILL_Power_Unlock Count  */
   /* times.                                                            */
void HCILL_Decrement_Power_Lock(unsigned int Count)
{
   int Flags = 0;
   
   /* Enter a critical section to modify the HCILL Lock Count.          */
   CriticalEnter(&Flags);

   if(HCILL_Lock >= Count)
      HCILL_Lock -= Count;
   else
      HCILL_Lock = 0;
   
   /* Exit the critical region.                                         */
   CriticalExit(Flags);
}

   /* The following exists to get the current HCILL_Lock count.         */
int HCILL_Get_Power_Lock_Count(void)
{
   return(HCILL_Lock);
}

   /* The following function exists to get the state of the HCILL state */
   /* machine.                                                          */
HCILL_State_t HCILL_GetState(void)
{
   return(HCILL_State);
}

   /* The following functions exists to put the state machine in a      */
   /* Controller initiated wake-up.                                     */
int HCILL_ControllerInitWakeup(void)
{
   int Flags   = 0;
   int ret_val = 0;
   
   /* Enter a critical section to modify the HCILL State.               */
   CriticalEnter(&Flags);
    
   /* Only change the state machine state if we are in sleep mode.      */
   if(HCILL_State == hsSleep)
   {
      HCILL_State = hsControllerInitWakeup;
      ret_val     = 1;
   } 
   
   /* Exit the critical region.                                         */
   CriticalExit(Flags);
   
   return (ret_val);
}

   /* The following exists to initiate a host wakeup. Returns 1 if Host */
   /* should Send HCILL_WAKEUP_IND followed by lowering RTS line.       */
int HCILL_HostInitWakeup(void)
{  
   int Flags   = 0;
   int ret_val = 0;
   
   /* Enter a critical section to modify the HCILL State.               */
   CriticalEnter(&Flags);
   
   /* Only change the state machine state if we are in sleep mode.      */
   if(HCILL_State == hsSleep)
   {
      HCILL_State = hsHostInitWakeup;
      ret_val     = 1;
   }
   
   /* Exit the critical region.                                         */
   CriticalExit(Flags);
   
   return (ret_val);
}
 
   /* The following function exists to process possible HCILL Commands  */
   /* and acknowledgements. It returns an enumerated type that          */
   /* specifies the action that is requested of the caller. It should   */
   /* only be called when the caller is sure that the characters passed */
   /* in are not BT HCI related characters.                             */
HCILL_Action_Request_t HCILL_Process_Characters(unsigned char *Buffer,int Count,unsigned int *Processed)
{
   HCILL_Action_Request_t ret_val = haNone;
   int                    Done    = 0;
   int                    Index;
   int                    Flags;      

   /* Verify that the parameters seem semi-valid before continueing.    */
   if((Buffer) && (Count) && (Processed) && (IsHCILLCharacter(Buffer[0])))
   {
      /* Loop through all characters or until we have processed as many */
      /* characters as possible.                                        */
      for(Index = 0; (Index < Count) && (!Done); Index++)
      {
         switch(Buffer[Index])
         {
            case HCILL_GO_TO_SLEEP_IND:
               /* Enter a critical section to modify the HCILL State.   */
               CriticalEnter(&Flags);

               /* Ignore, this except in the Awake state.               */
               if(HCILL_State == hsAwake)
               {
                  /* Set that we are expecting to go to sleep.          */
                  HCILL_State = hsWaitSendSleepAck;

                  /* Exit the critical region.                          */
                  CriticalExit(Flags);

                  /* We should only receive HCILL_GO_TO_SLEEP_IND when  */
                  /* we awake, however we may have Type 2 collision in  */
                  /* which case this message should be ignored. If we   */
                  /* are in a correct state to receive it the caller    */
                  /* should send a HCILL_GO_TO_SLEEP_ACK.               */
                  ret_val = haSendSleepAck;                                  
               }
               else
               {
                  /* Exit the critical region.                          */
                  CriticalExit(Flags);
               }
               
               ++(*Processed);
               break;
            case HCILL_GO_TO_SLEEP_ACK:
               /* Error, controller should never send us an this        */
               /* because we never send HCILL_GO_TO_SLEEP_IND.          */
               Done = 1;
               break;
            case HCILL_WAKE_UP_IND:
               /* Enter a critical section to modify the HCILL State.   */
               CriticalEnter(&Flags);

               /* Handle Type 1 collisions, where the Host and          */
               /* Controller both trying to wake each other up.         */
               if(HCILL_State == hsHostInitWakeup)
               {
                  /* Return to the Awake state.                         */
                  HCILL_State = hsAwake;       
                                
                  /* Exit the critical region.                          */
                  CriticalExit(Flags);

                  /* We do not need to wait on HCILL_WAKEUP_ACK, thus   */
                  /* there there is nothing to do.                      */
                  ret_val = haNone;
               }
               else
               {
                  /* The only other time we should receive this is when */
                  /* we are in the sleep state and the controller is    */
                  /* attempting to wake us up.                          */   
                  if(HCILL_State == hsControllerInitWakeup)
                  {
                     /* Exit the critical region.                       */
                     CriticalExit(Flags);

                     /* Tell the caller to send a Wakeup                */
                     /* Acknowledgement.                                */
                     ret_val = haSendWakeupAck;
                  }
                  else
                  {
                     /* Exit the critical region.                       */
                     CriticalExit(Flags);
                  }
               }
               
               ++(*Processed);
               break;
            case HCILL_WAKE_UP_ACK:
               /* Enter a critical section to modify the HCILL State.   */
               CriticalEnter(&Flags);

               /* We are now awake.                                     */
               HCILL_State = hsAwake;

               /* Exit the critical region.                             */
               CriticalExit(Flags);

               /* Nothing to do on return, we are done.                 */
               ret_val = haNone;
               
               /* Credit the character as being processed.              */
               ++(*Processed);
               
               Done = 1;
               break;
            default:
               /* If we do not understand the character as an HCILL     */
               /* Command or Acknowledgement then we are done.          */
               Done = 1;
               break;
         }
      }
   }
   
   return(ret_val);
}

   /* The following function exists to flag the state machine that an   */
   /* action requested in return value from HCILL_Process_Characters    */
   /* has been taken.                                                   */
int HCILL_ActionTaken(HCILL_Action_Request_t Action)
{
   int ret_val = 0;
   int Flags;

   /* Determine the next state based on the requested action that was   */
   /* just completed.                                                   */
   switch(Action)
   {
      case haSendSleepAck:
         /* Enter a critical section to modify the HCILL State.         */
         CriticalEnter(&Flags);

         /* We should only have sent a Sleep Ack when we were waiting   */
         /* to send HCILL_GO_TO_SLEEP_ACK right before entering the     */
         /* sleep state.                                                */
         if(HCILL_State == hsWaitSendSleepAck)
         {
            /* We are sleeping.                                         */
            HCILL_State = hsSleep;

            /* Exit the critical region.                                */
            CriticalExit(Flags);

            ret_val     = 1;
         }
         else
         {
            /* Exit the critical region.                                */
            CriticalExit(Flags);
         }
         break;
      case haSendWakeupAck:
         /* Enter a critical section to modify the HCILL State.         */
         CriticalEnter(&Flags);

         /* We should only be sending Wake-Up Acks if we are in         */
         /* Controller initiated wake-up state.                         */
         if(HCILL_State == hsControllerInitWakeup)
         {
            /* We have now been woken up by the controller.             */
            HCILL_State = hsAwake;

            /* Exit the critical region.                                */
            CriticalExit(Flags);

            ret_val     = 1;
         }
         else
         {
            /* Exit the critical region.                                */
            CriticalExit(Flags);
         }
         break;    
   }
   
   return(ret_val);
}
