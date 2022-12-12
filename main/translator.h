/**
 * @file translator.h
 * @author Vojtěch Dvořák (xdvora3o)
 * @date 2022-12-12
 */


#ifndef __TRANSLATOR__
#define __TRANSLATOR__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define MODULE_TAG "TRANSLATOR" //< Module name

#define MAXIMUM_MESSAGE_LEN 1 //< Maximum size of one buffer stored in message queue
#define MAXIMUM_MESSAGE_NUM 1024 //< MAximum size of letter queue

#define MAXIMUM_OUT_CONTROL_NUM 4096 //< Maximum length of the out control queue


/**
 * @brief Control structure for controlling buzzer and led (e. g. if buzz_state is > 0, 
 * it means that is should be turned on)
 * 
 */
typedef struct out_control {
    uint8_t buzz_state;
    uint8_t led_state;
} out_control_t;


/**
 * @brief Translation unit stored in the lookup table
 * 
 */
typedef struct translation {
    char ch; //< Character
    char *mc; //< Morse code representation
} translation_t;


extern QueueHandle_t out_queue; //< Queue of out controls
extern QueueHandle_t queue; //< Queue of letters


/**
 * @brief Handle for semaphore that should be checked before reading from the out control queue 
 * (to letter consistency)
 * 
 */
extern SemaphoreHandle_t out_queue_sem;


/**
 * @brief Initilizes structures for translator
 * 
 * @return esp_err_t ESP_OK if everthing went OK
 */
void translate(void *arg);


/**
 * @brief Initilizes structures for translator
 * 
 * @return esp_err_t ESP_OK if everthing went OK
 */
esp_err_t translator_init();

#endif
