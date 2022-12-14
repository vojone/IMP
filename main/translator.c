/**
 * @file translator.c
 * @author Vojtěch Dvořák (xdvora3o)
 * @date 2022-12-12
 */

#include "translator.h"

QueueHandle_t out_queue = NULL; //< Queue of out controls
QueueHandle_t queue = NULL; //< Queue of letters


/**
 * @brief Handle for semaphore that should be checked before reading from the out control queue 
 * (to letter consistency)
 * 
 */
SemaphoreHandle_t out_queue_sem = NULL;



/**
 * @brief Performs translation of character to the sequence of . and - (or /)
 * 
 * @param tb_tr char to be translated
 * @return const char* translated sequence or NULL if letter was not found
 */
const char *char_lookup(char tb_tr) {
    static translation_t tr_tab[] = {
        { .ch = 0, .mc = NULL}, 
        { .ch = ' ', .mc = "/"},       { .ch = '.', .mc = "//"},        { .ch = '1', .mc = ".----"}, 
        { .ch = '2', .mc = "..---"},    { .ch = '3', .mc = "...--"},    { .ch = '4', .mc = "....-"}, 
        { .ch = '5', .mc = "....."},    { .ch = '6', .mc = "-...."},    { .ch = '7', .mc = "--..."}, 
        { .ch = '8', .mc = "---.."},    { .ch = '9', .mc = "----."},    { .ch = '0', .mc = "-----"},  
        { .ch = 'a', .mc = ".-"},       { .ch = 'b', .mc = "-..."},     { .ch = 'c', .mc = "-.-."}, 
        { .ch = 'd', .mc = "-.."},      { .ch = 'e', .mc = "."},        { .ch = 'f', .mc = "..-."},   
        { .ch = 'g', .mc = "--."},      { .ch = 'h', .mc = "...."},     { .ch = 'i', .mc = ".."},      
        { .ch = 'j', .mc = ".---"},     { .ch = 'k', .mc = "-.-"},      { .ch = 'l', .mc = ".-.."},     
        { .ch = 'm', .mc = "--"},       { .ch = 'n', .mc = "-."},       { .ch = 'o', .mc = "---"},      
        { .ch = 'p', .mc = ".--."},     { .ch = 'q', .mc = "--.-"},     { .ch = 'r', .mc = ".-."},  
        { .ch = 's', .mc = "..."},      { .ch = 't', .mc = "-"},        { .ch = 'u', .mc = "..-"},  
        { .ch = 'v', .mc = "...-"},     { .ch = 'w', .mc = ".--"},      { .ch = 'x', .mc = "-..-"}, 
        { .ch = 'y', .mc = "-.--"},     { .ch = 'z', .mc = "--.."},     { .ch = 0, .mc = NULL}, 
    };

    static const size_t approx_middle_i = 19; //Aproximately middle of the tab (just optimization, TABLE MUST BE CORRECTLY ORDERED BY ASCII codes!)
    static translation_t * approx_middle = &(tr_tab[approx_middle_i]); //Ptr to the approx middle of the tab

    int i = approx_middle_i;

    //Perform seraching from the middle in the right direction until the stop is found (THERE MUST BE STOPS IN THE TABLE)
    for(; tr_tab[i].ch && tr_tab[i].mc && tr_tab[i].ch != tb_tr; tb_tr < approx_middle->ch ? i-- : i++);

    if(tr_tab[i].ch && tr_tab[i].mc) { //Letter was found in the table
        return tr_tab[i].mc;
    }
    else {
        return NULL;
    }
}


/**
 * @brief Initilizes structures for translator
 * 
 * @return esp_err_t ESP_OK if everthing went OK
 */
esp_err_t translator_init() {
    queue = xQueueCreate(MAXIMUM_MESSAGE_NUM, MAXIMUM_MESSAGE_LEN + 1);
    if(!queue) {
        ESP_LOGE(TRANSLATOR_TAG, "Unable to create queue for letters!");

        return ESP_ERR_NO_MEM;
    }

    out_queue = xQueueCreate(MAXIMUM_OUT_CONTROL_NUM, sizeof(out_control_t));
    if(!out_queue) {
        ESP_LOGE(TRANSLATOR_TAG, "Unable to create queue for out control!");

        return ESP_ERR_NO_MEM;
    }

    out_queue_sem = xSemaphoreCreateBinary();
    if(!out_queue_sem) {
        ESP_LOGE(TRANSLATOR_TAG, "Unable to create semaphore for out queue!");

        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(out_queue_sem);

    return ESP_OK;
}


/**
 * @brief Converts uppercase letters to lowe case equivalent
 * 
 * @return char lower case letter if input was letter, otherwise returns input char
 */
char do_char_correction(char ch) {
    char ret = ch;

    if(ch > 'A' && ch < 'Z') {
        ret = ch + ('a' - 'A');
    }

    return ret;
}



/**
 * @brief Translates letters fro queue to the control structures (that can be easily intepreted)
 * 
 * @param arg No args are necessary
 */
void translate(void *arg) {
    char buffer[MAXIMUM_MESSAGE_LEN + 1];

    while(1) {
        //Try get letter (buffer) from the queue
        if(xQueueReceive(queue, buffer, (TickType_t)5)) { //5 ticks block if letter is not currently available
            ESP_LOGI(MODULE_TAG, "Read %s from letter queue, translating to morse code\n", buffer);

            //Translate every letter in the buffer
            for(int i = 0; i < MAXIMUM_MESSAGE_LEN; i++) {
                char cur_char = buffer[i];

                const char *morse_code = char_lookup(do_char_correction(cur_char)); //Find translation for the current letter
                if(!morse_code) {
                    ESP_LOGE(TRANSLATOR_TAG, "Unable to find character in lookup table!");
                    continue;
                }
                else {
                    //Take semaphore (avoid leaking some .,- or / before translating the whole letter)
                    if(xSemaphoreTake(out_queue_sem, portMAX_DELAY) == pdTRUE) {

                        for(int j = 0; morse_code[j]; j++) {
                            out_control_t out_c = { .buzz_state = 0, .led_state = 0, .gap = 0};
                            switch(morse_code[j]) {
                                case '.':
                                    out_c.buzz_state = DOT_BUZZER_INT; //Beep interval for .
                                    break;
                                case '-' :
                                    out_c.buzz_state = DASH_BUZZER_INT; //Beep interval for -
                                    break;
                                default:
                                    out_c.led_state = SLASH_LED_INT; //Led interval for other chars (/)
                                    break;
                            }

                            if(morse_code[j+1] == '\0') { //Add gap if symbol is the last from the letter
                                out_c.gap = GAP_BETWEEN_LETTERS;
                            }

                            //Send translated symbol to the out control queue
                            if(xQueueSend(out_queue, &out_c, (TickType_t)5) != pdPASS) {
                                ESP_LOGE(TRANSLATOR_TAG, "Writing letter to the queue failed!");
                            }
                        }

                        //The whole letter is tranlated so release the semaphore
                        xSemaphoreGive(out_queue_sem);

                        ESP_LOGI(TRANSLATOR_TAG, "Translated to %s and written it to out control queue", morse_code);
                    }
                    else {
                        ESP_LOGI(TRANSLATOR_TAG, "Unable to obtain out_queue_sem! Skipping %c...", cur_char);
                    }
                }
            }
        }
    }
}

