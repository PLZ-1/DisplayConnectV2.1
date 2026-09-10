#pragma once
#define USER_SETUP_INFO "DisplayConnect LOLIN32 Lite ST7796S"
#define ST7796_DRIVER
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS 27
#define TFT_DC 26
#define TFT_RST 25
// Backlight wiring depends on the exact display module; no GPIO assumed.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
#define SPI_FREQUENCY 27000000
#define SPI_READ_FREQUENCY 16000000
