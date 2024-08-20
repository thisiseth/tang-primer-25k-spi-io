#include "pmod_esp32s3.h"
#include "driver/gpio.h"

#ifdef PMOD_OCTAL_SPI_IN_USE

#include "led_strip.h"
#include "led_strip_rmt.h"

static led_strip_handle_t ws2812;

#endif

bool pmod_esp32s3_init()
{
#ifndef PMOD_OCTAL_SPI_IN_USE
    gpio_set_direction(PMOD_LED_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(PMOD_LED_PINK, GPIO_MODE_OUTPUT);
#else
    led_strip_config_t strip_config = 
    {
        .strip_gpio_num = PMOD_LED_WS2812,        // The GPIO that connected to the LED strip's data line
        .max_leds = 1,                            // The number of LEDs in the strip,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,            // LED strip model
        .flags.invert_out = false,                // whether to invert the output signal
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = 
    {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .flags.with_dma = false,   
    };

    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &ws2812) != ESP_OK)
        return false;
#endif

    gpio_set_direction(PMOD_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PMOD_BUTTON, GPIO_PULLUP_ONLY);

    return true;
}

#ifdef PMOD_OCTAL_SPI_IN_USE

void pmod_esp32s3_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(ws2812, 0, r, g, b);
    led_strip_refresh(ws2812);
}

#else
	
void pmod_esp32s3_led_set_green(bool enabled)
{
    gpio_set_level(PMOD_LED_GREEN, enabled);
}

void pmod_esp32s3_led_set_pink(bool enabled)
{
    gpio_set_level(PMOD_LED_PINK, enabled);
}

#endif