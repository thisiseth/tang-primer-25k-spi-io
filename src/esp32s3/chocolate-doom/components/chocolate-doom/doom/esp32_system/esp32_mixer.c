#include "esp32_mixer.h"
#include "freertos/freertos.h"
#include "freertos/semphr.h"
#include "assert.h"
#include <string.h>

#define ESP32_MIXER_CHANNELS 4

typedef struct 
{
    esp32_mixer_audio_requested_cb_t callback;
    int volumeDivisor;
} esp32_mixer_channel_t;

static esp32_mixer_channel_t channels[ESP32_MIXER_CHANNELS];
static DMA_ATTR int16_t mixer_buffer[FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_SAMPLES*2];

static SemaphoreHandle_t mixer_mutex = NULL;

static void fpga_driver_audio_requested_callback(uint32_t *buffer, int *sampleCount, int maxSampleCount)
{
    xSemaphoreTake(mixer_mutex, portMAX_DELAY);

    memset(buffer, 0, maxSampleCount*4);

    for (int i = 0; i < ESP32_MIXER_CHANNELS; ++i)
    {
        if (channels[i].callback == NULL)
            continue;

        channels[i].callback((uint32_t*)mixer_buffer, maxSampleCount);

        for (int j = 0; j < maxSampleCount*2; ++j)
        {   
            int32_t val = ((int16_t*)buffer)[j];

            val +=  mixer_buffer[j] / channels[i].volumeDivisor;

            if (val > 32767)
                val = 32767;
            else if (val < -32767)
                val = -32767;

            ((int16_t*)buffer)[j] = (int16_t)val;
        }
    }

    *sampleCount = maxSampleCount;

    xSemaphoreGive(mixer_mutex);
}

void esp32_mixer_init(void)
{
    if (mixer_mutex != NULL)
        return;

    mixer_mutex = xSemaphoreCreateMutex();

    fpga_driver_register_audio_requested_cb(fpga_driver_audio_requested_callback);
}

esp32_mixer_callback_handle_t esp32_mixer_register_audio_requested_cb(esp32_mixer_audio_requested_cb_t callback, int volumeDivisor)
{
    assert(callback != NULL);

    xSemaphoreTake(mixer_mutex, portMAX_DELAY);

    esp32_mixer_callback_handle_t handle = NULL;

    for (int i = 0; i < ESP32_MIXER_CHANNELS; ++i)
    {
        if (channels[i].callback != NULL)
            continue;

        channels[i].callback = callback;
        channels[i].volumeDivisor = volumeDivisor;
        handle = &channels[i];
        break;
    }

    xSemaphoreGive(mixer_mutex);

    return handle;
}

void esp32_mixer_unregister_audio_requested_cb(esp32_mixer_callback_handle_t handle)
{
    xSemaphoreTake(mixer_mutex, portMAX_DELAY);

    esp32_mixer_channel_t *channel = (esp32_mixer_channel_t*)handle;

    channel->callback = NULL;

    xSemaphoreGive(mixer_mutex);
}
