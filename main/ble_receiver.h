/**
 * @file ble_receiver.h
 * @author Vojtěch Dvořák (xdvora3o)
 * @date 2022-12-05
 */

#ifndef __BLE_RECEIVER__
#define __BLE_RECEIVER__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"


#define MODULE_TAG "BLE_REC"


#define DEVICE_NAME "Morse code - receiver"


#define GATTS_SERVICE_UUID_MORSE_CODE_RECEIVER 0xabcd

#define GATTS_CHAR_UUID_MORSE_CODE_RECEIVER_LETTER 0x0000
#define GATTS_DESCR_UIID_MORSE_CODE_RECEIVER_LETTER 0x0000

#define GATTS_CHAR_UUID_MORSE_CODE_RECEIVER_VOL 0x0001
#define GATTS_DESCR_UIID_MORSE_CODE_RECEIVER_VOL 0x0001

#define GATTS_NUM_HANDLE_MORSE_CODE 7 //< The number of addresable attributes on a GATT server (service, characteristic, char_val, char_descriptor)
//1 service + 2 characteristics + 2 characteristic values + 2 characteristic descriptors


enum profiles {
    MORSE_CODE_RECEIVER_ID,
    PROFILE_NUM,   
};

enum morse_code_rec_chars {
    LETTER_CHAR,
    VOLUME_CHAR,  
    MORSE_CODE_REC_CHAR_NUM,
};


uint16_t morse_code_char_handle_tab[MORSE_CODE_REC_CHAR_NUM];


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
 * @param evt 
 * @param gatts_if 
 * @param param 
 */
void gatts_profile_morse_code_event_handler(esp_gatts_cb_event_t evt, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);


extern struct gatts_profile_inst profile_tab[];


esp_err_t bluetooth_init(void (*write_event_handler_func)(esp_ble_gatts_cb_param_t *));


#endif
