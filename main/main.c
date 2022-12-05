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

#include "ble_receiver.h"


QueueHandle_t queue;


#define MODULE_TAG "MORSE_CODE"

#define MAXIMUM_MESSAGE_LEN 1
#define MAXIMUM_MESSAGE_NUM 1024

void update_volume(uint8_t new_volume) {
    float perc = (float)new_volume/255.0;

    unsigned new_duty = (unsigned)((1 << LEDC_TIMER_13_BIT)*perc);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, new_duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void write_event_handler(esp_ble_gatts_cb_param_t *params) {
    char buffer[MAXIMUM_MESSAGE_LEN + 1];
    esp_err_t err;

    if(params->write.handle == profile_tab[MORSE_CODE_RECEIVER_ID].char_handle_tab[VOLUME_CHAR]) {
        ESP_LOGI(MODULE_TAG, "Writing to volume characteristic");

        err = esp_ble_gatts_set_attr_value(params->write.handle, 1, &(params->write.value[0]));
        ESP_ERROR_CHECK(err);

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

/**
 * @brief 
 * 
 * @param arg 
 */
void morse_beep(void *arg) {
    esp_err_t err;
    char buffer[MAXIMUM_MESSAGE_LEN + 1];

    while(1) {
        if(xQueueReceive(queue, buffer, (TickType_t)5)) { //5 ticks block if letter is not currently available
            printf("Read %s Duty %d\n", buffer, ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    
            err = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            ESP_ERROR_CHECK(err);

            err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            ESP_ERROR_CHECK(err);

            vTaskDelay(1000/portTICK_RATE_MS);

            err = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            ESP_ERROR_CHECK(err);
        }

        vTaskDelay(1000/portTICK_RATE_MS);
    }
}


TaskHandle_t morse_beep_handle = NULL;

void app_main(void) {
    esp_err_t err;

    queue = xQueueCreate(MAXIMUM_MESSAGE_NUM, MAXIMUM_MESSAGE_LEN + 1);
    if(!queue) {
        ESP_LOGE(MODULE_TAG, "Unable to create queue!");
    }

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 5000,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    err = ledc_timer_config(&ledc_timer);
    ESP_ERROR_CHECK(err);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = GPIO_NUM_5,
        .duty = 4095,
        .hpoint = 0,
    };

    err = ledc_channel_config(&ledc_channel);
    ESP_ERROR_CHECK(err);

    err = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ESP_ERROR_CHECK(err);

    err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) { //< Potentially recoverable errors
        ESP_ERROR_CHECK(nvs_flash_erase()); //< Try to erase NVS and then init it again
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    bluetooth_init(write_event_handler);

    xTaskCreatePinnedToCore(morse_beep, "morse_beep", 4096, NULL, 10, &morse_beep_handle, 1);
}