// LILYGO T-Embed CC1101 — exact copy of official Setup214_LilyGo_T_Embed_PN532.h
// plus USE_HSPI_PORT for ESP32-S3 (default FSPI can cause black screen / faults).
// From: https://github.com/Xinyuan-LilyGO/T-Embed-CC1101/blob/master/lib/TFT_eSPI/User_Setups/Setup214_LilyGo_T_Embed_PN532.h

#define USER_SETUP_LOADED 1
#define USER_SETUP_ID 214

#define USE_HSPI_PORT  // ESP32-S3: use HSPI; default port can cause black screen

#define ST7789_DRIVER

#define TFT_WIDTH 170
#define TFT_HEIGHT 320

#define TFT_INVERSION_ON

#define TFT_BL 21
#define TFT_BACKLIGHT_ON HIGH
#define TFT_MISO 10
#define TFT_MOSI 9
#define TFT_SCLK 11
#define TFT_CS 41
#define TFT_DC 16
#define TFT_RST -1

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY 80000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000
