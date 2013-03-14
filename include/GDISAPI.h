/*****< GDISAPI.h >************************************************************/
/*      Copyright 2001 - 2012 Stonestreet One.                                */
/*      All Rights Reserved.                                                  */
/*                                                                            */
/*  GDISAPI - GATT Service Dicovery Module Type Definitions, Prototypes, and  */
/*            Constants.                                                      */
/*                                                                            */
/*  Author:  Tim Cook                                                         */
/*                                                                            */
/*** MODIFICATION HISTORY *****************************************************/
/*                                                                            */
/*   mm/dd/yy  F. Lastname    Description of Modification                     */
/*   --------  -----------    ------------------------------------------------*/
/*   09/20/11  T. Cook        Initial creation.                               */
/******************************************************************************/
#ifndef __GDISAPIH__
#define __GDISAPIH__

#include "SS1BTPS.h"        /* Bluetooth Stack API Prototypes/Constants.      */
#include "SS1BTGAT.h"       /* Bluetooth Stack GATT API Prototypes/Constants. */

   /* Error Return Codes.                                               */

   /* Error Codes that are smaller than these (less than -1000) are     */
   /* related to the Bluetooth Protocol Stack itself (see BTERRORS.H).  */
#define GDIS_ERROR_INVALID_PARAMETER                     (-1000)
#define GDIS_ERROR_NOT_INITIALIZED                       (-1001)
#define GDIS_ERROR_INVALID_BLUETOOTH_STACK_ID            (-1002)
#define GDIS_ERROR_INSUFFICIENT_RESOURCES                (-1003)
#define GDIS_ERROR_INTERNAL_ERROR                        (-1004)
#define GDIS_ERROR_ACTION_NOT_ALLOWED                    (-1005)
#define GDIS_ERROR_DEVICE_LIST_EMPTY                     (-1006)
#define GDIS_ERROR_PROFILE_LIST_EMPTY                    (-1007)
#define GDIS_ERROR_INVALID_PROFILE_ID                    (-1008)
#define GDIS_ERROR_SERVICE_DISCOVERY_OUTSTANDING         (-1009)

   /* The following structure defines the information that is returned  */
   /* about a discovered GATT Service.                                  */
typedef struct _tagGDIS_Service_Information_t
{
   GATT_UUID_t Service_UUID;
   Word_t      Start_Handle;
   Word_t      End_Handle;
} GDIS_Service_Information_t;

#define GDIS_SERVICE_INFORMATION_DATA_SIZE               (sizeof(GDIS_Service_Information_t))

   /* The following structure defines the information that is returned  */
   /* about a discovered GATT Characteristic Descriptor.                */
typedef struct _tagGDIS_Characteristic_Descriptor_Information_t
{
   Word_t      Characteristic_Descriptor_Handle;
   GATT_UUID_t Characteristic_Descriptor_UUID;
} GDIS_Characteristic_Descriptor_Information_t;

#define GDIS_CHARACTERISTIC_DESCRIPTOR_INFORMATION_DATA_SIZE (sizeof(GDIS_Characteristic_Descriptor_Information_t))

   /* The following structure defines the information that is returned  */
   /* about a discovered GATT Characteristic.                           */
typedef struct _tagGDIS_Characteristic_Information_t
{
   GATT_UUID_t                                   Characteristic_UUID;
   Word_t                                        Characteristic_Handle;
   Byte_t                                        Characteristic_Properties;
   unsigned int                                  NumberOfDescriptors;
   GDIS_Characteristic_Descriptor_Information_t *DescriptorList;
} GDIS_Characteristic_Information_t;

#define GDIS_CHARACTERISTIC_INFORMATION_DATA_SIZE        (sizeof(GDIS_Characteristic_Information_t))

   /* GDIS Event API Types.                                             */
typedef enum
{
   etGDIS_Service_Discovery_Indication,
   etGDIS_Service_Discovery_Complete
} GDIS_Event_Type_t;

   /* The following structure represents the data that is returned in a */
   /* Service Discovery Indication event.                               */
typedef struct _tagGDIS_Service_Discovery_Indication_Data_t
{
   unsigned int                       ConnectionID;
   GDIS_Service_Information_t         ServiceInformation;
   unsigned int                       NumberOfCharacteristics;
   GDIS_Characteristic_Information_t *CharacteristicInformationList;
} GDIS_Service_Discovery_Indication_Data_t;

#define GDIS_SERVICE_DISCOVERY_INDICATION_DATA_SIZE      (sizeof(GDIS_Service_Discovery_Indication_Data_t))

   /* The following structure represents the data that is returned in a */
   /* Service Discovery Complete event.                                 */
typedef struct _tagGDIS_Service_Discovery_Complete_Data_t
{
   unsigned int ConnectionID;
   Byte_t       Status;
} GDIS_Service_Discovery_Complete_Data_t;

#define GDIS_SERVICE_DISCOVERY_COMPLETE_DATA_SIZE  (sizeof(GDIS_Service_Discovery_Complete_Data_t))

#define GDIS_SERVICE_DISCOVERY_STATUS_SUCCESS             0x00
#define GDIS_SERVICE_DISCOVERY_STATUS_RESPONSE_ERROR      0x01
#define GDIS_SERVICE_DISCOVERY_STATUS_RESPONSE_TIMEOUT    0x02
#define GDIS_SERVICE_DISCOVERY_STATUS_DEVICE_DISCONNECTED 0x03
#define GDIS_SERVICE_DISCOVERY_STATUS_UNKNOWN_ERROR       0x04

   /* The following structure represents the container structure for    */
   /* Holding all GDIS Event Data.                                      */
typedef struct _tagDISC_Event_Data_t
{
   GDIS_Event_Type_t Event_Data_Type;
   Word_t            Event_Data_Size;
   union
   {
      GDIS_Service_Discovery_Indication_Data_t *GDIS_Service_Discovery_Indication_Data;
      GDIS_Service_Discovery_Complete_Data_t   *GDIS_Service_Discovery_Complete_Data;
   } Event_Data;
} GDIS_Event_Data_t;

#define GDIS_EVENT_DATA_SIZE                                (sizeof(GDIS_Event_Data_t))

   /* The following declared type represents the Prototype Function for */
   /* a DISC Profile Event Data Callback.  This function will be called */
   /* whenever a DISC Event occurs that is associated with the specified*/
   /* Bluetooth Stack ID.  This function passes to the caller the       */
   /* Bluetooth Stack ID, the DISC Event Data that occurred and the DISC*/
   /* Event Callback Parameter that was specified when this Callback was*/
   /* installed.  The caller is free to use the contents of the DISC    */
   /* Event Data ONLY in the context of this callback.  If the caller   */
   /* requires the Data for a longer period of time, then the callback  */
   /* function MUST copy the data into another Data Buffer.  This       */
   /* function is guaranteed NOT to be invoked more than once           */
   /* simultaneously for the specified installed callback (i.e.  this   */
   /* function DOES NOT have be reentrant).  It needs to be noted       */
   /* however, that if the same Callback is installed more than once,   */
   /* then the callbacks will be called serially.  Because of this, the */
   /* processing in this function should be as efficient as possible.   */
   /* It should also be noted that this function is called in the Thread*/
   /* Context of a Thread that the User does NOT own.  Therefore,       */
   /* processing in this function should be as efficient as possible    */
   /* (this argument holds anyway because another DISC Profile Event    */
   /* will not be processed while this function call is outstanding).   */
   /* ** NOTE ** This function MUST NOT Block and wait for events that  */
   /*            can only be satisfied by Receiving DISC Event Packets. */
   /*            A Deadlock WILL occur because NO DISC Event Callbacks  */
   /*            will be issued while this function is currently        */
   /*            outstanding.                                           */
typedef void (BTPSAPI *GDIS_Event_Callback_t)(unsigned int BluetoothStackID, GDIS_Event_Data_t *GDIS_Event_Data, unsigned long CallbackParameter);

   /* The following function is responsible for initializing an GDIS    */
   /* Context Layer for the specified Bluetooth Protocol Stack.  This   */
   /* function will allocate and initialize an GDIS Context Information */
   /* structure associated with the specified Bluetooth Stack ID.  This */
   /* function returns zero if successful, or a negative error code if  */
   /* there was an error.                                               */
BTPSAPI_DECLARATION int BTPSAPI GDIS_Initialize(unsigned int BluetoothStackID);

#ifdef INCLUDE_DEBUG_API_PROTOTYPES
   typedef int (BTPSAPI *PFN_GDIS_Initialize_t)(unsigned int BluetoothStackID);
#endif

   /* The following function is responsible for releasing any resources */
   /* that the GDIS Layer associated with the Bluetooth Protocol Stack, */
   /* specified by the Bluetooth Stack ID, has allocated.  Upon         */
   /* completion of this function, ALL GDIS functions will fail if used */
   /* on the specified Bluetooth Protocol Stack.                        */
BTPSAPI_DECLARATION void BTPSAPI GDIS_Cleanup(unsigned int BluetoothStackID);

#ifdef INCLUDE_DEBUG_API_PROTOTYPES
   typedef void (BTPSAPI *PFN_GDIS_Cleanup_t)(unsigned int BluetoothStackID);
#endif

   /* The following function is used to initiate the Service Discovery  */
   /* process or queue additional requests.  The function takes as its  */
   /* first parameter the BluetoothStackID that is associated with the  */
   /* Bluetooth Device.  The second parameter is the connection ID of   */
   /* the remote device that is to be searched.  The third and fourth   */
   /* parameters specify an optional list of UUIDs to search for.  The  */
   /* final two parameters define the Callback function and parameter to*/
   /* use when the service discovery is complete.  The function returns */
   /* zero on success and a negative return value if there was an error.*/
   /* * NOTE * The NumberOfUUID and UUIDList are optional.  If they are */
   /*          specified then they specify the list of services that    */
   /*          should be searched for.  Only services with UUIDs that   */
   /*          are in this list will be discovered.                     */
BTPSAPI_DECLARATION int BTPSAPI GDIS_Service_Discovery_Start(unsigned int BluetoothStackID, unsigned int ConnectionID, unsigned int NumberOfUUID, GATT_UUID_t *UUIDList, GDIS_Event_Callback_t ServiceDiscoveryCallback, unsigned long ServiceDiscoveryCallbackParameter);

#ifdef INCLUDE_DEBUG_API_PROTOTYPES
   typedef int (BTPSAPI *PFN_GDIS_Service_Discovery_Start_t)(unsigned int BluetoothStackID, unsigned int ConnectionID, unsigned int NumberOfUUID, GATT_UUID_t *UUIDList, GDIS_Event_Callback_t ServiceDiscoveryCallback, unsigned long ServiceDiscoveryCallbackParameter);
#endif

   /* The following function is used to terminate the Device Discovery  */
   /* process.  The function takes as its parameter the BluetoothStackID*/
   /* that is associated with the Bluetooth Device.  The function       */
   /* returns a negative return value if there was an error and Zero in */
   /* success.                                                          */
   /* * NOTE * This function will cancel any discovery operations that  */
   /*          are currently in progress and release all request        */
   /*          information in the queue that are waiting to be executed.*/
BTPSAPI_DECLARATION int BTPSAPI GDIS_Service_Discovery_Stop(unsigned int BluetoothStackID, unsigned int ConnectionID);

#ifdef INCLUDE_DEBUG_API_PROTOTYPES
   typedef int (BTPSAPI *PFN_GDIS_Service_Discovery_Stop_t)(unsigned int BluetoothStackID, unsigned int ConnectionID);
#endif

#endif
