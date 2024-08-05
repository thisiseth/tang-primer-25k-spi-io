#pragma once

#include <stdint.h>

#define FPGA_DRIVER_PALETTE_SIZE_BYTES      (256*3)

#define FPGA_DRIVER_FRAME_WIDTH             (320)
#define FPGA_DRIVER_FRAME_HEIGHT            (240)
#define FPGA_DRIVER_FRAMEBUFFER_SIZE_BYTES  (FPGA_DRIVER_FRAME_WIDTH*FPGA_DRIVER_FRAME_HEIGHT)

#define FPGA_DRIVER_AUDIO_HDMI_FIFO_SAMPLES (1024)
#define FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_SAMPLES  (256)
#define FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_BYTES    (FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_SAMPLES*4)

#define FPGA_DRIVER_AUDIO_SAMPLE_RATE       (48000)

typedef struct 
{
    int pinCsGpu;
    int pinCsIo;
    int pinSclk;
    int pinD0;
    int pinD1;
    int pinD2;
    int pinD3;
} fpga_driver_config_t;

typedef enum 
{
    FPGA_DRIVER_VSYNC_DONT_WAIT_OVERWRITE_PREVIOUS,
    FPGA_DRIVER_VSYNC_WAIT_IF_PREVIOUS_NOT_PRESENTED
} fpga_driver_vsync_mode_t;

typedef struct
{
    uint8_t keyboardModifiers;
    uint8_t keyboardKeys[6];

    uint8_t mouseKeys;
    int32_t mouseX;
    int32_t mouseY;
    int32_t mouseWheel;
} hid_status_t;

typedef void (*fpga_driver_audio_requested_cb_t)(uint32_t *buffer, int *sampleCount, int maxSampleCount);

bool fpga_driver_init(fpga_driver_config_t *config);

bool fpga_driver_is_connected(void);

void fpga_driver_get_framebuffer(uint8_t **palette, uint8_t **framebuffer);

void fpga_driver_present_frame(uint8_t **palette, uint8_t **framebuffer, fpga_driver_vsync_mode_t vsync);

void fpga_driver_register_audio_requested_cb(fpga_driver_audio_requested_cb_t callback);

void fpga_driver_hid_get_status(hid_status_t *status);


