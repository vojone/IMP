/**
 * @file main.c
 * @author Vojtěch Dvořák (xdvora3o)
 * @date 2022-11-04
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/timer.h"

#include "ble_receiver.h"

#define LEDC_TIMER_RESOLUTION LEDC_TIMER_13_BIT
#define LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_CHANNEL LEDC_CHANNEL_0
#define BUZZER_LEDC_TIMER LEDC_TIMER_0
#define LEDC_TIMER_FREQ 5000

#define BUZZER_GPIO GPIO_NUM_5


QueueHandle_t queue, out_queue;


#define APP_NAME "MORSE_CODE"

#define MAXIMUM_MESSAGE_LEN 1
#define MAXIMUM_MESSAGE_NUM 1024

#define MAXIMUM_OUT_CONTROL_NUM 4096

#define TIMER_DIVIDER (16)
#define TIMER_SCALE (TIMER_BASE_CLK / TIMER_DIVIDER)


void update_volume(uint8_t new_volume) {
    uint16_t vol_handle = profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[VOLUME_CHAR];
    esp_err_t err = esp_ble_gatts_set_attr_value(vol_handle, 1, &new_volume);
    ESP_ERROR_CHECK(err);

    float perc = (float)new_volume/255.0;

    unsigned new_duty = (unsigned)((1 << LEDC_TIMER_RESOLUTION)*perc);

    ledc_set_duty(LEDC_SPEED_MODE, BUZZER_CHANNEL, new_duty);
    ledc_update_duty(LEDC_SPEED_MODE, BUZZER_CHANNEL);
}

void write_event_handler(esp_ble_gatts_cb_param_t *params) {
    char buffer[MAXIMUM_MESSAGE_LEN + 1];

    if(params->write.handle == profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[VOLUME_CHAR]) {
        ESP_LOGI(MODULE_TAG, "Writing to volume characteristic");

        update_volume(params->write.value[0]);
    }
    else if(params->write.handle == profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[LETTER_CHAR]) {
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
    else {
        ESP_LOGE(MODULE_TAG, "Unrecognized handle!, handle=%d", params->write.handle);
    }
}

typedef struct out_control {
    uint8_t buzz_state;
    uint8_t led_state;
} out_control_t;


static bool IRAM_ATTR out_control_routine(void *args) {
    esp_err_t err;
    BaseType_t higher_priority_task_woken = pdFALSE;
    out_control_t out_control;
    bool will_be_returned = false;

    ets_printf("Called out control_routine\n");

    if(xQueueReceiveFromISR(out_queue, &out_control, &higher_priority_task_woken)) { //5 ticks block if letter is not currently available
        if(out_control.buzz_state > 0) {
            out_control.buzz_state--;

            will_be_returned = true;

            err = ledc_update_duty(LEDC_SPEED_MODE, BUZZER_CHANNEL);
            ESP_ERROR_CHECK(err);
        }
        else {
            err = ledc_stop(LEDC_SPEED_MODE, BUZZER_CHANNEL, 0);
            ESP_ERROR_CHECK(err);
        }

        if(out_control.led_state > 0) {
            out_control.led_state--;

            will_be_returned = true;
        }
        else {

        }
    }

    return higher_priority_task_woken == pdTRUE;
}


/**
 * @brief 
 * 
 * @param arg 
 */
void transate(void *arg) {
    esp_err_t err;
    char buffer[MAXIMUM_MESSAGE_LEN + 1];

    while(1) {
        if(xQueueReceive(queue, buffer, (TickType_t)5)) { //5 ticks block if letter is not currently available
            printf("Read %s Duty %d\n", buffer, ledc_get_duty(LEDC_SPEED_MODE, BUZZER_CHANNEL));
        }

        vTaskDelay(1000/portTICK_RATE_MS);
    }
}


/**
 * @brief 
 * 
 * @return esp_err_t 
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

    err = ledc_stop(LEDC_SPEED_MODE, BUZZER_CHANNEL, 0);
    if(err) {
        ESP_LOGE(APP_NAME, "ledc_stop failed!");
        return err;
    }

    return ESP_OK;
}


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

    err = timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 3 * TIMER_SCALE); //Time in secs * scale
    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "timer_set_alarm_value failed!");
        return err;
    }

    err = timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    if(err != ESP_OK) {
        ESP_LOGE(APP_NAME, "timer_enable_intr failed!");
        return err;
    }

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


TaskHandle_t translator_handle = NULL;

void app_main(void) {
    esp_err_t err;

    queue = xQueueCreate(MAXIMUM_MESSAGE_NUM, MAXIMUM_MESSAGE_LEN + 1);
    if(!queue) {
        ESP_LOGE(MODULE_TAG, "Unable to create queue for letters!");
    }

    out_queue = xQueueCreate(MAXIMUM_OUT_CONTROL_NUM, sizeof(out_control_t));
    if(!out_queue) {
        ESP_LOGE(MODULE_TAG, "Unable to create queue for out control!");
    }

    err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) { //< Potentially recoverable errors
        ESP_ERROR_CHECK(nvs_flash_erase()); //< Try to erase NVS and then init it again
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = ledc_init();
    ESP_ERROR_CHECK(err);

    err = bluetooth_init(write_event_handler);
    ESP_ERROR_CHECK(err);

    err = out_control_timer_init();
    ESP_ERROR_CHECK(err);

    xTaskCreatePinnedToCore(transate, "translator", 4096, NULL, 10, &translator_handle, 1);
}