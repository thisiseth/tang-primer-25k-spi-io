#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_vfs_dev.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "math.h"

#include "fpga_driver.h"
#include "pmod_esp32s3.h"

#define PIXEL_IDX(x, y) ((x) + (y)*FPGA_DRIVER_FRAME_WIDTH)
#define PIXEL_INBOUNDS(x, y) ((x) >= 0 && (x) < FPGA_DRIVER_FRAME_WIDTH && (y) >= 0 && (y) < FPGA_DRIVER_FRAME_HEIGHT)

int audio_saw;

uint32_t inline next_sample_saw()
{
    if (audio_saw > 1023)
        audio_saw = 0;
    else
        ++audio_saw;

    uint32_t ret;
          // LEFT  
    ret = audio_saw;

    if (gpio_get_level(PMOD_BUTTON))
        ret |= audio_saw << 16; // RIGHT

    return ret;
}

#define SINE_FREQ 200
#define SINE_AMPLITUDE 1024

float phase = 0.0;
float phaseIncrement = 2.0 * M_PI * SINE_FREQ / FPGA_DRIVER_AUDIO_SAMPLE_RATE;

uint32_t next_sample_sine() 
{
    int16_t sample = (int16_t)(SINE_AMPLITUDE * sinf(phase));
    phase += phaseIncrement;
    
    if (phase >= 2.0 * M_PI) 
        phase -= 2.0 * M_PI;
    
    uint32_t combinedSample = (uint16_t)sample;

    if (gpio_get_level(PMOD_BUTTON))
        combinedSample |= (uint32_t)sample << 16; // RIGHT

    return combinedSample;
}

void audio_requested_callback(uint32_t *buffer, int *sampleCount, int maxSampleCount)
{
    for (int i = 0; i < maxSampleCount; ++i)
        buffer[i] = next_sample_sine();

    *sampleCount = maxSampleCount;
}

void hid_event_callback(fpga_driver_hid_event_t hidEvent)
{
    switch (hidEvent.type)
    {
        case FPGA_DRIVER_HID_EVENT_KEY_DOWN:
            printf("key_down: %d modifiers %d\n", hidEvent.keyEvent.keyCode, hidEvent.keyEvent.modifiers);
            break;
        case FPGA_DRIVER_HID_EVENT_KEY_UP:
            printf("key_up: %d modifiers %d\n", hidEvent.keyEvent.keyCode, hidEvent.keyEvent.modifiers);
            break;
        case FPGA_DRIVER_HID_EVENT_MOUSE_MOVE:
            printf("mouse_move: x:%ld y:%ld w:%ld buttons %d\n", 
                hidEvent.mouseMoveEvent.moveX, 
                hidEvent.mouseMoveEvent.moveY, 
                hidEvent.mouseMoveEvent.moveWheel, 
                hidEvent.mouseMoveEvent.pressedButtons);
            break;
        case FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_DOWN:
            printf("mouse_button_down: %d\n", hidEvent.mouseButtonEvent.buttonCode);
            break;
        case FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_UP:
            printf("mouse_button_up: %d\n", hidEvent.mouseButtonEvent.buttonCode);
            break;
        default:
            printf("unknown hid event type %d", hidEvent.type);
            break;
    }
}

void user_task(void *arg)
{
    uint8_t *palette, *framebuffer;

    fpga_driver_get_framebuffer(&palette, &framebuffer);

    //initialize palettes with something
    for (int i = 0; i < 2; ++i)
    {
        for (int j = 0; j < 256; ++j)
        {
            palette[3*j] = j;
            palette[3*j + 1] = j;
            palette[3*j + 2] = j;
        }

        fpga_driver_present_frame(&palette, &framebuffer, FPGA_DRIVER_VSYNC_WAIT_IF_PREVIOUS_NOT_PRESENTED);
    }

    int64_t time = 0;
    int fps = 0;

    int temp1 = 0;

    fpga_driver_register_audio_requested_cb(audio_requested_callback);
    fpga_driver_register_hid_event_cb(hid_event_callback);

    fpga_driver_hid_status_t hid_status;

    fpga_driver_hid_get_status(&hid_status);

    int zero_x = hid_status.mouseX - FPGA_DRIVER_FRAME_WIDTH/2, 
        zero_y = hid_status.mouseY - FPGA_DRIVER_FRAME_HEIGHT/2, 
        zero_wheel = hid_status.mouseWheel;

    for (;;)
    {
        taskYIELD();

        int64_t new_time = esp_timer_get_time();

        if (new_time - time >= 1000000)
        {
            //printf("FPS: %d\n", fps);
            fps = 0;
            time = new_time;
        }

        ++fps;

        ////////////////////////////

        for (int i = 0; i < FPGA_DRIVER_FRAMEBUFFER_SIZE_BYTES; ++i)
        {
            framebuffer[i] = i % 320 + temp1;
        }

        int cur_x = hid_status.mouseX - zero_x, 
            cur_y = hid_status.mouseY - zero_y,
            cur_wheel = hid_status.mouseWheel - zero_wheel;

        for (int i = -1; i <= 1; ++i)
            for (int j = -1; j <= 1; ++j)
                if (PIXEL_INBOUNDS(cur_x + i, cur_y + j))
                    framebuffer[PIXEL_IDX(cur_x + i, cur_y + j)] = (!i & !j) ? 255 : 0;

        ++temp1;
EXT_RAM_ATTR
        //fpga_driver_present_frame(&palette, &framebuffer, FPGA_DRIVER_VSYNC_DONT_WAIT_OVERWRITE_PREVIOUS);
        fpga_driver_present_frame(&palette, &framebuffer, FPGA_DRIVER_VSYNC_WAIT_IF_PREVIOUS_NOT_PRESENTED);
        fpga_driver_hid_get_status(&hid_status);
    }
}

void app_main(void)
{
    gpio_set_direction(PMOD_LED_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(PMOD_LED_PINK, GPIO_MODE_OUTPUT);
    gpio_set_direction(PMOD_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PMOD_BUTTON, GPIO_PULLUP_ONLY);

    fpga_driver_config_t driver_config = 
    {
        .pinCsGpu = PMOD_FPGA_SPI_CS0,
        .pinCsIo = PMOD_FPGA_SPI_CS1,
        .pinSclk = PMOD_FPGA_SPI_SCLK,
        .pinD0 = PMOD_FPGA_SPI_D0,
        .pinD1 = PMOD_FPGA_SPI_D1,
        .pinD2 = PMOD_FPGA_SPI_D2,
        .pinD3 = PMOD_FPGA_SPI_D3
    };

    if (!fpga_driver_init(&driver_config))
        printf("failed to init driver\n");

    xTaskCreatePinnedToCore(user_task, "user_task", 4096, NULL, tskIDLE_PRIORITY+1, NULL, 1);
}