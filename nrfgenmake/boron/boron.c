#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "assert.h"
#include "nordic_common.h"
#include "app_timer.h"
#include "app_scheduler.h"
#include "app_error.h"
#include "app_util.h"
#include "bsp_btn_ble.h"
#include "ble.h"
#include "ble_gap.h"
#include "ble_hci.h"
#include "ble_dis.h"
#include "ble_db_discovery.h"
#include "peer_manager.h"
#include "peer_manager_handler.h"
#include "fds.h"
#include "nrf_fstorage.h"
#include "ble_conn_state.h"
#include "nrfx_wdt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrf_ble_scan.h"
#include "nrf_ble_gatt.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_fstorage.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "boron.h"

#define MANUFACTURER_NAME "Signal Labs"
#define APP_TIMER_OP_QUEUE_SIZE 3
#define APP_TIMER_PRESCALER 0 
#define BLE_UUID_OUR_BASE_UUID {0x23, 0xD1, 0x13, 0xEF, 0x5F, 0x78, 0x23, 0x15, 0xDE, 0xEF, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00}
#define BLE_UUID_OUR_SERVICE 0x152C
#define CLOCK_READ_UUID 0x3907
#define CLOCK_READ_INDX 0
#define CLOCK_WRITE_UUID 0x3909
#define CLOCK_WRITE_INDX 1
#define NUMBER_OUR_ATTRIBUTES 2

#define SCHED_MAX_EVENT_DATA_SIZE 16 
// (was previously APP_TIMER_SCHED_EVENT_DATA_SIZE, which is 8)
#define SCHED_QUEUE_SIZE 10

// for bonding, copied from app_rscs
#define SEC_PARAM_BOND              1  /**< Perform bonding. */
#define SEC_PARAM_MITM              0  
#define SEC_PARAM_LESC              0 
#define SEC_PARAM_KEYPRESS          0 
#define SEC_PARAM_IO_CAPABILITIES   BLE_GAP_IO_CAPS_NONE 
#define SEC_PARAM_OOB               0    
#define SEC_PARAM_MIN_KEY_SIZE      7                                  
#define SEC_PARAM_MAX_KEY_SIZE      16                                 

#define APP_BLE_CONN_CFG_TAG    1                                       
#define APP_BLE_OBSERVER_PRIO   3
#define APP_SOC_OBSERVER_PRIO   1
#define SCAN_INTERVAL 180 // scan interval in units of 0.625 millisecond
#define SCAN_WINDOW 90    // scan window in units of 0.625 millisecond
#define SCAN_TIMEOUT 3    // 3 second scan
#define DATA_LENGTH_MAX 244   // empirically determined max char size
#define MEMCHUNKS_POOL 720  // total memory is 720*244 
#define BALANCE_UUID_DEVICE 0x180a
#define BALANCE_UUID_PRIMARY 0x8b09

/*
#define CONN_INTERVAL_DEFAULT (uint16_t)(MSEC_TO_UNITS(7.5, UNIT_1_25_MS)) // at central side
#define CONN_INTERVAL_MIN (uint16_t)(MSEC_TO_UNITS(7.5, UNIT_1_25_MS))  // in units of 1.25 ms
*/
#define CONN_INTERVAL_DEFAULT (uint16_t)(MSEC_TO_UNITS(100, UNIT_1_25_MS)) // at central side
#define CONN_INTERVAL_MIN (uint16_t)(MSEC_TO_UNITS(100, UNIT_1_25_MS))  // in units of 1.25 ms

#define CONN_INTERVAL_MAX (uint16_t)(MSEC_TO_UNITS(500, UNIT_1_25_MS)) 
// #define CONN_SUP_TIMEOUT (uint16_t)(MSEC_TO_UNITS(4000,  UNIT_10_MS)) // 4 sec timeout 
#define CONN_SUP_TIMEOUT (uint16_t)(MSEC_TO_UNITS(20000,  UNIT_10_MS)) // 20 sec timeout 
#define SLAVE_LATENCY 0

typedef struct {
  uint16_t att_mtu;      
  uint16_t conn_interval;  
  ble_gap_phys_t  phys;          
  uint8_t data_len;     
  bool conn_evt_len_ext_enabled;  
  } test_params_t;
static test_params_t m_test_params = {
  .att_mtu = NRF_SDH_BLE_GATT_MAX_MTU_SIZE,
  .data_len = NRF_SDH_BLE_GAP_DATA_LENGTH,
  .conn_interval = CONN_INTERVAL_DEFAULT,
  .conn_evt_len_ext_enabled = true,
  // .phys.tx_phys = BLE_GAP_PHY_2MBPS | BLE_GAP_PHY_1MBPS | BLE_GAP_PHY_CODED,
  // .phys.rx_phys = BLE_GAP_PHY_2MBPS | BLE_GAP_PHY_1MBPS | BLE_GAP_PHY_CODED
  .phys.tx_phys = BLE_GAP_PHY_1MBPS,
  .phys.rx_phys = BLE_GAP_PHY_1MBPS
   };

/******* this is the struct needed to do a write operation *********
 typedef struct {
  uint8_t write_op;  // see BLE_GATT_WRITE_OPS
  uint8_t flags;     // see BLE_GATT_EXEC_WRITE_FLAGS (we ignore this) 
  uint16_t handle;   // Handle to attribute to be written
  uint16_t offset;   // WRITE_CMD and WRITE_REQ, offset must be 0
  uint16_t len;       
  uint8_t const *p_value;   // -> value data
  } ble_gattc_write_params_t;
 BLE_GATT_WRITE_OPS = 
  BLE_GATT_OP_INVALID,BLE_GATT_OP_WRITE_REQ,BLE_GATT_OP_WRITE_CMD,
  BLE_GATT_OP_SIGN_WRITE_CMD,BLE_GATT_OP_PREP_WRITE_REQ,BLE_GATT_OP_EXEC_WRITE_REQ
  BLE_GATT_EXEC_WRITE_FLAGS = 
  BLE_GATT_EXEC_WRITE_FLAG_PREPARED_CANCEL,BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE  
 */

// variable for struct of reading
bps_reading_t bps_reading = {
  .fence = {0x55,0xaa,0xff,0x77},
  .status = {0,0,0,0},  
  .clock = 0,
  .systolic = 0,
  .diastolic = 0,
  .pulse = 0,
  };

/*******************************************************************
 * application main data structure, initially should be zeroed out 
 */ 
typedef struct readings_s readings_t;
struct readings_s {
  uint32_t time;
  uint8_t sys;
  uint8_t dia;
  uint8_t pul;
  };
typedef struct balapp_s balapp_t;
struct balapp_s {
  uint32_t start_clock;  // unix time when boot (read from DS3231)
//  uint32_t clock;        // unix timer incremented each second 
  ble_gap_addr_t peer_addr;
  uint16_t conn_handle;
  bool exchange_completed;  // true after connect parameters settle
  // NOTE: ble_gatts_char_handles_t is defined in ble_gatts.h, 
  // and contains handles: value_handle, cccd_handle, and a couple
  // more that we are not interested in; here we have one such 
  // group for each characteristic in the Balance BPS primary service
  // Note: here [I],[N],[W] are for Indicate, Notify and Write
  ble_gatts_char_handles_t serv8a90; // no idea what is this 
  ble_gatts_char_handles_t serv8a91; // the Measurement service [I]
  ble_gatts_char_handles_t serv8a92; // the Intermediate Meas service [N]
  ble_gatts_char_handles_t serv8a81; // the Download (ie command) service [W]
  ble_gatts_char_handles_t serv8a82; // the Upload service [I]
  ble_gatts_char_handles_t serv2a29; // Manufacturer Name
  ble_gatts_char_handles_t serv2a25; // Serial Number
  ble_gatts_char_handles_t serv2a27; // Hardware Revision
  ble_gatts_char_handles_t serv2a28; // Software Revision
  ble_gatts_char_handles_t serv2a26; // Firmware Revision
  // REMARK: on the server side, the "ble_add_char_params_t" is used
  // to add characteristics to a service, fields in ble_srv_common.h
  // BELOW are flags (hopefully initially false by zero) meaning that the
  // structs above are not yet established; should all be True after 
  // the discovery is complete
  bool got8a91;
  bool got8a92;
  bool got8a81;
  bool got8a82;
  uint8_t devkey[4];   // device key
  uint8_t reading[16]; // reading
  uint8_t buffer[64];  // general purpose read/write buffer
  // this is for control flow: the state of an abstract FSM
  uint8_t phase;       // State in FSM of Control Flow
  bool reboot_scheduled;   // force reboot in idle loop
  bool pending_transfer;   // there are readings to transfer
  bool connected;          // true if known to be connected
  ble_gattc_write_params_t writeparm;
  uint8_t num_readings; 
  uint8_t cursor_readings;  
  uint8_t boron_retries;   // only retry a few times ($$$ for bandwidth bugs!!)
  readings_t readings[16]; // up to 16 readings
  uint32_t latest_reading; // timestamp of most recent HVX transfer
  };
balapp_t m_balapp;

// define Balance BPS Monitor's primary UUID for comparison
ble_uuid_t balance_uuid = {
  .type = BLE_UUID_TYPE_BLE,
  .uuid = BALANCE_UUID_PRIMARY 
  };
ble_uuid_t balance_device = {
  .type = BLE_UUID_TYPE_BLE,
  .uuid = BALANCE_UUID_DEVICE
  };

NRF_BLE_SCAN_DEF(m_scan);
NRF_BLE_GATT_DEF(m_gatt);
BLE_DB_DISCOVERY_DEF(m_db_disc); // Primary Service Discovery
APP_TIMER_DEF(timer_id);
APP_TIMER_DEF(kickoff_id);
APP_TIMER_DEF(milli_id);
nrfx_wdt_channel_id wdtchan = 1;
nrfx_wdt_config_t wdtconfig = NRFX_WDT_DEAFULT_CONFIG; 

// not really used for a scan parameter, but needed to connect
static const ble_gap_scan_params_t m_scan_param = {
  .active         = 0,    // Passive scanning.
  .filter_policy  = BLE_GAP_SCAN_FP_ACCEPT_ALL, // Do not use whitelist.
  .interval       = (uint16_t)SCAN_INTERVAL, // Scan interval.
  .window         = (uint16_t)SCAN_WINDOW,   // Scan window.
  .timeout        = 0,    // Never stop scanning unless explicit asked to.
  .scan_phys      = BLE_GAP_PHY_AUTO            // Automatic PHY selection.
   };

// Connection parameters requested for connection.
static ble_gap_conn_params_t m_conn_param = {
  .min_conn_interval = CONN_INTERVAL_MIN,  
  .max_conn_interval = CONN_INTERVAL_MAX, 
  .slave_latency     = SLAVE_LATENCY,      
  .conn_sup_timeout  = CONN_SUP_TIMEOUT  
  };

void i2c_init(); 
extern boronstate_t * boronapi_read();
extern bool boronapi_write(bps_reading_t * p); 
extern void clockapi_init();
extern uint32_t get_clock();
extern void message_clock_set(void * parameter, uint16_t size);
extern uint8_t * clock_buffer;
void cccd_indicate(uint16_t handle_cccd);
void send_command(uint8_t* command, uint8_t len);
void send_date();
void send_pass();
void send_disc();
void FSM(uint8_t newphase);
void db_disc_handler(ble_db_discovery_evt_t * p_evt);
void read_char(uint16_t handle);
void read_info();

void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name) {
  app_error_handler(0xDEADBEEF, line_num, p_file_name);
  }

void ledsoff() {
  bsp_board_led_on(0);
  bsp_board_led_on(1);
  bsp_board_led_on(2);
  bsp_board_led_on(3);
  }



/****** Example of reading format from hex dump ***********
 1E 9C 00 4D 00 74 00 AC|...M.t..
 21 9C 59 2B 00 01 00 00|!.Y+....

4D = Diastolic
9C = Systolic
2B = Pulse
AC219159 = date?

So
   N s s d d a a u
   u u u p p N N N
**********************************************************/
void save_reading() {
  readings_t * p; 
  if (m_balapp.reading[0] == 0) return;
  if (m_balapp.num_readings >= 16) return;
  p = &m_balapp.readings[m_balapp.num_readings];
  m_balapp.num_readings++;
  memcpy(&p->time,&m_balapp.reading[7],4);
  p->sys = m_balapp.reading[1];
  p->dia = m_balapp.reading[3];
  p->pul = m_balapp.reading[11];
  m_balapp.latest_reading = get_clock(); 
  }
void show_readings() {
  for (int i=0; i<m_balapp.num_readings; i++) {
    readings_t * p = &m_balapp.readings[i];
    NRF_LOG_INFO("BPS Sys %d Dia %d Pulse %d Time %x",
       p->sys, p->dia, p->pul, p->time);
    }
  }

// the db_discovery module is just a convenient front-end to 
// the sd_ble_gattc_primary_services_discover() -- a lower layer
// way to interrogate UUIDs and handles; since it is used twice
// in our program, we have a function to start/restart things
void discovery_start() {
  ret_code_t err_code;
  // err_code = ble_db_discovery_init(db_disc_handler); // set handler
  // APP_ERROR_CHECK(err_code);
  // err_code = ble_db_discovery_evt_register(&balance_uuid);
  // APP_ERROR_CHECK(err_code);
  // NRF_LOG_INFO("discovery start");
  err_code = ble_db_discovery_start(&m_db_disc,m_balapp.conn_handle);
  APP_ERROR_CHECK(err_code);
  }

// call FSM using "size" as the target state
void doFSM(void * parameter, uint16_t size) {
  UNUSED_PARAMETER(parameter);
  // NRF_LOG_INFO("doFSM parameter %d",size);
  FSM(size);
  }
// schedule later FSM call
void schedFSM(uint8_t newphase) {
  // hacky way to encode a byte value 
  uint16_t targetphase = newphase;
  app_sched_event_put(&m_balapp,targetphase,&doFSM);
  }
// delay for some milliseconds, then schedule FSM call 
void milli_handler(void * p_context) {
  uint8_t newphase = ((uint8_t*)p_context)-(m_balapp.buffer);
  schedFSM(newphase);
  }
void delayFSM(uint8_t newphase) {
  app_timer_start(milli_id,APP_TIMER_TICKS(100),m_balapp.buffer+newphase);
  }
// Finite State Machine for control flow -- based on 
// m_balapp.phase, which is initially zero
void FSM(uint8_t newphase) {
  ret_code_t err_code;
  // NRF_LOG_INFO("FSM in phase %d newphase %d",m_balapp.phase,newphase);
  switch (newphase) {
    case 1: // launch discovery
      // discovery_start();  // start by enumerating device services
      err_code = sd_ble_gattc_primary_services_discover(m_balapp.conn_handle,1,&balance_uuid);
      if (err_code == NRF_ERROR_BUSY) {
	schedFSM(1);
	return;
	}
      break;
    case 20: // discovery complete, but no writes or reads to handles yet
      // NRF_LOG_INFO("attempt write to 8a91 cccd");
      cccd_indicate(m_balapp.serv8a91.cccd_handle);  // next in event handler
      break;
    case 21:
      cccd_indicate(m_balapp.serv8a82.cccd_handle);  // next in event handler 
      break;
    case 22:
      // this case is empty because we depend on device to 
      // send an HVX with devkey, which will trigger FSM(23)
      break;
    case 23:
      // we have devkey! 
      send_pass();
      break;
    case 24:
      // set the device date
      send_date();
      break;
    case 25:
      send_disc();
      schedFSM(26);
      break;
    case 26:
      // show_readings();
      // m_balapp.reboot_scheduled = true;
      break;
    case 30: 
      // NRF_LOG_INFO("try to read 2a26");
      read_char(m_balapp.serv2a26.value_handle); 
      break;
    case 100:  // process first/next reading for transfer
      if (m_balapp.num_readings == 0 || 
          m_balapp.cursor_readings >= m_balapp.num_readings) {
	 m_balapp.reboot_scheduled = true;
	 break;
	 }
      // attempt to read boron state, to validate it is awake and ready
      boronapi_read(); // the callback will trigger retry or transition
      break;
    case 101: { // try to transfer one reading to boron
      readings_t * p = &m_balapp.readings[m_balapp.cursor_readings]; 
      bps_reading.systolic = p->sys;
      bps_reading.diastolic = p->dia;
      bps_reading.pulse = p->pul;
      bps_reading.clock = p->time + 1262304000U;
      memset(bps_reading.status,0,sizeof(bps_reading.status));
      NRF_LOG_INFO("writing bps reading to boron");
      boronapi_write(&bps_reading);
      break;
      }
    case 102: 
    default:
      break;
    }
    m_balapp.phase = newphase; // other uses are limited, please search them out 
  }
void boronapi_write_done( bool worked ) {
  if (worked) {
    m_balapp.cursor_readings++;
    FSM(100);
    return;
    }	  
  else {
    NRF_LOG_INFO("making boron retry");
    NRF_LOG_FLUSH();
    m_balapp.boron_retries++;
    if (m_balapp.boron_retries > 2) {
      // just give up, abandon all ..
      m_balapp.reboot_scheduled = true;
      delayFSM(100);
      return;
      }
    delayFSM(101);  // retry
    }
  }	
void boronapi_read_done( boronstate_t * p ) {
  if (!m_balapp.pending_transfer) {
    // here will be reaction to ordinary boron status query
    return;
    }
  // case when pending transfer
  if (p == NULL) { // failure to contact boron
    m_balapp.reboot_scheduled = true;
    NRF_LOG_INFO("boron missing");
    NRF_LOG_FLUSH();
    return;
    }
  // here would be place to validate response, if needed
  bsp_board_led_off(1);
  bsp_board_led_off(2);
  bsp_board_led_off(3);
  FSM(101);
  }

// Used for testing -- how to change MAC address (for spoofing, for emulating, etc)
void setmac() {
  ret_code_t ret;
  ble_gap_addr_t newmac;
  // uint8_t refaddr[] = {0xf8,0xad,0xcb,0x01,0xb1,0xd0};
  uint8_t refaddr[] = {0x30,0x5a,0x3a,0x90,0x72,0x22};
  ret = sd_ble_gap_addr_get(&newmac);
  APP_ERROR_CHECK(ret);
  NRF_LOG_INFO("old mac address");
  NRF_LOG_RAW_HEXDUMP_INFO(newmac.addr,6);
  memset((uint8_t*)&newmac,0,sizeof(newmac));
  newmac.addr_type = BLE_GAP_ADDR_TYPE_PUBLIC;
  memcpy(newmac.addr,refaddr,sizeof(newmac.addr));
  ret = sd_ble_gap_addr_set(&newmac);
  APP_ERROR_CHECK(ret);
  ret = sd_ble_gap_addr_get(&newmac);
  APP_ERROR_CHECK(ret);
  NRF_LOG_INFO("new mac address");
  NRF_LOG_RAW_HEXDUMP_INFO(newmac.addr,6);
  }

// Scanning: init, start, parse
uint32_t adv_report_parse(uint8_t type, 
		ble_data_t * p_advdata, 
		ble_data_t * p_typedata) {
  // this parser extracts name, service, etc, from an advertisement payload
  // argument "type" says what kind of field to extract
  uint32_t index = 0;
  uint8_t * p_data;
  p_data = p_advdata->p_data;
  while (index < p_advdata->len) {
    uint8_t field_length = p_data[index];
    uint8_t field_type = p_data[index+1];
    if (field_type == type) {
      p_typedata->p_data = &p_data[index+2];
      p_typedata->len = field_length-1;
      return NRF_SUCCESS;
      }
    index += field_length+1;
    }
  return NRF_ERROR_NOT_FOUND;
  }
void scan_init() { // use simple form, no filters
  ret_code_t ret;
  ret = nrf_ble_scan_init(&m_scan, NULL, NULL);
  APP_ERROR_CHECK(ret);
  }
void scan_start() {
  ret_code_t ret;
  ret = nrf_ble_scan_start(&m_scan);
  APP_ERROR_CHECK(ret);
  // ret = bsp_indication_set(BSP_INDICATE_SCANNING);
  // APP_ERROR_CHECK(ret);
  }
void reboot() {
  ret_code_t err_code;
  nrf_ble_scan_stop();
  sd_ble_gap_disconnect(m_balapp.conn_handle,
		 BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
  err_code = sd_nvic_SystemReset();
  APP_ERROR_CHECK(err_code);
  }
void softdevice_init() {
  ret_code_t err_code;
  err_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(err_code);
  }
void appsched_init() {
  APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
  }
void reset() {
  ret_code_t err_code;
  nrf_ble_scan_stop();
  sd_ble_gap_disconnect(m_balapp.conn_handle,
     BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
  err_code = nrf_sdh_disable_request();
  APP_ERROR_CHECK(err_code);
  appsched_init();
  softdevice_init();
  }
/*
void connect_task(void * parameter, uint16_t size) {
void connect() {
  ret_code_t err_code;
  m_balapp.conn_handle = BLE_CONN_HANDLE_INVALID; 
  err_code = sd_ble_gap_connect(&m_balapp.peer_addr,
      &m_scan_param,
      &m_conn_param,
      APP_BLE_CONN_CFG_TAG);
  if (err_code != NRF_SUCCESS) 
    NRF_LOG_INFO("sd_ble_gap_connect failed 0x%x",err_code);
  }
*/
void scan_inspect(ble_gap_evt_adv_report_t const * p_adv) {
  // scrutinze an advertisement, returning ??? if it is a device worth
  // connecting to and investigating further
  /*** Note, for the adv_report, the library the header says one thing, 
   * the documentation says otherwise -- documentation says it has seven fields, 
   * peer_addr, direct_addr, rssi, then an aggregate byte
   * for scan_rsp, type, and dlen (5 bits), followed by up to
   * 31 bytes of data -- and if scan_rsp is 1, ignore this report
   * BUT one header file (ble_gap.h) has this instead:
   * 1. type field, two bytes, it is a bit-defined aggregate 
   *    uint16_t connectable: 1
   *    uint16_t scannable: 1
   *    uint16_t directed: 1
   *    uint16_t scan_response: 1
   *    uint16_t extended_pdu: 1
   *    uint16_t status: 2 
   *    uint16_t reserved: 9
   * 2. peer address (ble_gap_addr_t) 
   * 3. direct address (ble_gap_addr_t)
   * 4. primary_phy, a byte
   * 5. secondary phy, a byte
   * 6. tx_power, a byte
   * 7. rssi, a byte 
   * 8. ch_index, a byte for channel index of this report 
   * 9. set_id, a byte
   * 10. data_id, two bytes but really 12 bits
   * 11. data, which is a ble_data_t (len and pointer)
  *****/
	    // uint8_t* q = p_adv->data.p_data;
	    // uint8_t  n = p_adv->data.len;
  ret_code_t err_code;
  ble_data_t inspectName; // ble_data_t has just two fields, 
  ble_data_t inspectServ; //   len and p_data (pointer)
  uint32_t now = get_clock();  // current unix time
  uint16_t service_code = 0;
  bool foundName = false;
  // unclear we should be getting scan response packet (more advanced)
  if (p_adv->type.scan_response == 1) return;
  // wait 3 seconds, ?? no idea why!
  if (m_balapp.start_clock == 0) return;  // has to be initialized
  if (now - m_balapp.start_clock < 3) return;  
  // for debugging, let's ignore the non-connectable devices
  if (p_adv->type.connectable == 0) return; 
  // NRF_LOG_INFO("adv evt rssi %d channel %d",p_adv->rssi,p_adv->ch_index);
  // NRF_LOG_INFO("scannable %d directed %d resp %d",
//			    p_adv->type.scannable,
//			    p_adv->type.directed,
  //                          p_adv->type.scan_response
//			    );
  err_code = adv_report_parse(9,&m_scan.scan_buffer,&inspectName);
  if (err_code == NRF_SUCCESS) { 
    // NRF_LOG_INFO("name");
    // NRF_LOG_RAW_HEXDUMP_INFO (inspectName.p_data, inspectName.len);
    if (memcmp(inspectName.p_data,"01490BT1",8) == 0) foundName = true; 
    }
  err_code = adv_report_parse(3,&m_scan.scan_buffer,&inspectServ);
    if (err_code == NRF_SUCCESS && inspectServ.len == 2)  { 
      service_code = *(uint16_t*)inspectServ.p_data;
      // NRF_LOG_INFO("service");
      // NRF_LOG_RAW_HEXDUMP_INFO(inspectServ.p_data,inspectServ.len);
      }
  if (service_code == BALANCE_UUID_PRIMARY) { };
  if (foundName) {
    memcpy((uint8_t*)&m_balapp.peer_addr,
           (uint8_t*)&p_adv->peer_addr,sizeof(ble_gap_addr_t));
    // NRF_LOG_INFO("peer mac address");
    // NRF_LOG_RAW_HEXDUMP_INFO(m_balapp.peer_addr.addr,6);
    err_code = sd_ble_gap_connect(&m_balapp.peer_addr,
      &m_scan_param, &m_conn_param, APP_BLE_CONN_CFG_TAG);
    if (err_code != NRF_SUCCESS) 
      NRF_LOG_INFO("sd_ble_gap_connect failed 0x%x",err_code);
    }
  }

// typedef struct {
//    uint16_t     conn_handle; 
//    tx_request_t type;  
//    union {
//     uint16_t read_handle; 
//     write_params_t write_req;
//    } req; } tx_message_t;
//
// typedef struct {
//    uint8_t  gattc_value[WRITE_MESSAGE_LENGTH];
//    ble_gattc_write_params_t gattc_params;
// } write_params_t;
//

// used to write INDICATE to CCCD handle
/*
void tx_buffer_process(void) {
  if (m_tx_index != m_tx_insert_index) {
    uint32_t err_code;
    if (m_tx_buffer[m_tx_index].type == READ_REQ) {
      err_code = sd_ble_gattc_read(m_tx_buffer[m_tx_index].conn_handle,
                    m_tx_buffer[m_tx_index].req.read_handle,
                    0);
      }
    else {
      err_code = sd_ble_gattc_write(m_tx_buffer[m_tx_index].conn_handle,
                    &m_tx_buffer[m_tx_index].req.write_req.gattc_params);
      }
    if (err_code == NRF_SUCCESS) {
      m_tx_index++;
      m_tx_index &= TX_BUFFER_MASK;
      }
    else {
      NRF_LOG_DEBUG("SD Read/Write API returns error, will retry later.");
      }
    }
}
*/

void read_char(uint16_t handle) {
  ret_code_t err_code;
  err_code = sd_ble_gattc_read(m_balapp.conn_handle,handle,0);
  APP_ERROR_CHECK(err_code);
  }

void read_info() {
  ret_code_t err_code;
  uint16_t handles[5];
  handles[0] = m_balapp.serv2a29.value_handle;
  handles[1] = m_balapp.serv2a25.value_handle;
  handles[2] = m_balapp.serv2a27.value_handle;
  handles[3] = m_balapp.serv2a28.value_handle;
  handles[4] = m_balapp.serv2a26.value_handle;
  err_code = sd_ble_gattc_char_values_read(m_balapp.conn_handle,handles,5);
  APP_ERROR_CHECK(err_code);
  }

void send_pass() {
  uint8_t setpass[5];
  // uint8_t dbuff[9];
  uint8_t * k = (uint8_t*)&m_balapp.peer_addr.addr;
  setpass[0] = 0x20; // op-code for setting password
  for (int i=0;i<4;i++) setpass[i+1] = m_balapp.devkey[i] ^ *(k+i); 
  send_command(setpass,5);
  }
void send_date() {
  uint8_t setdate[5];
  // device's clock is unix time (reverse digits) - 1262304000 (Jan 1 2010)
  // or maybe 2014? That would be 1388534400
  // uint32_t devclock = m_balapp.clock - 1262304000U; 
  uint32_t devclock = get_clock() - 1262304000U; 
  uint8_t * k = (uint8_t*)&devclock;
  devclock -= 5*60*60;   // six-hour offset from UTC? DST? WTF? 
  setdate[0] = 0x02;  // opcode for setting date/time
  // for (int i=0; i<4; i++) setdate[i+1] = *(k+3-i);
  for (int i=0; i<4; i++) setdate[i+1] = *(k+i);
  send_command(setdate,5);
  }
void send_disc() {
  uint8_t halt = 0x22;
  send_command(&halt,1);
  }

void send_command(uint8_t* command, uint8_t len) {
  uint32_t err_code;
  memcpy(m_balapp.buffer,command,len);
  // NRF_LOG_INFO("send command on handle %x",m_balapp.serv8a81.value_handle);
  // NRF_LOG_HEXDUMP_INFO(m_balapp.buffer,len);
  m_balapp.writeparm.p_value = m_balapp.buffer;
  m_balapp.writeparm.len = len;
  m_balapp.writeparm.handle = m_balapp.serv8a81.value_handle;
  m_balapp.writeparm.offset = 0;
  m_balapp.writeparm.write_op = BLE_GATT_OP_WRITE_REQ;
  err_code = sd_ble_gattc_write(m_balapp.conn_handle,&m_balapp.writeparm);
  APP_ERROR_CHECK(err_code);
  }
void cccd_indicate(uint16_t handle_cccd) {
  uint32_t err_code;
  uint16_t cccd_val = BLE_GATT_HVX_INDICATION;
  m_balapp.buffer[0] = LSB_16(cccd_val);
  m_balapp.buffer[1] = MSB_16(cccd_val);
  m_balapp.writeparm.p_value = m_balapp.buffer;  
  m_balapp.writeparm.len = BLE_CCCD_VALUE_LEN; 
  m_balapp.writeparm.handle = handle_cccd;
  m_balapp.writeparm.offset = 0;
  m_balapp.writeparm.write_op = BLE_GATT_OP_WRITE_REQ;
  err_code = sd_ble_gattc_write(m_balapp.conn_handle,&m_balapp.writeparm);
  APP_ERROR_CHECK(err_code);
  }

void db_disc_handler(ble_db_discovery_evt_t * p_evt) {
  if (p_evt->evt_type != BLE_DB_DISCOVERY_COMPLETE) return;
  FSM(30);
  }

void db_disc_handler_save(ble_db_discovery_evt_t * p_evt) {
  // Type can be one of these:
  //   BLE_DB_DISCOVERY_COMPLETE, BLE_DB_DISCOVERY_ERROR, 
  //   BLE_DB_DISCOVERY_SRV_NOT_FOUND, BLE_DB_DISCOVERY_AVAILABLE
  // Note: p_evt-> params.discovered_db 
  //   (type is ble_gatt_db_srv_t) is only valid if the discovery 
  //   is complete, and has these fields:
  //       ble_uuid_t srv_uuid  --  UUID of the service
  //       uint8_t char_count   --  number of characteristics 
  //       ble_gattc_handle_range_t handle_range  -- range of handles
  //         (above is a struct with just two 16-bit handles named
  //          start_handle and end_handle) 
  //       ble_gatt_db_char_t       characteristics[BLE_GATT_DB_MAX_CHARS]
  //   the program should iterate for the char_count items in 
  //   characteristics; with each, fields of ble_gatt_db_char_t are:
  //       characteristic -- type is ble_gattc_char_t, see below
  //       then cccd_handle (super important), plus three rarely used:
  //       ext_prop_handle, user_desc_handle, report_ref_handle (prolly n/a)
  //   the ble_gattc_char_t type has these fields
  //       uuid -- type is ble_uuit_t, which has fields type and uuid
  //       char_props -- type is ble_gatt_char_props_t, see ble_gatt.h
  //       char_ext_props (1 bit) -- if there are extended properties
  //       handle_decl -- handle of the characteristic declaration
  //       handle_value -- handle of the characteristic value
  // NRF_LOG_INFO("db_disc_handler called");
  if (p_evt->evt_type != BLE_DB_DISCOVERY_COMPLETE) return;
  // NRF_LOG_INFO("discovery complete for %x",p_evt->params.discovered_db.srv_uuid.uuid);
  // NRF_LOG_INFO("discover count %d",p_evt->params.discovered_db.char_count);
  for (int i=0; i < p_evt->params.discovered_db.char_count; i++) {
    ble_gatt_db_char_t* q = 
	      // misspelling of characteristics is Nordic's fault
	      &p_evt->params.discovered_db.charateristics[i];
    int id = (uint16_t)q->characteristic.uuid.uuid;
    // NRF_LOG_INFO("saw uuid %x",id);
    switch (id) {
      case 0x8a90: // no clue what is this
	m_balapp.serv8a90.value_handle = q->characteristic.handle_value;
	m_balapp.serv8a90.cccd_handle  = q->cccd_handle;
	break;
      case 0x8a91: // for final measurement reading [I]
	m_balapp.serv8a91.value_handle = q->characteristic.handle_value;
	m_balapp.serv8a91.cccd_handle  = q->cccd_handle;
        if (q->characteristic.char_props.indicate) m_balapp.got8a91 = true;
	break;
      case 0x8a92: // intermediate measurement readings (unused by us) [N]
	m_balapp.serv8a92.value_handle = q->characteristic.handle_value;
	m_balapp.serv8a92.cccd_handle  = q->cccd_handle;
        if (q->characteristic.char_props.notify) m_balapp.got8a92 = true;
	break;
      case 0x8a81: // download, ie. writing commands, should be writable [W]
	m_balapp.serv8a81.value_handle = q->characteristic.handle_value;
	m_balapp.serv8a81.cccd_handle  = q->cccd_handle;
        if (q->characteristic.char_props.write) m_balapp.got8a81 = true;
	break;
      case 0x8a82: // upload (unused by us) [I]
	m_balapp.serv8a82.value_handle = q->characteristic.handle_value;
	m_balapp.serv8a82.cccd_handle  = q->cccd_handle;
        if (q->characteristic.char_props.indicate) m_balapp.got8a82 = true;
	break;
      case 0x2a29: // Manufacture Name
	m_balapp.serv2a29.value_handle = q->characteristic.handle_value;
	break;
      case 0x2a25: // Serial Number 
	m_balapp.serv2a25.value_handle = q->characteristic.handle_value;
	break;
      case 0x2a27: // Hardware Revision 
	m_balapp.serv2a27.value_handle = q->characteristic.handle_value;
	break;
      case 0x2a28: // Software Revision 
	m_balapp.serv2a28.value_handle = q->characteristic.handle_value;
	break;
      case 0x2a26: // Firmware Revision 
	m_balapp.serv2a26.value_handle = q->characteristic.handle_value;
	break;
      default: break;
      }
    }
  /*
  NRF_LOG_INFO("8a90 value handle %x",m_balapp.serv8a90.value_handle);
  NRF_LOG_INFO("8a90 cccd handle %x",m_balapp.serv8a90.cccd_handle);
  NRF_LOG_INFO("8a91 value handle %x",m_balapp.serv8a91.value_handle);
  NRF_LOG_INFO("8a91 cccd handle %x",m_balapp.serv8a91.cccd_handle);
  NRF_LOG_INFO("8a92 value handle %x",m_balapp.serv8a92.value_handle);
  NRF_LOG_INFO("8a92 cccd handle %x",m_balapp.serv8a92.cccd_handle);
  NRF_LOG_INFO("8a81 value handle %x",m_balapp.serv8a81.value_handle);
  NRF_LOG_INFO("8a81 cccd handle %x",m_balapp.serv8a81.cccd_handle);
  NRF_LOG_INFO("8a82 value handle %x",m_balapp.serv8a82.value_handle);
  NRF_LOG_INFO("8a82 cccd handle %x",m_balapp.serv8a82.cccd_handle);
  NRF_LOG_INFO("2a29 value handle %x",m_balapp.serv2a29.value_handle);
  NRF_LOG_INFO("2a25 value handle %x",m_balapp.serv2a25.value_handle);
  NRF_LOG_INFO("2a27 value handle %x",m_balapp.serv2a27.value_handle);
  NRF_LOG_INFO("2a28 value handle %x",m_balapp.serv2a28.value_handle);
  NRF_LOG_INFO("2a26 value handle %x",m_balapp.serv2a26.value_handle);
  */
  // Looks good - we can proceed to next step, writing CCCD
  if (m_balapp.got8a91 && m_balapp.got8a92 &&  
		  m_balapp.got8a81 && m_balapp.got8a82) {  
    schedFSM(20);
    return;
    }
  //  NRF_LOG_INFO("Balance Device NOT Recognized.") 
  return;
  }

void soc_evt_handler(uint32_t evt_id, void * p_context) {
  switch (evt_id) {
    case NRF_EVT_FLASH_OPERATION_SUCCESS:
    case NRF_EVT_FLASH_OPERATION_ERROR:
         scan_start();
      break;
    default:
      break;
    }
  }

/*
void timer_handler(void * p_context) {
  UNUSED_PARAMETER(p_context);
  m_balapp.clock++;
  // bsp_board_led_invert(0);
  }
*/

// handler called regularly by clockapi, once per second 
void auxiliary_tick_handler(uint32_t t) {
  if (m_balapp.start_clock == 0) m_balapp.start_clock = get_clock();
  nrfx_wdt_feed();
  /*
  if (m_balapp.latest_reading != 0 &&
      (get_clock()-m_balapp.latest_reading > 2)) {
	 show_readings();
	 m_balapp.reboot_scheduled = true; 
	 } 
  */
  }

void kickoff_handler(void * p_context) {
  scan_start();
  }

void timers_init(void) {
  ret_code_t err_code;
  err_code = app_timer_init();
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_create(&kickoff_id,
                  APP_TIMER_MODE_SINGLE_SHOT,
                  kickoff_handler);
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_create(&milli_id,
                  APP_TIMER_MODE_SINGLE_SHOT,
                  milli_handler);
  APP_ERROR_CHECK(err_code);
  }
void timers_start(void) {
  app_timer_start(timer_id,APP_TIMER_TICKS(1000),NULL);
  app_timer_start(kickoff_id,APP_TIMER_TICKS(500),NULL);
  }

void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context) {
  ret_code_t err_code;
  bool found = false;
  ble_gap_evt_t const * p_gap_evt = &p_ble_evt->evt.gap_evt;
  static const uint8_t expected_evt_types[] = {   
    // first are 0x10,0x11,0x13,0x1D (mainline BLE_GAP)
    BLE_GAP_EVT_CONNECTED,BLE_GAP_EVT_DISCONNECTED,
    BLE_GAP_EVT_SEC_PARAMS_REQUEST,BLE_GAP_EVT_ADV_REPORT,
    // next are 0x24 (options ?)
    BLE_GAP_OPT_AUTH_PAYLOAD_TIMEOUT, 
    // then 0x31,0x32,0x33,0x3a,0x38,0x39 (gattc types)
    BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP,BLE_GATTC_EVT_CHAR_DISC_RSP,
    BLE_GATTC_EVT_DESC_DISC_RSP,BLE_GATTC_EVT_EXCHANGE_MTU_RSP,
    BLE_GATTC_EVT_WRITE_RSP,BLE_GATTC_EVT_HVX, 
    // plus 0x36,0x37
    BLE_GATTC_EVT_READ_RSP,BLE_GATTC_EVT_CHAR_VALS_READ_RSP};
  if (m_balapp.reboot_scheduled) return; 
  for (int i=0;i<sizeof(expected_evt_types);i++) {
    if ((uint8_t)p_ble_evt->header.evt_id == expected_evt_types[i]) 
       found = true;
    }
  if (!found) { 
    NRF_LOG_INFO("unexpected ble_evt_handler event %x",
		    p_ble_evt->header.evt_id);
    }
  switch (p_ble_evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
      nrf_ble_scan_stop();  // just to be sure scan stopped
      m_balapp.conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
      // crank up power 
      err_code = sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_CONN,
		   m_balapp.conn_handle,8); 
      APP_ERROR_CHECK(err_code);
      m_balapp.connected = true;
      FSM(1);   // start discovery
      break;

    case BLE_GAP_EVT_DISCONNECTED:
      // see ble_hci.h for the names of "reasons" for disconnect
      NRF_LOG_INFO(
             "Disconnected. reason: 0x%x phase: %d",
             p_gap_evt->params.disconnected.reason,
	     m_balapp.phase);
      m_balapp.connected = false;
      NRF_LOG_INFO("num readings found = %d",m_balapp.num_readings);
      if (m_balapp.num_readings == 0) m_balapp.reboot_scheduled = true;
      else m_balapp.cursor_readings = 0; // the idle loop will see what to do
      break;

    case BLE_GAP_EVT_TIMEOUT:
      if (p_gap_evt->params.timeout.src == BLE_GAP_TIMEOUT_SRC_CONN) {
        NRF_LOG_INFO("Connection Request timed out.");
        }
      break;

    case BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP:
      // NRF_LOG_INFO("Prim Srv Disc Rsp");
      FSM(30);
      break;

    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
      err_code = sd_ble_gap_sec_params_reply(p_ble_evt->evt.gap_evt.conn_handle,             BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
      APP_ERROR_CHECK(err_code);
      break;

    case BLE_GAP_EVT_CONN_PARAM_UPDATE: {
      // m_conn_interval_configured = true;
      NRF_LOG_INFO("Connection interval updated: 0x%x, 0x%x.",
           p_gap_evt->params.conn_param_update.conn_params.min_conn_interval,
           p_gap_evt->params.conn_param_update.conn_params.max_conn_interval);
      } 
      break;

    case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST: {
      ble_gap_conn_params_t params;
      params = p_gap_evt->params.conn_param_update_request.conn_params;
      err_code = sd_ble_gap_conn_param_update(p_gap_evt->conn_handle, &params);
      APP_ERROR_CHECK(err_code);
      NRF_LOG_INFO("Connection interval updated (upon request): 0x%x, 0x%x.",
          p_gap_evt->params.conn_param_update_request.conn_params.min_conn_interval,
          p_gap_evt->params.conn_param_update_request.conn_params.max_conn_interval);
      } 
      break;

    case BLE_GATTS_EVT_SYS_ATTR_MISSING: {
      err_code = sd_ble_gatts_sys_attr_set(p_gap_evt->conn_handle, NULL, 0, 0);
      APP_ERROR_CHECK(err_code);
      } 
      break;

    case BLE_GAP_EVT_PHY_UPDATE: {
      NRF_LOG_INFO("PHY update event.");
      ble_gap_evt_phy_update_t const * p_phy_evt = &p_ble_evt->evt.gap_evt.params.phy_update;
      if (p_phy_evt->status == BLE_HCI_STATUS_CODE_LMP_ERROR_TRANSACTION_COLLISION) {
        // Ignore LL collisions.
        NRF_LOG_DEBUG("LL transaction collision during PHY update.");
        } 
        // on_ble_gap_evt_connected(p_gap_evt);
      } 
      break;

    case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
      err_code = sd_ble_gap_phy_update(p_gap_evt->conn_handle, &m_test_params.phys);
      APP_ERROR_CHECK(err_code);
      } 
      break;

    case BLE_GATTC_EVT_TIMEOUT:
      // Disconnect on GATT Client timeout event.
      NRF_LOG_DEBUG("GATT Client Timeout.");
      err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
            BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
      if (err_code != NRF_SUCCESS && err_code != BLE_ERROR_INVALID_CONN_HANDLE &&
         err_code != NRF_ERROR_INVALID_STATE) {
         APP_ERROR_CHECK(err_code);
         }
      m_balapp.connected = false;
      break;

    case BLE_GATTS_EVT_TIMEOUT:
      // Disconnect on GATT Server timeout event.
      NRF_LOG_DEBUG("GATT Server Timeout.");
      err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
      if (err_code != NRF_SUCCESS && err_code != BLE_ERROR_INVALID_CONN_HANDLE &&
         err_code != NRF_ERROR_INVALID_STATE) {
         APP_ERROR_CHECK(err_code);
	 }
      m_balapp.connected = false;
      break;

    case BLE_GAP_EVT_ADV_REPORT: {
      scan_inspect(&p_gap_evt->params.adv_report);
      } break;

    case BLE_GATTC_EVT_HVX: { 
      // struct ble_gattc_evt_hvx_t is relevant here (params.hvx.len etc),
      // and has these fields:
      //  uint16_t handle
      //  uint8_t type (see BLE_GATT_HVX_TYPES)
      //  uint16_t len
      //  uint8_t data[]
      int len = p_ble_evt->evt.gattc_evt.params.hvx.len;
      uint8_t* data = (uint8_t*)p_ble_evt->evt.gattc_evt.params.hvx.data;
      if (p_ble_evt->evt.gattc_evt.conn_handle != m_balapp.conn_handle) break;
      if (p_ble_evt->evt.gattc_evt.params.hvx.type != BLE_GATT_HVX_INDICATION) break; 
      NRF_LOG_INFO("HVX len %d handle %x lookfor %x",
                      len,
		      p_ble_evt->evt.gattc_evt.params.hvx.handle,
		      m_balapp.serv8a91.value_handle);
      if (p_ble_evt->evt.gattc_evt.params.hvx.handle == m_balapp.serv8a91.value_handle) {
         if (len != 16) break; 
         memcpy(m_balapp.reading,data,16);  
	 save_reading();
         // IMPORTANT - need to send server a value confirmation
         err_code = sd_ble_gattc_hv_confirm(
	              p_ble_evt->evt.gattc_evt.conn_handle,
                      p_ble_evt->evt.gattc_evt.params.hvx.handle);
         APP_ERROR_CHECK(err_code);
	 delayFSM(25);
	 }
      if (p_ble_evt->evt.gattc_evt.params.hvx.handle == 
		     m_balapp.serv8a82.value_handle) {
         if (p_ble_evt->evt.gattc_evt.params.hvx.data[0] < 5 || data[0] != 0xa1) break; 
         memcpy(m_balapp.devkey,data+1,4);  // random device key
         err_code = sd_ble_gattc_hv_confirm(
	              p_ble_evt->evt.gattc_evt.conn_handle,
                      p_ble_evt->evt.gattc_evt.params.hvx.handle);
         APP_ERROR_CHECK(err_code);
         FSM(23);  // both cccd handles written, device key recorded 
         }
      break;
      }

     // client gets write (to CCCD) from server
     case BLE_GATTS_EVT_WRITE:
       // on_ble_gatt_write(p_ble_evt);
       break;

     case BLE_GATTS_EVT_HVN_TX_COMPLETE:
       // on_tx_complete(p_ble_evt);
       break;
	 
     case BLE_GATTC_EVT_WRITE_RSP:
       if (p_ble_evt->evt.gattc_evt.params.write_rsp.handle == 
		       m_balapp.serv8a91.cccd_handle) {
         FSM(21); // 20 -> 21 transition complete, initiate next transition
	 break;
	 }
       if (p_ble_evt->evt.gattc_evt.params.write_rsp.handle == 
		       m_balapp.serv8a82.cccd_handle) {
	 // remain in phase 22 until hvx on 8a82
	 FSM(22); 
	 break;
         }
       if (p_ble_evt->evt.gattc_evt.params.write_rsp.handle == 
		       m_balapp.serv8a81.value_handle) {
	 switch (m_balapp.phase) {
	   case 23: // after writing password
	     FSM(24);
	     break;
	   case 24: // after writing date
	     break; // wait for HVX to trigger next transition
	   case 25: // after writing disconnect
	     FSM(26);  // NOTE: this should be obsolete! 
	     break;
	   default:
	     break;
           }
	 break;
         }
       NRF_LOG_INFO("EVT_WRITE_RSP for unknown handle");
       break;

     case BLE_GATTC_EVT_READ_RSP: {
       /*
       ble_gattc_evt_t const * p_gattc_evt = &p_ble_evt->evt.gattc_evt;
       NRF_LOG_INFO("read response %x %x %x %x",
           p_gattc_evt->params.read_rsp.handle,
           p_gattc_evt->params.read_rsp.offset,
           p_gattc_evt->params.read_rsp.len,
           p_gattc_evt->params.read_rsp.data[0]);
       */
	 }
       FSM(20);
       break;

     case BLE_GATTC_EVT_CHAR_VALS_READ_RSP: {
       ble_gattc_evt_t const * p_gattc_evt = &p_ble_evt->evt.gattc_evt;
       NRF_LOG_INFO("read multi got %d bytes",
	   p_gattc_evt->params.char_vals_read_rsp.len);
       // the other field is "values" which starts stream of data returned
       }						    
       break;

     default:
       break;
     }
  }

void wdthandler() { };
void wdt_init() {
  ret_code_t err_code;
  err_code = nrfx_wdt_init(&wdtconfig,wdthandler);
  APP_ERROR_CHECK(err_code);
  err_code = nrfx_wdt_channel_alloc(&wdtchan);
  APP_ERROR_CHECK(err_code);
  nrfx_wdt_enable(); 
  }

void gatt_evt_handler(nrf_ble_gatt_t * p_gatt, 
		nrf_ble_gatt_evt_t const * p_evt) {
  if (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED) {
    // NRF_LOG_INFO("ATT MTU exchange completed.");
    m_balapp.exchange_completed = true;
    }
  }

void gatt_init() {
  ret_code_t err_code;
  err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
  APP_ERROR_CHECK(err_code);
  err_code = nrf_ble_gatt_att_mtu_central_set(&m_gatt, 
		  NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
  APP_ERROR_CHECK(err_code);
  }

void discovery_init() {
  ret_code_t err_code;
  err_code = ble_db_discovery_init(db_disc_handler); // set handler
  APP_ERROR_CHECK(err_code);
  err_code = ble_db_discovery_evt_register(&balance_uuid);
  APP_ERROR_CHECK(err_code);
  }

void ble_stack_init(void) { 
  ret_code_t err_code;

  // Configure the BLE stack using the default settings.
  // Fetch the start address of the application RAM.
  uint32_t ram_start = 0;
  err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
  APP_ERROR_CHECK(err_code);

  // Enable BLE stack.
  err_code = nrf_sdh_ble_enable(&ram_start);
  APP_ERROR_CHECK(err_code);
  // setmac();

  // Register a handler for BLE events.
  NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO,
		  ble_evt_handler, NULL);
  NRF_SDH_SOC_OBSERVER(m_soc_observer, APP_SOC_OBSERVER_PRIO,
		  soc_evt_handler, NULL);
  }

void bsp_event_handler(bsp_event_t event) {
  switch (event) {
     case BSP_EVENT_SLEEP:
       nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_SYSOFF);
       break;
     case BSP_EVENT_DISCONNECT:
       break;
     default:
       break;
     }
  }

void buttons_leds_init(void) {
  ret_code_t err_code;
  // bsp_event_t startup_event;
  err_code = bsp_init(BSP_INIT_LEDS, bsp_event_handler);
  APP_ERROR_CHECK(err_code);
  // err_code = bsp_btn_ble_init(NULL, &startup_event);
  // APP_ERROR_CHECK(err_code);
  ledsoff();
  /* EXTRA testing
  nrf_gpio_cfg_output(0*32+3);
  nrf_gpio_cfg_output(0*32+4);
  nrf_gpio_cfg_output(0*32+28);
  nrf_gpio_pin_set(0*32+3);
  nrf_gpio_pin_clear(0*32+4);
  nrf_gpio_pin_set(0*32+28);
  */
  }

void log_init(void) {
  ret_code_t err_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(err_code);
  NRF_LOG_DEFAULT_BACKENDS_INIT();
  }

void power_management_init(void) {
  ret_code_t err_code;
  err_code = nrf_pwr_mgmt_init();
  APP_ERROR_CHECK(err_code);
  }

void idle_state_handle(void) {
  if (NRF_LOG_PROCESS() == false) {
    nrf_pwr_mgmt_run();
    }
  }

void app_init() {
  memset((uint8_t*)&m_balapp,0,sizeof(balapp_t));
  // set handle values (even if discovered, these should be accurate)
  m_balapp.serv8a90.value_handle = 0x000b;
  m_balapp.serv8a91.value_handle = 0x000d;
  m_balapp.serv8a91.cccd_handle  = 0x000e;
  m_balapp.serv8a92.value_handle = 0x0010;
  m_balapp.serv8a92.cccd_handle  = 0x0011;
  m_balapp.serv8a81.value_handle = 0x0013;
  m_balapp.serv8a81.cccd_handle  = 0x0000;
  m_balapp.serv8a82.value_handle = 0x0015;
  m_balapp.serv8a82.cccd_handle  = 0x0016;
  m_balapp.serv2a29.value_handle = 0x001d;
  m_balapp.serv2a25.value_handle = 0x0021;
  m_balapp.serv2a27.value_handle = 0x0023;
  m_balapp.serv2a28.value_handle = 0x0027;
  m_balapp.serv2a26.value_handle = 0x0025;
  }

int main(void) {
  log_init();
  timers_init();
  wdt_init();
  buttons_leds_init();
  appsched_init();
  power_management_init();
  app_init();
  softdevice_init();
  ble_stack_init();
  gatt_init();
  i2c_init();
  timers_start();
  clockapi_init();  // comes after timer init
  // discovery_init();
  scan_init();
  NRF_LOG_INFO("scan initiated");
  for (;;) {
    idle_state_handle();
    bsp_board_led_invert(0);
    if (m_balapp.reboot_scheduled) reboot();
    if (!m_balapp.connected && m_balapp.num_readings > 0 && !m_balapp.pending_transfer) {
      m_balapp.pending_transfer = true;
      reset();
      FSM(100); // curiously, app_sched_event_put(&m_balapp,100,&doFSM) does not work! 
      }
    NRF_LOG_FLUSH();
    app_sched_execute();
    }
  }
