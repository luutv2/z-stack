#include "ZComdef.h"
#include "OSAL.h"
#include "AddrMgr.h"
#include "AssocList.h"
#include "BindingTable.h"

/* NWK */
#include "nwk_util.h"
#include "nwk_globals.h"
#include "APS.h"

/* Security */
#include "ssp.h"

/* ZDO */
#include "rtg.h"
#include "ZDConfig.h"
#include "ZGlobals.h"

/* ZMain */
#include "OnBoard.h"

/* HAL */
#include "hal_lcd.h"
#include "hal_timer.h"

// Maximums for the data buffer queue
#define NWK_MAX_DATABUFS_WAITING    8     // Waiting to be sent to MAC
#define NWK_MAX_DATABUFS_SCHEDULED  5     // Timed messages to be sent
#define NWK_MAX_DATABUFS_CONFIRMED  5     // Held after MAC confirms
#define NWK_MAX_DATABUFS_TOTAL      12    // Total number of buffers

// 1-255 (0 -> 256) X RTG_TIMER_INTERVAL
// A known shortcoming is that when a message is enqueued as "hold" for a
// sleeping device, the timer tick may have counted down to 1, so that msg
// will not be held as long as expected. If NWK_INDIRECT_MSG_TIMEOUT is set to 1
// the hold time will vary randomly from 0 - CNT_RTG_TIMER ticks.
// So the hold time will vary within this interval:
// { (NWK_INDIRECT_MSG_TIMEOUT-1)*CNT_RTG_TIMER,
//                                    NWK_INDIRECT_MSG_TIMEOUT*CNT_RTG_TIMER }
#define NWK_INDIRECT_CNT_RTG_TMR    1
// To hold msg for sleeping end devices for 30 secs:
// #define CNT_RTG_TIMER            1
// #define NWK_INDIRECT_MSG_TIMEOUT 30
// To hold msg for sleeping end devices for 30 mins:
// #define CNT_RTG_TIMER            60
// #define NWK_INDIRECT_MSG_TIMEOUT 30
// To hold msg for sleeping end devices for 30 days:
// #define CNT_RTG_TIMER            60
// #define NWK_INDIRECT_MSG_TIMEOUT (30 * 24 * 60)
// Maximum msgs to hold per associated device.
#define NWK_INDIRECT_MSG_MAX_PER    3
// Maximum total msgs to hold for all associated devices.
#define NWK_INDIRECT_MSG_MAX_ALL    \
                            (NWK_MAX_DATABUFS_TOTAL - NWK_INDIRECT_MSG_MAX_PER)

// Variables for MAX list size
CONST uint16 gNWK_MAX_DEVICE_LIST = NWK_MAX_DEVICES;

// Variables for MAX Sleeping End Devices
CONST uint8 gNWK_MAX_SLEEPING_END_DEVICES = NWK_MAX_DEVICES - NWK_MAX_ROUTERS;

// Variables for MAX data buffer levels
CONST uint8 gNWK_MAX_DATABUFS_WAITING = NWK_MAX_DATABUFS_WAITING;
CONST uint8 gNWK_MAX_DATABUFS_SCHEDULED = NWK_MAX_DATABUFS_SCHEDULED;
CONST uint8 gNWK_MAX_DATABUFS_CONFIRMED = NWK_MAX_DATABUFS_CONFIRMED;
CONST uint8 gNWK_MAX_DATABUFS_TOTAL = NWK_MAX_DATABUFS_TOTAL;

CONST uint8 gNWK_INDIRECT_CNT_RTG_TMR = NWK_INDIRECT_CNT_RTG_TMR;
CONST uint8 gNWK_INDIRECT_MSG_MAX_PER = NWK_INDIRECT_MSG_MAX_PER;
CONST uint8 gNWK_INDIRECT_MSG_MAX_ALL = NWK_INDIRECT_MSG_MAX_ALL;

// change this if using a different stack profile...
// Cskip array
uint16 *Cskip;

#if ( STACK_PROFILE_ID == ZIGBEEPRO_PROFILE )
  uint8 CskipRtrs[1] = {0};
  uint8 CskipChldrn[1] = {0};
#elif ( STACK_PROFILE_ID == HOME_CONTROLS )
  uint8 CskipRtrs[MAX_NODE_DEPTH+1] = {6,6,6,6,6,0};
  uint8 CskipChldrn[MAX_NODE_DEPTH+1] = {20,20,20,20,20,0};
#elif ( STACK_PROFILE_ID == GENERIC_STAR )
  uint8 CskipRtrs[MAX_NODE_DEPTH+1] = {5,5,5,5,5,0};
  uint8 CskipChldrn[MAX_NODE_DEPTH+1] = {5,5,5,5,5,0};
#elif ( STACK_PROFILE_ID == NETWORK_SPECIFIC )
  uint8 CskipRtrs[MAX_NODE_DEPTH+1] = {5,5,5,5,5,0};
  uint8 CskipChldrn[MAX_NODE_DEPTH+1] = {5,5,5,5,5,0};
#endif // STACK_PROFILE_ID

// Minimum lqi value that is required for association
uint8 gMIN_TREE_LINK_COST = MIN_LQI_COST_3;

// Statically defined Associated Device List
associated_devices_t AssociatedDevList[NWK_MAX_DEVICES];

CONST uint8 gMAX_RTG_ENTRIES = MAX_RTG_ENTRIES;
CONST uint8 gMAX_RTG_SRC_ENTRIES = MAX_RTG_SRC_ENTRIES;
CONST uint8 gMAX_RREQ_ENTRIES = MAX_RREQ_ENTRIES;

CONST uint8 gMAX_NEIGHBOR_ENTRIES = MAX_NEIGHBOR_ENTRIES;

 // Table of neighboring nodes (not including child nodes)
neighborEntry_t neighborTable[MAX_NEIGHBOR_ENTRIES];

CONST uint8 gMAX_SOURCE_ROUTE = MAX_SOURCE_ROUTE;

CONST uint8 gMAX_BROADCAST_QUEUED = MAX_BROADCAST_QUEUED;

// Routing table
rtgEntry_t rtgTable[MAX_RTG_ENTRIES];

#if defined ( ZIGBEE_SOURCE_ROUTING )
  rtgSrcEntry_t rtgSrcTable[MAX_RTG_SRC_ENTRIES];
  uint16 rtgSrcRelayList[MAX_SOURCE_ROUTE];
#endif

// Table of current RREQ packets in the network
rtDiscEntry_t rtDiscTable[MAX_RREQ_ENTRIES];

// Table of data broadcast packets currently in circulation.
bcastEntry_t bcastTable[MAX_BCAST];

// These 2 arrays are to be used as an array of struct { uint8, uint32 }.
uint8 bcastHoldHandle[MAX_BCAST];
uint32 bcastHoldAckMask[MAX_BCAST];

CONST uint8 gMAX_BCAST = MAX_BCAST;

// For tree addressing, this switch allows the allocation of a 
// router address to an end device when end device address are 
// all used up.  If this option is enabled, address space
// could be limited.
CONST uint8 gNWK_TREE_ALLOCATE_ROUTERADDR_FOR_ENDDEVICE = FALSE;

#if defined ( ZIGBEE_STOCHASTIC_ADDRESSING )
// number of link status periods after the last received address conflict report
// (network status command)
CONST uint8 gNWK_CONFLICTED_ADDR_EXPIRY_TIME = NWK_CONFLICTED_ADDR_EXPIRY_TIME;
#endif

#if defined ( ZIGBEE_FREQ_AGILITY )
CONST uint8 gNWK_FREQ_AGILITY_ALL_MAC_ERRS = NWK_FREQ_AGILITY_ALL_MAC_ERRS;
#endif
  
/*********************************************************************
 * APS GLOBAL VARIABLES
 */

// The Maximum number of binding records
// This number is defined in BindingTable.h - change it there.
CONST uint16 gNWK_MAX_BINDING_ENTRIES = NWK_MAX_BINDING_ENTRIES;

#if defined ( REFLECTOR )
  // The Maximum number of cluster IDs in a binding record
  // This number is defined in BindingTable.h - change it there.
  CONST uint8 gMAX_BINDING_CLUSTER_IDS = MAX_BINDING_CLUSTER_IDS;

  CONST uint16 gBIND_REC_SIZE = sizeof( BindingEntry_t );

  // Binding Table
  BindingEntry_t BindingTable[NWK_MAX_BINDING_ENTRIES];
#endif

// Maximum number allowed in the groups table.
CONST uint8 gAPS_MAX_GROUPS = APS_MAX_GROUPS;

// APS End Device Broadcast Table
#if ( ZG_BUILD_ENDDEVICE_TYPE )
  apsEndDeviceBroadcast_t apsEndDeviceBroadcastTable[APS_MAX_ENDDEVICE_BROADCAST_ENTRIES];
  uint8 gAPS_MAX_ENDDEVICE_BROADCAST_ENTRIES = APS_MAX_ENDDEVICE_BROADCAST_ENTRIES;
#endif

/*********************************************************************
 * SECURITY GLOBAL VARIABLES
 */

// This is the default pre-configured key,
// change this to make a unique key
// SEC_KEY_LEN is defined in ssp.h.
CONST uint8 defaultKey[SEC_KEY_LEN] =
{
#if defined ( APP_TP ) || defined ( APP_TP2 )
  // Key for ZigBee Conformance Testing
  0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa
#else
  // Key for In-House Testing
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
#endif
};

// This is the default pre-configured Trust Center Link key,
// change this to make a unique key, SEC_KEY_LEN is defined in ssp.h.
CONST uint8 defaultTCLinkKey[SEC_KEY_LEN] =
{
  0x56, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,
  0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77
};

/*********************************************************************
 * STATUS STRINGS
 */
#if defined ( LCD_SUPPORTED )
  const char PingStr[]         = "Ping Rcvd from";
  const char AssocCnfStr[]     = "Assoc Cnf";
  const char SuccessStr[]      = "Success";
  const char EndDeviceStr[]    = "EndDevice:";
  const char ParentStr[]       = "Parent:";
  const char ZigbeeCoordStr[]  = "ZigBee Coord";
  const char NetworkIDStr[]    = "Network ID:";
  const char RouterStr[]       = "Router:";
  const char OrphanRspStr[]    = "Orphan Response";
  const char SentStr[]         = "Sent";
  const char FailedStr[]       = "Failed";
  const char AssocRspFailStr[] = "Assoc Rsp fail";
  const char AssocIndStr[]     = "Assoc Ind";
  const char AssocCnfFailStr[] = "Assoc Cnf fail";
  const char EnergyLevelStr[]  = "Energy Level";
  const char ScanFailedStr[]   = "Scan Failed";
#endif

void nwk_globals_init( void )
{
  AddrMgrInit( NWK_MAX_ADDRESSES );
  
#if !defined ( ZIGBEE_STOCHASTIC_ADDRESSING )
  if ( ZSTACK_ROUTER_BUILD )
  {
    // Initialize the Cskip Table
    Cskip = osal_mem_alloc(sizeof(uint16) *(MAX_NODE_DEPTH+1));
    RTG_FillCSkipTable(CskipChldrn, CskipRtrs, MAX_NODE_DEPTH, Cskip);
  }
#endif
  
  // To compile out the Link Status Feature, set NWK_LINK_STATUS_PERIOD
  // to 0 (compiler flag).
  if ( ZSTACK_ROUTER_BUILD && NWK_LINK_STATUS_PERIOD )
  {
    NLME_InitLinkStatus();
  }
  
#if defined ( ZIGBEE_FREQ_AGILITY )
  NwkFreqAgilityInit();
#endif
}

void NIB_init()
{
  _NIB.SequenceNum = LO_UINT16(osal_rand());

  _NIB.nwkProtocolVersion = ZB_PROT_VERS;
  _NIB.MaxDepth = MAX_NODE_DEPTH;

#if ( NWK_MODE == NWK_MODE_MESH )
  _NIB.beaconOrder = BEACON_ORDER_NO_BEACONS;
  _NIB.superFrameOrder = BEACON_ORDER_NO_BEACONS;
#endif

  _NIB.BroadcastDeliveryTime = zgBcastDeliveryTime;
  _NIB.PassiveAckTimeout     = zgPassiveAckTimeout;
  _NIB.MaxBroadcastRetries   = zgMaxBcastRetires;

  _NIB.ReportConstantCost = 0;
  _NIB.RouteDiscRetries = 0;
  _NIB.SecureAllFrames = USE_NWK_SECURITY;
  _NIB.nwkAllFresh = NWK_ALL_FRESH;
   
  if ( ZG_SECURE_ENABLED )
  {
    _NIB.SecurityLevel = SECURITY_LEVEL;
  }
  else
  {
    _NIB.SecurityLevel = 0;
  }
   
#if defined ( ZIGBEEPRO )
  _NIB.SymLink = FALSE;
#else
  _NIB.SymLink = TRUE;
#endif  
  
  _NIB.CapabilityInfo = ZDO_Config_Node_Descriptor.CapabilityFlags;

  _NIB.TransactionPersistenceTime = zgIndirectMsgTimeout;

  _NIB.RouteDiscoveryTime = 5;
  _NIB.RouteExpiryTime = zgRouteExpiryTime;

  _NIB.nwkDevAddress = INVALID_NODE_ADDR;
  _NIB.nwkLogicalChannel = 0;
  _NIB.nwkCoordAddress = INVALID_NODE_ADDR;
  osal_memset( _NIB.nwkCoordExtAddress, 0, Z_EXTADDR_LEN );
  _NIB.nwkPanId = INVALID_NODE_ADDR;

  osal_cpyExtAddr( _NIB.extendedPANID, zgExtendedPANID );
   
  _NIB.nwkKeyLoaded = FALSE;
   
#if defined ( ZIGBEE_STOCHASTIC_ADDRESSING )
  _NIB.nwkAddrAlloc  = NWK_ADDRESSING_STOCHASTIC;
  _NIB.nwkUniqueAddr = FALSE;
#else
  _NIB.nwkAddrAlloc  = NWK_ADDRESSING_DISTRIBUTED;
  _NIB.nwkUniqueAddr = TRUE;
#endif

  _NIB.nwkLinkStatusPeriod = NWK_LINK_STATUS_PERIOD; 
  _NIB.nwkRouterAgeLimit = NWK_ROUTE_AGE_LIMIT;
 
  //MTO and source routing
  _NIB.nwkConcentratorDiscoveryTime = zgConcentratorDiscoveryTime;
  _NIB.nwkIsConcentrator = zgConcentratorEnable;
  _NIB.nwkConcentratorRadius = zgConcentratorRadius;
  _NIB.nwkSrcRtgExpiryTime = SRC_RTG_EXPIRY_TIME;

#if defined ( ZIGBEE_MULTICAST )
  _NIB.nwkUseMultiCast = TRUE;
#else
  _NIB.nwkUseMultiCast = FALSE;
#endif  
  _NIB.nwkManagerAddr = 0x0000;
  _NIB.nwkUpdateId = 0;
  _NIB.nwkTotalTransmissions = 0;

  if ( ZSTACK_ROUTER_BUILD )
  {
#if defined ( ZIGBEE_STOCHASTIC_ADDRESSING )
    NLME_InitStochasticAddressing();
#else
    NLME_InitTreeAddressing();
#endif
  }
}

void nwk_Status( uint16 statusCode, uint16 statusValue )
{
  switch ( statusCode )
  {
    case NWK_STATUS_COORD_ADDR: // The state is coordinator addr
      show("NWK_STATUS_COORD_ADDR");
      if ( ZSTACK_ROUTER_BUILD )
      {
        #if defined (LCD_SUPPORTED) 
            HalLcdWriteString( (char*)ZigbeeCoordStr, HAL_LCD_LINE_1 );
            HalLcdWriteStringValue( (char*)NetworkIDStr, statusValue, 16, HAL_LCD_LINE_2 );
            #if defined (Location) // The location initilization display state
                halMcuWaitMs(300);
                HalLcd_HW_Clear();
                #if defined (CoordinatorKB)
                    HalLcdWriteString("[  ,   ][  ,   ]", HAL_LCD_LINE_1);
                    HalLcdWriteString("[  ,   ][  ,   ]", HAL_LCD_LINE_2);
                #endif
                #if defined (CoordinatorEB)
                    HalLcdWriteString("ID:     ,[     ]", HAL_LCD_LINE_1);
                    HalLcdWriteString("ID:     ,[     ]", HAL_LCD_LINE_2);
                    HalLcdWriteString("ID:     ,[     ]", HAL_LCD_LINE_3);
                    HalLcdWriteString("ID:     ,[     ]", HAL_LCD_LINE_4);
                #endif
            #endif
        #endif
      }
      break;

    case NWK_STATUS_ROUTER_ADDR: // The state is router addr
      show("NWK_STATUS_ROUTER_ADDR");
      if ( ZSTACK_ROUTER_BUILD )
      {
        #if defined (LCD_SUPPORTED)
            HalLcdWriteStringValue( (char*)RouterStr, statusValue, 16, HAL_LCD_LINE_1 ); 
        #endif
      }
      break;

    case NWK_STATUS_ORPHAN_RSP:
      show("NWK_STATUS_ORPHAN_RSP");
      if ( ZSTACK_ROUTER_BUILD )
      {
        #if defined (LCD_SUPPORTED)
        if ( statusValue == ZSuccess )
            HalLcdWriteScreen( (char*)OrphanRspStr, (char*)SentStr );
        else
            HalLcdWriteScreen( (char*)OrphanRspStr, (char*)FailedStr );
        #endif
      }
      break;

    case NWK_ERROR_ASSOC_RSP:
      show("NWK_ERROR_ASSOC_RSP");
      if ( ZSTACK_ROUTER_BUILD )
      {
        #if defined (LCD_SUPPORTED)
            HalLcdWriteString( (char*)AssocRspFailStr, HAL_LCD_LINE_1 );
            HalLcdWriteValue( (uint32)(statusValue), 16, HAL_LCD_LINE_2 );
        #endif
      }
      break;
      
    case NWK_STATUS_ED_ADDR: // The state is end-device addr
      show("NWK_STATUS_ED_ADDR");
      if ( ZSTACK_END_DEVICE_BUILD )
      {
        #if defined (LCD_SUPPORTED)
            HalLcdWriteStringValue( (char*)EndDeviceStr, statusValue, 16, HAL_LCD_LINE_1 );
        #endif
      }
      break;

    case NWK_STATUS_PARENT_ADDR: // The state is parent addr
      show("NWK_STATUS_PARENT_ADDR");
      #if defined (LCD_SUPPORTED)      
          HalLcdWriteStringValue( (char*)ParentStr, statusValue, 16, HAL_LCD_LINE_2 );
          #if defined (Location)
              halMcuWaitMs(300);
              HalLcd_HW_Clear();
              #if defined (RouterKB)
                  HalLcdWriteString("[  ,   ][  ,   ]", HAL_LCD_LINE_1);
                  HalLcdWriteString("[  ,   ][  ,   ]", HAL_LCD_LINE_2);
              #endif
              #if defined (RouterEB)
                  HalLcdWriteString("ID:     ,[     ]", HAL_LCD_LINE_1);
                  HalLcdWriteString("ID:     ,[     ]", HAL_LCD_LINE_2);
                  HalLcdWriteString("ID:     ,[     ]", HAL_LCD_LINE_3);
                  HalLcdWriteString("ID:     ,[     ]", HAL_LCD_LINE_4);
              #endif
          #endif 
      #endif
      break;

    case NWK_STATUS_ASSOC_CNF:
      show("NWK_STATUS_ASSOC_CNF");
      #if defined (LCD_SUPPORTED)
          HalLcdWriteScreen( (char*)AssocCnfStr, (char*)SuccessStr );
      #endif
      break;

    case NWK_ERROR_ASSOC_CNF_DENIED:
      show("NWK_ERROR_ASSOC_CNF_DENIED");
      #if defined (LCD_SUPPORTED)
          HalLcdWriteString((char*)AssocCnfFailStr, HAL_LCD_LINE_1 );
          HalLcdWriteValue( (uint32)(statusValue), 16, HAL_LCD_LINE_2 );
      #endif
          halMcuWaitMs(5000); // delay 5 seconds
          WatchDogEnable( WDTISH ); // Use watch dog to reset the system
      break;

    case NWK_ERROR_ENERGY_SCAN_FAILED: // The state is scan failed
      show("NWK_ERROR_ENERGY_SCAN_FAILED");
      #if defined (LCD_SUPPORTED)
          HalLcdWriteScreen( (char*)EnergyLevelStr, (char*)ScanFailedStr );
      #endif
      break;
  }
}