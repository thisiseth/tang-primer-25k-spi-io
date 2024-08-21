#pragma once

#include "fpga_driver.h"

#define ESP32_MIXER_SAMPLE_RATE FPGA_DRIVER_AUDIO_SAMPLE_RATE

typedef void (*esp32_mixer_audio_requested_cb_t)(uint32_t *buffer, int sampleCount);

typedef void* esp32_mixer_callback_handle_t;

void esp32_mixer_init(void);

esp32_mixer_callback_handle_t esp32_mixer_register_audio_requested_cb(esp32_mixer_audio_requested_cb_t callback, int volumeDivisor);
void esp32_mixer_unregister_audio_requested_cb(esp32_mixer_callback_handle_t callback);


