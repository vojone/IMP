# IMP project - Morse code bluetooth receiver
Simple morse code receiver (ESP32) and transmitter (WebBluetooth). ESP32 beeps received letters and blinks between words and sentences.
Project is based on ESP-IDF (https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) and WebBluetooth.
For more information see project documentation.

## Author

Vojtěch Dvořák (xdvora3o)


## Usage

First you need to flash the code to you board (see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) and connect peripherals to it (buzzer and led).
See `main.c` macros for pins on which should be these peripherals connected.
Then you can run `transmitter/index.html` in browser, that supports WebBluetooth and sends letters or message to the board (after Connection).