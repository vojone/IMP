/**
 * @file ble_receiver.h
 * @author Vojtěch Dvořák (xdvora3o)
 * @date 2022-12-12
 */

#ifndef __BLE_RECEIVER__
#define __BLE_RECEIVER__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"


/**
 * @brief The name of this module
 * 
 */
#define MODULE_TAG "BLE_REC"


/**
 * @brief The name of this bluetooth device
 * 
 */
#define DEVICE_NAME "Morse code - receiver"


/**
 * @brief UUIDs
 * 
 */
#define GATTS_SERVICE_UUID_MORSE_CODE_RECEIVER 0xabcd

#define GATTS_CHAR_UUID_MORSE_CODE_RECEIVER_LETTER 0x0000
#define GATTS_DESCR_UIID_MORSE_CODE_RECEIVER_LETTER 0x0000

#define GATTS_CHAR_UUID_MORSE_CODE_RECEIVER_VOL 0x0001
#define GATTS_DESCR_UIID_MORSE_CODE_RECEIVER_VOL 0x0001

#define GATTS_CHAR_UUID_MORSE_CODE_RECEIVER_ABORT 0x0002
#define GATTS_DESCR_UIID_MORSE_CODE_RECEIVER_ABORT 0x0002

#define GATTS_NUM_HANDLE_MORSE_CODE 10 //< The number of addresable attributes on a GATT server (service, characteristic, char_val, char_descriptor)
//1 service + 3 characteristics + 3 characteristic values + 3 characteristic descriptors

/**
 * @brief Led pin for 
 * 
 */
#define CONNECTION_GPIO GPIO_NUM_2

/**
 * @brief Profile indexes
 * 
 */
enum profiles {
    MORSE_CODE_RECEIVER_ID,
    PROFILE_NUM,   
};


/**
 * @brief Indexes of characteristics
 * 
 */
enum morse_code_rec_chars {
    LETTER_CHAR, //< Characteristic for writing message
    VOLUME_CHAR, //< Characteristic for writing and reading volume
    ABORT_CHAR,  //< Characteristic for abroting beeping
    MORSE_CODE_REC_CHAR_NUM,
};


/**
 * @brief Tab with charcteric handles
 * 
 */
uint16_t morse_code_char_handle_tab[MORSE_CODE_REC_CHAR_NUM];


/**
 * @brief Element of GATT profile table
 * 
 */
struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    uint16_t *char_handle_tab;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

/**
 * @brief Handling function of morse code app that operates on this GATT server
 * 
 * @param evt incoming event
 * @param gatts_if GATTS interface
 * @param param event parameters
 */
void gatts_profile_morse_code_event_handler(esp_gatts_cb_event_t evt, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);


/**
 * @brief Tabs with BLE profiles (we have only one service)
 * 
 */
extern struct gatts_profile_inst profile_tab[];


/**
 * @brief Inititializes the bluetooth module 
 * 
 * @param write_event_handler_func Callback fro write GATT events
 * @param add_char_cb_func Callback for add char GATT event (can be used for initialization of characteristics values)
 * @return esp_err_t ESP_OK if eferything went OK
 */
esp_err_t bluetooth_init(void (*write_event_handler_func)(esp_ble_gatts_cb_param_t *), void (*add_char_cb_func)(uint16_t));


#endif
