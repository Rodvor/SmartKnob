#pragma once

// Known-good TFT_eSPI 2.5.43 setup for the SmartKnob GC9A01 display.
// Copy this file over TFT_eSPI/User_Setup.h after installing or restoring
// TFT_eSPI. The touch controller is handled directly by ESP32.ino over I2C.

#define USER_SETUP_INFO "SmartKnob GC9A01"

#define GC9A01_DRIVER

#define TFT_CS    5
#define TFT_DC    4
#define TFT_RST   0
#define TFT_MOSI  16
#define TFT_SCLK  15
#define TFT_BL    2

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY 2500000
