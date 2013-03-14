/*****< main.c >***************************************************************/
/*      Copyright 2001 - 2012 Stonestreet One.                                */
/*      All Rights Reserved.                                                  */
/*                                                                            */
/*  MAIN - Main application implementation.                                   */
/*                                                                            */
/*  Author:  Tim Cook                                                         */
/*                                                                            */
/*** MODIFICATION HISTORY *****************************************************/
/*                                                                            */
/*   mm/dd/yy  F. Lastname    Description of Modification                     */
/*   --------  -----------    ------------------------------------------------*/
/*   10/28/11  T. Cook        Initial creation.                               */
/******************************************************************************/
#include "HAL.h"                 /* Function for Hardware Abstraction.        */
#include "Main.h"                /* Main application header.                  */
#include "EHCILL.h"              /* eHCILL Implementation Header.             */

#define Display(_x)                                do { BTPS_OutputMessage _x; } while(0)

#define MAX_COMMAND_LENGTH                         (64)  /* Denotes the max   */
                                                         /* buffer size used  */
                                                         /* for user commands */
                                                         /* input via the     */
                                                         /* User Interface.   */

#define LED_TOGGLE_RATE_SUCCESS                    (500) /* The LED Toggle    */
                                                         /* rate when the demo*/
                                                         /* successfully      */
                                                         /* starts up.        */

   /* The following parameters are used when configuring HCILL Mode.    */
#define HCILL_MODE_INACTIVITY_TIMEOUT              (500)
#define HCILL_MODE_RETRANSMIT_TIMEOUT              (100)

   /* Internal Variables to this Module (Remember that all variables    */
   /* declared static are initialized to 0 automatically by the         */
   /* compiler as part of standard C/C++).                              */
static unsigned int BluetoothStackID;

   /* Application Tasks.                                                */
static void DisplayCallback(char Character);
static unsigned long GetTickCallback(void);
static void IdleFunction(void *UserParameter);
static void MainThread(void);

   /* The following function is registered with the application so that */
   /* it can display strings to the debug UART.                         */
static void DisplayCallback(char Character)
{
   HAL_ConsoleWrite(1, &Character);
}

   /* The following function is registered with the application so that */
   /* it can get the current System Tick Count.                         */
static unsigned long GetTickCallback(void)
{
   return(HAL_GetTickCount());
}



static void ButtonPollFunction(void *UserParameter)
{
	port2_poll();
}

   /* The following function is responsible for checking the idle state */
   /* and possibly entering LPM3 mode.                                  */
static void IdleFunction(void *UserParameter)
{
   HCILL_State_t HCILL_State;

   /* Determine the HCILL State.                                        */
   HCILL_State = HCILL_GetState();

   /* If the stack is Idle and we are in HCILL Sleep, then we may enter */
   /* LPM3 mode (with Timer Interrupts disabled).                       */
   if((BSC_QueryStackIdle(BluetoothStackID)) && (HCILL_State == hsSleep) && (!HCILL_Get_Power_Lock_Count()))
   {
      /* Enter MSP430 LPM3 with Timer Interrupts disabled (we will      */
      /* require an interrupt to wake us up from this state).           */

	  // dont go to sleep
      HAL_LowPowerMode((unsigned char)FALSE);
   }
}

   /* The following function is the main user interface thread.  It     */
   /* opens the Bluetooth Stack and then drives the main user interface.*/
static void MainThread(void)
{
   int                     Result;
   BTPS_Initialization_t   BTPS_Initialization;
   HCI_DriverInformation_t HCI_DriverInformation;

   /* Configure the UART Parameters.                                    */
   HCI_DRIVER_SET_COMM_INFORMATION(&HCI_DriverInformation, 1, 115200, cpUART);
   HCI_DriverInformation.DriverInformation.COMMDriverInformation.InitializationDelay = 100;

   /* Set up the application callbacks.                                 */
   BTPS_Initialization.GetTickCountCallback  = GetTickCallback;
   BTPS_Initialization.MessageOutputCallback = DisplayCallback;

   /* Initialize the application.                                       */
   if((Result = InitializeApplication(&HCI_DriverInformation, &BTPS_Initialization)) > 0)
   {
      /* Save the Bluetooth Stack ID.                                   */
      BluetoothStackID = (unsigned int)Result;

      /* Go ahead an enable HCILL Mode.                                 */
      HCILL_Init();
      HCILL_Configure(BluetoothStackID, HCILL_MODE_INACTIVITY_TIMEOUT, HCILL_MODE_RETRANSMIT_TIMEOUT, TRUE);

      // add our polling function to the scheduler
	  // period = 50ms
	  if(BTPS_AddFunctionToScheduler(ButtonPollFunction, NULL, 50))
	  {
        /* Add the idle function (which determines if LPM3 may be entered)*/
		/* to the scheduler.                                              */
		if(BTPS_AddFunctionToScheduler(IdleFunction, NULL, HCILL_MODE_INACTIVITY_TIMEOUT))
		{
		   /* Loop forever and execute the scheduler.                     */
		   while(1)
			  BTPS_ExecuteScheduler();
		}
	  }
   }
}

   /* The following is the Main application entry point.  This function */
   /* will configure the hardware and initialize the OS Abstraction     */
   /* layer, create the Main application thread and start the scheduler.*/
int main(void)
{
   /* Turn off the watchdog timer                                       */
   WDTCTL = WDTPW | WDTHOLD;

   /* Configure the hardware for its intended use.                      */
   HAL_ConfigureHardware();


   // init hardware inputs
   P2DIR = 0;
   	P2REN = BIT0 + BIT1 + BIT2 + BIT3;
   	P2OUT = BIT0 + BIT1 + BIT2 + BIT3;

   	// enable interrupts to wake MSP from low power mode if necessary
   	P2IE = BIT0 + BIT1 + BIT2 + BIT3;
   	P2IES = P2IN;

   /* Enable interrupts and call the main application thread.           */
   __enable_interrupt();
   MainThread();

   Display(("Something went wrong, initiating software POR\r\n"));

   	/* MainThread should run continously, if it exits an error occured.  */
   	int i;
   	for(i = 0; i < 40; i++)
   	{
   		HAL_LedToggle(0);
   		BTPS_Delay(100);
   	}

   	// do a software POR reset
   	PMMCTL0 = PMMPW + PMMSWPOR + (PMMCTL0 & 0x0003);
}


