/*****< hcitrans.c >***********************************************************/
/*      Copyright 2000 - 2012 Stonestreet One.                                */
/*      All Rights Reserved.                                                  */
/*                                                                            */
/*  UART HCITRANS - HCI Transport Layer for use with Bluetopia.               */
/*                                                                            */
/*  Author:  Rory Sledge                                                      */
/*                                                                            */
/*** MODIFICATION HISTORY *****************************************************/
/*                                                                            */
/*   mm/dd/yy  F. Lastname    Description of Modification                     */
/*   --------  -----------    ------------------------------------------------*/
/*   10/25/01  R. Sledge      Initial creation.                               */
/******************************************************************************/
#include "BTPSKRNL.h"            /* Bluetooth Kernel Protoypes/Constants.     */
#include "HCITRANS.h"            /* HCI Transport Prototypes/Constants.       */

#include "HAL.h"                 /* MSP430 Hardware Abstraction API.          */
#include "HRDWCFG.h"             /* MSP430 Exp Board Setup and Utilities      */
#include "EHCILL.h"              /* eHCILL implementation header.             */

#define TRANSPORT_ID                                             1

   /* The amount of time in MS that HCITR_COMWrite should wait for a    */
   /* response to a previously sent Wake Up Indication before timing    */
   /* out and sending it again.                                         */
#define HCITRANS_EHCILL_WAIT_WAKEUP_ACK_MS                       (200)

   /* Macro to wake up CPU from LPM.                                    */
#define LPM_EXIT()                                               LPM3_EXIT

   /* The following MACRO returns a Boolean that is TRUE if the UART    */
   /* Transmit is Active or FALSE otherwise.                            */
#define UART_TRANSMIT_ACTIVE()   (HWREG8(UartContext.UartBase + MSP430_UART_STAT_OFFSET) & 0x01)

   /* The following MACRO is used to raise RTS so that the Bluetooth    */
   /* chip will not send us any data until we are ready to receive the  */
   /* data.                                                             */
#define FLOW_OFF() (BT_DISABLE_FLOW())

   /* The following MACRO is used to re-enable RTS if necessary.  This  */
   /* MACRO will not lower RTS unless UART_CONTEXT_FLAG_FLOW_ENABLED is */
   /* set in the UartContext Flags AND (UART_CONTEXT_FLAG_RTS_HIGH AND  */
   /* UART_CONTEXT_FLAG_HCILL_FLOW_OFF) are both cleared.               */
#define FLOW_ON()                                                                                                    \
{                                                                                                                    \
   if ((UartContext.Flags & (UART_CONTEXT_FLAG_FLOW_ENABLED | UART_CONTEXT_FLAG_RTS_HIGH | UART_CONTEXT_FLAG_HCILL_FLOW_OFF)) == UART_CONTEXT_FLAG_FLOW_ENABLED)  \
      BT_ENABLE_FLOW();                                                                                              \
}

   /* Macros to change the interrupt state.                             */
#define DISABLE_INTERRUPTS() \
{                            \
   __disable_interrupt();    \
}

#define ENABLE_INTERRUPTS()  \
{                            \
   __enable_interrupt();     \
}

#define DEFAULT_INPUT_BUFFER_SIZE                                128
#define DEFAULT_OUTPUT_BUFFER_SIZE                               64
#define XOFF_LIMIT                                               32
#define XON_LIMIT                                                128

#define UART_CONTEXT_FLAG_OPEN_STATE                             0x0001
#define UART_CONTEXT_FLAG_HCILL_FLOW_OFF                         0x0002
#define UART_CONTEXT_FLAG_FLOW_ENABLED                           0x0004
#define UART_CONTEXT_FLAG_RX_OVERRUN                             0x0008
#define UART_CONTEXT_FLAG_TRANSMIT_ENABLED                       0x0010
#define UART_CONTEXT_FLAG_TX_FLOW_ENABLED                        0x0020
#define UART_CONTEXT_FLAG_TX_PRIMED                              0x0040
#define UART_CONTEXT_FLAG_RTS_HIGH                               0x0080

typedef struct _tagUartContext_t
{
   unsigned char          ID;
   unsigned long          UartBase;
   unsigned char          RxBuffer[DEFAULT_INPUT_BUFFER_SIZE];
   int                    RxBufferSize;
   volatile int           RxBytesFree;
   int                    RxInIndex;
   int                    RxOutIndex;
   int                    XOffLimit;
   int                    XOnLimit;
   unsigned char          TxBuffer[DEFAULT_OUTPUT_BUFFER_SIZE];
   int                    TxBufferSize;
   volatile int           TxBytesFree;
   int                    TxInIndex;
   int                    TxOutIndex;
   unsigned char          Flags;
   HCILL_Action_Request_t HCILL_Action;
   Byte_t                 HCILL_Byte;
} UartContext_t;

   /* Internal Variables to this Module (Remember that all variables    */
   /* declared static are initialized to 0 automatically by the         */
   /* compiler as part of standard C/C++).                              */
static UartContext_t  UartContext;
static unsigned int   HCITransportOpen;

   /* COM Data Callback Function and Callback Parameter information.    */
static HCITR_COMDataCallback_t _COMDataCallback;
static unsigned long           _COMCallbackParameter;

   /* Local Function Prototypes.                                        */
static void FlushRxFIFO(unsigned long Base);
static void TxTransmit(void);
static void RxProcess(void);
static void WakupController(void);
static void SentHCILLCharacter(HCILL_Action_Request_t HCILLAction, Byte_t HCILL_Byte);
static void DisableTransmitter(void);
static void EnableTransmitter(void);
static void LoadTransmitBuffer(unsigned int Length, unsigned char *Buffer);

   /* The following function is used to unload all of the characters in */
   /* the receive FIFO.                                                 */
static void FlushRxFIFO(unsigned long Base)
{
   unsigned long Dummy;
   /* WHILE FIFO NOT EMPTY.                                             */
   while(!UARTReceiveBufferEmpty(Base))
   {
      /* Remove the character from the FIFO.                            */
      Dummy = UARTReceiveBufferReg(Base);
      if(Dummy)
         Dummy = 0;
   }
}

   /* The following function, which should be called with interrupts    */
   /* disabled, exists to transmit a character and update the Tx Buffer */
   /* Counts.                                                           */
static void TxTransmit(void)
{
   /* if Tx Flow is Enabled (CTS is low) and the Tx Buffer is not empty */
   /* send a character and update the counts.                           */
   if((UartContext.Flags & UART_CONTEXT_FLAG_TX_FLOW_ENABLED) && (UartContext.TxBytesFree != UartContext.TxBufferSize))
   {
      /* Load the Tx Buffer.                                            */
      UARTTransmitBufferReg(UartContext.UartBase) = UartContext.TxBuffer[UartContext.TxOutIndex];

      /* Update the circular buffer counts.                             */
      UartContext.TxBytesFree++;
      UartContext.TxOutIndex++;

      /* Check if we need to wrap the buffer.                           */
      if(UartContext.TxOutIndex >= UartContext.TxBufferSize)
         UartContext.TxOutIndex = 0;

      /* The UART Tx side is now primed.                                */
      UartContext.Flags |= (UART_CONTEXT_FLAG_TX_PRIMED);
   }
   else
   {
      /* Disable the transmit interrupt.                                */
      UARTIntDisableTransmit(UartContext.UartBase);

      /* The Tx Transmitter is no longer primed.                        */
      UartContext.Flags &= (~UART_CONTEXT_FLAG_TX_PRIMED);
   }
}

   /* The following function is the Interrupt Service Routine for the   */
   /* UART RX interrupt.                                                */
#pragma vector=BT_UART_IV
__interrupt void UartInterrupt(void)
{
   Word_t        VectorRegister;
   volatile char Dummy;

   /* Read the Vector register once to determine the cause of the       */
   /* interrupt.                                                        */
   VectorRegister = BT_UART_IVR;

   /* Determine the cause of the interrupt (Rx or Tx).                  */
   if(VectorRegister == USCI_UCRXIFG)
   {
      /* Check to see if there is buffer space to receive the data.     */
      if(UartContext.RxBytesFree)
      {
         /* check to see if an overrun occured.                         */
         if(HWREG8(UartContext.UartBase + MSP430_UART_STAT_OFFSET) & 0xEC)
            HAL_ConsoleWrite(1, "?");

         /* Read the character from the UART Receive Buf.               */
         UartContext.RxBuffer[UartContext.RxInIndex++] = UARTReceiveBufferReg(UartContext.UartBase);

         /* Credit the received character.                              */
         --(UartContext.RxBytesFree);

         /* Check to see if we have reached the end of the Buffer and   */
         /* need to loop back to the beginning.                         */
         if(UartContext.RxInIndex >= UartContext.RxBufferSize)
            UartContext.RxInIndex = 0;

         /* Check to see if we need to Disable Rx Flow                  */
         /* RxThread will re-enable flow control when it is possible    */
         if((UartContext.Flags & UART_CONTEXT_FLAG_FLOW_ENABLED) && (UartContext.RxBytesFree <= UartContext.XOffLimit))
         {
            /* if Flow is Enabled then disable it                       */
            UartContext.Flags &= (~UART_CONTEXT_FLAG_FLOW_ENABLED);
            FLOW_OFF();
         }
      }
      else
      {
         /* We have data in the FIFO, but no place to put the data,     */
         /* so will will have to flush the FIFO and discard the data.   */
         Dummy = UARTReceiveBufferReg(UartContext.UartBase);

         /* Flag that we have encountered an RX Overrun.                */
         /* Also Disable Rx Flow.                                       */
         UartContext.Flags |= UART_CONTEXT_FLAG_RX_OVERRUN;
         UartContext.Flags &= (~UART_CONTEXT_FLAG_FLOW_ENABLED);
         FLOW_OFF();
      }
   }
   else
   {
      if(VectorRegister == USCI_UCTXIFG)
      {
         /* Process the Transmit Empty Interrupt.                       */
         TxTransmit();
      }
   }

   /* Exit from LPM if necessary (this statement will have no effect if */
   /* we are not currently in low power mode).                          */
   LPM_EXIT();
}

   /* The following function is the CTS Pin Change Interrupt.           */
   /* It is called when the CTS Line changes. This routine must change  */
   /* the interrupt polarity and flag what state the CTS line is in.    */
int CtsInterrupt(void)
{
   int ret_val = 0;

   /* Check the current GPIO Interrupt Edge Polarity.                   */
   /* A negative edge polarity indicates that the CTS pin is low, this  */
   /* means that we can re-enable TX Flow and set up the DMA to send    */
   /* more data. Pos Edge Polarity means we should turn off Tx Flow and */
   /* credit and data that was sent before this happened and turn off   */
   /* the DMA Controller.                                               */
   if(BT_CTS_INT_IS_NEG_EDGE())
   {
      /* The CTS Interrupt should now be set to positive edge since CTS */
      /* is low.                                                        */
      BT_CTS_INT_POS_EDGE();

      /* Flag that we are now allowed to continue sending data.         */
      UartContext.Flags |= UART_CONTEXT_FLAG_TX_FLOW_ENABLED;

      /* If the transmitter is enabled go ahead and prime the UART if   */
      /* needed.                                                        */
      if((UartContext.Flags & UART_CONTEXT_FLAG_TRANSMIT_ENABLED) && (UartContext.TxBytesFree != UartContext.TxBufferSize))
      {
         /* Re-enable the transmit interrupt.                           */
         UARTIntEnableTransmit(UartContext.UartBase);

         /* Reprime the transmitter.                                    */
         TxTransmit();
      }

      /* If we are at the Negative edge of a wake-up pulse, indicated   */
      /* by the HCILL state being in Controller Initiated Wakeup then   */
      /* we should have exited LPM3 on the previous CTS interrupt and   */
      /* can now assume that the Uart and Rx DMA are ready to receive   */
      /* the wake-up indication command byte. The configured CTS pulse  */
      /* should be long enough to ensure that this happens. (150us      */
      /* should do)                                                     */
      if(HCILL_GetState() == hsControllerInitWakeup)
      {
         /* Flag that the RTS may be lowered, if it is raised as part of*/
         /* the eHCILL protocol.                                        */
         UartContext.Flags &= ~UART_CONTEXT_FLAG_HCILL_FLOW_OFF;

         /* Re-enable Flow (RTS low) if necessary.                      */
         FLOW_ON();
      }
   }
   else
   {
      /* Negative edge active CTS Interrupt (CTS is high).              */
      BT_CTS_INT_NEG_EDGE();

      /* Flag that we cannot transmit.                                  */
      UartContext.Flags &= (~UART_CONTEXT_FLAG_TX_FLOW_ENABLED);

      /* Flag that we are no longer primed.                             */
      UartContext.Flags &= (~UART_CONTEXT_FLAG_TX_PRIMED);

      /* Put EHCILL State machine into Controller initiated wakeup      */
      /* mode (if we are alseep, no side affects to calling this        */
      /* function if we are not in sleep mode.                          */
      if(HCILL_GetState() != hsAwake)
      {
         /* Attempt to flag Controller Initiated Wakeup. This may not   */
         /* happen if Host and Controller both attempt to perform wake  */
         /* ups at the same time.                                       */
         if (HCILL_ControllerInitWakeup())
         {

#ifdef __DISABLE_SMCLK__

            /* Request that the SMCLK stay active.                      */
            /* * NOTE * Since we are executing code the SMCLK is        */
            /*          currently active, however what this function    */
            /*          calls does is enable SMCLK requests, so that    */
            /*          when LPM3 is next entered the UART may request  */
            /*          that the clock stays active.                    */
            HAL_EnableSMCLK(HAL_PERIPHERAL_BLUETOOTH_UART);

#endif

            /* Flag to the ISR that we should exit LPM3.                */
            ret_val = 1;
         }
      }
   }

   return(ret_val);
}

   /* The following thread is used to process the data that has been    */
   /* received from the UART and placed in the receive buffer.          */
static void RxProcess(void)
{
   unsigned int MaxWrite;
   unsigned int Count;

   /* Determine the number of characters that can be delivered.         */
   Count = (UartContext.RxBufferSize - UartContext.RxBytesFree);
   if(Count)
   {
      /* Determine the maximum number of characters that we can send    */
      /* before we reach the end of the buffer.  We need to process     */
      /* the smaller of the max characters of the number of             */
      /* characters that are in the buffer.                             */
      MaxWrite = (UartContext.RxBufferSize-UartContext.RxOutIndex);
      Count    = (MaxWrite < Count)?MaxWrite:Count;

      /* Call the upper layer back with the data.                       */
      if((Count) && (_COMDataCallback))
         (*_COMDataCallback)(TRANSPORT_ID, Count, &UartContext.RxBuffer[UartContext.RxOutIndex], _COMCallbackParameter);

      /* Adjust the Out Index and handle any looping.                   */
      UartContext.RxOutIndex += Count;
      if(UartContext.RxOutIndex >= UartContext.RxBufferSize)
         UartContext.RxOutIndex = 0;

      /* Enter a critical region to update counts and also create       */
      /* new Rx records if needed.                                      */
      DISABLE_INTERRUPTS();

      /* Credit the amount that was sent to the upper layer.            */
      UartContext.RxBytesFree += Count;

      ENABLE_INTERRUPTS();

      if((!(UartContext.Flags & UART_CONTEXT_FLAG_FLOW_ENABLED)) && (UartContext.RxBytesFree > UartContext.XOnLimit))
      {
         DISABLE_INTERRUPTS();

         /* Flag that flow is re-enabled.                               */
         UartContext.Flags |= UART_CONTEXT_FLAG_FLOW_ENABLED;

         ENABLE_INTERRUPTS();

         /* Re-enable flow                                              */
         FLOW_ON();
      }
   }
   else
   {
      /* Check to see if we have an HCILL byte that we need to send out,*/
      /* however only send it out if nothing is currently pending.      */
      if(UartContext.HCILL_Action != haNone)
      {
         /* If there are no characters currently being transmitted then */
         /* go ahead and send the requested byte.  If a character is    */
         /* currently being sent we will not send anything AND we will  */
         /* wait for the re-transmission.                               */
         if(!UART_TRANSMIT_ACTIVE())
         {
            UARTTransmitBufferReg(UartContext.UartBase) = UartContext.HCILL_Byte;
            SentHCILLCharacter(UartContext.HCILL_Action, UartContext.HCILL_Byte);
            UartContext.HCILL_Action = haNone;

            /* Wait for the transmission to complete before exiting.    */
            while(UART_TRANSMIT_ACTIVE())
               ;
         }
      }
   }
}

   /* Perform a host wakeup procedure.                                  */
static void WakupController(void)
{
   HCILL_State_t HCILL_State;
   unsigned long ElapsedTicks;
   unsigned long CurrentTickCount;
   unsigned long PreviousTickCount;
   Boolean_t     Done    = FALSE;
   Boolean_t     Timeout = FALSE;
   Byte_t        Temp;

   /* Atomically lock the HCILL State Machine into Host Initiated       */
   /* Wakeup.                                                           */
   if((HCITransportOpen) && (HCILL_HostInitWakeup()))
   {
      /* Load the byte into the transmit buffer.                        */
      Temp = HCILL_WAKE_UP_IND;
      LoadTransmitBuffer(1, &Temp);
	
      /* Signal that RTS state may now be toggled.                      */
      DISABLE_INTERRUPTS();
      UartContext.Flags &= (~UART_CONTEXT_FLAG_HCILL_FLOW_OFF);
      ENABLE_INTERRUPTS();

      /* Lower RTS so that the controller may respond.                  */
      FLOW_ON();

      /* Loop and process characters received from the controller.      */
      PreviousTickCount = BTPS_GetTickCount();
      Timeout           = FALSE;
      while((!Done) && (!Timeout))
      {
         /* call RxProcess to get the Wake-Up Acknowledgement.          */
         RxProcess();

         /* First, let's calculate the Elapsed Number of Ticks          */
         /* that have occurred since the last time through this         */
         /* loop (taking into account the possibility that the          */
         /* Tick Counter could have wrapped).                           */
         CurrentTickCount = BTPS_GetTickCount();

         ElapsedTicks = CurrentTickCount - PreviousTickCount;
         if(ElapsedTicks & 0x80000000)
            ElapsedTicks = CurrentTickCount + (0xFFFFFFFF - PreviousTickCount) + 1;

         /* Check the state to see if we have been woken.               */
         HCILL_State = HCILL_GetState();
         if(HCILL_State == hsAwake)
            Done = TRUE;
         else
         {
            /* Check to see if we have timed out or if we should delay  */
            /* and check for a response.                                */
            if (ElapsedTicks >= HCITRANS_EHCILL_WAIT_WAKEUP_ACK_MS)
               Timeout = TRUE;
            else
               BTPS_Delay(2);
         }
      }
   }
}

   /* The following function is responsible for taking the appropriate  */
   /* action when an eHCILL character has been sent to the controller.  */
static inline void SentHCILLCharacter(HCILL_Action_Request_t HCILLAction, Byte_t HCILL_Byte)
{
#ifdef __DISABLE_SMCLK__

   /* Disable the SMCLK Request if we are going to sleep (and no other  */
   /* module is using it).                                              */
   if(HCILL_Byte == HCILL_GO_TO_SLEEP_ACK)
   {
      /* Enter a critical section and signal to the hardware layer that */
      /* this UART no longer needs the clock.                           */
      /* * NOTE * Since we are executing code the SMCLK is currently    */
      /*          active, however what this function calls does is      */
      /*          disable SMCLK requests, so that when LPM3 is next     */
      /*          entered the UART cannot request that the clock does   */
      /*          not stays active when it shouldn't.                   */
      while(UART_TRANSMIT_ACTIVE())
         ;

      DISABLE_INTERRUPTS();

      if(UartContext.TxBytesFree == UartContext.TxBufferSize)
         HAL_DisableSMCLK(HAL_PERIPHERAL_BLUETOOTH_UART);

      ENABLE_INTERRUPTS();
   }

#endif

   /* Notifiy HCILL state machine that requested action has been taken. */
   HCILL_ActionTaken(HCILLAction);
}

   /* The following function is used to disable Transmit Operation.     */
   /* * NOTE * This function MUST be called with interrupts DISABLED.   */
static void DisableTransmitter(void)
{
   /* Dis-able the transmit functionality.                              */
   UartContext.Flags &= ~UART_CONTEXT_FLAG_TRANSMIT_ENABLED;

   /* Disable the Transmit Interrupt.                                   */
   UARTIntDisableTransmit(UartContext.UartBase);
}

   /* The following function is used to re-enable Transmit Operation.   */
   /* * NOTE * This function MUST be called with interrupts DISABLED.   */
static void EnableTransmitter(void)
{
   /* Re-enable the transmit functionality.                             */
   UartContext.Flags |= UART_CONTEXT_FLAG_TRANSMIT_ENABLED;

   /* Check to see if we should reprime the transmitter.  We will only  */
   /* do this if CTS is low.                                            */
   if(UartContext.Flags & UART_CONTEXT_FLAG_TX_FLOW_ENABLED)
   {
      /* Re-enable the transmit interrupt.                              */
      UARTIntEnableTransmit(UartContext.UartBase);

      /* Reprime the transmitter.                                       */
      TxTransmit();
   }
}

   /* The following function is responsible for loading the Transmit    */
   /* Buffer with characters to transmit on the UART.  This function    */
   /* will spin until a spot is found in the Transmit Buffer for the    */
   /* requested characters and it will return the Index of the first    */
   /* character in the stream into the final parameter to this function.*/
static void LoadTransmitBuffer(unsigned int Length, unsigned char *Buffer)
{
   unsigned int Count;

   /* Process all of the data.                                          */
   while(Length)
   {
      /* Loop until space becomes available in the Tx Buffer            */
      while (UartContext.TxBytesFree <= 0)
         ;

      /* The data may have to be copied in 2 phases.  Calculate the     */
      /* number of character that can be placed in the buffer before the*/
      /* buffer must be wrapped.                                        */
      Count = UartContext.TxBufferSize-UartContext.TxInIndex;

      /* Make sure we dont copy over data waiting to be sent.           */
      Count = (UartContext.TxBytesFree < Count)?(UartContext.TxBytesFree):(Count);

      /* Next make sure we arent trying to copy greater than what we are*/
      /* given.                                                         */
      Count = (Count > Length)?Length:Count;
      BTPS_MemCopy(&(UartContext.TxBuffer[UartContext.TxInIndex]), Buffer, Count);

      /* Update the number of free bytes in the buffer.  Since this     */
      /* count can also be updated in the interrupt routine, we will    */
      /* have have to update this with interrupts disabled.             */
      DISABLE_INTERRUPTS();
      UartContext.TxBytesFree -= Count;
      ENABLE_INTERRUPTS();

      /* Adjust the count and index values.                             */
      Buffer                += Count;
      Length                -= Count;
      UartContext.TxInIndex += Count;
      if(UartContext.TxInIndex >= UartContext.TxBufferSize)
         UartContext.TxInIndex = 0;

      /* Check to see if we need to prime the transmitter.  The Tx      */
      /* Interrupt will flag that that it is disabled if thinks that    */
      /* TxBytesFree == TxBufferSize, re-enable if it is necessary.     */
      if(((UartContext.Flags & (UART_CONTEXT_FLAG_TRANSMIT_ENABLED | UART_CONTEXT_FLAG_TX_FLOW_ENABLED)) == (UART_CONTEXT_FLAG_TRANSMIT_ENABLED | UART_CONTEXT_FLAG_TX_FLOW_ENABLED)) && (!(UartContext.Flags & UART_CONTEXT_FLAG_TX_PRIMED)))
      {
         /* Start sending data to the Uart Transmit FIFO.               */
         DISABLE_INTERRUPTS();

         /* Enable the transmit interrupt.                              */
         UARTIntEnableTransmit(UartContext.UartBase);

         /* Prime the transmitter.                                      */
         TxTransmit();

         ENABLE_INTERRUPTS();
      }
   }
}

   /* The following function is responsible for opening the HCI         */
   /* Transport layer that will be used by Bluetopia to send and receive*/
   /* COM (Serial) data.  This function must be successfully issued in  */
   /* order for Bluetopia to function.  This function accepts as its    */
   /* parameter the HCI COM Transport COM Information that is to be used*/
   /* to open the port.  The final two parameters specify the HCI       */
   /* Transport Data Callback and Callback Parameter (respectively) that*/
   /* is to be called when data is received from the UART.  A successful*/
   /* call to this function will return a non-zero, positive value which*/
   /* specifies the HCITransportID that is used with the remaining      */
   /* transport functions in this module.  This function returns a      */
   /* negative return value to signify an error.                        */
int BTPSAPI HCITR_COMOpen(HCI_COMMDriverInformation_t *COMMDriverInformation, HCITR_COMDataCallback_t COMDataCallback, unsigned long CallbackParameter)
{
   int ret_val;
   volatile char dummy = 0;

   /* First, make sure that the port is not already open and make sure  */
   /* that valid COMM Driver Information was specified.                 */
   if((!HCITransportOpen) && (COMMDriverInformation) && (COMDataCallback))
   {
      /* Initialize the return value for success.                       */
      ret_val               = TRANSPORT_ID;

      /* Note the COM Callback information.                             */
      _COMDataCallback      = COMDataCallback;
      _COMCallbackParameter = CallbackParameter;

      /* Try to Open the port for Reading/Writing.                      */
      BTPS_MemInitialize(&UartContext, 0, sizeof(UartContext_t));

      UartContext.UartBase     = BT_UART_MODULE_BASE;
      UartContext.ID           = 1;
      UartContext.RxBufferSize = DEFAULT_INPUT_BUFFER_SIZE;
      UartContext.RxBytesFree  = DEFAULT_INPUT_BUFFER_SIZE;
      UartContext.XOffLimit    = XOFF_LIMIT;
      UartContext.XOnLimit     = XON_LIMIT;
      UartContext.TxBufferSize = DEFAULT_OUTPUT_BUFFER_SIZE;
      UartContext.TxBytesFree  = DEFAULT_OUTPUT_BUFFER_SIZE;
      UartContext.HCILL_Action = haNone;

      /* Check to see if this is the first time that the port has been  */
      /* opened.                                                        */
      if(!HCITransportOpen)
      {
         /* Configure the Bluetooth Slow Clock.                         */
         BT_CONFIG_SLOW_CLOCK();

         /* Configure the TXD and RXD pins as UART peripheral pins.     */
         BT_CONFIG_UART_PINS();

         /* configures the RTS and CTS lines.                           */
         BT_CONFIG_RTS_PIN();
         BT_CONFIG_CTS_PIN();

         /* disable Flow through Local RTS line.                        */
         FLOW_OFF();

         /* configure the Device Reset line                             */
         BT_CONFIG_RESET();

         /* Set the Baud rate up.                                       */
         HAL_CommConfigure(UartContext.UartBase, COMMDriverInformation->BaudRate, (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE));

         /* Disable Tx Flow, later we will check RTS and see if we      */
         /* should enable it, but enable our receive flow.              */
         DISABLE_INTERRUPTS();
         UartContext.Flags &= (~UART_CONTEXT_FLAG_TX_FLOW_ENABLED);
         UartContext.Flags |= UART_CONTEXT_FLAG_FLOW_ENABLED;
         ENABLE_INTERRUPTS();

         /* Bring the Bluetooth Device out of Reset.                    */
         BT_DEVICE_RESET();
         BTPS_Delay(10);
         BT_DEVICE_UNRESET();

         /* Bring CTS Line Low to Indicate that we are ready to receive.*/
         FLOW_ON();

         /* Check to see if we need to enable Tx Flow.                  */
         if(BT_CTS_READ())
         {
            /* CTS is High so we cannot send data at this time. We will */
            /* configure the CTS Interrupt to be Negative Edge Active.  */
            DISABLE_INTERRUPTS();
            UartContext.Flags &= (~UART_CONTEXT_FLAG_TX_FLOW_ENABLED);
            BT_CTS_INT_NEG_EDGE();
            ENABLE_INTERRUPTS();
         }
         else
         {
            /* CTS is low and ergo we may send data to the controller.  */
            /* The CTS interrupt will be set to fire on the Positive    */
            /* Edge.                                                    */
            DISABLE_INTERRUPTS();
            UartContext.Flags |= (UART_CONTEXT_FLAG_TX_FLOW_ENABLED);
            BT_CTS_INT_POS_EDGE();
            ENABLE_INTERRUPTS();
         }

         /* Clear any data that is in the Buffer.                       */
         FlushRxFIFO(UartContext.UartBase);

         /* Enable Receive interrupt.                                   */
         UARTIntEnableReceive(UartContext.UartBase);

         /* Disable Transmit Interrupt.                                  */
         UARTIntDisableTransmit(UartContext.UartBase);

         DISABLE_INTERRUPTS();

         /* Flag that the UART Tx Buffer will need to be primed.        */
         UartContext.Flags &= (~UART_CONTEXT_FLAG_TX_PRIMED);

         /* Enable the transmit functionality.                          */
         UartContext.Flags |= UART_CONTEXT_FLAG_TRANSMIT_ENABLED;

         ENABLE_INTERRUPTS();

         /* Check to see if we need to delay after opening the COM Port.*/
         if(COMMDriverInformation->InitializationDelay)
            BTPS_Delay(COMMDriverInformation->InitializationDelay);

         /* Flag that the HCI Transport is open.                        */
         HCITransportOpen = 1;
      }
   }
   else
      ret_val = HCITR_ERROR_UNABLE_TO_OPEN_TRANSPORT;

   return(ret_val);
}

   /* The following function is responsible for closing the the specific*/
   /* HCI Transport layer that was opened via a successful call to the  */
   /* HCITR_COMOpen() function (specified by the first parameter).      */
   /* Bluetopia makes a call to this function whenever an either        */
   /* Bluetopia is closed, or an error occurs during initialization and */
   /* the driver has been opened (and ONLY in this case).  Once this    */
   /* function completes, the transport layer that was closed will no   */
   /* longer process received data until the transport layer is         */
   /* Re-Opened by calling the HCITR_COMOpen() function.                */
   /* * NOTE * This function *MUST* close the specified COM Port.       */
   /*          This module will then call the registered COM Data       */
   /*          Callback function with zero as the data length and NULL  */
   /*          as the data pointer.  This will signify to the HCI       */
   /*          Driver that this module is completely finished with the  */
   /*          port and information and (more importantly) that NO      */
   /*          further data callbacks will be issued.  In other words   */
   /*          the very last data callback that is issued from this     */
   /*          module *MUST* be a data callback specifying zero and NULL*/
   /*          for the data length and data buffer (respectively).      */
void BTPSAPI HCITR_COMClose(unsigned int HCITransportID)
{
   HCITR_COMDataCallback_t COMDataCallback;
   unsigned long           CallbackParameter;

   /* Check to make sure that the specified Transport ID is valid.      */
   if((HCITransportID == TRANSPORT_ID) && (HCITransportOpen))
   {
      /* Appears to be valid, go ahead and close the port.              */
      UARTIntDisableReceive(UartContext.UartBase);
      UARTIntDisableTransmit(UartContext.UartBase);

      /* Clear the UartContext Flags.                                   */
      DISABLE_INTERRUPTS();
      UartContext.Flags = 0;
      ENABLE_INTERRUPTS();

      /* Place the Bluetooth Device in Reset.                           */
      BT_DEVICE_RESET();

      /* Note the Callback information.                                 */
      COMDataCallback   = _COMDataCallback;
      CallbackParameter = _COMCallbackParameter;

      /* Flag that the HCI Transport is no longer open.                 */
      HCITransportOpen = 0;

      /* Flag that there is no callback information present.            */
      _COMDataCallback      = NULL;
      _COMCallbackParameter = 0;

      /* All finished, perform the callback to let the upper layer know */
      /* that this module will no longer issue data callbacks and is    */
      /* completely cleaned up.                                         */
      if(COMDataCallback)
         (*COMDataCallback)(HCITransportID, 0, NULL, CallbackParameter);
   }
}

   /* The following function is responsible for instructing the         */
   /* specified HCI Transport layer (first parameter) that was opened   */
   /* via a successful call to the HCITR_COMOpen() function to          */
   /* reconfigure itself with the specified information.  This          */
   /* information is completely opaque to the upper layers and is passed*/
   /* through the HCI Driver layer to the transport untouched.  It is   */
   /* the responsibility of the HCI Transport driver writer to define   */
   /* the contents of this member (or completely ignore it).            */
   /* * NOTE * This function does not close the HCI Transport specified */
   /*          by HCI Transport ID, it merely reconfigures the          */
   /*          transport.  This means that the HCI Transport specified  */
   /*          by HCI Transport ID is still valid until it is closed    */
   /*          via the HCI_COMClose() function.                         */
void BTPSAPI HCITR_COMReconfigure(unsigned int HCITransportID, HCI_Driver_Reconfigure_Data_t *DriverReconfigureData)
{
   unsigned long                 BaudRate;
   HCI_Driver_Reconfigure_Data_t DisableRxTxData;

   /* Check to make sure that the specified Transport ID is valid.      */
   if((HCITransportID == TRANSPORT_ID) && (HCITransportOpen) && (DriverReconfigureData) && (DriverReconfigureData->ReconfigureCommand == HCI_COMM_DRIVER_RECONFIGURE_DATA_COMMAND_CHANGE_PARAMETERS))
   {
      /* Disable Transmit and Receive while we change the baud rate.    */
      DisableRxTxData.ReconfigureCommand = HCI_COMM_DRIVER_DISABLE_UART_TX_RX;
      DisableRxTxData.ReconfigureData    = NULL;
      HCITR_COMReconfigure(0, &DisableRxTxData);

      /* Configure the requested baud rate.                             */
      BaudRate = *((unsigned long *)DriverReconfigureData->ReconfigureData);

      UARTIntDisableReceive(UartContext.UartBase);
      HAL_CommConfigure(UartContext.UartBase, BaudRate, (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE));
      UARTIntEnableReceive(UartContext.UartBase);

      /* Small delay to let the COM Port reconfigure itself.            */
      BTPS_Delay(1);

      /* Re-enable Transmit and Receive operation.                      */
      DisableRxTxData.ReconfigureCommand = HCI_COMM_DRIVER_DISABLE_UART_TX_RX;
      DisableRxTxData.ReconfigureData    = (void *)DWORD_SIZE;
      HCITR_COMReconfigure(0, &DisableRxTxData);
   }

   /* Check to see if there is a global reconfigure parameter.          */
   if((DriverReconfigureData) && (!HCITransportID) && (HCITransportOpen))
   {
      /* Check to see if we are being instructed to disable RX/TX.      */
      if(DriverReconfigureData->ReconfigureCommand == HCI_COMM_DRIVER_DISABLE_UART_TX_RX)
      {
         if(DriverReconfigureData->ReconfigureData)
         {
            /* Flow on.                                                 */
            DISABLE_INTERRUPTS();

            /* Clear the RTS High Flag.                                 */
            UartContext.Flags &= ~UART_CONTEXT_FLAG_RTS_HIGH;

            /* Re-enable the Transmitter.                               */
            EnableTransmitter();

            ENABLE_INTERRUPTS();

            /* Flow the Bluetooth chip on if necessary.                 */
            FLOW_ON();
         }
         else
         {
            /* We are being asked to flow off all Bluetooth UART        */
            /* Transactions.                                            */
            DISABLE_INTERRUPTS();

            /* Disable the Receiver.                                    */
            FLOW_OFF();

            /* Flag that the RTS Line should stay high.                 */
            UartContext.Flags |= UART_CONTEXT_FLAG_RTS_HIGH;

            /* Disable the Transmit Operation.                          */
            DisableTransmitter();

            ENABLE_INTERRUPTS();

            /* Wait until we have finished transmitting any bytes in the*/
            /* UART Transmit Buffer.                                    */
            while(UART_TRANSMIT_ACTIVE())
               ;
         }
      }
   }
}

   /* The following function is provided to allow a mechanism for       */
   /* modules to force the processing of incoming COM Data.             */
   /* * NOTE * This function is only applicable in device stacks that   */
   /*          are non-threaded.  This function has no effect for device*/
   /*          stacks that are operating in threaded environments.      */
void BTPSAPI HCITR_COMProcess(unsigned int HCITransportID)
{
   /* Check to make sure that the specified Transport ID is valid.      */
   if((HCITransportID == TRANSPORT_ID) && (HCITransportOpen))
      RxProcess();
}

   /* The following function is responsible for actually sending data   */
   /* through the opened HCI Transport layer (specified by the first    */
   /* parameter).  Bluetopia uses this function to send formatted HCI   */
   /* packets to the attached Bluetooth Device.  The second parameter to*/
   /* this function specifies the number of bytes pointed to by the     */
   /* third parameter that are to be sent to the Bluetooth Device.  This*/
   /* function returns a zero if the all data was transfered sucessfully*/
   /* or a negetive value if an error occurred.  This function MUST NOT */
   /* return until all of the data is sent (or an error condition       */
   /* occurs).  Bluetopia WILL NOT attempt to call this function        */
   /* repeatedly if data fails to be delivered.  This function will     */
   /* block until it has either buffered the specified data or sent all */
   /* of the specified data to the Bluetooth Device.                    */
   /* * NOTE * The type of data (Command, ACL, SCO, etc.) is NOT passed */
   /*          to this function because it is assumed that this         */
   /*          information is contained in the Data Stream being passed */
   /*          to this function.                                        */
int BTPSAPI HCITR_COMWrite(unsigned int HCITransportID, unsigned int Length, unsigned char *Buffer)
{
   int           ret_val = 0;
   HCILL_State_t State;

   /* Check to make sure that the specified Transport ID is valid and   */
   /* the output buffer appears to be valid as well.                    */
   if((HCITransportID == TRANSPORT_ID) && (HCITransportOpen) && (Length) && (Buffer))
   {
      /* Make sure eHCILL state machine is in the awake state.          */
      /* Wake up the controller chip if necessary.                      */
      State = HCILL_GetState();
      if((State == hsSleep) || (UartContext.HCILL_Action != haNone))
      {
#ifdef __DISABLE_SMCLK__

         /* Request that the SMCLK stay active.                         */
         /* * NOTE * Since we are executing code the SMCLK is currently */
         /*          active, however what this function calls does is   */
         /*          enable SMCLK requests, so that when LPM3 is next   */
         /*          entered the UART may request that the clock stays  */
         /*          active.                                            */
         DISABLE_INTERRUPTS();

         HAL_EnableSMCLK(HAL_PERIPHERAL_BLUETOOTH_UART);

         ENABLE_INTERRUPTS();

         /* Add a delay while the SMCLK stabilizes.                     */
         __delay_cycles(2500);

#endif

         /* If we have an HCILL Byte to send then go ahead and send it  */
         /* AND mark that we have sent the requested byte.              */
         if(UartContext.HCILL_Action != haNone)
         {
            /* Wait until we have finished transmitting all bytes.      */
            while(UART_TRANSMIT_ACTIVE())
               ;

            /* Since we have finished transmitting all bytes go ahead   */
            /* and transmit the HCILL byte and mark that it has been    */
            /* sent.                                                    */
            UARTTransmitBufferReg(UartContext.UartBase) = UartContext.HCILL_Byte;
            SentHCILLCharacter(UartContext.HCILL_Action, UartContext.HCILL_Byte);
            UartContext.HCILL_Action = haNone;

            /* Wait until we have transmitted this byte.                */
            while(UART_TRANSMIT_ACTIVE())
               ;
         }

         /* Only try to wake up the controller if we are still asleep   */
         /* after waiting for the UART Module Clock (SMCLK) to          */
         /* stabilize.                                                  */
         if(HCILL_GetState() == hsSleep)
            WakupController();

         /* Check the state and make sure we woke up the controller     */
         /* successfully.                                               */
         State = HCILL_GetState();
         if(State != hsAwake)
         {
            ret_val = HCITR_ERROR_WRITING_TO_PORT;
            Length  = 0;
         }
      }

      /* Load the Transmit Buffer with the selected characters.         */
      LoadTransmitBuffer(Length, Buffer);

      /* Return success to the caller.                                  */
      ret_val = 0;
   }

   return(ret_val);
}

   /* The following function is called when an invalid start of packet  */
   /* byte is received.  This function can this handle this case as     */
   /* needed.  The first parameter is the Transport ID that was         */
   /* from a successfull call to HCITR_COMWrite and the second          */
   /* parameter is the invalid start of packet byte that was received.  */
void BTPSAPI HCITR_InvalidStart_Callback(unsigned int HCITransportID, unsigned char Data)
{
   Byte_t                  HCILL_Byte;
   Boolean_t               SendByte;
   unsigned int            HCILLProcessed = 0;
   HCILL_Action_Request_t  HCILLAction    = haNone;

   /* Check to make sure that the specified Transport ID is valid and   */
   /* the Transport is currently open.                                  */
   if((HCITransportID == TRANSPORT_ID) && (HCITransportOpen))
   {
      /* attempt to process the character has an HCILL character.       */
      HCILLAction = HCILL_Process_Characters(&Data, 1, &HCILLProcessed);
      if(HCILLProcessed)
      {
         /* If the character was an HCILL character then we should take */
         /* the appropriate action.                                     */
         SendByte = FALSE;
         switch(HCILLAction)
         {
            case haSendSleepAck:
               DISABLE_INTERRUPTS();

               /* Signal that RTS state must not be toggled because of  */
               /* eHCILL.                                               */
               UartContext.Flags |= UART_CONTEXT_FLAG_HCILL_FLOW_OFF;

               ENABLE_INTERRUPTS();

               /* Controller is requesting to go into Deep Sleep mode.  */
               /* First we will pull RTS high.                          */
               FLOW_OFF();

               /* Next we will send the sleep Ack, this will put us in  */
               /* sleep mode.                                           */
               HCILL_Byte  = HCILL_GO_TO_SLEEP_ACK;
               SendByte    = TRUE;
               break;
            case haSendWakeupAck:
               /* First send out the byte the Wakeup Ack byte.          */
               /* No need to enable flow. That will be done in CTS      */
               /* interrupt routine.                                    */
               HCILL_Byte  = HCILL_WAKE_UP_ACK;
               SendByte    = TRUE;
               break;
         }

         /* Check to see if we need to send a byte to the controller.   */
         if(SendByte)
         {
            /* If there are no characters currently being transmitted   */
            /* then go ahead and send the requested byte.  If a         */
            /* character is currently being sent we will not send       */
            /* anything AND we will wait for the re-transmission.       */
            if(!UART_TRANSMIT_ACTIVE())
            {
               UARTTransmitBufferReg(UartContext.UartBase) = HCILL_Byte;
               SentHCILLCharacter(HCILLAction, HCILL_Byte);
               HCILLAction = haNone;
               HCILL_Byte  = 0;

               /* Wait for the transmission to complete before exiting.*/
               while(UART_TRANSMIT_ACTIVE())
                  ;
            }

            /* Save the Action AND the byte that we were supposed to    */
            /* send out.                                                */
            UartContext.HCILL_Action = HCILLAction;
            UartContext.HCILL_Byte   = HCILL_Byte;
         }
         else
         {
            if(!SendByte)
            {
               /* Notifiy HCILL state machine that requested action has */
               /* been taken.                                           */
               HCILL_ActionTaken(HCILLAction);
            }
         }
      }
      else
         BTPS_OutputMessage(">, 0x%02X.\n", Data);
   }
}

   /* The following function is used to determine how many Rx Bytes are */
   /* ready to be processed by the HCI Transport Layer Module.          */
unsigned int BTPSAPI HCITR_RxBytesReady(unsigned int HCITransportID)
{
   unsigned int Count = 0;

   if(HCITransportOpen)
      Count = UartContext.RxBufferSize - UartContext.RxBytesFree;
   
   return(Count);
}
