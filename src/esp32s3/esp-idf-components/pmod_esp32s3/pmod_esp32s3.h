#pragma once

#include "sdkconfig.h"
#include <stdint.h>
#include <stdbool.h>

#if defined(CONFIG_ESPTOOLPY_OCT_FLASH) || defined(CONFIG_SPIRAM_MODE_OCT)
    #define PMOD_OCTAL_SPI_IN_USE
#endif
 
#define PMOD_FPGA_SPI_CS0 41
#define PMOD_FPGA_SPI_CS1 39
#define PMOD_FPGA_SPI_SCLK 2

#define PMOD_FPGA_SPI_D0 5
#define PMOD_FPGA_SPI_D1 7
#define PMOD_FPGA_SPI_D2 16
#define PMOD_FPGA_SPI_D3 18

#define PMOD_BUTTON 0

#ifndef PMOD_OCTAL_SPI_IN_USE
    #define PMOD_LED_GREEN 37
    #define PMOD_LED_PINK 35
#else
    #define PMOD_LED_WS2812 38
#endif

bool pmod_esp32s3_init();

#ifdef PMOD_OCTAL_SPI_IN_USE
void pmod_esp32s3_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
#else
void pmod_esp32s3_led_set_green(bool enabled);
void pmod_esp32s3_led_set_pink(bool enabled);
#endif