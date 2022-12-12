/**
 * @file main.c
 * @author Vojtěch Dvořák (xdvora3o)
 * @date 2022-12-12
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/timer.h"
#include "nvs.h"

#include "ble_receiver.h"
#include "translator.h"


#define APP_NAME "MORSE_CODE" //App name (for logs)


#define LEDC_TIMER_RESOLUTION LEDC_TIMER_13_BIT //< Timer resolution for buzzer PWM
#define LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE //< Speed mode for buzzer PWM
#define BUZZER_CHANNEL LEDC_CHANNEL_0 //< Channel for buzzer PWM
#define BUZZER_LEDC_TIMER LEDC_TIMER_0 //< Timer for buzzer PWM
#define LEDC_TIMER_FREQ 5000 //< Timer frequency for buzzer PWM

#define BUZZER_GPIO GPIO_NUM_5 
#define LED_GPIO GPIO_NUM_14


nvs_handle_t settings_nvs; //< Handle for storing settings (like volume)

//Handle keys
#define VOLUME_NVS_KEY "volume" 
#define SETTINGS_NVS_KEY "m_c_settings"

//Timer settings
#define TIMER_DIVIDER (16)
#define TIMER_SCALE (TIMER_BASE_CLK / TIMER_DIVIDER)


//Base time interval (dettermines the length of one out_control interval)
#define BASE_TIME_INT_MS 250



/**
 * @brief Updates volume level of the morse receiver
 * 
 * @param new_volume The new volume level
 */
void update_volume(uint8_t new_volume) {

    //Remeber value (in NVS)
    esp_err_t err = nvs_set_u8(settings_nvs, VOLUME_NVS_KEY, new_volume);
    ESP_ERROR_CHECK(err);

    err = nvs_commit(settings_nvs);
    ESP_ERROR_CHECK(err);

    uint8_t val;
    err = nvs_get_u8(settings_nvs, VOLUME_NVS_KEY, &val);
    ESP_LOGI(APP_NAME, "val %d", val);
    ESP_ERROR_CHECK(err);

    //Update characteristic value
    uint16_t vol_handle = profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[VOLUME_CHAR];
    err = esp_ble_gatts_set_attr_value(vol_handle, 1, &new_volume);
    ESP_ERROR_CHECK(err);

    float perc = (float)new_volume/255.0;

    unsigned new_duty = (unsigned)((1 << LEDC_TIMER_RESOLUTION)*perc);

    //Update duty of PWM
    ledc_set_duty(LEDC_SPEED_MODE, BUZZER_CHANNEL, new_duty);
}


/**
 * @brief Write event handler for bluetooth module
 * 
 * @param params 
 */
void write_event_handler(esp_ble_gatts_cb_param_t *params) {
    char buffer[MAXIMUM_MESSAGE_LEN + 1];

    if(params->write.handle == profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[VOLUME_CHAR]) { //Volume write
        ESP_LOGI(MODULE_TAG, "Writing to volume characteristic");

        update_volume(params->write.value[0]);
    }
    else if(params->write.handle == profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[LETTER_CHAR]) { //Letter (meesage) write
        ESP_LOGI(MODULE_TAG, "Writing to letter characteristic");

        size_t message_len = MAXIMUM_MESSAGE_LEN >= params->write.len ? MAXIMUM_MESSAGE_LEN : params->write.len;

        for(int i = 0; i < params->write.len && i < MAXIMUM_MESSAGE_NUM; i++) {
            memcpy(buffer, params->write.value, message_len);
            memset(&(buffer[message_len]), '\0', 1);
            // printf("%d\n", params->write.len);
            // printf("%c %c\n", buffer[0], buffer[1]);
            if(xQueueSend(queue, buffer, (TickType_t)0) != pdPASS) {
                ESP_LOGE(MODULE_TAG, "Writing letter to the queue failed!");
            }
        }
    }
    else if(params->write.handle == profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[ABORT_CHAR]) { //Abort char
        ESP_LOGI(MODULE_TAG, "Writing to abort characteristic");

        if(queue)
            xQueueReset(queue);
        if(out_queue)
            xQueueReset(out_queue);

        esp_err_t err = ledc_stop(LEDC_SPEED_MODE, BUZZER_CHANNEL, 0);
        ESP_ERROR_CHECK(err);

        err = gpio_set_level(LED_GPIO, 0);
        ESP_ERROR_CHECK(err);
    }
    else { //Unrecognized char
        ESP_LOGE(MODULE_TAG, "Unrecognized handle!, handle=%d", params->write.handle);
    }
}


/**
 * @brief ISR for interrupts from timer (they comes every BASE_TIME_INT_MS), translates out control sequence to beeping and blinking
 * 
 * @param args 
 * @return true 
 * @return false 
 */
static bool IRAM_ATTR out_control_routine(void *args) {
    esp_err_t err;
    BaseType_t higher_priority_task_woken = pdFALSE;
    out_control_t out_control;
    bool will_be_returned = false;

    if(xSemaphoreTakeFromISR(out_queue_sem, &higher_priority_task_woken) == pdTRUE) {
        if(xQueueReceiveFromISR(out_queue, &out_control, &higher_priority_task_woken)) {
            ets_printf("Picked BUZZ %d LED %d\n", out_control.buzz_state, out_control.led_state);

            if(out_control.buzz_state > 0) { //Beep if related out control is greater than zero
                out_control.buzz_state--;

                will_be_returned = true;

                err = ledc_update_duty(LEDC_SPEED_MODE, BUZZER_CHANNEL);
                ESP_ERROR_CHECK(err);
            }
            else {
                err = ledc_stop(LEDC_SPEED_MODE, BUZZER_CHANNEL, 0);
                ESP_ERROR_CHECK(err);
            }

            if(out_control.led_state > 0) { //Turn led on if related out control is greater than zero
                out_control.led_state--;

                will_be_returned = true;

                ets_printf("turning led on");
                err = gpio_set_level(LED_GPIO, 1);
                ESP_ERROR_CHECK(err);
            }
            else {
                err = gpio_set_level(LED_GPIO, 0);
                ESP_ERROR_CHECK(err);
            }

            if(will_be_returned == true) { //Return the outcontrol to out control queue if there is still something to do in it
                xQueueSendToFrontFromISR(out_queue, &out_control, &higher_priority_task_woken);
            }   
        }

        xSemaphoreGiveFromISR(out_queue_sem, &higher_priority_task_woken);
    }

    return higher_priority_task_woken == pdTRUE;
}




/**
 * @brief Initialization of PWM (ledc) for buzzer
 * 
 * @return esp_err_t ESP_OK if everyhing went ok
 */
esp_err_t ledc_init() {
    esp_err_t err;

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_RESOLUTION,
        .freq_hz = LEDC_TIMER_FREQ,
        .speed_mode = LEDC_SPEED_MODE,
        .timer_num = BUZZER_LEDC_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    err = ledc_timer_config(&ledc_timer);
    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "ledc_stop failed!");
        return err;
    }

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_SPEED_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BUZZER_GPIO,
        .duty = 0,
        .hpoint = 0,
    };

    err = ledc_channel_config(&ledc_channel);
    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "ledc_channel_config failed!");
        return err;
    }

    //Initial level of PWM
    err = ledc_stop(LEDC_SPEED_MODE, BUZZER_CHANNEL, 0);
    if(err) {
        ESP_LOGE(APP_NAME, "ledc_stop failed!");
        return err;
    }

    return ESP_OK;
}


/**
 * @brief Initilization of timer for beeping and blinking
 * 
 * @return esp_err_t ESP_OK if everyhing went ok
 */
esp_err_t out_control_timer_init() {
    esp_err_t err;

    timer_config_t config = {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = true,
    };

    err = timer_init(TIMER_GROUP_0, TIMER_0, &config);
    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "timer_init failed!");
        return err;
    }

    err = timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "timer_set_counter_value failed!");
        return err;
    }

    //Interupts should come every BASE_TIME_INT_MS
    err = timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, (int)((BASE_TIME_INT_MS * 1e-3) * TIMER_SCALE)); //Time in secs * scale
    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "timer_set_alarm_value failed!");
        return err;
    }

    err = timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "timer_enable_intr failed!");
        return err;
    }

    //Register ISR
    err = timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, out_control_routine, NULL,  0);
    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "timer_isr_callback_add failed!");
        return err;
    }

    err = timer_start(TIMER_GROUP_0, TIMER_0);
    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "timer_start failed!");
        return err;
    }

    return ESP_OK;
}


/**
 * @brief Restores the volume level after the reset
 * 
 * @return esp_err_t ESP_OK if everything went OK
 */
esp_err_t restore_volume() {
    uint8_t stored_volume, initial_volume = 128;
    esp_err_t err = nvs_get_u8(settings_nvs, VOLUME_NVS_KEY, &stored_volume);
    if(err == ESP_ERR_NVS_NOT_FOUND) { //Volume was not written yet
        ESP_LOGI(APP_NAME, "Volume initialization! (to %d)", initial_volume);
        err = nvs_set_u8(settings_nvs, VOLUME_NVS_KEY, initial_volume);

        stored_volume = initial_volume;

        err = nvs_commit(settings_nvs);
        ESP_ERROR_CHECK(err);
    }   

    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "Volume restoration failed! (0x%x)", err - ESP_ERR_NVS_BASE);
        return err;
    }

    update_volume(stored_volume);

    return ESP_OK;
}


/**
 * @brief Initialization of volume characteristic after reset
 * 
 * @param char_handle 
 */
void char_added_cb(uint16_t char_handle) {
    if(profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[VOLUME_CHAR] == char_handle) {
        ESP_ERROR_CHECK(restore_volume());
    }
}


TaskHandle_t translator_handle = NULL;

/**
 * @brief The main body of the app
 * 
 */
void app_main(void) {
    esp_err_t err;

    err = translator_init();
    ESP_ERROR_CHECK(err);

    err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) { //< Potentially recoverable errors
        ESP_LOGE(APP_NAME, "Erasing flash!");
        ESP_ERROR_CHECK(nvs_flash_erase()); //< Try to erase NVS and then init it again
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = nvs_open(SETTINGS_NVS_KEY, NVS_READWRITE, &settings_nvs);
    ESP_ERROR_CHECK(err);

    err = ledc_init();
    ESP_ERROR_CHECK(err);

    err = bluetooth_init(write_event_handler, char_added_cb);
    ESP_ERROR_CHECK(err);

    //Intialization of buzzer and led
    err = out_control_timer_init();
    ESP_ERROR_CHECK(err);

    gpio_pad_select_gpio(LED_GPIO);
    err = gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    ESP_ERROR_CHECK(err);

    err = gpio_set_level(LED_GPIO, 0);
    ESP_ERROR_CHECK(err);


    xTaskCreatePinnedToCore(translate, "translator", 4096, NULL, 10, &translator_handle, 1);
}