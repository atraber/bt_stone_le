/*****< btpsvend.c >***********************************************************/
/*      Copyright 2008 - 2012 Stonestreet One.                                */
/*      All Rights Reserved.                                                  */
/*                                                                            */
/*  BTPSVEND - Vendor specific functions/definitions used to define a set of  */
/*             vendor specific functions supported by the Bluetopia Protocol  */
/*             Stack.  These functions may be unique to a given hardware      */
/*             platform.                                                      */
/*                                                                            */
/*  Author:  Dave Wooldridge                                                  */
/*                                                                            */
/*** MODIFICATION HISTORY *****************************************************/
/*                                                                            */
/*   mm/dd/yy  F. Lastname    Description of Modification                     */
/*   --------  -----------    ------------------------------------------------*/
/*   03/14/08  D. Wooldridge  Initial creation.                               */
/******************************************************************************/
#include "SS1BTPS.h"          /* Bluetopia API Prototypes/Constants.          */
#include "HCIDRV.h"
#include "HCITRANS.h"

#include "BTPSKRNL.h"
#include "BTPSVEND.h"

   /* By default we will include the PAN1316 patch (which is the smaller*/
   /* patch version).  This can be used on the CC2560A, CC2564, CC2567  */
   /* and CC2569 TI Chipsets.                                           */

#ifdef __SUPPORT_PAN1315_PATCH__

   #include "Patch_PAN1315.h"

#else

   #include "Patch_PAN1316.h"

#endif

   /* Miscellaneous Type Declarations.                                  */

#define RETURN_BUFFER_SIZE  (32)    /* Represents a size large enough   */
                                    /* to hold the return result from   */
                                    /* any of the patch ram setup       */
                                    /* commands.                        */

   /* The following variable is used to track whether or not the Vendor */
   /* Specific Commands (and Patch RAM commands) have already been      */
   /* issued to the device.  This is done so that we do not issue them  */
   /* more than once for a single device (there is no need).  They could*/
   /* be potentially issued because the HCI Reset hooks (defined below) */
   /* are called for every HCI_Reset() that is issued.                  */
static Boolean_t VendorCommandsIssued;

   /* Internal Function Prototypes.                                     */
static void MovePatchBytes(unsigned char *Dest, unsigned long *Source, unsigned int Length);
static Boolean_t Download_Patch(unsigned int BluetoothStackID, unsigned int PatchLength, unsigned long PatchPointer, Byte_t *ReturnBuffer, Byte_t *TempBuffer);

   /* The following function is used to copy BTPSVEND_PATCH_LOCATION    */
   /* Patch data to a local buffer.  This is done because a SMALL data  */
   /* model configuration can't access data past 64k.  The function     */
   /* receives as its first parameter the pointer to the buffer to copy */
   /* from 20 bit memory into.  The second parameter is a pointer       */
   /* (stored as an unsigned long) to the data to copy from 20 bit      */
   /* memory.  The last parameter is the number of bytes that are to be */
   /* moved.                                                            */
static void MovePatchBytes(unsigned char *Dest, unsigned long *Source, unsigned int Length)
{
   /* Check to make sure that the parameters passed in appear valid.    */
   if((Source) && (Dest) && (Length))
   {

#ifdef __TI_COMPILER_VERSION__

      /* Copy all of the requested data from Data 20 memory into Data 16*/
      /* memory.                                                        */
      while(Length--)
         *Dest++ = _data20_read_char((*Source)++);

#else

#ifdef __IAR_SYSTEMS_ICC__

      /* Copy all of the requested data from Data 20 memory into Data 16*/
      /* memory.                                                        */
      while(Length--)
         *Dest++ = *((unsigned char BTPSVEND_PATCH_LOCATION *)(*Source)++);

#endif

#endif

   }
   else
   {
      /* Clear the Packet Flag.                                         */
      *Dest = 0;
   }
}

   /* The following function is provided to allow a mechanism to        */
   /* download the specified Patch Data to the CC25xx device.  This     */
   /* function does not disable the co-processor.  This function returns*/
   /* TRUE if successful or FALSE if there was an error.                */
static Boolean_t Download_Patch(unsigned int BluetoothStackID, unsigned int PatchLength, unsigned long PatchPointer, Byte_t *ReturnBuffer, Byte_t *TempBuffer)
{
   int           Result;
   int           Cnt;
   Byte_t        Status;
   Byte_t        OGF;
   Byte_t        Length;
   Word_t        OCF;
   Boolean_t     ret_val;
   unsigned long Temp;
   unsigned int  PatchLen;

   /* First, make sure the input parameters appear to be semi-valid.    */
   if((BluetoothStackID) && (PatchLength) && (PatchPointer))
   {
      ret_val = TRUE;

      /* Clear the status value and initialize all variables.     */
      Cnt      = 0;
      PatchLen = PatchLength;
      Temp     = PatchPointer;
      while(PatchLen)
      {
         /* Copy the header data to local buffer and check for a  */
         /* valid start of packet.                                */
         MovePatchBytes(TempBuffer, &Temp, 4);
         if(TempBuffer[0] == 0x01)
         {
            /* Copy the command parameters into Data 16 memory.   */
            MovePatchBytes(&TempBuffer[4], &Temp, TempBuffer[3]);

            /* Skip over the first parameter.  The patch data     */
            /* formatted to a specific structure and the first    */
            /* byte is not used in this application.  Get the OGF */
            /* and OCF values an make a call to perform the vendor*/
            /* specific function.                                 */
            OGF     = (TempBuffer[2] >> 2);
            OCF     = ((unsigned short)TempBuffer[1] + ((unsigned short)(TempBuffer[2] & 0x03) << 8));
            Length  = RETURN_BUFFER_SIZE;

            Result = HCI_Send_Raw_Command(BluetoothStackID, OGF, OCF, TempBuffer[3], &TempBuffer[4], &Status, &Length, ReturnBuffer, TRUE);
            Cnt++;

            /* If the function was successful, update the count   */
            /* and pointer for the next command.                  */
            if((Result < 0) || (Status != 0) || (ReturnBuffer[0]))
            {
               DBG_MSG(DBG_ZONE_VENDOR, ("PatchDevice[%d] ret_val = %d Status %d Result %d\r\n", Cnt, Result, Status, ReturnBuffer[0]));
               DBG_MSG(DBG_ZONE_VENDOR, ("Length = %d\r\n",Length));
               DBG_DUMP(DBG_ZONE_VENDOR, (TempBuffer[3], &TempBuffer[4]));

               ret_val = FALSE;
               break;
            }

            /* Advance to the next Patch Entry.                   */
            PatchLen -= (TempBuffer[3] + 4);
         }
         else
            PatchLen = 0;
      }
   }
   else
      ret_val = FALSE;

   return(ret_val);
}

   /* The following function prototype represents the vendor specific   */
   /* function which is used to implement any needed Bluetooth device   */
   /* vendor specific functionality that needs to be performed before   */
   /* the HCI Communications layer is opened.  This function is called  */
   /* immediately prior to calling the initialization of the HCI        */
   /* Communications layer.  This function should return a BOOLEAN TRUE */
   /* indicating successful completion or should return FALSE to        */
   /* indicate unsuccessful completion.  If an error is returned the    */
   /* stack will fail the initialization process.                       */
   /* * NOTE * The parameter passed to this function is the exact       */
   /*          same parameter that was passed to BSC_Initialize() for   */
   /*          stack initialization.  If this function changes any      */
   /*          members that this pointer points to, it will change the  */
   /*          structure that was originally passed.                    */
   /* * NOTE * No HCI communication calls are possible to be used in    */
   /*          this function because the driver has not been initialized*/
   /*          at the time this function is called.                     */
Boolean_t BTPSAPI HCI_VS_InitializeBeforeHCIOpen(HCI_DriverInformation_t *HCI_DriverInformation)
{
   /* Flag that we have not issued the first Vendor Specific Commands   */
   /* before the first reset.                                           */
   VendorCommandsIssued = FALSE;

   return(TRUE);
}

   /* The following function prototype represents the vendor specific   */
   /* function which is used to implement any needed Bluetooth device   */
   /* vendor specific functionality after the HCI Communications layer  */
   /* is initialized (the driver only).  This function is called        */
   /* immediately after returning from the initialization of the HCI    */
   /* Communications layer (HCI Driver).  This function should return a */
   /* BOOLEAN TRUE indicating successful completion or should return    */
   /* FALSE to indicate unsuccessful completion.  If an error is        */
   /* returned the stack will fail the initialization process.          */
   /* * NOTE * No HCI layer function calls are possible to be used in   */
   /*          this function because the actual stack has not been      */
   /*          initialized at this point.  The only initialization that */
   /*          has occurred is with the HCI Driver (hence the HCI       */
   /*          Driver ID that is passed to this function).              */
Boolean_t BTPSAPI HCI_VS_InitializeAfterHCIOpen(unsigned int HCIDriverID)
{
   return(TRUE);
}

   /* The following function prototype represents the vendor specific   */
   /* function which is used to implement any needed Bluetooth device   */
   /* vendor specific functions after the HCI Communications layer AND  */
   /* the HCI Stack layer has been initialized.  This function is called*/
   /* after all HCI functionality is established, but before the initial*/
   /* HCI Reset is sent to the stack.  The function should return a     */
   /* BOOLEAN TRUE to indicate successful completion or should return   */
   /* FALSE to indicate unsuccessful completion.  If an error is        */
   /* returned the stack will fail the initialization process.          */
   /* * NOTE * At the time this function is called HCI Driver and HCI   */
   /*          layer functions can be called, however no other stack    */
   /*          layer functions are able to be called at this time       */
   /*          (hence the HCI Driver ID and the Bluetooth Stack ID      */
   /*          passed to this function).                                */
Boolean_t BTPSAPI HCI_VS_InitializeBeforeHCIReset(unsigned int HCIDriverID, unsigned int BluetoothStackID)
{
   /* If we haven't issued the Vendor Specific Commands yet, then go    */
   /* ahead and issue them.  If we have, then there isn't anything to   */
   /* do.                                                               */
   if(!VendorCommandsIssued)
   {
      /* Flag that we have issued the Vendor Specific Commands (so we   */
      /* don't do it again if someone issues an HCI_Reset().            */
      VendorCommandsIssued = TRUE;
   }

   return(TRUE);
}

   /* The following function prototype represents the vendor specific   */
   /* function which is used to implement any needed Bluetooth device   */
   /* vendor specific functionality after the HCI layer has issued any  */
   /* HCI Reset as part of the initialization.  This function is called */
   /* after all HCI functionality is established, just after the initial*/
   /* HCI Reset is sent to the stack.  The function should return a     */
   /* BOOLEAN TRUE to indicate successful completion or should return   */
   /* FALSE to indicate unsuccessful completion.  If an error is        */
   /* returned the stack will fail the initialization process.          */
   /* * NOTE * At the time this function is called HCI Driver and HCI   */
   /*          layer functions can be called, however no other stack    */
   /*          layer functions are able to be called at this time (hence*/
   /*          the HCI Driver ID and the Bluetooth Stack ID passed to   */
   /*          this function).                                          */
Boolean_t BTPSAPI HCI_VS_InitializeAfterHCIReset(unsigned int HCIDriverID, unsigned int BluetoothStackID)
{
   Byte_t    *TempBuf;
   Byte_t    *ReturnBuffer;
   Boolean_t  ret_val;

   /* Verify that the parameters that were passed in appear valid.      */
   if((HCIDriverID) && (BluetoothStackID))
   {
      DBG_MSG(DBG_ZONE_VENDOR, ("HCI_VS_InitializeAfterHCIReset\r\n"));

      /* Allocate a buffer for the return result.                       */
      if((ReturnBuffer = (Byte_t *)BTPS_AllocateMemory(RETURN_BUFFER_SIZE)) != NULL)
      {
         /* Allocate a temporary buffer to bring the Patch RAM HCI      */
         /* Command Parameters into Data 16 memory.                     */
         if((TempBuf =(Byte_t *) BTPS_AllocateMemory(260)) != NULL)
         {
            /* First download the Base Patch.                           */
            if((ret_val = Download_Patch(BluetoothStackID, BasePatchLength, BasePatchPointer, ReturnBuffer, TempBuf)) == TRUE)
            {
               /* Next if LE support is enabled go ahead and download   */
               /* the LE Patch.                                         */ 

#ifdef __SUPPORT_LOW_ENERGY__

               /* Download the LE Patch.                                */
               ret_val = Download_Patch(BluetoothStackID, LowEnergyPatchLength, LowEnergyPatchPointer, ReturnBuffer, TempBuf);

#else

               ret_val = TRUE;

#endif

            }

            /* Free the previously allocated tempory buffer.            */
            BTPS_FreeMemory(TempBuf);
         }
         else
            ret_val = FALSE;

         BTPS_FreeMemory(ReturnBuffer);
      }
      else
         ret_val = FALSE;
   }
   else
      ret_val = FALSE;

   /* Print Success/Failure status.                                     */
   if (ret_val)
      DBG_MSG(DBG_ZONE_VENDOR, ("HCI_VS_InitializeAfterHCIReset Success\r\n"));
   else
      DBG_MSG(DBG_ZONE_VENDOR, ("HCI_VS_InitializeAfterHCIReset Failure.\r\n"));

   return(ret_val);
}

   /* The following function prototype represents the vendor specific   */
   /* function which would is used to implement any needed Bluetooth    */
   /* device vendor specific functionality before the HCI layer is      */
   /* closed.  This function is called at the start of the HCI_Cleanup()*/
   /* function (before the HCI layer is closed), at which time all HCI  */
   /* functions are still operational.  The caller is NOT able to call  */
   /* any other stack functions other than the HCI layer and HCI Driver */
   /* layer functions because the stack is being shutdown (i.e.         */
   /* something has called BSC_Shutdown()).  The caller is free to      */
   /* return either success (TRUE) or failure (FALSE), however, it will */
   /* not circumvent the closing down of the stack or of the HCI layer  */
   /* or HCI Driver (i.e. the stack ignores the return value from this  */
   /* function).                                                        */
   /* * NOTE * At the time this function is called HCI Driver and HCI   */
   /*          layer functions can be called, however no other stack    */
   /*          layer functions are able to be called at this time (hence*/
   /*          the HCI Driver ID and the Bluetooth Stack ID passed to   */
   /*          this function).                                          */
Boolean_t BTPSAPI HCI_VS_InitializeBeforeHCIClose(unsigned int HCIDriverID, unsigned int BluetoothStackID)
{
   return(TRUE);
}

   /* The following function prototype represents the vendor specific   */
   /* function which is used to implement any needed Bluetooth device   */
   /* vendor specific functionality after the entire Bluetooth Stack is */
   /* closed.  This function is called during the HCI_Cleanup()         */
   /* function, after the HCI Driver has been closed.  The caller is    */
   /* free return either success (TRUE) or failure (FALSE), however, it */
   /* will not circumvent the closing down of the stack as all layers   */
   /* have already been closed.                                         */
   /* * NOTE * No Stack calls are possible in this function because the */
   /*          entire stack has been closed down at the time this       */
   /*          function is called.                                      */
Boolean_t BTPSAPI HCI_VS_InitializeAfterHCIClose(void)
{
   return(TRUE);
}

   /* The following function prototype represents the vendor specific   */
   /* function which is used to enable a specific vendor specific       */
   /* feature.  This can be used to reconfigure the chip for a specific */
   /* feature (i.e. if a special configuration/patch needs to be        */
   /* dynamically loaded it can be done in this function).  This        */
   /* function returns TRUE if the feature was able to be enabled       */
   /* successfully, or FALSE if the feature was unable to be enabled.   */
   /* * NOTE * This functionality is not normally supported by default  */
   /*          (i.e. a custom stack build is required to enable this    */
   /*          functionality).                                          */
Boolean_t BTPSAPI HCI_VS_EnableFeature(unsigned int BluetoothStackID, unsigned long Feature)
{
   return(FALSE);
}

   /* The following function prototype represents the vendor specific   */
   /* function which is used to enable a specific vendor specific       */
   /* feature.  This can be used to reconfigure the chip for a specific */
   /* feature (i.e. if a special configuration/patch needs to be        */
   /* dynamically loaded it can be done in this function).  This        */
   /* function returns TRUE if the feature was able to be disabled      */
   /* successfully, or FALSE if the feature was unable to be disabled.  */
   /* * NOTE * This functionality is not normally supported by default  */
   /*          (i.e. a custom stack build is required to enable this    */
   /*          functionality).                                          */
Boolean_t BTPSAPI HCI_VS_DisableFeature(unsigned int BluetoothStackID, unsigned long Feature)
{
   return(FALSE);
}

