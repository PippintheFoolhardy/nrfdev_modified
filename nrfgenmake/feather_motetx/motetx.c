#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_sdm.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "app_error.h"
#include "app_scheduler.h"
#include "app_timer.h"
#include "nrf_802154.h"
#include "nrf_radio.h"
#include "nrf_802154_types.h"
#include "nrf_802154_clock.h"
#include "boards.h"
#include "IEEE802154.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_pwr_mgmt.h"

#define MAX_MESSAGE_SIZE 37 
#define CHANNEL 26 
#define PACKET_MAX_PAYLOAD 26 

#define APP_BLE_CONN_CFG_TAG 1 // identifying the SoftDevice BLE configuration
#define APP_BLE_OBSERVER_PRIO 3 
#define SCHED_MAX_EVENT_DATA_SIZE APP_TIMER_SCHED_EVENT_DATA_SIZE
#define SCHED_QUEUE_SIZE 10

// for mote 802.15.4    
typedef uint16_t mac_addr_t;
struct motepacket {
  uint8_t length;
  uint16_t fcf;
  uint8_t data_seq_no;
  mac_addr_t pan;
  mac_addr_t dest;
  mac_addr_t src;
  uint8_t iframe;    // field for 6lowpan, is 0x3f for TinyOS
  uint8_t type;
  uint8_t data[PACKET_MAX_PAYLOAD];
  uint8_t fcs[2];    // will become CRC, don't count this in length.
  };
typedef struct motepacket motepacket_t;

APP_TIMER_DEF(mote_timer_id);

uint8_t seqno = 0; 
uint8_t buffer[sizeof(motepacket_t)];
motepacket_t * packetptr = (motepacket_t *)buffer;
uint8_t nnqslotstate;
uint32_t ticksremain;
uint8_t nnqvect[3];

// int8_t  powervalues[] = {-30,-20,-16,-12,-8,-4,0,4};

uint8_t extended_address[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
uint8_t short_address[]    = {0x06, 0x07};
uint8_t pan_id[]           = {0x04, 0x05};

static void mote_timeout_handler(void * p_context);
void motesetup() {
    ret_code_t err_code;

    nrf_802154_init();
    if (!nrf_802154_clock_hfclk_is_running()) nrf_802154_clock_hfclk_start();
    nrf_802154_short_address_set(short_address);
    nrf_802154_extended_address_set(extended_address);
    nrf_802154_pan_id_set(pan_id);
    nrf_802154_promiscuous_set(true);
    nrf_802154_channel_set(CHANNEL);
    nrf_802154_tx_power_set(4);  // 4 is the supposed maximum power
       // but has bad results; so try 0 and see how that works
    err_code = app_timer_create(&mote_timer_id,
		                APP_TIMER_MODE_SINGLE_SHOT,
                                mote_timeout_handler);
    APP_ERROR_CHECK(err_code);
    err_code = app_timer_start(mote_timer_id, 
		    APP_TIMER_TICKS(5000), NULL);
    APP_ERROR_CHECK(err_code);

    packetptr->fcf = 0x0000;
    packetptr->fcf &= 1 << IEEE154_FCF_ACK_REQ;
    packetptr->fcf |= ( ( IEEE154_TYPE_DATA << IEEE154_FCF_FRAME_TYPE )
                  |  ( 1 << IEEE154_FCF_INTRAPAN )
                  |  ( IEEE154_ADDR_SHORT << IEEE154_FCF_DEST_ADDR_MODE )
                  |  ( IEEE154_ADDR_SHORT << IEEE154_FCF_SRC_ADDR_MODE ) );
    packetptr->data_seq_no = 0;
    packetptr->pan = 0x22;     // GROUP
    packetptr->dest = 0xFFFF;  // BCAST_ADDR
    packetptr->src = 99;       // ID of sender
    packetptr->iframe = 0x3f;  // 6LowPan designator 
    packetptr->type = 0x37;    // (bogus for now, could have meaning later)
    // -1 for length, +2 for unknown reason - seems to sort of work, 
    // perhaps because of the CRC bytes in the fcs?
    packetptr->length =  offsetof(motepacket_t,data) + 2 + PACKET_MAX_PAYLOAD - 1;
    }

void motestate(void * parameter, uint16_t size) {
    nrf_802154_state_t c;
    c = nrf_802154_state_get(); 
    NRF_LOG_INFO("state of 802154 = %d",c);
    }

void motetx(void * parameter, uint16_t size) {
    bool b;
    uint8_t c;
    ret_code_t err_code;
    NRF_LOG_INFO("motetx start");
    NRF_LOG_INFO("hfclk_is_ready = %d",
		    nrf_802154_clock_hfclk_is_running());
    c = nrf_802154_state_get(); 
    if (c != NRF_802154_STATE_SLEEP) {
       NRF_LOG_INFO("802154 state = %d",c);
       nrf_802154_sleep();
       // nrf_802154_deinit();
       err_code = app_timer_start(mote_timer_id, 
		    APP_TIMER_TICKS(5000), NULL);
       APP_ERROR_CHECK(err_code);
       return;
       }
    // in sleep state, so schedule for re-tx later
    err_code = app_timer_start(mote_timer_id, 
		    APP_TIMER_TICKS(5000), NULL);
    APP_ERROR_CHECK(err_code);
    packetptr->data_seq_no = seqno++;
    for (int i=0; i<PACKET_MAX_PAYLOAD; i++) packetptr->data[i] = seqno+i;
    bsp_board_led_invert(0);

    b = nrf_802154_transmit_raw(buffer,true);
    c = nrf_802154_state_get(); 
    NRF_LOG_INFO("802154_transmit_raw attempted with result %d,%d",b,c);
    NRF_LOG_FLUSH();
    bsp_board_led_invert(1);
    }

/*
void nrf_802154_tx_started (const uint8_t *p_frame) {
    NRF_LOG_INFO("802154 tx started");
    NRF_LOG_FLUSH();
    }
    */

void nrf_802154_transmitted_raw(const uint8_t *p_frame, 
		uint8_t *p_ack, int8_t power, uint8_t lqi) {
    nrf_802154_sleep();
    }

void nrf_802154_transmit_failed (const uint8_t *p_frame, nrf_802154_tx_error_t error) {
    nrf_802154_sleep();
    }

static void mote_timeout_handler(void * p_context) {
    app_sched_event_put(NULL,1,&motetx);
    NRF_LOG_INFO("timeout scheduled event");
    }

void leds_init(void) {
    // ret_code_t err_code;
    bsp_board_init(BSP_INIT_LEDS);
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

static void timers_init(void) {
    ret_code_t err_code;
    err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
    }

void sdhble_init(void) {
    ret_code_t err_code;
    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);
    }

int main(void) {
    log_init();
    timers_init();
    leds_init();
    power_management_init();
    APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
    sdhble_init();
    motesetup();
    // Enter main loop.
    for (;;) {
	app_sched_execute();
  	sd_app_evt_wait();
        // nrf_pwr_mgmt_run();
        }
    }

