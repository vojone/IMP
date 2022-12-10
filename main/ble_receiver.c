/**
 * @file ble_receiver.c
 * @author Vojtěch Dvořák (xdvora3o)
 * @date 2022-12-05
 */

#include "ble_receiver.h"

static void (*write_event_handler)(esp_ble_gatts_cb_param_t *) = NULL;
static void (*add_char_cb)(uint16_t) = NULL;


//Based on https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/bluedroid/ble/gatt_server/tutorial/Gatt_Server_Example_Walkthrough.md

static esp_gatt_char_prop_t morse_code_letter_properties = ESP_GATT_CHAR_PROP_BIT_WRITE; //< Just hint for client what actions he is able to do with characteristic
static esp_gatt_perm_t morse_code_letter_permissions = ESP_GATT_PERM_WRITE; //< The GATT server will reject read event of morse code message characteristic


static esp_gatt_char_prop_t morse_code_vol_properties = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ; //< Just hint for client what actions he is able to do with characteristic
static esp_gatt_perm_t morse_code_vol_permissions = ESP_GATT_PERM_WRITE | ESP_GATT_PERM_READ; //< The GATT server will reject read event of morse code message characteristic


struct gatts_profile_inst profile_tab[PROFILE_NUM] = { //< Table with all provided profiles of this GATT server
    [MORSE_CODE_RECEIVER_ID] = {
        .gatts_cb = gatts_profile_morse_code_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
        .char_handle_tab = morse_code_char_handle_tab,
    },
};


//Following attribute value is not used - there is queue instead

/**
 * @brief Value of letter to be beeped
 * 
 */
uint8_t morse_code_letter_val[] = { 0x00 };

/**
 * @brief Characteristic value for storing letter to be beeped
 * 
 */
esp_attr_value_t morse_code_letter_char_val = {
    .attr_max_len = 1,
    .attr_len = 1,
    .attr_value = morse_code_letter_val
};



uint8_t morse_code_volume_val[] = { 0x00 };

esp_attr_value_t morse_code_volume_char_val = {
    .attr_max_len = 1,
    .attr_len = 1,
    .attr_value = morse_code_volume_val,
};


/**
 * @brief Initialized structure for creating advertise packets (=advertising data content)
 * 
 */
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false, //< Are this data for scan response?
    .include_name = true, //< Does this data contain the device name?
    .include_txpower = true, //< TX power = the worst-case transmit power
    .min_interval = 0x0006, //< 0x006*1.25 ms = 7.5 ms - Preffered minimal interval between each connection
    .max_interval = 0x0010, //< 0x0010*1.25 ms = 20 ms
    .appearance = 0x00, //< Unknown (due to https://specificationrefs.bluetooth.com/assigned-values/Appearance%20Values.pdf)
    .manufacturer_len = 0, //< Service manufacturer
    .p_manufacturer_data = NULL, 
    .service_data_len = 0, 
    .p_service_data = NULL, //< Service data point
    .service_uuid_len = 0, //< Service UUID - there is no need to add it to advertising packets
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};


/**
 * @brief Initialized structure for sending responses to device scan
 * @see adv_data for description of the members
 */
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true, //< Are this data scan response?
    .include_name = true,
    .include_txpower = true,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0, //< Service UUID - there is no need to add it to scan response packets
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};


/**
 * @brief Advertising parameters for GAP layer
 * 
 */
static esp_ble_adv_params_t adv_params = {
    //Advertising interval = the time between advertising events
    .adv_int_min = 0x20, //< Minimum advertising interval (0x20 * 0.625 ms) - (min 20ms)
    .adv_int_max = 0x40, //< Maximum advertising interval (0x40 * 0.625 ms) - (max 10.24s)
    .adv_type = ADV_TYPE_IND, //< Peripheral (this) device request connection to any central device
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC, //< Device address type
    .channel_map = ADV_CHNL_ALL, //< What channel shold be used for advertising (2.402 MHz, 2.2426 MHz, and 2.480 MHz)
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY //< Accept all connection and scan req from all devices
};



static uint8_t adv_config_done = 0; //<
#define ADV_CONFIG_FLAG 1 //< Flag signalizing, that advertising configuration is not done
#define SCAN_RESPONSE_CONFIG_FLAG 2 //< Flag signalizing, that adv scan response is not done yet



/**
 * @brief Prints bluetooth addres to the informational log
 * 
 */
void print_bluetooth_addr() {
    const uint8_t *addr = esp_bt_dev_get_address();

    ESP_LOGI(MODULE_TAG, "Bluetooth addr: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(addr));
}



/**
 * @brief 
 * 
 * @param evt 
 * @param gatts_if 
 * @param params 
 */
void gatts_profile_morse_code_event_handler(esp_gatts_cb_event_t evt, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *params) {
    esp_err_t err;

    switch (evt)
    {
    case ESP_GATTS_REG_EVT: //< Registrering Morse code profile (app)
        ESP_LOGI(MODULE_TAG, "REGISTER_APP_EVT, status=%d, app_id=%d\n", params->reg.status, params->reg.app_id);
        profile_tab[MORSE_CODE_RECEIVER_ID].service_id.is_primary = true; //Intializing GATT service id for this profile
        profile_tab[MORSE_CODE_RECEIVER_ID].service_id.id.inst_id = 0x00;
        profile_tab[MORSE_CODE_RECEIVER_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        profile_tab[MORSE_CODE_RECEIVER_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_MORSE_CODE_RECEIVER;
        
        esp_ble_gap_set_device_name(DEVICE_NAME); //Set the name of this device

        err = esp_ble_gap_config_adv_data(&adv_data); //< Start setting our (custom) advertising data
        if(err != ESP_OK) {
            ESP_LOGE(MODULE_TAG, "%s: esp_ble_gap_config_adv_data failed (%s)", __func__, esp_err_to_name(err));
        }
        adv_config_done |= ADV_CONFIG_FLAG; //< Set the flag to 1 to avoid advertising before all the data are set properly
        
        err = esp_ble_gap_config_adv_data(&scan_rsp_data); //< Start setting our (custom) advertising response data
        if(err != ESP_OK) {
            ESP_LOGE(MODULE_TAG, "%s: esp_ble_gap_config_adv_data failed (%s)", __func__, esp_err_to_name(err));
        }
        adv_config_done |= SCAN_RESPONSE_CONFIG_FLAG;

        esp_ble_gatts_create_service( //< Create service of the morse code application profile
            gatts_if, 
            &profile_tab[MORSE_CODE_RECEIVER_ID].service_id, 
            GATTS_NUM_HANDLE_MORSE_CODE
        );

        break;
    
    case ESP_GATTS_CREATE_EVT: //< Create service with characteristics
        ESP_LOGI(MODULE_TAG, "CREATE_EVT, status=%d, serv_handle=%d", params->reg.status, params->create.service_handle);
        profile_tab[MORSE_CODE_RECEIVER_ID].service_handle = params->create.service_handle; //< Storing the generated service handle in profile tab (for ref the service later)
    
        esp_ble_gatts_start_service(profile_tab[MORSE_CODE_RECEIVER_ID].service_handle);

        profile_tab[MORSE_CODE_RECEIVER_ID].char_uuid.len = ESP_UUID_LEN_16;
        profile_tab[MORSE_CODE_RECEIVER_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_MORSE_CODE_RECEIVER_LETTER; //< Setting the UUID of characteristic
        err = esp_ble_gatts_add_char( //< Adding characteristic for accepting letters from central device
            profile_tab[MORSE_CODE_RECEIVER_ID].service_handle,
            &profile_tab[MORSE_CODE_RECEIVER_ID].char_uuid,
            morse_code_letter_permissions,
            morse_code_letter_properties,
            &morse_code_letter_char_val, //< Buffer to store letter
            NULL
        );
        if(err != ESP_OK) {
            ESP_LOGE(MODULE_TAG, "%s: esp_ble_gatts_add_char failed (%s)", __func__, esp_err_to_name(err));
        }
        else {
            ESP_LOGI(MODULE_TAG, "%s letter characteristic is adding!", __func__);
        }

        profile_tab[MORSE_CODE_RECEIVER_ID].char_uuid.len = ESP_UUID_LEN_16;
        profile_tab[MORSE_CODE_RECEIVER_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_MORSE_CODE_RECEIVER_VOL; //< Setting the UUID of characteristic
        err = esp_ble_gatts_add_char( //< Adding characteristic for reading and setting volume of buzzer
            profile_tab[MORSE_CODE_RECEIVER_ID].service_handle,
            &profile_tab[MORSE_CODE_RECEIVER_ID].char_uuid,
            morse_code_vol_permissions,
            morse_code_vol_properties,
            &morse_code_volume_char_val, //< Buffer to store volume value
            NULL
        );
        if(err != ESP_OK) {
            ESP_LOGE(MODULE_TAG, "%s: esp_ble_gatts_add_char failed (%s)", __func__, esp_err_to_name(err));
        }
        else {
            ESP_LOGI(MODULE_TAG, "%s volume characteristic is adding!", __func__);
        }

        break;

    case ESP_GATTS_START_EVT: //< Service started
        ESP_LOGI(MODULE_TAG, "START_EVT, status=%d, handle=%d", params->start.status, params->start.service_handle);
        break;
    
    case ESP_GATTS_ADD_CHAR_EVT: //< Characteristic were added
        ESP_LOGI(MODULE_TAG, "ADD_CHAR_EVT, status=%d, attr_handle=%d, service_handle=%d",
            params->add_char.status, 
            params->add_char.attr_handle, 
            params->add_char.service_handle
        ); 

        //Read the initial value
        uint16_t length = 0;
        const uint8_t *char_byte;

        err = esp_ble_gatts_get_attr_value(params->add_char.attr_handle, &length, &char_byte); //< Read the attribute value of characteristic
        if(err != ESP_OK) {
            ESP_LOGE(MODULE_TAG, "%s: esp_ble_gatts_get_attr_value failed (%d)", __func__, err);
        }

        ESP_LOGI(MODULE_TAG, "The char length=%x", length);
        for(int i = 0; i < length; i++) {
            ESP_LOGI(MODULE_TAG, "char[%d]=%x", i, char_byte[i]);
        }

        profile_tab[MORSE_CODE_RECEIVER_ID].descr_uuid.len = ESP_UUID_LEN_16;

        //Choose the right descriptor uuid
        if(params->add_char.char_uuid.uuid.uuid16 == GATTS_CHAR_UUID_MORSE_CODE_RECEIVER_LETTER) {
            profile_tab[MORSE_CODE_RECEIVER_ID].descr_uuid.uuid.uuid16 = GATTS_DESCR_UIID_MORSE_CODE_RECEIVER_LETTER;
            profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[LETTER_CHAR] = params->add_char.attr_handle;
        }
        else {
            profile_tab[MORSE_CODE_RECEIVER_ID].descr_uuid.uuid.uuid16 = GATTS_DESCR_UIID_MORSE_CODE_RECEIVER_VOL;
            profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[VOLUME_CHAR] = params->add_char.attr_handle;
        }

        if(add_char_cb != NULL) { //Calling custom add_char callback (e. g. for initialization of the char val)
            add_char_cb(params->add_char.attr_handle);
        }

        err = esp_ble_gatts_add_char_descr( //< Adding the characteristic descriptor event
            profile_tab[MORSE_CODE_RECEIVER_ID].service_handle,
            &profile_tab[MORSE_CODE_RECEIVER_ID].descr_uuid,
            morse_code_letter_permissions,
            NULL,
            NULL
        );
        if(err != ESP_OK) {
            ESP_LOGE(MODULE_TAG, "%s: esp_ble_gatts_add_char_descr %s", __func__, esp_err_to_name(err));
        }

        break;

    case ESP_GATTS_ADD_CHAR_DESCR_EVT: //< Characteristic decriptor was added
        ESP_LOGI(MODULE_TAG, "ADD_DESCR_EVT, status=%d, attr_handle=%d, service_handle=%d",
            params->add_char_descr.status, 
            params->add_char_descr.attr_handle,  
            params->add_char_descr.service_handle
        );
        break;

    case ESP_GATTS_CONNECT_EVT: //< Client connected
        ESP_LOGI(MODULE_TAG, "CONNECT_EVT, conn_id=%d, remote=" ESP_BD_ADDR_STR,
            params->connect.conn_id,
            ESP_BD_ADDR_HEX(params->connect.remote_bda)
        );
        profile_tab[MORSE_CODE_RECEIVER_ID].conn_id = params->connect.conn_id; //< Save client conn id to profile tab
        break;

    case ESP_GATTS_READ_EVT: //< Clien wants to read char
        ESP_LOGI(MODULE_TAG, "READ_EVT, remote=" ESP_BD_ADDR_STR " read_handle=%d", 
            ESP_BD_ADDR_HEX(params->read.bda),
            params->read.handle
        );

        esp_gatt_rsp_t response; 
        memset(&response, 0, sizeof(esp_gatt_rsp_t)); //< Initialize the structure for the response
        
        response.attr_value.handle = params->read.handle;

         //Read the initial value
        length = 0;

        err = esp_ble_gatts_get_attr_value(params->read.handle, &length, &char_byte); //< Read the attribute value of characteristic
        if(err != ESP_OK) {
            ESP_LOGE(MODULE_TAG, "%s: esp_ble_gatts_get_attr_value failed (%d)", __func__, err);
        }

        ESP_LOGI(MODULE_TAG, "The char length=%x", length);
        for(int i = 0; i < length; i++) {
            ESP_LOGI(MODULE_TAG, "char[%d]=%x", i, char_byte[i]);
        }

        response.attr_value.len = length;
        response.attr_value.value[0] = char_byte[0];

        esp_ble_gatts_send_response( //< Send the  response
            gatts_if, 
            params->read.conn_id,
            params->read.trans_id,
            ESP_GATT_OK,
            &response
        );

        break;

    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(MODULE_TAG, "WRITE_EVT, status=%d", params->rsp.status);
        ESP_LOGI(MODULE_TAG, "WRITE_EVT, handle=%d, conn_id=%d, trans_id=%d", params->write.handle, params->write.conn_id, params->write.trans_id);
        esp_log_buffer_hex(MODULE_TAG, params->write.value, params->write.len);

        if(!params->write.is_prep) {
            if(profile_tab[MORSE_CODE_RECEIVER_ID].descr_handle == params->write.handle && params->write.len == 2) {
                uint16_t descr_val = params->write.value[1] << 8 | params->write.value[0];
                if(descr_val == 0x0001) {
                    ESP_LOGI(MODULE_TAG, "Sending notification");
                    if(morse_code_vol_properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) {
                        //Only needed if it support notify
                    } 
                }
                else if(descr_val == 0x0002) {
                    ESP_LOGI(MODULE_TAG, "Sending indication");
                    if(morse_code_vol_properties & ESP_GATT_CHAR_PROP_BIT_INDICATE) {
                        //Only needed if it support indication
                    } 
                }
                else if(descr_val == 0x0000) {
                    ESP_LOGI(MODULE_TAG, "Sending notification and indication is disabled");
                }
                else {
                    ESP_LOGI(MODULE_TAG, "Unexpected value!");
                }
            }
        }
        if(params->write.need_rsp) { //Response is needed
            if(params->write.is_prep) {
                ESP_LOGI(MODULE_TAG, "Long write");
                //Only needed if long write is supported
            }
            else {
                ESP_LOGI(MODULE_TAG, "Short write");
                esp_ble_gatts_send_response(gatts_if, params->write.conn_id, params->write.trans_id, ESP_GATT_OK, NULL);
            }   
        }
        else {
            if(write_event_handler)
                write_event_handler(params);
        }

        break;

    case ESP_GATTS_SET_ATTR_VAL_EVT:
        ESP_LOGI(MODULE_TAG, "SET_ATTR_VAL_EVT, handle=%d", params->set_attr_val.attr_handle);
        break;

    case ESP_GATTS_EXEC_WRITE_EVT:
        ESP_LOGI(MODULE_TAG, "EXEC_WRITE_EVT, flag=%d", params->exec_write.exec_write_flag);
        break;

    case ESP_GATTS_RESPONSE_EVT:
        ESP_LOGI(MODULE_TAG, "RESPONSE_EVT, status=%d", params->rsp.status);
        break;

    case ESP_GATTS_DISCONNECT_EVT: //< Remote disconnects -> start advertising again
        ESP_LOGI(MODULE_TAG, "DISCONNECT_EVT, remote=" ESP_BD_ADDR_STR, 
            ESP_BD_ADDR_HEX(params->disconnect.remote_bda)
        );

        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GATTS_MTU_EVT: //< MTU was set
        ESP_LOGI(MODULE_TAG, "MTU_EVT, mtu=%d", params->mtu.mtu);
        break;

    default:
        ESP_LOGI(MODULE_TAG, "UNKNOWN GATTS EVT, id=%d", evt);

        break;
    }
}


/**
 * @brief Handling function for events that occurs on the GAP layer
 * 
 * @param evt 
 * @param params 
 */
void gap_event_handler(esp_gap_ble_cb_event_t evt, esp_ble_gap_cb_param_t *params) {
    switch (evt)
    {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~ADV_CONFIG_FLAG); //< Advertising data setting is complete, so set the corresponding flag to 0
        if(adv_config_done == 0) { //< But check other flags in adv_config_done, if there is something that is not done yet
            esp_ble_gap_start_advertising(&adv_params); //< Start advertising with predefined parameters
        }
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~SCAN_RESPONSE_CONFIG_FLAG); //< Advertising data setting is complete, so set the corresponding flag to 0
        if(adv_config_done == 0) { //< But check other flags in adv_config_done
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT: //< The start of advertising was completed
        if(params->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(MODULE_TAG, "%s: The starting of advertising failed", __func__);
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT: //< Advertising was requested to stop and it is done
        if(params->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(MODULE_TAG, "%s: The stopping of advertising failed", __func__);
        }
        else {
            ESP_LOGI(MODULE_TAG, "%s: The advertising was succesfully stopped", __func__);
        }
        break;

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(MODULE_TAG, "Update of the connection parameters");
        ESP_LOGI(MODULE_TAG, "status=%d, addr=" ESP_BD_ADDR_STR " min_int=%d, max_int=%d, conn_int=%d, latency=%d, timeout=%d",
            params->update_conn_params.status, //< Status of changing params
            ESP_BD_ADDR_HEX(params->update_conn_params.bda), //< Bluetooth device adress (addres of the central device)
            params->update_conn_params.min_int, //< Min wanted interval for connection if peripheral (this device) request update
            params->update_conn_params.max_int,
            params->update_conn_params.conn_int, //< How often will central device ask for the data
            params->update_conn_params.latency, //< Latency of reponse to central device request
            params->update_conn_params.timeout //< After this time, the connection will be considered as lost
        );

        break;

    default:
        ESP_LOGI(MODULE_TAG, "%s: Not handled GAP event came (code: %d)", __func__, evt);
        break;
    }
}


void gatts_event_handler(esp_gatts_cb_event_t evt, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *params) {
    if(evt == ESP_GATTS_REG_EVT) { //< Application profile is registrered
        if(params->reg.status == ESP_GATT_OK) {
            profile_tab[params->reg.app_id].gatts_if = gatts_if; //< Assigning the interface to the application (different app -> different interface)
            ESP_LOGI(MODULE_TAG, "Interface were assigned to the app id=%d", params->reg.app_id);
        }
        else {
            ESP_LOGI(MODULE_TAG, "Registering app failed (%d)", params->reg.app_id);
            return;
        }
    }

    for(int idx = 0; idx < PROFILE_NUM; idx++) { //< Search for the corresponding callback function (due to given gatts interface)
        
        if(gatts_if == ESP_GATT_IF_NONE || gatts_if == profile_tab[idx].gatts_if) { //< The interface was not assigned to the current profile OR it is the interface of current profile
            if(profile_tab[idx].gatts_cb) { //< Check if the application profile has defined the callback
                profile_tab[idx].gatts_cb(evt, gatts_if, params);
            }
        }
    }
}

void register_write_event_handler(void (*write_event_handler_func)(esp_ble_gatts_cb_param_t *)) {
    write_event_handler = write_event_handler_func;
}


void register_add_char_cb(void (*add_char_cb_func)(uint16_t)) {
    add_char_cb = add_char_cb_func;
}


esp_err_t bluetooth_init(void (*write_event_handler_func)(esp_ble_gatts_cb_param_t *), void (*add_char_cb_func)(uint16_t)) {
    esp_err_t err;

    esp_bt_controller_config_t bt_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT(); //< Use default configuration
    err = esp_bt_controller_init(&bt_config); //< Initialize and allocate task and other resources
    if(err != ESP_OK) {
        ESP_LOGE(MODULE_TAG, "%s: esp_bt_controller_init failed (%s)", __func__, esp_err_to_name(err));
        return err;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE); //< Enable the BT controller with BLE mode
    if(err != ESP_OK) {
        ESP_LOGE(MODULE_TAG, "%s: esp_bt_controller_enable failed (%s)", __func__, esp_err_to_name(err));
        return err;
    }

    err = esp_bluedroid_init(); // Intializing of bluedroid (bluetooth host)
    if(err != ESP_OK) {
        ESP_LOGE(MODULE_TAG, "%s: esp_bluedroid_init failed (%s)", __func__, esp_err_to_name(err));
        return err;
    }

    err = esp_bluedroid_enable();
    if(err != ESP_OK) {
        ESP_LOGE(MODULE_TAG, "%s: esp_bluedroid_enable failed (%s)", __func__, esp_err_to_name(err));
        return err;
    }

    //Bluetooth stack should be up now
    register_write_event_handler(write_event_handler_func);
    register_add_char_cb(add_char_cb_func);

    err = esp_ble_gatts_register_callback(gatts_event_handler); //< Registering handling function for events, that come from GATT server
    if(err != ESP_OK) {
        ESP_LOGE(MODULE_TAG, "%s: esp_ble_gatts_register_callback failed (%s)", __func__, esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gap_register_callback(gap_event_handler);
    if(err != ESP_OK) {
        ESP_LOGE(MODULE_TAG, "%s: esp_ble_gap_register_callback failed (%s)", __func__, esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gatts_app_register(MORSE_CODE_RECEIVER_ID); //Registering application
    if(err != ESP_OK) {
        ESP_LOGE(MODULE_TAG, "%s: esp_ble_gatts_app_register failed (%s)", __func__, esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

//End of the part based on https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/bluedroid/ble/gatt_server/tutorial/Gatt_Server_Example_Walkthrough.md
