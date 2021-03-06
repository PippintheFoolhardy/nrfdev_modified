#include <stdint.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_sdm.h"
#include "app_error.h"
#include "app_scheduler.h"
#include "app_timer.h"
#include "ble_db_discovery.h"
#include "ble.h"
#include "ble_err.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "sensorsim.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "bsp_btn_ble.h"
#include "nrf_sdh_soc.h"
#include "bsp_btn_ble.h"
#include "peer_manager.h"
#include "peer_manager_handler.h"
#include "fds.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_lesc.h"
#include "nrf_ble_qwr.h"
#include "ble_conn_state.h"
#include "nrf_pwr_mgmt.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "motebadge.h"

#define SCHED_MAX_EVENT_DATA_SIZE APP_TIMER_SCHED_EVENT_DATA_SIZE
#define SCHED_QUEUE_SIZE 10

#define APP_ADV_INTERVAL 300 
#define APP_ADV_DURATION 18000 
#define APP_BLE_CONN_CFG_TAG 1
#define APP_BLE_OBSERVER_PRIO 3 

#define MIN_CONN_INTERVAL MSEC_TO_UNITS(400, UNIT_1_25_MS)
#define MAX_CONN_INTERVAL MSEC_TO_UNITS(650, UNIT_1_25_MS)
#define SLAVE_LATENCY 0 
#define CONN_SUP_TIMEOUT MSEC_TO_UNITS(4000, UNIT_10_MS)
#define FIRST_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(5000)
#define NEXT_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(30000)
#define MAX_CONN_PARAMS_UPDATE_COUNT 3
#define LESC_DEBUG_MODE 0

#define SEC_PARAM_BOND 1
#define SEC_PARAM_MITM 0
#define SEC_PARAM_LESC 1
#define SEC_PARAM_KEYPRESS 0
#define SEC_PARAM_IO_CAPABILITIES BLE_GAP_IO_CAPS_NONE
#define SEC_PARAM_OOB 0 
#define SEC_PARAM_MIN_KEY_SIZE 7
#define SEC_PARAM_MAX_KEY_SIZE 16

#define DEAD_BEEF 0xDEADBEEF 

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

// basic GATT connection/handler struct for this module
typedef enum {
    BLE_NOTIF_EVT_NOTIFICATION_ENABLED,  
    BLE_NOTIF_EVT_NOTIFICATION_DISABLED 
    } ble_bulksrv_evt_type_t;
typedef enum {
    CHAR_MODE_READ,
    CHAR_MODE_WRITE
    } app_char_type_t;	   
typedef struct {
    ble_bulksrv_evt_type_t evt_type;  
    } ble_bulksrv_evt_t;
typedef struct bulksrv_s bulksrv_t;
typedef void (*bulksrv_evt_handler_t) (bulksrv_t * p_bulksrv, ble_bulksrv_evt_t * p_evt);
//typedef void (*nrf_ble_amtc_evt_handler_t) (struct nrf_ble_amtc_t * p_ctx, nrf_ble_amtc_evt_t    * p_evt);

struct bulksrv_s {
 ble_uuid_t service_uuid;
 ble_uuid128_t base_uuid128;
 uint16_t conn_handle;
 uint16_t clockread_handle;
 uint16_t clockread_cccd_handle;
 uint16_t conn_handle_disc;
 uint16_t clockwrite_handle;
 uint16_t max_bulksrv_len;
 uint8_t uuid_type;
 uint32_t time;
 uint32_t count;
 bool notificationset;
 bool connected;
 bool transfer_in_progress;
 };
static bulksrv_t m_bulksrv;

static ble_uuid_t m_adv_uuids[] = {{BLE_UUID_DEVICE_INFORMATION_SERVICE, BLE_UUID_TYPE_BLE}};
typedef struct {
  ble_uuid_t uuid;
  ble_gatts_char_md_t char_md;
  ble_gatts_attr_md_t cccd_md;
  ble_gatts_attr_md_t attr_md;
  ble_gatts_attr_t  attr_char_value;
  } charvar_t; 
// static charvar_t our_attributes[NUMBER_OUR_ATTRIBUTES];
// static dm_application_instance_t m_app_handle;


//   uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;   
//   ble_uuid128_t m_base_uuid128 = {BLE_UUID_OUR_BASE_UUID};
// NRF_SDH_BLE_OBSERVER(m_bulksrv,APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
// see https://devzone.nordicsemi.com/f/nordic-q-a/24292/sdk-14---how-to-set-up-a-ble-observer-for-custom-service

static uint8_t m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
static uint8_t m_enc_advdata[BLE_GAP_ADV_SET_DATA_SIZE_MAX];

static ble_gap_adv_data_t m_adv_data = {
    .adv_data = {
        .p_data = m_enc_advdata,
        .len    = BLE_GAP_ADV_SET_DATA_SIZE_MAX
        },
    .scan_rsp_data = {
        .p_data = NULL,
        .len    = 0
        }
    };

APP_TIMER_DEF(read_timer_id);
APP_TIMER_DEF(xfer_delay_id);
NRF_BLE_GATT_DEF(m_gatt);
NRF_BLE_QWR_DEF(m_qwr); 
bool qwr_initialized = false;
uint8_t qwr_uuid_type;
BLE_ADVERTISING_DEF(m_advertising);  
BLE_DB_DISCOVERY_DEF(m_db_disc);

extern void neopixel_init();
extern void neopixel_write(uint8_t* rgb);
extern void mem_init(bool force);
extern void mem_store(uint8_t* p);
extern uint32_t mem_cursor();
extern bool mem_empty();
extern void usbd_init();
extern void usbd_start();
extern void usbd_xfer();
extern bool usbd_process();
void xfer_delay_handler(void * p_context); 
static void db_disc_handler(ble_db_discovery_evt_t * p_evt);
void transfer_data(void * parameter, uint16_t size);
void softdevice_init();
void ble_stack_init();
void advertising_data_set(void);
void delete_bonds();
void services_init();

/*
static void sleep_mode_enter(void) {
  sd_power_system_off(); // <--- note timer is not running :(
  }
*/

void readstart(void * parameter, uint16_t size) {
  ret_code_t err_code;
  if (m_bulksrv.notificationset) {
      NRF_LOG_INFO("time %d received count = %d",m_bulksrv.time,m_bulksrv.count);
      }
  err_code = app_timer_start(read_timer_id,APP_TIMER_TICKS(1000), NULL);
  APP_ERROR_CHECK(err_code);
  }

void reader_timeout_handler(void * p_context) {
  // ret_code_t err_code;
  /*
  if (!m_bulksrv.activity_seen) {
    sd_ble_gap_adv_stop(m_adv_handle);
    err_code = sd_ble_gap_disconnect(m_bulksrv.conn_handle,
                                     BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
    if (err_code != NRF_SUCCESS && err_code != BLE_ERROR_INVALID_CONN_HANDLE) {
      APP_ERROR_CHECK(err_code);
      }              
    app_timer_stop_all();
    if (!m_bulksrv.connected) app_sched_event_put(NULL,1,&transfer_data);
    }
  */
  m_bulksrv.time++;
  app_sched_event_put(NULL,1,&readstart);
  }

// void bsp_event_handler(bsp_event_t event);
static void leds_init() {
    bsp_board_init(BSP_INIT_LEDS);
    }

static void log_init(void) {
  ret_code_t err_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(err_code);
  NRF_LOG_DEFAULT_BACKENDS_INIT();
  }

static void timer_init(void) {
  ret_code_t err_code;
  err_code = app_timer_init();
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_create(&read_timer_id,
              APP_TIMER_MODE_SINGLE_SHOT,
              reader_timeout_handler);
  APP_ERROR_CHECK(err_code);
  err_code = app_timer_create(&xfer_delay_id,
              APP_TIMER_MODE_SINGLE_SHOT,
              xfer_delay_handler);
  APP_ERROR_CHECK(err_code);
  }
static void gap_params_init(void) {
  ble_gap_conn_params_t   gap_conn_params;
  ble_gap_conn_sec_mode_t sec_mode;
  memset(&gap_conn_params, 0, sizeof(gap_conn_params));
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
  sd_ble_gap_device_name_set(&sec_mode,(const uint8_t *)
		  "Signal Labs",strlen("Signal Labs"));
  sd_ble_gap_appearance_set(BLE_APPEARANCE_UNKNOWN);
  gap_conn_params.min_conn_interval = MSEC_TO_UNITS(100, UNIT_1_25_MS); 
  gap_conn_params.max_conn_interval = MSEC_TO_UNITS(200, UNIT_1_25_MS);
  gap_conn_params.slave_latency     = 0; 
  gap_conn_params.conn_sup_timeout  = MSEC_TO_UNITS(4000, UNIT_10_MS); 
  sd_ble_gap_ppcp_set(&gap_conn_params);
  }

// handle a gatt event
void ble_bulksrv_on_gatt_evt(bulksrv_t * p_bulksrv, nrf_ble_gatt_evt_t const * p_gatt_evt) {
  NRF_LOG_INFO("on gatt evt");
  if ((p_bulksrv->conn_handle == p_gatt_evt->conn_handle)
        && (p_gatt_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)) {
      p_bulksrv->max_bulksrv_len = p_gatt_evt->params.att_mtu_effective;
      }
  }
static void gatt_evt_handler(nrf_ble_gatt_t * p_gatt, 
		nrf_ble_gatt_evt_t const * p_evt) {
  switch (p_evt->evt_id) {
	  case NRF_BLE_GATT_EVT_ATT_MTU_UPDATED: {
               NRF_LOG_INFO("GATT ATT MTU on connection 0x%x changed to %d.",
                   p_evt->conn_handle,
                   p_evt->params.att_mtu_effective);
	       } break;

          case NRF_BLE_GATT_EVT_DATA_LENGTH_UPDATED: {
               NRF_LOG_INFO("Data length updated to %u bytes.", 
	                    p_evt->params.data_length);
               } break;
    }
  }

static void gatt_init(void) {
    ret_code_t err_code = nrf_ble_gatt_init(&m_gatt,gatt_evt_handler);
    APP_ERROR_CHECK(err_code);
    }

static void nrf_qwr_error_handler(uint32_t nrf_error) {
    APP_ERROR_HANDLER(nrf_error);
    }

void services_init(void) {
  ret_code_t err_code;
  ble_uuid_t dis_uuid;
  ble_uuid_t serv_uuid;
  ble_uuid128_t base_uuid = {BLE_UUID_OUR_BASE_UUID};
  memset(&m_bulksrv, 0, sizeof(m_bulksrv));
  m_bulksrv.conn_handle = BLE_CONN_HANDLE_INVALID;
  m_bulksrv.conn_handle_disc = BLE_CONN_HANDLE_INVALID;
  m_bulksrv.clockread_handle = BLE_GATT_HANDLE_INVALID;
  m_bulksrv.clockwrite_handle = BLE_GATT_HANDLE_INVALID;
  m_bulksrv.clockread_cccd_handle = BLE_GATT_HANDLE_INVALID;
  if (!qwr_initialized) {
     nrf_ble_qwr_init_t qwr_init = {0};
     err_code = sd_ble_uuid_vs_add(&base_uuid, &qwr_uuid_type);
     APP_ERROR_CHECK(err_code);
     qwr_init.error_handler = nrf_qwr_error_handler;
     err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
     APP_ERROR_CHECK(err_code);
     qwr_initialized = true;
     } 
  m_bulksrv.uuid_type = qwr_uuid_type;
  serv_uuid.uuid = BLE_UUID_OUR_SERVICE;
  serv_uuid.type = m_bulksrv.uuid_type;
  dis_uuid.uuid = BLE_UUID_DEVICE_INFORMATION_SERVICE;
  dis_uuid.type = BLE_UUID_TYPE_BLE;
  // set up discovery handler
  ble_db_discovery_close();  // clean up previous data
  err_code = ble_db_discovery_init(db_disc_handler);
  APP_ERROR_CHECK(err_code);
  // say which services we want to discover
  // NRF_LOG_INFO("register for uuid %d type %d",serv_uuid.uuid, serv_uuid.type);
  err_code = ble_db_discovery_evt_register(&serv_uuid);
  APP_ERROR_CHECK(err_code);
  // NRF_LOG_INFO("register for uuid %d type %d",dis_uuid.uuid, dis_uuid.type);
  err_code = ble_db_discovery_evt_register(&dis_uuid);
  APP_ERROR_CHECK(err_code);
  }

void delete_bonds(void) {
  ret_code_t err_code;
  // NRF_LOG_INFO("Erase bonds!");
  err_code = pm_peers_delete();
  APP_ERROR_CHECK(err_code);
  }

void advertising_start() {
  if (m_adv_handle ==  BLE_GAP_ADV_SET_HANDLE_NOT_SET) advertising_data_set();
  ret_code_t err_code = sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG);
  APP_ERROR_CHECK(err_code);
  }

static void pm_evt_handler(pm_evt_t const * p_evt) {
    pm_handler_on_pm_evt(p_evt);
    pm_handler_flash_clean(p_evt);

    switch (p_evt->evt_id) {
        case PM_EVT_PEERS_DELETE_SUCCEEDED:
            advertising_start();
            break;
        default:
            break;
        }
    }
static void peer_manager_init(void) {
    ble_gap_sec_params_t sec_param;
    ret_code_t           err_code;
    err_code = pm_init();
    APP_ERROR_CHECK(err_code);

    memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));
    // Security parameters to be used for all security procedures.
    sec_param.bond           = SEC_PARAM_BOND;
    sec_param.mitm           = SEC_PARAM_MITM;
    sec_param.lesc           = SEC_PARAM_LESC;
    sec_param.keypress       = SEC_PARAM_KEYPRESS;
    sec_param.io_caps        = SEC_PARAM_IO_CAPABILITIES;
    sec_param.oob            = SEC_PARAM_OOB;
    sec_param.min_key_size   = SEC_PARAM_MIN_KEY_SIZE;
    sec_param.max_key_size   = SEC_PARAM_MAX_KEY_SIZE;
    sec_param.kdist_own.enc  = 1;
    sec_param.kdist_own.id   = 1;
    sec_param.kdist_peer.enc = 1;
    sec_param.kdist_peer.id  = 1;
    err_code = pm_sec_params_set(&sec_param);
    APP_ERROR_CHECK(err_code);
    err_code = pm_register(pm_evt_handler);
    APP_ERROR_CHECK(err_code);
    }


static void on_conn_params_evt(ble_conn_params_evt_t * p_evt) {
  NRF_LOG_INFO("on_conn_params_evt %d",p_evt->evt_type);
  if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
    sd_ble_gap_disconnect(m_bulksrv.conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
    }
  }

static void conn_params_error_handler(uint32_t nrf_error) {
  APP_ERROR_HANDLER(nrf_error);
  }

static void conn_params_init(void) {
  ret_code_t             err_code;
  ble_conn_params_init_t cp_init;

  memset(&cp_init, 0, sizeof(cp_init));

  cp_init.p_conn_params                  = NULL;
  cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
  cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
  cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
  // cp_init.start_on_bulksrvy_cccd_handle    = m_bulksrv.bulksrv_handles.cccd_handle;
  cp_init.disconnect_on_fail             = false;
  cp_init.evt_handler                    = on_conn_params_evt;
  cp_init.error_handler                  = conn_params_error_handler;
  err_code = ble_conn_params_init(&cp_init);
  APP_ERROR_CHECK(err_code);
  }

static void finish_connect(ble_db_discovery_evt_t * p_evt) {
  ret_code_t err_code;
  uint8_t rgb[3] = {0x00,0x33,0xff};  
  // ble_gatts_value_t gatts_val;
  uint16_t cccd_valuebase = BLE_GATT_HVX_NOTIFICATION;
  uint8_t cccd_value[BLE_CCCD_VALUE_LEN];
  ble_gattc_write_params_t params;
  memset(&params, 0, sizeof(params));
  params.handle = m_bulksrv.clockread_cccd_handle;
  params.len = BLE_CCCD_VALUE_LEN;
  params.p_value = cccd_value;
  params.offset   = 0;
  params.write_op = BLE_GATT_OP_WRITE_REQ; // or maybe WRITE_CMD ??
  cccd_value[0] = LSB_16(cccd_valuebase);
  cccd_value[1] = MSB_16(cccd_valuebase);
  err_code = sd_ble_gattc_write(m_bulksrv.conn_handle,&params);
  APP_ERROR_CHECK(err_code);
  app_timer_stop_all();
  err_code = app_timer_start(read_timer_id,APP_TIMER_TICKS(1000), NULL);
  APP_ERROR_CHECK(err_code);
  neopixel_write(rgb);
  }

static void db_disc_handler(ble_db_discovery_evt_t * p_evt) {
  // ref: ble_db_discovery.h for type of p_evt
  // Check if the clock service was discovered.
  if (p_evt->evt_type != BLE_DB_DISCOVERY_COMPLETE) return;
  if (p_evt->params.discovered_db.srv_uuid.uuid != BLE_UUID_OUR_SERVICE) return;
  m_bulksrv.conn_handle_disc = p_evt->conn_handle;
  // find characteristic handles
  for (uint32_t i = 0; i < p_evt->params.discovered_db.char_count; i++) {
    ble_uuid_t uuid = p_evt->params.discovered_db.charateristics[i].characteristic.uuid;
    if (uuid.uuid == CLOCK_READ_UUID) {
       m_bulksrv.clockread_handle =
            p_evt->params.discovered_db.charateristics[i].characteristic.handle_value;
       m_bulksrv.clockread_cccd_handle =
            p_evt->params.discovered_db.charateristics[i].cccd_handle;
       }
    if (uuid.uuid == CLOCK_WRITE_UUID) {
       m_bulksrv.clockwrite_handle =
            p_evt->params.discovered_db.charateristics[i].characteristic.handle_value;
       }
    }
  // after loop -- schedule things to start here
  if (    m_bulksrv.clockread_handle != BLE_GATT_HANDLE_INVALID
       && m_bulksrv.clockwrite_handle != BLE_GATT_HANDLE_INVALID
       && m_bulksrv.clockread_cccd_handle != BLE_GATT_HANDLE_INVALID )
          finish_connect(p_evt);
  }

void on_hvx(ble_evt_t const * p_ble_evt) {
  if (p_ble_evt->evt.gattc_evt.conn_handle != m_bulksrv.conn_handle) return;
  if (p_ble_evt->evt.gattc_evt.params.hvx.handle == m_bulksrv.clockread_handle) {
    NRF_LOG_RAW_HEXDUMP_INFO((uint8_t*)p_ble_evt->evt.gattc_evt.params.hvx.data,8)
    mem_store((uint8_t*)p_ble_evt->evt.gattc_evt.params.hvx.data);
    m_bulksrv.count++;
    }
  }

static void on_write_response(ble_evt_t const * p_ble_evt) {
  if (m_bulksrv.conn_handle != p_ble_evt->evt.gattc_evt.conn_handle) return;
  if (p_ble_evt->evt.gattc_evt.params.write_rsp.handle == m_bulksrv.clockread_cccd_handle) {
     NRF_LOG_INFO("setting notification");
     m_bulksrv.notificationset = true;
     }
  }

static void on_read_response(ble_evt_t const * p_ble_evt) {
  NRF_LOG_DEBUG("read response");
  }

void transfer_data(void * parameter, uint16_t size) {
  ret_code_t err_code;
  uint8_t rgb[3] = {0x33,0x55,0x00};
  ble_gap_adv_data_t refresh_adv_data = {
    .adv_data = {
        .p_data = m_enc_advdata,
        .len    = BLE_GAP_ADV_SET_DATA_SIZE_MAX
        },
    .scan_rsp_data = {
        .p_data = NULL,
        .len    = 0
        }
    };
  if (m_bulksrv.transfer_in_progress) return;
  m_bulksrv.transfer_in_progress = true;
  neopixel_write(rgb);
  NRF_LOG_INFO("total transfer time %d seconds",m_bulksrv.time);
  NRF_LOG_INFO("restarting");
  NRF_LOG_FLUSH();
  usbd_xfer();  // launch USB data transfer
  err_code = app_timer_start(xfer_delay_id,APP_TIMER_TICKS(5000), NULL);
  APP_ERROR_CHECK(err_code);
  // m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
  m_adv_data = refresh_adv_data;
  }
void xfer_delay_handler(void * p_context) {
  // assumption: m_bulksrv.connected is false, and it won't hurt
  // to refresh all fields in m_bulksrv
  ret_code_t err_code;
  if (mem_empty()) {
     NRF_LOG_INFO("advertising restart after xfer");
     //	instead of sd_nvic_SystemReset() we want to keep the 
     //	USBD connection established and do minimal work
     services_init(); // wipe out and reset m_bulksrv
     advertising_start();
     }
  else { // we are polling the USB transfer until mem_cursor() is zero 
     err_code = app_timer_start(xfer_delay_id,APP_TIMER_TICKS(500), NULL);
     APP_ERROR_CHECK(err_code);
     }
  }	

static void on_ble_gap_evt_connected(ble_gap_evt_t const * p_gap_evt) {
  ret_code_t err_code;
  ble_gap_phys_t const phys = {
    .rx_phys = BLE_GAP_PHY_2MBPS,
    .tx_phys = BLE_GAP_PHY_2MBPS,
    };
  if (m_bulksrv.conn_handle == BLE_CONN_HANDLE_INVALID) {
    NRF_LOG_INFO("Connected.");
    }
  // err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
  // APP_ERROR_CHECK(err_code);

  // 0x3002 means invalid connection handle, but somehow that
  // is tolerable (maybe because we get called later) - see ble_err.h   
  err_code = sd_ble_gap_phy_update(m_bulksrv.conn_handle,&phys);
  if (err_code != NRF_SUCCESS && err_code != BLE_ERROR_INVALID_CONN_HANDLE) {
     APP_ERROR_CHECK(err_code);
     }

  m_bulksrv.conn_handle = p_gap_evt->conn_handle;
  err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr,m_bulksrv.conn_handle);
  APP_ERROR_CHECK(err_code);

  // NRF_LOG_INFO("Discovering GATT database... %d",m_bulksrv.conn_handle);
  err_code  = ble_db_discovery_start(&m_db_disc,m_bulksrv.conn_handle);
  APP_ERROR_CHECK(err_code);
  }

static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context) {
    ret_code_t err_code;

    switch (p_ble_evt->header.evt_id) {

        case BLE_GAP_EVT_CONNECTED: {
            ble_gap_evt_t const * p_gap_evt = &p_ble_evt->evt.gap_evt;
	    m_bulksrv.connected = true;
            NRF_LOG_DEBUG("BLE_GAP_EVT_CONNECTED");
	    on_ble_gap_evt_connected(p_gap_evt);
            } break;

        case BLE_GAP_EVT_DISCONNECTED:
            NRF_LOG_INFO("Disconnected, reason %d.", p_ble_evt->evt.gap_evt.params.disconnected.reason);
            sd_ble_gap_adv_stop(m_adv_handle);
	    m_bulksrv.connected = false;
            app_timer_stop_all();
	    app_sched_event_put(NULL,1,&transfer_data);
            break;

        case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
            NRF_LOG_DEBUG("PHY update request.");
            ble_gap_phys_t const phys =
            {
                // .rx_phys = BLE_GAP_PHY_AUTO,
                // .tx_phys = BLE_GAP_PHY_AUTO,
                .rx_phys = BLE_GAP_PHY_2MBPS,
                .tx_phys = BLE_GAP_PHY_2MBPS,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
            APP_ERROR_CHECK(err_code);
            } break;

        case BLE_GATTC_EVT_TIMEOUT:
            // Disconnect on GATT Client timeout event.
            NRF_LOG_DEBUG("GATT Client Timeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if (err_code != NRF_SUCCESS && err_code != BLE_ERROR_INVALID_CONN_HANDLE) {
               APP_ERROR_CHECK(err_code);
               }
            sd_ble_gap_adv_stop(m_adv_handle);
            app_timer_stop_all();
	    if (!m_bulksrv.connected) app_sched_event_put(NULL,1,&transfer_data);
            break;
        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            NRF_LOG_DEBUG("BLE_GAP_EVT_SEC_PARAMS_REQUEST");
            break;

        case BLE_GAP_EVT_AUTH_KEY_REQUEST:
            NRF_LOG_INFO("BLE_GAP_EVT_AUTH_KEY_REQUEST");
            break;

        case BLE_GAP_EVT_LESC_DHKEY_REQUEST:
            NRF_LOG_INFO("BLE_GAP_EVT_LESC_DHKEY_REQUEST");
            break;

        case BLE_GAP_EVT_AUTH_STATUS:
            NRF_LOG_INFO("BLE_GAP_EVT_AUTH_STATUS");
                      //    p_ble_evt->evt.gap_evt.params.auth_status.auth_status,
                      //    p_ble_evt->evt.gap_evt.params.auth_status.bonded,
                      //    p_ble_evt->evt.gap_evt.params.auth_status.sm1_levels.lv4,
                      //    *((uint8_t *)&p_ble_evt->evt.gap_evt.params.auth_status.kdist_own),
                     //     *((uint8_t *)&p_ble_evt->evt.gap_evt.params.auth_status.kdist_peer));
            break;

	case BLE_GATTC_EVT_REL_DISC_RSP:
	    NRF_LOG_INFO("BLE_GATTC_EVT_REL_DISC_RSP");
	    break;

	case BLE_GATTC_EVT_CHAR_DISC_RSP:
	    NRF_LOG_INFO("BLE_GATTC_EVT_CHAR_DISC_RSP");
	    break;

	case BLE_GATTC_EVT_DESC_DISC_RSP:
	    NRF_LOG_INFO("BLE_GATTC_EVT_DESC_DISC_RSP");
	    break;

        case BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP:
	    NRF_LOG_INFO("BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP");
	    break;
	
	case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
	    NRF_LOG_INFO("BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST");
	    break;

        case BLE_GATTC_EVT_HVX:
	    // NRF_LOG_INFO("BLE_GATTC_EVT_HVX");
            on_hvx(p_ble_evt);
            break;

        case BLE_GATTC_EVT_WRITE_RSP:
	    NRF_LOG_INFO("BLE_GATTC_EVT_WRITE_RSP");
            on_write_response(p_ble_evt);
            break;

        case BLE_GATTC_EVT_READ_RSP:
	    NRF_LOG_INFO("BLE_GATTC_EVT_READ_RSP");
            on_read_response(p_ble_evt);
            break;

	case BLE_GATTC_EVT_EXCHANGE_MTU_RSP:
            NRF_LOG_INFO("BLE_GATTC_EVT_EXCHANGE_MTU_RSP");
	    break;
	
	case BLE_CONN_CFG_GATTS:
	    NRF_LOG_INFO("BLE_CONN_CFG_GATTS");
	    break;

	case BLE_CONN_CFG_L2CAP:
	    NRF_LOG_INFO("BLE_CONN_CFG_L2CAP");
	    break;
	
        default:
            NRF_LOG_INFO("got ble event %d",p_ble_evt->header.evt_id);
            // No implementation needed.
            break;
	}
    if (mem_cursor() >= MEMCHUNKS_POOL) {
        sd_ble_gap_adv_stop(m_adv_handle);
	if (m_bulksrv.connected) {
          err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                     BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
	  if (err_code != NRF_SUCCESS && err_code != BLE_ERROR_INVALID_CONN_HANDLE) {
	     NRF_LOG_INFO("disconnect failed reason %d",err_code);
             }
	  m_bulksrv.connected = false;
	  }
        }
    }

void softdevice_init() {   
  ret_code_t err_code;
  err_code = nrf_sdh_enable_request();
  APP_ERROR_CHECK(err_code);
  }

void ble_stack_init(void) {
  ret_code_t err_code;

  uint32_t ram_start = 0;
  err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
  APP_ERROR_CHECK(err_code);

  // Enable BLE stack.
  err_code = nrf_sdh_ble_enable(&ram_start);
  APP_ERROR_CHECK(err_code);

  // Register a handler for BLE events.
  NRF_SDH_BLE_OBSERVER(m_bulksrv,APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
  }

void advertising_data_set(void) {
    ret_code_t ret;

    static ble_gap_adv_params_t const adv_params = {
       .properties    = {
          .type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED,
          },
       .p_peer_addr   = NULL,
       .filter_policy = BLE_GAP_ADV_FP_ANY,
       .interval      = APP_ADV_INTERVAL,
       .duration      = 0,
       .primary_phy   = BLE_GAP_PHY_1MBPS, // long range is (BLE_GAP_PHY_CODED)
       .secondary_phy = BLE_GAP_PHY_1MBPS|BLE_GAP_PHY_2MBPS,
       };

    static ble_advdata_t const adv_data = {
       .name_type          = BLE_ADVDATA_FULL_NAME,
       .flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE,
       .include_appearance = true, 
       .uuids_complete     = {1,m_adv_uuids}
       };

    ret = ble_advdata_encode(&adv_data, m_adv_data.adv_data.p_data, &m_adv_data.adv_data.len);
    APP_ERROR_CHECK(ret);
    if (m_adv_handle == BLE_GAP_ADV_SET_HANDLE_NOT_SET) {
      ret = sd_ble_gap_adv_set_configure(&m_adv_handle, &m_adv_data, &adv_params);
      APP_ERROR_CHECK(ret);
      }
    }

/*
void bsp_event_handler(bsp_event_t event) {
    ret_code_t err_code;
    switch (event) {
        case BSP_EVENT_SLEEP:
            sleep_mode_enter();
            break;
        case BSP_EVENT_DISCONNECT:
	    NRF_LOG_INFO("Bsp disconnect");
            err_code = sd_ble_gap_disconnect(m_bulksrv.conn_handle,
                          BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if (err_code != NRF_SUCCESS && err_code != BLE_ERROR_INVALID_CONN_HANDLE &&
		   err_code != NRF_ERROR_INVALID_STATE) {
              APP_ERROR_CHECK(err_code);
              }
	    m_bulksrv.connected = false;
            break;
        case BSP_EVENT_WHITELIST_OFF:
            if (m_bulksrv.conn_handle == BLE_CONN_HANDLE_INVALID) {
                err_code = ble_advertising_restart_without_whitelist(
				&m_advertising);
                if (err_code != NRF_ERROR_INVALID_STATE) {
                    APP_ERROR_CHECK(err_code);
                    }
                }
	case BSP_EVENT_KEY_2:
            // NRF_LOG_INFO("Key_2 Pressed");
            //    app_sched_event_put(NULL,1,&motetx);
            break;
        default:
            break;
        }
    }
    */

void power_management_init(void) {
  ret_code_t err_code;
  err_code = nrf_pwr_mgmt_init();
  APP_ERROR_CHECK(err_code);
  }


void power_manage(void) {
  // sd_app_evt_wait();
  // ret_code_t err_code = nrf_ble_lesc_request_handler();
  // APP_ERROR_CHECK(err_code);
  if (NRF_LOG_PROCESS() == false) nrf_pwr_mgmt_run();
  }

void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name) {
  app_error_handler(DEAD_BEEF, line_num, p_file_name);
  }

static void init_all() {
  uint8_t rgb[3] = {0xff,0x00,0x00};
  log_init();
  timer_init();
  leds_init();
  neopixel_init();
  neopixel_write(rgb);
  APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
  mem_init(true);
  power_management_init();
  usbd_init();  // must come before softdevice_init
  softdevice_init();
  ble_stack_init();
  gap_params_init();
  gatt_init();
  services_init();
  // advertising_data_set();
  conn_params_init();
  peer_manager_init();
  delete_bonds();
  }

int main(void) {
  init_all();
  usbd_start();
  NRF_LOG_INFO("app running");
  for (;;) { 
    while (usbd_process()) { };
    app_sched_execute();
    NRF_LOG_FLUSH();
    power_manage(); 
    }
  }
