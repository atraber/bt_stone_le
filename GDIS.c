/*****< gdis.c >***************************************************************/
/*      Copyright 2001 - 2012 Stonestreet One.                                */
/*      All Rights Reserved.                                                  */
/*                                                                            */
/*  GDIS - Bluetooth Stack Service GATT Service Discovery for Stonestreet One */
/*         Bluetooth Protocol Stack.                                          */
/*                                                                            */
/*  Author:  Tim Cook                                                         */
/*                                                                            */
/*** MODIFICATION HISTORY *****************************************************/
/*                                                                            */
/*   mm/dd/yy  F. Lastname    Description of Modification                     */
/*   --------  -----------    ------------------------------------------------*/
/*   09/20/11  T. Cook        Initial creation.                               */
/******************************************************************************/
#include "SS1BTGDI.h"           /* Bluetooth GDIS API Prototypes/Constants.   */
#include "GDIS.h"               /* Bluetooth GDIS Prototypes/Constants.       */

#include "SS1BTPS.h"            /* Bluetooth Stack API Prototypes/Constants.  */
#include "BTPSKRNL.h"           /* BTPS Kernel Prototypes/Constants.          */

   
typedef enum
{
   ssServiceDiscovery,
   ssIncludeDiscovery,
   ssCharacteristiscDiscovery,
   ssCharacteristicDescriptorDiscovery
} GDISSearchState_t;

typedef struct _tagGDISDescriptorInfo_t
{
   GATT_UUID_t                      Characteristic_UUID;
   Word_t                           Descriptor_Handle;
   struct _tagGDISDescriptorInfo_t *NextDescriptorInfoPtr;
} GDISDescriptorInfo_t;

#define GDIS_DESCRIPTOR_INFO_DATA_SIZE          (sizeof(GDISDescriptorInfo_t))

typedef struct _tagGDISharacteristicInfo_t
{   
   GATT_UUID_t                         Characteristic_UUID;
   Word_t                              Characteristic_Handle;
   Byte_t                              Characteristic_Properties;
   unsigned int                        DecriptorCount;
   GDISDescriptorInfo_t               *DescriptorList;
   struct _tagGDISharacteristicInfo_t *NextCharacteristicInfoPtr;
} GDISCharacteristicInfo_t;

#define GDIS_CHARACTERISTIC_INFO_DATA_SIZE      (sizeof(GDISCharacteristicInfo_t))

typedef struct _tagGDISServiceInfo_t
{
   GATT_UUID_t                   Service_UUID;
   Word_t                        StartHandle;
   Word_t                        EndHandle;
   Word_t                        OutstandingRequestHandle;
   unsigned int                  CharacteristicCount;
   unsigned int                  DescriptorCount;
   GDISCharacteristicInfo_t     *CharacteristicInfoList;
   struct _tagGDISServiceInfo_t *NextServiceInfoPtr;
} GDISServiceInfo_t;

#define GDIS_SERVICE_INFO_DATA_SIZE             (sizeof(GDISServiceInfo_t))

typedef struct _tagGDISDiscoveryInfo_t
{
   unsigned int                    ConnectionID;
   unsigned int                    OutstandingTransactionID;
   GDISSearchState_t               SearchState;
   GDISServiceInfo_t              *ServiceInfoList;
   GDIS_Event_Callback_t           ServiceDiscoveryCallback;
   unsigned long                   CallbackParameter;
   unsigned int                    NumberOfUUID;
   GATT_UUID_t                    *ServiceUUID;
   struct _tagGDISDiscoveryInfo_t *NextDiscoveryInfoPtr;
} GDISDiscoveryInfo_t;

#define GDIS_SERVICE_DISCOVERY_INFO_DATA_SIZE   (sizeof(GDISDiscoveryInfo_t))

   /* GDIS Context Information Block.  This structure contains All      */
   /* information associated with a specific Bluetooth Stack ID (member */
   /* is present in this structure).                                    */
typedef struct _tagGDIS_Context_t
{
   unsigned int               BluetoothStackID;
   unsigned int               ConnectionCallbackID;
   GDISDiscoveryInfo_t       *DiscoveryInfoList;   
   struct _tagGDIS_Context_t *NextGDISContextPtr;
} GDISContext_t;

#define GDIS_CONTEXT_DATA_SIZE               (sizeof(GDISContext_t))

   /* Internal Variables to this Module (Remember that all variables    */
   /* declared static are initialized to 0 automatically by the         */
   /* compiler as part of standard C/C++).                              */
static GDISContext_t *GDISContextList;         /* Variable which holds the   */
                                                /* First Entry (Head of List) */
                                                /* of All currently opened    */
                                                /* DISC Profile instances.    */

static Boolean_t GDISContextListInitialized;    /* Variable that flags that   */
                                                /* is used to denote that this*/
                                                /* module has been            */
                                                /* successfully initialized.  */

   /* The following are the prototypes of local functions.              */
static Boolean_t InitializeModule(void);
static void CleanupModule(void);

static GDISContext_t *AddGDISContextEntry(GDISContext_t **ListHead, GDISContext_t *EntryToAdd);
static GDISContext_t *SearchGDISContextEntry(GDISContext_t **ListHead, unsigned int BluetoothStackID);
static GDISContext_t *DeleteGDISContextEntry(GDISContext_t **ListHead, unsigned int BluetoothStackID);
static void FreeGDISContextEntryMemory(GDISContext_t *EntryToFree);
static void FreeGDISContextList(GDISContext_t **ListHead);

static GDISDiscoveryInfo_t *AddDiscoveryInfoEntry(GDISDiscoveryInfo_t **ListHead, GDISDiscoveryInfo_t *EntryToAdd);
static GDISDiscoveryInfo_t *SearchDiscoveryInfoEntry(GDISDiscoveryInfo_t **ListHead, unsigned int ConnectionID);
static GDISDiscoveryInfo_t *DeleteDiscoveryInfoEntry(GDISDiscoveryInfo_t **ListHead, unsigned int ConnectionID);
static void FreeDiscoveryInfoEntryMemory(GDISDiscoveryInfo_t *EntryToFree);
static void FreeDiscoveryInfoList(GDISDiscoveryInfo_t **ListHead);

static GDISServiceInfo_t *AddServiceInfoEntry(GDISServiceInfo_t **ListHead, GDISServiceInfo_t *EntryToAdd);
static GDISServiceInfo_t *DeleteServiceInfoEntry(GDISServiceInfo_t **ListHead, Word_t StartHandle);
static void FreeServiceInfoEntryMemory(GDISServiceInfo_t *EntryToFree);
static void FreeServiceInfoList(GDISServiceInfo_t **ListHead);

static GDISCharacteristicInfo_t *AddCharacteristicInfoEntry(GDISCharacteristicInfo_t **ListHead, GDISCharacteristicInfo_t *EntryToAdd);
static GDISCharacteristicInfo_t *SearchCharacteristicInfoEntry(GDISCharacteristicInfo_t **ListHead, Word_t Characteristic_Handle);
static void FreeCharacteristicInfoEntryMemory(GDISCharacteristicInfo_t *EntryToFree);
static void FreeCharacteristicInfoList(GDISCharacteristicInfo_t **ListHead);

static GDISDescriptorInfo_t *AddDescriptorInfoEntry(GDISDescriptorInfo_t **ListHead, GDISDescriptorInfo_t *EntryToAdd);
static void FreeDescriptorInfoList(GDISDescriptorInfo_t **ListHead);

static void CharacteristicDiscoveryRequest(GDISContext_t *GDISContext, GDISDiscoveryInfo_t *GDISDiscoveryInfo, GDISServiceInfo_t *GDISServiceInfo, Word_t StartHandle);
static Boolean_t CharacteristicDescriptorDiscoveryRequest(GDISContext_t *GDISContext, GDISDiscoveryInfo_t *GDISDiscoveryInfo, GDISServiceInfo_t *GDISServiceInfo, GDISCharacteristicInfo_t *CurrentCharacteristic, Word_t Handle);

static Boolean_t CompareServiceUUID(GATT_UUID_t *UUID_1, GATT_UUID_t *UUID_2);
static Boolean_t CompareServiceToServiceList(GDISDiscoveryInfo_t *GDISDiscoveryInfo, GATT_UUID_t *UUID);
static int ConfigureServiceDiscoveryByUUID(unsigned int BluetoothStackID, GDISDiscoveryInfo_t *DiscoveryInfoPtr, unsigned int NumberOfUUID, GATT_UUID_t *UUIDList);
static void ServiceDiscoveryStateMachine(GDISContext_t *GDISContext, GDISDiscoveryInfo_t *GDISDiscoveryInfo);
static void FormatAndDispatchDiscoveryCallback(GDISContext_t *GDISContext, GDISDiscoveryInfo_t *GDISDiscoveryInfo, GDISServiceInfo_t *GDISServiceInfo);
static void FormatAndDispatchDiscoveryCompleteCallback(GDISContext_t *GDISContext, unsigned int ConnectionID, Byte_t Status);

   /* BTPS Callback function prototypes.                                */
static void BTPSAPI GATT_ClientEventCallback(unsigned int BluetoothStackID, GATT_Client_Event_Data_t *GATT_Client_Event_Data, unsigned long CallbackParameter);
static void BTPSAPI GATT_Connection_Event_Callback(unsigned int BluetoothStackID, GATT_Connection_Event_Data_t *GATT_Connection_Event_Data, unsigned long CallbackParameter);

   /* The following are the prototypes of the registerer callback       */
   /* functions                                                         */

   /* The following function is a utility function that is used to      */
   /* reduce the ifdef blocks that are needed to handle the difference  */
   /* between module initialization for Threaded and NonThreaded stacks.*/
static Boolean_t InitializeModule(void)
{
   /* All we need to do is flag that we are initialized.                */
   if(!GDISContextListInitialized)
   {
      GDISContextListInitialized = TRUE;

      GDISContextList            = NULL;
   }

   return(TRUE);
}

   /* The following function is a utility function that exists to       */
   /* perform stack specific (threaded versus nonthreaded) cleanup.     */
static void CleanupModule(void)
{
   /* Flag that we are no longer initialized.                           */
   GDISContextListInitialized = FALSE;
}

   /* The following function adds the specified Entry to the specified  */
   /* List.  This function allocates and adds an entry to the list that */
   /* has the same attributes as the Entry passed into this function.   */
   /* This function will return NULL if NO Entry was added.  This can   */
   /* occur if the element passed in was deemed invalid or the actual   */
   /* List Head was invalid.                                            */
   /* ** NOTE ** This function does not insert duplicate entries into   */
   /*            the list.  An element is considered a duplicate if the */
   /*            DISC Profile ID field is the same as an entry already  */
   /*            the list.  When this occurs, this function returns in  */
   /*            NULL.                                                  */
static GDISContext_t *AddGDISContextEntry(GDISContext_t **ListHead, GDISContext_t *EntryToAdd)
{
   return((GDISContext_t *)BSC_AddGenericListEntry(GDIS_CONTEXT_DATA_SIZE, ekUnsignedInteger, BTPS_STRUCTURE_OFFSET(GDISContext_t, BluetoothStackID), GDIS_CONTEXT_DATA_SIZE, BTPS_STRUCTURE_OFFSET(GDISContext_t, NextGDISContextPtr), (void **)(ListHead), (void *)(EntryToAdd)));
}

   /* The following function searches the specified List for the        */
   /* specified DISC Profile ID.  This function returns NULL if either  */
   /* the List Head is invalid, the DISC Profile ID is invalid, or the  */
   /* specified DISC Profile ID was NOT found.                          */
static GDISContext_t *SearchGDISContextEntry(GDISContext_t **ListHead, unsigned int BluetoothStackID)
{
   return((GDISContext_t *)BSC_SearchGenericListEntry(ekUnsignedInteger, (void *)(&BluetoothStackID), BTPS_STRUCTURE_OFFSET(GDISContext_t, BluetoothStackID), BTPS_STRUCTURE_OFFSET(GDISContext_t, NextGDISContextPtr), (void **)(ListHead)));
}

   /* The following function searches the specified DISC Profile List   */
   /* for the specified BluetoothStackID and removes it from the List.  */
   /* This function returns NULL if either the DISC List Head is        */
   /* invalid, the BluetoothStackID is invalid, or the specified DISC   */
   /* Entry was NOT present in the list.  The entry returned will have  */
   /* the Next Entry field set to NULL, and the caller is responsible   */
   /* for deleting the memory associated with this entry by calling the */
   /* FreeDISCContextEntryMemory() function.                            */
static GDISContext_t *DeleteGDISContextEntry(GDISContext_t **ListHead, unsigned int BluetoothStackID)
{
   return((GDISContext_t *)BSC_DeleteGenericListEntry(ekUnsignedInteger, (void *)(&BluetoothStackID), BTPS_STRUCTURE_OFFSET(GDISContext_t, BluetoothStackID), BTPS_STRUCTURE_OFFSET(GDISContext_t, NextGDISContextPtr), (void **)(ListHead)));
}

   /* This function frees the specified DISC Information member.  No    */
   /* check is done on this entry other than making sure it is NOT NULL.*/
static void FreeGDISContextEntryMemory(GDISContext_t *EntryToFree)
{
   if(EntryToFree)
   {
      if(EntryToFree->DiscoveryInfoList)
         FreeDiscoveryInfoList(&(EntryToFree->DiscoveryInfoList));

      if(EntryToFree->ConnectionCallbackID)
         GATT_Un_Register_Connection_Events(EntryToFree->BluetoothStackID, EntryToFree->ConnectionCallbackID);

      BTPS_FreeMemory(EntryToFree);
   }
}

   /* The following function deletes (and free's all memory) every      */
   /* element of the specified DISC Information List.  Upon return of   */
   /* this function, the Head Pointer is set to NULL.                   */
static void FreeGDISContextList(GDISContext_t **ListHead)
{
   GDISContext_t *EntryToFree;
   GDISContext_t *tmpEntry;

   if(ListHead)
   {
      /* Simply traverse the list and free every element present.       */
      EntryToFree = *ListHead;

      while(EntryToFree)
      {
         tmpEntry    = EntryToFree;
         EntryToFree = EntryToFree->NextGDISContextPtr;

         FreeGDISContextEntryMemory(tmpEntry);
      }

      /* Make sure the List appears to be empty.                        */
      *ListHead = NULL;
   }
}

   /* The following function adds the specified Entry to the specified  */
   /* List.  This function allocates and adds an entry to the list that */
   /* has the same attributes as the Entry passed into this function.   */
   /* This function will return NULL if NO Entry was added.  This can   */
   /* occur if the element passed in was deemed invalid or the actual   */
   /* List Head was invalid.                                            */
   /* ** NOTE ** This function does not insert duplicate entries into   */
   /*            the list.  An element is considered a duplicate if the */
   /*            BD_ADDR ID field is the same as an entry already the   */
   /*            list.  When this occurs, this function returns in NULL.*/
static GDISDiscoveryInfo_t *AddDiscoveryInfoEntry(GDISDiscoveryInfo_t **ListHead, GDISDiscoveryInfo_t *EntryToAdd)
{
   return((GDISDiscoveryInfo_t *)BSC_AddGenericListEntry(GDIS_SERVICE_DISCOVERY_INFO_DATA_SIZE, ekUnsignedInteger, BTPS_STRUCTURE_OFFSET(GDISDiscoveryInfo_t, ConnectionID), GDIS_SERVICE_DISCOVERY_INFO_DATA_SIZE, BTPS_STRUCTURE_OFFSET(GDISDiscoveryInfo_t, NextDiscoveryInfoPtr), (void **)(ListHead), (void *)(EntryToAdd)));
}

   /* The following function searches the specified List for the        */
   /* specified Service Request ID.  This function returns NULL if      */
   /* either the List Head is invalid, the Service Request ID is        */
   /* invalid, or the specified Service Request ID was NOT found.       */
static GDISDiscoveryInfo_t *SearchDiscoveryInfoEntry(GDISDiscoveryInfo_t **ListHead, unsigned int ConnectionID)
{
   return((GDISDiscoveryInfo_t *)BSC_SearchGenericListEntry(ekUnsignedInteger, (void *)(&ConnectionID), BTPS_STRUCTURE_OFFSET(GDISDiscoveryInfo_t, ConnectionID), BTPS_STRUCTURE_OFFSET(GDISDiscoveryInfo_t, NextDiscoveryInfoPtr), (void **)(ListHead)));
}

   /* The following function searches the specified Search Item List for*/
   /* the specified BD_ADDR and removes it from the List.  This function*/
   /* returns NULL if either the List Head is invalid or an Entry with  */
   /* the specified BD_ADDR was NOT present in the list.  The entry     */
   /* returned will have the Next Entry field set to NULL, and the      */
   /* caller is responsible for deleting the memory associated with this*/
   /* entry by calling the FreeSearchItemEntryMemory() function.        */
static GDISDiscoveryInfo_t *DeleteDiscoveryInfoEntry(GDISDiscoveryInfo_t **ListHead, unsigned int ConnectionID)
{
   return((GDISDiscoveryInfo_t *)BSC_DeleteGenericListEntry(ekUnsignedInteger, (void *)(&ConnectionID), BTPS_STRUCTURE_OFFSET(GDISDiscoveryInfo_t, ConnectionID), BTPS_STRUCTURE_OFFSET(GDISDiscoveryInfo_t, NextDiscoveryInfoPtr), (void **)(ListHead)));
}

   /* This function frees the specified Search Item List member.  No    */
   /* check is done on this entry other than making sure it is NOT NULL.*/
static void FreeDiscoveryInfoEntryMemory(GDISDiscoveryInfo_t *EntryToFree)
{
   if(EntryToFree)
   {
      if(EntryToFree->ServiceInfoList)
         FreeServiceInfoList(&(EntryToFree->ServiceInfoList));

      if(EntryToFree->ServiceUUID)
         BTPS_FreeMemory(EntryToFree->ServiceUUID);
      
      BTPS_FreeMemory(EntryToFree);
   }
}

   /* The following function deletes (and free's all memory) every      */
   /* element of the specified Search Item List.  Upon return of this   */
   /* function, the Head Pointer is set to NULL.                        */
static void FreeDiscoveryInfoList(GDISDiscoveryInfo_t **ListHead)
{
   GDISDiscoveryInfo_t *EntryToFree;
   GDISDiscoveryInfo_t *tmpEntry;

   if(ListHead)
   {
      /* Simply traverse the list and free every element present.       */
      EntryToFree = *ListHead;

      while(EntryToFree)
      {
         tmpEntry    = EntryToFree;
         EntryToFree = EntryToFree->NextDiscoveryInfoPtr;

         FreeDiscoveryInfoEntryMemory(tmpEntry);
      }

      /* Make sure the List appears to be empty.                        */
      *ListHead = NULL;
   }
}

   /* The following function adds the specified Entry to the specified  */
   /* List.  This function allocates and adds an entry to the list that */
   /* has the same attributes as the Entry passed into this function.   */
   /* This function will return NULL if NO Entry was added.  This can   */
   /* occur if the element passed in was deemed invalid or the actual   */
   /* List Head was invalid.                                            */
   /* ** NOTE ** This function does not insert duplicate entries into   */
   /*            the list.  An element is considered a duplicate if the */
   /*            BD_ADDR ID field is the same as an entry already the   */
   /*            list.  When this occurs, this function returns in NULL.*/
static GDISServiceInfo_t *AddServiceInfoEntry(GDISServiceInfo_t **ListHead, GDISServiceInfo_t *EntryToAdd)
{
   return((GDISServiceInfo_t *)BSC_AddGenericListEntry(GDIS_SERVICE_INFO_DATA_SIZE, ekWord_t, BTPS_STRUCTURE_OFFSET(GDISServiceInfo_t, StartHandle), GDIS_SERVICE_INFO_DATA_SIZE, BTPS_STRUCTURE_OFFSET(GDISServiceInfo_t, NextServiceInfoPtr), (void **)(ListHead), (void *)(EntryToAdd)));
}

   /* The following function searches the specified Search Item List for*/
   /* the specified BD_ADDR and removes it from the List.  This function*/
   /* returns NULL if either the List Head is invalid or an Entry with  */
   /* the specified BD_ADDR was NOT present in the list.  The entry     */
   /* returned will have the Next Entry field set to NULL, and the      */
   /* caller is responsible for deleting the memory associated with this*/
   /* entry by calling the FreeSearchItemEntryMemory() function.        */
static GDISServiceInfo_t *DeleteServiceInfoEntry(GDISServiceInfo_t **ListHead, Word_t StartHandle)
{
   return((GDISServiceInfo_t *)BSC_DeleteGenericListEntry(ekWord_t, (void *)(&StartHandle), BTPS_STRUCTURE_OFFSET(GDISServiceInfo_t, StartHandle), BTPS_STRUCTURE_OFFSET(GDISServiceInfo_t, NextServiceInfoPtr), (void **)(ListHead)));
}

   /* This function frees the specified Search Item List member.  No    */
   /* check is done on this entry other than making sure it is NOT NULL.*/
static void FreeServiceInfoEntryMemory(GDISServiceInfo_t *EntryToFree)
{
   if(EntryToFree)
   {
      if(EntryToFree->CharacteristicInfoList)
         FreeCharacteristicInfoList(&(EntryToFree->CharacteristicInfoList));
      
      BTPS_FreeMemory(EntryToFree);
   }
}

   /* The following function deletes (and free's all memory) every      */
   /* element of the specified Search Item List.  Upon return of this   */
   /* function, the Head Pointer is set to NULL.                        */
static void FreeServiceInfoList(GDISServiceInfo_t **ListHead)
{
   GDISServiceInfo_t *EntryToFree;
   GDISServiceInfo_t *tmpEntry;

   if(ListHead)
   {
      /* Simply traverse the list and free every element present.       */
      EntryToFree = *ListHead;

      while(EntryToFree)
      {
         tmpEntry    = EntryToFree;
         EntryToFree = EntryToFree->NextServiceInfoPtr;

         FreeServiceInfoEntryMemory(tmpEntry);
      }

      /* Make sure the List appears to be empty.                        */
      *ListHead = NULL;
   }
}

   /* The following function adds the specified Entry to the specified  */
   /* List.  This function allocates and adds an entry to the list that */
   /* has the same attributes as the Entry passed into this function.   */
   /* This function will return NULL if NO Entry was added.  This can   */
   /* occur if the element passed in was deemed invalid or the actual   */
   /* List Head was invalid.                                            */
   /* ** NOTE ** This function does not insert duplicate entries into   */
   /*            the list.  An element is considered a duplicate if the */
   /*            BD_ADDR ID field is the same as an entry already the   */
   /*            list.  When this occurs, this function returns in NULL.*/
static GDISCharacteristicInfo_t *AddCharacteristicInfoEntry(GDISCharacteristicInfo_t **ListHead, GDISCharacteristicInfo_t *EntryToAdd)
{
   return((GDISCharacteristicInfo_t *)BSC_AddGenericListEntry(GDIS_CHARACTERISTIC_INFO_DATA_SIZE, ekWord_t, BTPS_STRUCTURE_OFFSET(GDISCharacteristicInfo_t, Characteristic_Handle), GDIS_CHARACTERISTIC_INFO_DATA_SIZE, BTPS_STRUCTURE_OFFSET(GDISCharacteristicInfo_t, NextCharacteristicInfoPtr), (void **)(ListHead), (void *)(EntryToAdd)));
}

   /* The following function searches the specified List for the        */
   /* specified Service Request ID.  This function returns NULL if      */
   /* either the List Head is invalid, the Service Request ID is        */
   /* invalid, or the specified Service Request ID was NOT found.       */
static GDISCharacteristicInfo_t *SearchCharacteristicInfoEntry(GDISCharacteristicInfo_t **ListHead, Word_t Characteristic_Handle)
{
   return((GDISCharacteristicInfo_t *)BSC_SearchGenericListEntry(ekWord_t, (void *)(&Characteristic_Handle), BTPS_STRUCTURE_OFFSET(GDISCharacteristicInfo_t, Characteristic_Handle), BTPS_STRUCTURE_OFFSET(GDISCharacteristicInfo_t, NextCharacteristicInfoPtr), (void **)(ListHead)));
}

   /* This function frees the specified Search Item List member.  No    */
   /* check is done on this entry other than making sure it is NOT NULL.*/
static void FreeCharacteristicInfoEntryMemory(GDISCharacteristicInfo_t *EntryToFree)
{
   if(EntryToFree)
   {
      if(EntryToFree->DescriptorList)
         FreeDescriptorInfoList(&(EntryToFree->DescriptorList));

      BTPS_FreeMemory(EntryToFree);
   }
}

   /* The following function deletes (and free's all memory) every      */
   /* element of the specified Search Item List.  Upon return of this   */
   /* function, the Head Pointer is set to NULL.                        */
static void FreeCharacteristicInfoList(GDISCharacteristicInfo_t **ListHead)
{
   GDISCharacteristicInfo_t *EntryToFree;
   GDISCharacteristicInfo_t *tmpEntry;

   if(ListHead)
   {
      /* Simply traverse the list and free every element present.       */
      EntryToFree = *ListHead;

      while(EntryToFree)
      {
         tmpEntry    = EntryToFree;
         EntryToFree = EntryToFree->NextCharacteristicInfoPtr;

         FreeCharacteristicInfoEntryMemory(tmpEntry);
      }

      /* Make sure the List appears to be empty.                        */
      *ListHead = NULL;
   }
}

   /* The following function adds the specified Entry to the specified  */
   /* List.  This function allocates and adds an entry to the list that */
   /* has the same attributes as the Entry passed into this function.   */
   /* This function will return NULL if NO Entry was added.  This can   */
   /* occur if the element passed in was deemed invalid or the actual   */
   /* List Head was invalid.                                            */
   /* ** NOTE ** This function does not insert duplicate entries into   */
   /*            the list.  An element is considered a duplicate if the */
   /*            BD_ADDR ID field is the same as an entry already the   */
   /*            list.  When this occurs, this function returns in NULL.*/
static GDISDescriptorInfo_t *AddDescriptorInfoEntry(GDISDescriptorInfo_t **ListHead, GDISDescriptorInfo_t *EntryToAdd)
{
   return((GDISDescriptorInfo_t *)BSC_AddGenericListEntry(GDIS_DESCRIPTOR_INFO_DATA_SIZE, ekWord_t, BTPS_STRUCTURE_OFFSET(GDISDescriptorInfo_t, Descriptor_Handle), GDIS_DESCRIPTOR_INFO_DATA_SIZE, BTPS_STRUCTURE_OFFSET(GDISDescriptorInfo_t, NextDescriptorInfoPtr), (void **)(ListHead), (void *)(EntryToAdd)));
}

   /* The following function deletes (and free's all memory) every      */
   /* element of the specified Search Item List.  Upon return of this   */
   /* function, the Head Pointer is set to NULL.                        */
static void FreeDescriptorInfoList(GDISDescriptorInfo_t **ListHead)
{
   BSC_FreeGenericListEntryList((void **)(ListHead), BTPS_STRUCTURE_OFFSET(GDISDescriptorInfo_t, NextDescriptorInfoPtr));
}

//xxx
static void CharacteristicDiscoveryRequest(GDISContext_t *GDISContext, GDISDiscoveryInfo_t *GDISDiscoveryInfo, GDISServiceInfo_t *GDISServiceInfo, Word_t StartHandle)
{
   int Result;

   /* Attempt to discover the characteristics of this service.          */
   Result = GATT_Discover_Characteristics(GDISContext->BluetoothStackID, GDISDiscoveryInfo->ConnectionID, StartHandle, GDISServiceInfo->EndHandle, GATT_ClientEventCallback, GDISDiscoveryInfo->ConnectionID);
   if(Result > 0)
   {
      /* Save the Transaction ID for this request.                      */
      GDISDiscoveryInfo->OutstandingTransactionID = (unsigned int)Result;
   }
   else
      DBG_MSG(DBG_ZONE_ANY, ("GATT_Discover_Characteristics returned %d.\r\n", Result));
}

//xxx
static Boolean_t CharacteristicDescriptorDiscoveryRequest(GDISContext_t *GDISContext, GDISDiscoveryInfo_t *GDISDiscoveryInfo, GDISServiceInfo_t *GDISServiceInfo, GDISCharacteristicInfo_t *CurrentCharacteristic, Word_t Handle)
{
   int                       Result;
   Word_t                    NextHandle;
   Boolean_t                 RequestIssued = FALSE;
   GDISCharacteristicInfo_t *tempEntry;
   
   /* Attempt to handle a case where there are too many descriptors that*/
   /* belong to a characteristic to fit in one response.                */
   if((CurrentCharacteristic) && (Handle) && (Handle < 0xFFFF))
   {
      if(CurrentCharacteristic->NextCharacteristicInfoPtr)
      {
         /* Subtract 1 from the next Characteristic Handle to take   */
         /* into account that the handle is for a Characteristic     */
         /* Value AND the handle before the Characteristic Handle is */
         /* the Characteristic Declaration which we don't care about.*/
         NextHandle = (Word_t)((CurrentCharacteristic->NextCharacteristicInfoPtr)->Characteristic_Handle-1);
      }
      else
         NextHandle = GDISServiceInfo->EndHandle;

      if((NextHandle - Handle) > 1)
      {
         /* Issue the request.                                          */
         Result = GATT_Discover_Characteristic_Descriptors(GDISContext->BluetoothStackID, GDISDiscoveryInfo->ConnectionID, (Word_t)Handle, (Word_t)(NextHandle-1), GATT_ClientEventCallback, GDISDiscoveryInfo->ConnectionID);
         if(Result > 0)
         {
            GDISDiscoveryInfo->OutstandingTransactionID = (unsigned int)Result;
            GDISServiceInfo->OutstandingRequestHandle   = CurrentCharacteristic->Characteristic_Handle;
            RequestIssued                               = TRUE;
         }
         else
            DBG_MSG(DBG_ZONE_DEVELOPMENT, ("GATT_Discover_Characteristic_Descriptors returned %d.\r\n", Result));
      }
   }

   /* Only continue if no request has been issued already.              */
   if(!RequestIssued)
   {
      /* Walk the Characteristic List to see where there may be         */
      /* descriptors.                                                   */
      if(!CurrentCharacteristic)
         CurrentCharacteristic = GDISServiceInfo->CharacteristicInfoList;
      else
         CurrentCharacteristic = CurrentCharacteristic->NextCharacteristicInfoPtr;     
        
      /* Walk the characteristic list starting from the characteristic  */
      /* we last did a Descriptor Discovery on, and determine if there  */
      /* may be characteristics.                                        */
      while(CurrentCharacteristic)
      {
         tempEntry = CurrentCharacteristic->NextCharacteristicInfoPtr;
         if(tempEntry)
         {
            /* Subtract 1 from the next Characteristic Handle to take   */
            /* into account that the handle is for a Characteristic     */
            /* Value AND the handle before the Characteristic Handle is */
            /* the Characteristic Declaration which we don't care about.*/
            NextHandle = (Word_t)(tempEntry->Characteristic_Handle-1);
         }
         else
         {
            if(GDISServiceInfo->EndHandle != 0xFFFF)
               NextHandle = (Word_t)(GDISServiceInfo->EndHandle+1);
            else
               NextHandle = 0xFFFF;
         }
         
         if(((NextHandle - CurrentCharacteristic->Characteristic_Handle) > 1) || ((NextHandle == 0xFFFF) && ((NextHandle - CurrentCharacteristic->Characteristic_Handle) >= 1)))
         {
            /* Issue the request.                                       */
            Result = GATT_Discover_Characteristic_Descriptors(GDISContext->BluetoothStackID, GDISDiscoveryInfo->ConnectionID, (Word_t)(CurrentCharacteristic->Characteristic_Handle+1), (Word_t)(NextHandle-1), GATT_ClientEventCallback, GDISDiscoveryInfo->ConnectionID);
            if(Result > 0)
            {
               GDISDiscoveryInfo->OutstandingTransactionID = (unsigned int)Result;
               GDISServiceInfo->OutstandingRequestHandle   = CurrentCharacteristic->Characteristic_Handle;
               RequestIssued                               = TRUE;
               break;
            }
            else
               DBG_MSG(DBG_ZONE_DEVELOPMENT, ("GATT_Discover_Characteristic_Descriptors returned %d.\r\n", Result));
         }
   
         /* Advance to the next entry.                                  */
         CurrentCharacteristic = tempEntry;
      }
   }

   return(RequestIssued);
}

   /* The following utility function is used to Compare 2 UUIDs.  The   */
   /* UUIDs are first checked to see if they are the same size.  If not,*/
   /* then both UUIDs will be converted to 128 bit UUIDs before         */
   /* compairing.                                                       */
static Boolean_t CompareServiceUUID(GATT_UUID_t *UUID_1, GATT_UUID_t *UUID_2)
{
   Boolean_t    ret_val;
   unsigned int UUID_Size_1;
   unsigned int UUID_Size_2;

   /* Verify that the parameters that were passed in appear valid.      */
   if((UUID_1) && (UUID_2))
   {
      UUID_Size_1 = (UUID_1->UUID_Type == guUUID_16)?UUID_16_SIZE:UUID_128_SIZE;
      UUID_Size_2 = (UUID_2->UUID_Type == guUUID_16)?UUID_16_SIZE:UUID_128_SIZE;

      /* Check to see if they are the same size.                        */
      if(UUID_Size_1 == UUID_Size_2)
      {
         /* Compare based on the type.                                  */
         if(UUID_1->UUID_Type == guUUID_16)
         {
            ret_val = (Boolean_t)COMPARE_UUID_16(UUID_1->UUID.UUID_16, UUID_2->UUID.UUID_16);
         }
         else
         {
            ret_val = (Boolean_t)COMPARE_UUID_128(UUID_1->UUID.UUID_128, UUID_2->UUID.UUID_128);
         }
      }
      else
      {
         /* Convert each into a 128 Bit UUID.                           */
         if(UUID_1->UUID_Type == guUUID_128)
         {
            ret_val = (Boolean_t)COMPARE_UUID_128_TO_UUID_16(UUID_1->UUID.UUID_128, UUID_2->UUID.UUID_16);
         }
         else
         {
            ret_val = (Boolean_t)COMPARE_UUID_128_TO_UUID_16(UUID_2->UUID.UUID_128, UUID_1->UUID.UUID_16);
         }
      }
   }
   else
      ret_val = FALSE;

   return(ret_val);
}

   /* The following function is a utility function that exists to check */
   /* if the specified UUID matches the UUID of a service that we are   */
   /* interested in discovery.  This function returns TRUE if the       */
   /* specified service is a service that should have service discovery */
   /* performed on or FALSE otherwise.                                  */
   /* * NOTE * This function is an internal function, as such no check  */
   /*          on the input parameters is performed.                    */
static Boolean_t CompareServiceToServiceList(GDISDiscoveryInfo_t *GDISDiscoveryInfo, GATT_UUID_t *UUID)
{
   Boolean_t    ret_val = FALSE;
   unsigned int Index;

   /* Determine if a list of services to search for was specified when  */
   /* this service discovery procedure was started.                     */
   if((GDISDiscoveryInfo->NumberOfUUID) && (GDISDiscoveryInfo->ServiceUUID))
   {
      /* Loop through the list of services to search for and compare to */
      /* the UUID that was passed in.                                   */
      for(Index=0;Index<GDISDiscoveryInfo->NumberOfUUID;Index++)
      {
         if(CompareServiceUUID(&(GDISDiscoveryInfo->ServiceUUID[Index]), UUID))
         {
            ret_val = TRUE;
            break;
         }
      }
   }
   else
   {
      /* No service list is specified so we should return TRUE here so  */
      /* that all services are discovered.                              */
      ret_val = TRUE;
   }

   return(ret_val);
}

   /* The following function is a utility function that exists to aid in*/
   /* configuring a service discovery by UUID operation.  This function */
   /* returns the Transaction ID of the transaction that is started or  */
   /* a negative error code.                                            */
   /* * NOTE * This function is an internal function, as such no check  */
   /*          on the input parameters is performed.                    */
static int ConfigureServiceDiscoveryByUUID(unsigned int BluetoothStackID, GDISDiscoveryInfo_t *DiscoveryInfoPtr, unsigned int NumberOfUUID, GATT_UUID_t *UUIDList)
{
   int ret_val;

   /* Allocate memory for the Service UUID list.                        */
   if((DiscoveryInfoPtr->ServiceUUID = BTPS_AllocateMemory(NumberOfUUID*sizeof(GATT_UUID_t))) != NULL)
   {
      /* Copy the caller specified UUID list into the array that we just*/
      /* allocated.                                                     */
      BTPS_MemCopy(DiscoveryInfoPtr->ServiceUUID, UUIDList, NumberOfUUID*sizeof(GATT_UUID_t));

      /* Save the number of UUIDs in the list.                          */
      DiscoveryInfoPtr->NumberOfUUID = NumberOfUUID;
      
      /* Attempt to start a service discovery operation.                */
      ret_val = GATT_Discover_Services(BluetoothStackID, DiscoveryInfoPtr->ConnectionID, 0x0001, 0xFFFF, GATT_ClientEventCallback, DiscoveryInfoPtr->ConnectionID);
   }
   else
      ret_val = GDIS_ERROR_INSUFFICIENT_RESOURCES;

   return(ret_val);
}
   
   /* The following function is used to provide a mechanism of moving the */
   /* service discovery state machine to the next state.                */
static void ServiceDiscoveryStateMachine(GDISContext_t *GDISContext, GDISDiscoveryInfo_t *GDISDiscoveryInfo)
{
   Boolean_t          MoveToNextService = FALSE;
   GDISServiceInfo_t *GDISServiceInfo = NULL;

   /* Determine the current state search state that we are in.          */
   switch(GDISDiscoveryInfo->SearchState)
   {
      case ssServiceDiscovery:
         /* If the Service Discovery process is done then we need to    */
         /* move onto Characteristic Discovery.                         */
         GDISDiscoveryInfo->SearchState = ssCharacteristiscDiscovery;
            
         /* Start a new Characteristic Discovery process.               */
         MoveToNextService              = TRUE;
         break;
      case ssCharacteristiscDiscovery:
         /* Move to the Characteristic Descriptor Discovery state.      */
         GDISDiscoveryInfo->SearchState = ssCharacteristicDescriptorDiscovery;
         
         /* The service that we will do characteristic descriptor       */
         /* discovery on is always the first service in the list.       */
         GDISServiceInfo = GDISDiscoveryInfo->ServiceInfoList;
         if(GDISServiceInfo)
         {
            /* Attempt to discovery characteristics on this service,    */
            /* beginning from the first characteristic.                 */
            if(!CharacteristicDescriptorDiscoveryRequest(GDISContext, GDISDiscoveryInfo, GDISServiceInfo, NULL, 0))
            {
               /* There were no locations found for descriptors for this*/
               /* service so go ahead and dispatch the Service Discovery*/
               /* Indication event.                                     */
               FormatAndDispatchDiscoveryCallback(GDISContext, GDISDiscoveryInfo, GDISServiceInfo);

               /* Flag that we want another Characteristic Discovery    */
               /* process started on the next service in the list.      */
               MoveToNextService = TRUE;
            }
         }
         else
            FormatAndDispatchDiscoveryCompleteCallback(GDISContext, GDISDiscoveryInfo->ConnectionID, GDIS_SERVICE_DISCOVERY_STATUS_SUCCESS);
         break;
      case ssCharacteristicDescriptorDiscovery:
         /* The service that we will do characteristic descriptor       */
         /* discovery on is always the first service in the list.       */
         GDISServiceInfo = GDISDiscoveryInfo->ServiceInfoList;
         if(GDISServiceInfo)
         {
            /* Dispatch the Service Discovery Indication Callback.      */
            /* * NOTE * This function will delete GDISServiceInfo from  */
            /*          the list.                                       */
            FormatAndDispatchDiscoveryCallback(GDISContext, GDISDiscoveryInfo, GDISServiceInfo);

            /* Flag that we want another Characteristic Discovery       */
            /* process started on the next service in the list.         */
            MoveToNextService = TRUE;
         }
         else
            FormatAndDispatchDiscoveryCompleteCallback(GDISContext, GDISDiscoveryInfo->ConnectionID, GDIS_SERVICE_DISCOVERY_STATUS_SUCCESS);
         break;
   }

   /* Determine if it has been requested that we start another          */
   /* characteristic discovery process on the next service in the list. */
   if(MoveToNextService)
   {
      /* The service that we will do characteristic discovery on is     */
      /* always the first service in the list.                          */
      GDISServiceInfo = GDISDiscoveryInfo->ServiceInfoList;
      if(GDISServiceInfo)
      {
         //xxx Move to Include
         /* Move to the Characteristic Discovery state for the Discovery*/
         /* operation.                                                  */
         GDISDiscoveryInfo->SearchState = ssCharacteristiscDiscovery;

         /* Start the Characteristic Discovery procedure on this        */
         /* service.                                                    */
         CharacteristicDiscoveryRequest(GDISContext, GDISDiscoveryInfo, GDISServiceInfo, GDISServiceInfo->StartHandle);
      }
      else
         FormatAndDispatchDiscoveryCompleteCallback(GDISContext, GDISDiscoveryInfo->ConnectionID, GDIS_SERVICE_DISCOVERY_STATUS_SUCCESS);
   }
}

   /* The following function is provided to allow a mechanism of        */
   /* formatting and performing a Service Discovery Callback.           */
static void FormatAndDispatchDiscoveryCallback(GDISContext_t *GDISContext, GDISDiscoveryInfo_t *GDISDiscoveryInfo, GDISServiceInfo_t *GDISServiceInfo)
{
   void                                         *EventBuffer;
   GDIS_Event_Data_t                             EventData;
   GDISDescriptorInfo_t                         *DescriptorInfoPtr;
   GDISCharacteristicInfo_t                     *CharacteristicInfoPtr;   
   GDIS_Characteristic_Information_t            *CurrentCharacteristic;
   GDIS_Service_Discovery_Indication_Data_t      DiscoveryIndicationData;
   GDIS_Characteristic_Descriptor_Information_t *CurrentDescriptor;

   /* Delete the Service Info entry that we are dispatching the         */
   /* discovery indication for.                                         */
   GDISServiceInfo = DeleteServiceInfoEntry(&(GDISDiscoveryInfo->ServiceInfoList), GDISServiceInfo->StartHandle);
   if(GDISServiceInfo)
   {
      /* Format the Header of the event.                                */
      EventData.Event_Data_Type                                   = etGDIS_Service_Discovery_Indication;
      EventData.Event_Data_Size                                   = GDIS_SERVICE_DISCOVERY_INDICATION_DATA_SIZE;
      EventData.Event_Data.GDIS_Service_Discovery_Indication_Data = &DiscoveryIndicationData;
   
      DiscoveryIndicationData.ConnectionID                        = GDISDiscoveryInfo->ConnectionID;
      DiscoveryIndicationData.ServiceInformation.Service_UUID     = GDISServiceInfo->Service_UUID;
      DiscoveryIndicationData.ServiceInformation.Start_Handle     = GDISServiceInfo->StartHandle;
      DiscoveryIndicationData.ServiceInformation.End_Handle       = GDISServiceInfo->EndHandle;
      DiscoveryIndicationData.NumberOfCharacteristics             = GDISServiceInfo->CharacteristicCount;
      DiscoveryIndicationData.CharacteristicInformationList       = NULL;
   
      /* Attempt to allocate memory for all of the characteristics and  */
      /* descriptors.                                                   */
      if((EventBuffer = BTPS_AllocateMemory(((GDISServiceInfo->CharacteristicCount * GDIS_CHARACTERISTIC_INFORMATION_DATA_SIZE) + (GDISServiceInfo->DescriptorCount * GDIS_CHARACTERISTIC_DESCRIPTOR_INFORMATION_DATA_SIZE)))) != NULL)
      {
         /* Setup of the characteristic and descriptor list pointers.   */
         CurrentCharacteristic                                 = (GDIS_Characteristic_Information_t *)EventBuffer;
         CurrentDescriptor                                     = (GDIS_Characteristic_Descriptor_Information_t *)(((Byte_t *)EventBuffer)+((unsigned int)(GDISServiceInfo->CharacteristicCount * GDIS_CHARACTERISTIC_INFORMATION_DATA_SIZE)));
         DiscoveryIndicationData.CharacteristicInformationList = CurrentCharacteristic;
   
         /* Walk the characteristic list.                               */
         CharacteristicInfoPtr                                 = GDISServiceInfo->CharacteristicInfoList;
         while(CharacteristicInfoPtr)
         {
            /* Format the current characteristic.                       */
            CurrentCharacteristic->Characteristic_UUID       = CharacteristicInfoPtr->Characteristic_UUID;
            CurrentCharacteristic->Characteristic_Handle     = CharacteristicInfoPtr->Characteristic_Handle;
            CurrentCharacteristic->Characteristic_Properties = CharacteristicInfoPtr->Characteristic_Properties;
            CurrentCharacteristic->NumberOfDescriptors       = CharacteristicInfoPtr->DecriptorCount;
            CurrentCharacteristic->DescriptorList            = NULL;

            /* Walk the Descriptor List for this Characteristic.        */
            DescriptorInfoPtr = CharacteristicInfoPtr->DescriptorList;
            while(DescriptorInfoPtr)
            {
               CurrentDescriptor->Characteristic_Descriptor_UUID   = DescriptorInfoPtr->Characteristic_UUID;
               CurrentDescriptor->Characteristic_Descriptor_Handle = DescriptorInfoPtr->Descriptor_Handle;

               /* If this is the first entry then set the DescriptorList*/
               /* pointer.                                              */
               if(CurrentCharacteristic->DescriptorList == NULL)
                  CurrentCharacteristic->DescriptorList = CurrentDescriptor;

               /* Advance the Current Descriptor pointer.               */
               CurrentDescriptor++;

               /* Advance to the next Descriptor.                       */
               DescriptorInfoPtr = DescriptorInfoPtr->NextDescriptorInfoPtr;
            }
            
            /* Advance the Current Characteristic pointer.              */
            CurrentCharacteristic++;

            /* Advance to the next entry.                               */
            CharacteristicInfoPtr = CharacteristicInfoPtr->NextCharacteristicInfoPtr;
         }
   
         __BTPSTRY
         {
            (*GDISDiscoveryInfo->ServiceDiscoveryCallback)(GDISContext->BluetoothStackID, &EventData, GDISDiscoveryInfo->CallbackParameter);
         }
         __BTPSEXCEPT(1)
         {
            /* Do Nothing.                                              */
         }
   
         /* Free the allocated memory.                                  */
         BTPS_FreeMemory(EventBuffer);
      }
      else
      {
         /* An error occurred allocating the memory but we will dispatch*/
         /* the event anyway.                                           */
         __BTPSTRY
         {
            (*GDISDiscoveryInfo->ServiceDiscoveryCallback)(GDISContext->BluetoothStackID, &EventData, GDISDiscoveryInfo->CallbackParameter);
         }
         __BTPSEXCEPT(1)
         {
            /* Do Nothing.                                              */
         }
      }

      /* Free the memory allocated for the Service Info entry.          */
      FreeServiceInfoEntryMemory(GDISServiceInfo);
   }
}

   /* The following function is provided to allow a mechanism of        */
   /* formatting and performing a Service Discovery Complete Callback.  */
static void FormatAndDispatchDiscoveryCompleteCallback(GDISContext_t *GDISContext, unsigned int ConnectionID, Byte_t Status)
{
   GDIS_Event_Data_t                       EventData;
   GDISDiscoveryInfo_t                    *GDISDiscoveryInfo;
   GDIS_Service_Discovery_Complete_Data_t  DiscoveryCompleteData;

   GDISDiscoveryInfo = DeleteDiscoveryInfoEntry(&(GDISContext->DiscoveryInfoList), ConnectionID);
   if(GDISDiscoveryInfo)
   {
      /* Format and Dispatch the Event.                                 */
      EventData.Event_Data_Type                                 = etGDIS_Service_Discovery_Complete;
      EventData.Event_Data_Size                                 = GDIS_SERVICE_DISCOVERY_COMPLETE_DATA_SIZE;
      EventData.Event_Data.GDIS_Service_Discovery_Complete_Data = &DiscoveryCompleteData;
   
      DiscoveryCompleteData.ConnectionID                        = GDISDiscoveryInfo->ConnectionID;
      DiscoveryCompleteData.Status                              = Status;
   
      /* Dispatch the event.                                            */
      __BTPSTRY
      {
         (*GDISDiscoveryInfo->ServiceDiscoveryCallback)(GDISContext->BluetoothStackID, &EventData, GDISDiscoveryInfo->CallbackParameter);
      }
      __BTPSEXCEPT(1)
      {
         /* Do Nothing.                                                 */
      }

      /* Free the memory allocated for this discovery process.          */
      FreeDiscoveryInfoEntryMemory(GDISDiscoveryInfo);
   }
}

   /* The following function is the GATT Client Callback that is used to*/
   /* process responses received to requests initiated by this discovery*/
   /* module.                                                           */
static void BTPSAPI GATT_ClientEventCallback(unsigned int BluetoothStackID, GATT_Client_Event_Data_t *GATT_Client_Event_Data, unsigned long CallbackParameter)
{
   int                                     Result;
   Word_t                                  Handle;
   unsigned int                            Index;
   GDISContext_t                          *GDISContextPtr;

   union
   {
      GDISServiceInfo_t                    GDISServiceInfo;
      GDISDescriptorInfo_t                 GDISDescriptorInfo;
      GDISCharacteristicInfo_t             GDISCharacteristicInfo;
   } Structure_Buffer;

   GDISServiceInfo_t                      *GDISServiceInfoPtr;
   GDISDiscoveryInfo_t                    *GDISDiscoveryInfo;
   GDISDescriptorInfo_t                   *GDISDescriptorInfoPtr;
   GDISCharacteristicInfo_t               *GDISCharacteristicInfoPtr;
   GATT_Service_Information_t             *ServiceInfo;
   GATT_Characteristic_Entry_t            *CharacteristicInfo;
   GATT_Characteristic_Descriptor_Entry_t *CharacteristicDescriptorInfo;

   /* Verify that all parameters to this callback are Semi-Valid.       */
   if(GATT_Client_Event_Data)
   {
      /* Lock the Bluetooth Stack to gain exclusive access to this      */
      /* Bluetooth Protocol Stack.                                      */
      if(!BSC_LockBluetoothStack(BluetoothStackID))
      {
         /* Acquire the List lock before grab the GDIS Context.         */
         if(BSC_AcquireListLock())
         {
            GDISContextPtr = SearchGDISContextEntry(&GDISContextList, BluetoothStackID);
            if(GDISContextPtr)
            {
               /* Release the previously acquired list lock.            */
               BSC_ReleaseListLock();
         
               /* Grab the Discovery Context for this request.          */
               GDISDiscoveryInfo = SearchDiscoveryInfoEntry(&(GDISContextPtr->DiscoveryInfoList), (unsigned int)CallbackParameter);
               if(GDISDiscoveryInfo)
               {
                  /* Determine the event that occurred.                 */
                  switch(GATT_Client_Event_Data->Event_Data_Type)
                  {
                     case etGATT_Client_Error_Response:
                        if(GATT_Client_Event_Data->Event_Data.GATT_Request_Error_Data)
                        {
                           /* Only print out the rest if it is valid.   */
                           if(GATT_Client_Event_Data->Event_Data.GATT_Request_Error_Data->ErrorType == retErrorResponse)
                           {
                              if(GATT_Client_Event_Data->Event_Data.GATT_Request_Error_Data->ErrorCode == ATT_PROTOCOL_ERROR_CODE_ATTRIBUTE_NOT_FOUND)
                              {
                                 /* Move to the next operation.         */
                                 ServiceDiscoveryStateMachine(GDISContextPtr, GDISDiscoveryInfo);
                              }
                              else
                                 FormatAndDispatchDiscoveryCompleteCallback(GDISContextPtr, GDISDiscoveryInfo->ConnectionID, GDIS_SERVICE_DISCOVERY_STATUS_RESPONSE_ERROR);
                           }
                           else
                              FormatAndDispatchDiscoveryCompleteCallback(GDISContextPtr, GDISDiscoveryInfo->ConnectionID, GDIS_SERVICE_DISCOVERY_STATUS_RESPONSE_TIMEOUT);
                        }
                        break;
                     case etGATT_Client_Service_Discovery_Response:
                        if(GATT_Client_Event_Data->Event_Data.GATT_Service_Discovery_Response_Data)
                        {
                           ServiceInfo = &(GATT_Client_Event_Data->Event_Data.GATT_Service_Discovery_Response_Data->ServiceInformationList[0]);
                           Handle      = 0xFFFF;
                           for(Index = 0; Index < (unsigned int)GATT_Client_Event_Data->Event_Data.GATT_Service_Discovery_Response_Data->NumberOfServices; Index++, ServiceInfo++)
                           {
                              /* Initialize the Service Info entry.     */
                              BTPS_MemInitialize(&(Structure_Buffer.GDISServiceInfo), 0, sizeof(GDISServiceInfo_t));

                              /* Save the information from the response.*/
                              Structure_Buffer.GDISServiceInfo.Service_UUID = ServiceInfo->UUID;
                              Structure_Buffer.GDISServiceInfo.StartHandle  = ServiceInfo->Service_Handle;
                              Structure_Buffer.GDISServiceInfo.EndHandle    = ServiceInfo->End_Group_Handle;

                              /* Set the Handle to the last End Handle +*/
                              /* 1 for the next request.                */
                              if(ServiceInfo->End_Group_Handle != 0xFFFF)
                                 Handle = (Word_t)(ServiceInfo->End_Group_Handle + 1);

                              /* Determine if this service should be    */
                              /* added to the list of services to search*/
                              /* for.                                   */
                              if(CompareServiceToServiceList(GDISDiscoveryInfo, &(Structure_Buffer.GDISServiceInfo.Service_UUID)))
                              {
                                 /* Add the Service Info Entry.         */
                                 GDISServiceInfoPtr = AddServiceInfoEntry(&(GDISDiscoveryInfo->ServiceInfoList), &(Structure_Buffer.GDISServiceInfo));
                                 if(GDISServiceInfoPtr == NULL)
                                 {
                                    DBG_MSG(DBG_ZONE_DEVELOPMENT, ("Failed to add Service Info Entry.\r\n"));
                                 }
                              }
                           }

                           /* Issue another Service Discovery request if*/
                           /* we have not reached the end of the client */
                           /* database.                                 */
                           if(Handle != 0xFFFF)
                           {
                              Result = GATT_Discover_Services(BluetoothStackID, GDISDiscoveryInfo->ConnectionID, Handle, 0xFFFF, GATT_ClientEventCallback, GDISDiscoveryInfo->ConnectionID);
                              if(Result > 0)
                                 GDISDiscoveryInfo->OutstandingTransactionID = (unsigned int)Result;
                              else
                              {
                                 DBG_MSG(DBG_ZONE_DEVELOPMENT, ("GATT_Discover_Services returned %d.\r\n", Result));

                                 FormatAndDispatchDiscoveryCompleteCallback(GDISContextPtr, GDISDiscoveryInfo->ConnectionID, GDIS_SERVICE_DISCOVERY_STATUS_UNKNOWN_ERROR);
                              }
                           }
                           else
                           {
                              /* Move to the next operation.            */
                              ServiceDiscoveryStateMachine(GDISContextPtr, GDISDiscoveryInfo);
                           }
                        }
                        break;
                     case etGATT_Client_Characteristic_Discovery_Response:
                        if(GATT_Client_Event_Data->Event_Data.GATT_Characteristic_Discovery_Response_Data)
                        {
                           /* Note that the service that we are doing   */
                           /* Characteristic Discovery on is ALWAYS the */
                           /* first entry in the list.  That is due to  */
                           /* the fact that we will delete the Service  */
                           /* Info entry when we have finished all      */
                           /* discovery processes on a given service.   */
                           GDISServiceInfoPtr = GDISDiscoveryInfo->ServiceInfoList;
                           if(GDISServiceInfoPtr)
                           {
                              /* Loop through the discovery             */
                              /* characteristics.                       */
                              CharacteristicInfo = &(GATT_Client_Event_Data->Event_Data.GATT_Characteristic_Discovery_Response_Data->CharacteristicEntryList[0]);
                              Handle             = GDISServiceInfoPtr->EndHandle;
                              for(Index = 0; Index < (unsigned int)GATT_Client_Event_Data->Event_Data.GATT_Characteristic_Discovery_Response_Data->NumberOfCharacteristics; Index++, CharacteristicInfo++)
                              {
                                 /* Initialize the Characteristic Info  */
                                 /* entry.                              */
                                 BTPS_MemInitialize(&(Structure_Buffer.GDISCharacteristicInfo), 0, sizeof(GDISCharacteristicInfo_t));
                                 
                                 Structure_Buffer.GDISCharacteristicInfo.Characteristic_UUID       = CharacteristicInfo->CharacteristicValue.CharacteristicUUID;
                                 Structure_Buffer.GDISCharacteristicInfo.Characteristic_Handle     = CharacteristicInfo->CharacteristicValue.CharacteristicValueHandle;
                                 Structure_Buffer.GDISCharacteristicInfo.Characteristic_Properties = CharacteristicInfo->CharacteristicValue.CharacteristicProperties;                              
                                 Handle                                                            = Structure_Buffer.GDISCharacteristicInfo.Characteristic_Handle;

                                 GDISCharacteristicInfoPtr = AddCharacteristicInfoEntry(&(GDISServiceInfoPtr->CharacteristicInfoList), &(Structure_Buffer.GDISCharacteristicInfo));
                                 if(GDISCharacteristicInfoPtr)
                                 {
                                    /* Update the Characteristic Counts.*/
                                    ++(GDISServiceInfoPtr->CharacteristicCount);
                                 }
                                 else
                                    DBG_MSG(DBG_ZONE_DEVELOPMENT, ("Failed to add Characteristic Info Entry.\r\n"));
                              }

                              /* Determine if we need to discover more  */
                              /* characteristics for this service.      */
                              if(Handle < GDISServiceInfoPtr->EndHandle)
                              {
                                 /* Issue another characteristic        */
                                 /* discovery request.                  */
                                 CharacteristicDiscoveryRequest(GDISContextPtr, GDISDiscoveryInfo, GDISServiceInfoPtr, (Word_t)(Handle+1));
                              }
                              else
                              {
                                 /* Move to the next operation.         */
                                 ServiceDiscoveryStateMachine(GDISContextPtr, GDISDiscoveryInfo);
                              }
                           }
                           else
                              DBG_MSG(DBG_ZONE_DEVELOPMENT, ("NULL Service list.\r\n"));
                        }
                        break;
                     case etGATT_Client_Characteristic_Descriptor_Discovery_Response:
                        if(GATT_Client_Event_Data->Event_Data.GATT_Characteristic_Descriptor_Discovery_Response_Data)
                        {
                           /* Note that the service that we are doing   */
                           /* Characteristic Descriptor Discovery on is */
                           /* ALWAYS the first entry in the list.  That */
                           /* is due to the fact that we will delete the*/
                           /* Service Info entry when we have finished  */
                           /* all discovery processes on a given        */
                           /* service.                                  */
                           GDISServiceInfoPtr = GDISDiscoveryInfo->ServiceInfoList;
                           if(GDISServiceInfoPtr)
                           {
                              /* Get the pointer to the characteristic  */
                              /* who own's these descriptors.           */
                              GDISCharacteristicInfoPtr = SearchCharacteristicInfoEntry(&(GDISServiceInfoPtr->CharacteristicInfoList), GDISServiceInfoPtr->OutstandingRequestHandle);
                              if(GDISCharacteristicInfoPtr)
                              {
                                 CharacteristicDescriptorInfo = &(GATT_Client_Event_Data->Event_Data.GATT_Characteristic_Descriptor_Discovery_Response_Data->CharacteristicDescriptorEntryList[0]);
                                 Handle                       = 0;
                                 for(Index = 0; Index < (unsigned int)GATT_Client_Event_Data->Event_Data.GATT_Characteristic_Descriptor_Discovery_Response_Data->NumberOfCharacteristicDescriptors; Index++, CharacteristicDescriptorInfo++)
                                 {
                                    /* Initialize the Descriptr Info    */
                                    /* entry.                           */
                                    BTPS_MemInitialize(&(Structure_Buffer.GDISDescriptorInfo), 0, sizeof(GDISDescriptorInfo_t));
                                    
                                    Structure_Buffer.GDISDescriptorInfo.Characteristic_UUID = CharacteristicDescriptorInfo->UUID;
                                    Structure_Buffer.GDISDescriptorInfo.Descriptor_Handle   = CharacteristicDescriptorInfo->Handle;
                                    Handle                                                  = CharacteristicDescriptorInfo->Handle;

                                    /* Increment the handle (if it is   */
                                    /* not the last handle in the       */
                                    /* database.                        */
                                    if(Handle < 0xFFFF)
                                       Handle++;

                                    GDISDescriptorInfoPtr = AddDescriptorInfoEntry(&(GDISCharacteristicInfoPtr->DescriptorList), &(Structure_Buffer.GDISDescriptorInfo));
                                    if(GDISDescriptorInfoPtr)
                                    {
                                       /* Update the Characteristic     */
                                       /* Descriptor Counts.            */
                                       ++(GDISServiceInfoPtr->DescriptorCount);
                                       ++(GDISCharacteristicInfoPtr->DecriptorCount);
                                    }
                                    else
                                       DBG_MSG(DBG_ZONE_DEVELOPMENT, ("Failed to add Characteristic Descriptor Entry.\r\n"));
                                 }

                                 /* Attempt to discovery more           */
                                 /* characteristics for this service.   */
                                 if(!CharacteristicDescriptorDiscoveryRequest(GDISContextPtr, GDISDiscoveryInfo, GDISServiceInfoPtr, GDISCharacteristicInfoPtr, Handle))
                                 {
                                    /* Move to the next operation.      */
                                    ServiceDiscoveryStateMachine(GDISContextPtr, GDISDiscoveryInfo);
                                 }
                              }
                           }
                        }
                        break;
                  }
               }
            }
            else
            {
               /* Release the previously acquired list lock.            */
               BSC_ReleaseListLock();
            }
         }

         /* UnLock the previously locked Bluetooth Stack.               */
         BSC_UnLockBluetoothStack(BluetoothStackID);
      }
   }
}

   /* The following function is the GATT Connection Callback that is    */
   /* used to monitor the status of connections that are being          */
   /* discovered by this module.                                        */
static void BTPSAPI GATT_Connection_Event_Callback(unsigned int BluetoothStackID, GATT_Connection_Event_Data_t *GATT_Connection_Event_Data, unsigned long CallbackParameter)
{
   GDISContext_t *GDISContextPtr;

   /* Verify that all parameters to this callback are Semi-Valid.       */
   if((GATT_Connection_Event_Data) && (GATT_Connection_Event_Data->Event_Data_Type == etGATT_Connection_Device_Disconnection))
   {
      /* Lock the Bluetooth Stack to gain exclusive access to this      */
      /* Bluetooth Protocol Stack.                                      */
      if(!BSC_LockBluetoothStack(BluetoothStackID))
      {
         /* Acquire the List lock before grab the GDIS Context.         */
         if(BSC_AcquireListLock())
         {
            GDISContextPtr = SearchGDISContextEntry(&GDISContextList, BluetoothStackID);
            if(GDISContextPtr)
            {
               /* Release the previously acquired list lock.            */
               BSC_ReleaseListLock();

               /* Verify that the event data is valid.                  */
               if(GATT_Connection_Event_Data->Event_Data.GATT_Device_Disconnection_Data)
               {
                  /* Dispatch the Discovery Complete Callback.          */
                  FormatAndDispatchDiscoveryCompleteCallback(GDISContextPtr, GATT_Connection_Event_Data->Event_Data.GATT_Device_Disconnection_Data->ConnectionID, GDIS_SERVICE_DISCOVERY_STATUS_DEVICE_DISCONNECTED);
               }
            }
            else
            {
               /* Release the previously acquired list lock.            */
               BSC_ReleaseListLock();
            }
         }

         /* UnLock the previously locked Bluetooth Stack.               */
         BSC_UnLockBluetoothStack(BluetoothStackID);
      }
   }
}

   /* The following function is responsible for making sure that the    */
   /* Bluetooth Stack DISC Module is Initialized correctly.  This       */
   /* function *MUST* be called before ANY other Bluetooth Stack DISC   */
   /* function can be called.  This function returns non-zero if the    */
   /* Module was initialized correctly, or a zero value if there was an */
   /* error.                                                            */
   /* * NOTE * Internally, this module will make sure that this function*/
   /*          has been called at least once so that the module will    */
   /*          function.  Calling this function from an external        */
   /*          location is not necessary.                               */
int InitializeGDISModule(void)
{
   return((int)InitializeModule());
}

   /* The following function is responsible for instructing the         */
   /* Bluetooth Stack DISC Module to clean up any resources that it has */
   /* allocated.  Once this function has completed, NO other Bluetooth  */
   /* Stack DISC Functions can be called until a successful call to the */
   /* InitializeDISCModule() function is made.  The parameter to this   */
   /* function specifies the context in which this function is being    */
   /* called.  If the specified parameter is TRUE, then the module will */
   /* make sure that NO functions that would require waiting/blocking on*/
   /* Mutexes/Events are called.  This parameter would be set to TRUE if*/
   /* this function was called in a context where threads would not be  */
   /* allowed to run.  If this function is called in the context where  */
   /* threads are allowed to run then this parameter should be set to   */
   /* FALSE.                                                            */
void CleanupGDISModule(Boolean_t ForceCleanup)
{
   /* Check to make sure that this module has been initialized.         */
   if(GDISContextListInitialized)
   {
      /* Wait for access to the DISC Information List.                  */
      if((ForceCleanup) || ((!ForceCleanup) && (BSC_AcquireListLock())))
      {
         /* Free the List and ALL its Resources.                        */
         FreeGDISContextList(&GDISContextList);

         if(!ForceCleanup)
            BSC_ReleaseListLock();
      }

      /* Cleanup the module.                                            */
      CleanupModule();
   }
}

   /* The following function is responsible for initializing an DISC    */
   /* Context Layer for the specified Bluetooth Protocol Stack.  This   */
   /* function will allocate and initialize an DISC Context Information */
   /* structure associated with the specified Bluetooth Stack ID.  This */
   /* function returns zero if successful, or a non-zero value if there */
   /* was an error.                                                     */
int BTPSAPI GDIS_Initialize(unsigned int BluetoothStackID)
{
   int            ret_val;
   GDISContext_t  DISCContext;
   GDISContext_t *DISCContextPtr;

   if(!GDISContextListInitialized)
      InitializeGDISModule();

   /* Make sure that the passed in parameters seem semi-valid.          */
   if((GDISContextListInitialized) && (BluetoothStackID))
   {
      /* Lock the Bluetooth Stack to gain exclusive access to this      */
      /* Bluetooth Protocol Stack.                                      */
      if(!BSC_LockBluetoothStack(BluetoothStackID))
      {
         /* Assign the information to the DISC Info Structure.          */
         BTPS_MemInitialize(&DISCContext, 0, GDIS_CONTEXT_DATA_SIZE);

         /* Set the default values for the DISC Info.                   */
         DISCContext.BluetoothStackID        = BluetoothStackID;

         /* Register a GATT Connection Callback.                        */
         ret_val = GATT_Register_Connection_Events(BluetoothStackID, GATT_Connection_Event_Callback, 0);
         if(ret_val > 0)
         {
            /* Save the Connection Callback ID.                         */
            DISCContext.ConnectionCallbackID = (unsigned int)ret_val;

            /* Acquire the List lock before we add the entry.           */
            if(BSC_AcquireListLock())
            {
               /* Add the information to the list.                      */
               DISCContextPtr = AddGDISContextEntry(&GDISContextList, &DISCContext);
               if(DISCContextPtr)
                  ret_val = 0;
               else
                  ret_val = GDIS_ERROR_INSUFFICIENT_RESOURCES;

               /* Release the previously acquired list lock.            */
               BSC_ReleaseListLock();
            }
            else
               ret_val = GDIS_ERROR_INVALID_PARAMETER;
         }
         else
            ret_val = GDIS_ERROR_INSUFFICIENT_RESOURCES;

         /* UnLock the previously locked Bluetooth Stack.               */
         BSC_UnLockBluetoothStack(BluetoothStackID);
      }
      else
         ret_val = GDIS_ERROR_INVALID_BLUETOOTH_STACK_ID;
   }
   else
      ret_val = GDIS_ERROR_INVALID_PARAMETER;

   return(ret_val);
}

   /* The following function is responsible for releasing any resources */
   /* that the DISC Layer associated with the Bluetooth Protocol Stack, */
   /* specified by the Bluetooth Stack ID, has allocated.  Upon         */
   /* completion of this function, ALL DISC functions will fail if used */
   /* on the specified Bluetooth Protocol Stack.                        */
void BTPSAPI GDIS_Cleanup(unsigned int BluetoothStackID)
{
   GDISContext_t *GDISContextPtr;

   /* Make sure that the passed in parameters seem semi-valid.          */
   if((GDISContextListInitialized) && (BluetoothStackID))
   {
      /* Lock the Bluetooth Stack to gain exclusive access to this      */
      /* Bluetooth Protocol Stack.                                      */
      if(!BSC_LockBluetoothStack(BluetoothStackID))
      {
         /* Acquire the List lock before we delete the entry.           */
         if(BSC_AcquireListLock())
         {
            /* Add the information to the list.                         */
            GDISContextPtr = DeleteGDISContextEntry(&GDISContextList, BluetoothStackID);
            if(GDISContextPtr)
            {
               FreeGDISContextEntryMemory(GDISContextPtr);

               if(GDISContextList == NULL)
               {
                  /* Cleanup the GDIS Module.                           */
                  CleanupGDISModule(TRUE);
               }
            }

            /* Release the previously acquired list lock.               */
            BSC_ReleaseListLock();
         }

         /* UnLock the previously locked Bluetooth Stack.               */
         BSC_UnLockBluetoothStack(BluetoothStackID);
      }
   }
}

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
int BTPSAPI GDIS_Service_Discovery_Start(unsigned int BluetoothStackID, unsigned int ConnectionID, unsigned int NumberOfUUID, GATT_UUID_t *UUIDList, GDIS_Event_Callback_t ServiceDiscoveryCallback, unsigned long ServiceDiscoveryCallbackParameter)
{
   int                  ret_val;
   GDISContext_t       *GDISContextPtr;
   GDISDiscoveryInfo_t  GDISDiscoverInfo;
   GDISDiscoveryInfo_t *GDISDiscoverInfoPtr;

   /* Verify that the input parameters are semi-valid.                  */
   if((GDISContextListInitialized) && (BluetoothStackID) && (ConnectionID) && (ServiceDiscoveryCallback))
   {
      /* Lock the Bluetooth Stack to gain exclusive access to this      */
      /* Bluetooth Protocol Stack.                                      */
      if(!BSC_LockBluetoothStack(BluetoothStackID))
      {
         /* Acquire the List lock before grab the GDIS Context.         */
         if(BSC_AcquireListLock())
         {
            GDISContextPtr = SearchGDISContextEntry(&GDISContextList, BluetoothStackID);
            if(GDISContextPtr)
            {
               /* Release the previously acquired list lock.            */
               BSC_ReleaseListLock();
               
               /* Verify that there is no Service Discovery Process     */
               /* outstanding for this device.                          */
               if(SearchDiscoveryInfoEntry(&(GDISContextPtr->DiscoveryInfoList), ConnectionID) == NULL)
               {
                  /* Initialize the Discovery Info entry.               */
                  BTPS_MemInitialize(&GDISDiscoverInfo, 0, sizeof(GDISDiscoveryInfo_t));

                  GDISDiscoverInfo.ConnectionID             = ConnectionID;
                  GDISDiscoverInfo.ServiceDiscoveryCallback = ServiceDiscoveryCallback;
                  GDISDiscoverInfo.CallbackParameter        = ServiceDiscoveryCallbackParameter;
                  GDISDiscoverInfo.SearchState              = ssServiceDiscovery;

                  /* Start a Service Discovery Request on this          */
                  /* connection.                                        */
                  if((NumberOfUUID) && (UUIDList))
                     ret_val = ConfigureServiceDiscoveryByUUID(BluetoothStackID, &GDISDiscoverInfo, NumberOfUUID, UUIDList);
                  else
                     ret_val = GATT_Discover_Services(BluetoothStackID, ConnectionID, 0x0001, 0xFFFF, GATT_ClientEventCallback, ConnectionID);

                  if(ret_val > 0)
                  {
                     /* Save the outstanding Transaction ID.            */
                     GDISDiscoverInfo.OutstandingTransactionID = (unsigned int)ret_val;

                     /* Add the Discovery Info Entry.                   */
                     GDISDiscoverInfoPtr = AddDiscoveryInfoEntry(&(GDISContextPtr->DiscoveryInfoList), &GDISDiscoverInfo);
                     if(GDISDiscoverInfoPtr)
                        ret_val = 0;
                     else
                     {
                        GATT_Cancel_Transaction(BluetoothStackID, GDISDiscoverInfo.OutstandingTransactionID);

                        ret_val = GDIS_ERROR_INSUFFICIENT_RESOURCES;
                     }
                  }

                  /* If an error occurred AND we allocated memory for   */
                  /* the Service UUID array, then we should free this   */
                  /* memory now.                                        */
                  if((ret_val) && (GDISDiscoverInfo.ServiceUUID))
                     BTPS_FreeMemory(GDISDiscoverInfo.ServiceUUID);
               }
               else
                  ret_val = GDIS_ERROR_SERVICE_DISCOVERY_OUTSTANDING;
            }
            else
            {
               /* Release the previously acquired list lock.            */
               BSC_ReleaseListLock();

               ret_val = GDIS_ERROR_NOT_INITIALIZED;
            }
         }
         else
            ret_val = GDIS_ERROR_INVALID_PARAMETER;

         /* UnLock the previously locked Bluetooth Stack.               */
         BSC_UnLockBluetoothStack(BluetoothStackID);
      }
      else
         ret_val = GDIS_ERROR_INVALID_BLUETOOTH_STACK_ID;
   }
   else
      ret_val = GDIS_ERROR_INVALID_PARAMETER;

   return(ret_val);
}

   /* The following function is used to terminate the Device Discovery  */
   /* process.  The function takes as its parameter the BluetoothStackID*/
   /* that is associated with the Bluetooth Device.  The function       */
   /* returns a negative return value if there was an error and Zero in */
   /* success.                                                          */
   /* * NOTE * This function will cancel any discovery operations that  */
   /*          are currently in progress and release all request        */
   /*          information in the queue that are waiting to be executed.*/
int BTPSAPI GDIS_Service_Discovery_Stop(unsigned int BluetoothStackID, unsigned int ConnectionID)
{
   int                  ret_val;
   GDISContext_t       *GDISContextPtr;
   GDISDiscoveryInfo_t *GDISDiscoverInfoPtr;

   /* Verify that the input parameters are semi-valid.                  */
   if((GDISContextListInitialized) && (BluetoothStackID) && (ConnectionID))
   {
      /* Lock the Bluetooth Stack to gain exclusive access to this      */
      /* Bluetooth Protocol Stack.                                      */
      if(!BSC_LockBluetoothStack(BluetoothStackID))
      {
         /* Acquire the List lock before grab the GDIS Context.         */
         if(BSC_AcquireListLock())
         {
            GDISContextPtr = SearchGDISContextEntry(&GDISContextList, BluetoothStackID);
            if(GDISContextPtr)
            {
               /* Release the previously acquired list lock.            */
               BSC_ReleaseListLock();

               /* Delete the Discovery Info Entry.                      */
               GDISDiscoverInfoPtr = DeleteDiscoveryInfoEntry(&(GDISContextPtr->DiscoveryInfoList), ConnectionID);
               if(GDISDiscoverInfoPtr)
               {
                  /* Cancel the outstanding transaction.                */
                  GATT_Cancel_Transaction(BluetoothStackID, GDISDiscoverInfoPtr->OutstandingTransactionID);
                  
                  /* Free the memory allocated for the discovery entry. */
                  FreeDiscoveryInfoEntryMemory(GDISDiscoverInfoPtr);

                  /* Return success to the caller.                      */
                  ret_val = 0;
               }
               else
                  ret_val = GDIS_ERROR_INVALID_PARAMETER;
            }
            else
            {
               /* Release the previously acquired list lock.            */
               BSC_ReleaseListLock();

               ret_val = GDIS_ERROR_NOT_INITIALIZED;
            }
         }
         else
            ret_val = GDIS_ERROR_INVALID_PARAMETER;

         /* UnLock the previously locked Bluetooth Stack.               */
         BSC_UnLockBluetoothStack(BluetoothStackID);
      }
      else
         ret_val = GDIS_ERROR_INVALID_BLUETOOTH_STACK_ID;
   }
   else
      ret_val = GDIS_ERROR_INVALID_PARAMETER;

   return(ret_val);
}
