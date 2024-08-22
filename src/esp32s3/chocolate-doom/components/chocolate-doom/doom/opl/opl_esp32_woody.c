//
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//     OPL SDL interface.
//

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "opl.h"
#include "opl_internal.h"

#include "opl_queue.h"

#include "freertos/freertos.h"
#include "freertos/semphr.h"

#include "woody_opl.h"
#include "esp32_mixer.h"

typedef struct
{
    unsigned int rate;        // Number of times the timer is advanced per sec.
    unsigned int enabled;     // Non-zero if timer is enabled.
    unsigned int value;       // Last value that was set.
    uint64_t expire_time;     // Calculated time that timer will expire.
} opl_timer_t;

// When the callback mutex is locked using OPL_Lock, callback functions
// are not invoked.

static SemaphoreHandle_t callback_mutex = NULL;

// Queue of callbacks waiting to be invoked.

static opl_callback_queue_t *callback_queue;

// Mutex used to control access to the callback queue.

static SemaphoreHandle_t callback_queue_mutex = NULL;

// Current time, in us since startup:

static uint64_t current_time;

// If non-zero, playback is currently paused.

static int opl_esp32_paused;

// Time offset (in us) due to the fact that callbacks
// were previously paused.

static uint64_t pause_offset;

// Register number that was written.

static int register_num = 0;

// Advance time by the specified number of samples, invoking any
// callback functions as appropriate.

// Timers; DBOPL does not do timer stuff itself.

static opl_timer_t timer1 = { 12500, 0, 0, 0 };
static opl_timer_t timer2 = { 3125, 0, 0, 0 };

static esp32_mixer_callback_handle_t mixer_handle = NULL;

static void AdvanceTime(unsigned int nsamples)
{
    opl_callback_t callback;
    void *callback_data;
    uint64_t us;

    xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);

    // Advance time.

    us = ((uint64_t) nsamples * OPL_SECOND) / ESP32_MIXER_SAMPLE_RATE;
    current_time += us;

    if (opl_esp32_paused)
    {
        pause_offset += us;
    }

    // Are there callbacks to invoke now?  Keep invoking them
    // until there are no more left.

    while (!OPL_Queue_IsEmpty(callback_queue)
        && current_time >= OPL_Queue_Peek(callback_queue) + pause_offset)
    {
        // Pop the callback from the queue to invoke it.

        if (!OPL_Queue_Pop(callback_queue, &callback, &callback_data))
        {
            break;
        }

        // The mutex stuff here is a bit complicated.  We must
        // hold callback_mutex when we invoke the callback (so that
        // the control thread can use OPL_Lock() to prevent callbacks
        // from being invoked), but we must not be holding
        // callback_queue_mutex, as the callback must be able to
        // call OPL_SetCallback to schedule new callbacks.

        xSemaphoreGive(callback_queue_mutex);

        xSemaphoreTake(callback_mutex, portMAX_DELAY);
        callback(callback_data);
        xSemaphoreGive(callback_mutex);

        xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);
    }

    xSemaphoreGive(callback_queue_mutex);
}

// Call the OPL emulator code to fill the specified buffer.

static void FillBuffer(uint8_t *buffer, unsigned int nsamples)
{
    assert(nsamples <= FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_SAMPLES);

    adlib_getsample((Bit16s *) buffer, nsamples);
}

// Callback function to fill a new sound buffer:

static void OPL_Audio_Callback(uint32_t *buffer, int sampleCount)
{
    unsigned int filled;
    uint8_t *buffer8;

    // Repeatedly call the OPL emulator update function until the buffer is
    // full.
    filled = 0;
    buffer8 = (uint8_t*)buffer;

    while (filled < sampleCount)
    {
        uint64_t next_callback_time;
        uint64_t nsamples;

        xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);

        // Work out the time until the next callback waiting in
        // the callback queue must be invoked.  We can then fill the
        // buffer with this many samples.

        if (opl_esp32_paused || OPL_Queue_IsEmpty(callback_queue))
            nsamples = sampleCount - filled;
        else
        {
            next_callback_time = OPL_Queue_Peek(callback_queue) + pause_offset;

            nsamples = (next_callback_time - current_time) * FPGA_DRIVER_AUDIO_SAMPLE_RATE;
            nsamples = (nsamples + OPL_SECOND - 1) / OPL_SECOND;

            if (nsamples > sampleCount - filled)
                nsamples = sampleCount - filled;
        }

        xSemaphoreGive(callback_queue_mutex);

        // Add emulator output to buffer.

        FillBuffer(buffer8 + filled * 4, nsamples);
        filled += nsamples;

        // Invoke callbacks for this point in time.

        AdvanceTime(nsamples);
    }
}

static void OPL_ESP32_Shutdown(void)
{
    if (mixer_handle != NULL)
    {
        esp32_mixer_unregister_audio_requested_cb(mixer_handle);
        mixer_handle = NULL;
    }
}

static int OPL_ESP32_Init(unsigned int port_base)
{
    if (opl_sample_rate != ESP32_MIXER_SAMPLE_RATE)
    {
        printf("OPL_ESP32 supports only samplerate %d while %d is requested\n", ESP32_MIXER_SAMPLE_RATE, opl_sample_rate);
        return 1;
    }

    opl_esp32_paused = 0;
    pause_offset = 0;

    // Queue structure of callbacks to invoke.

    callback_queue = OPL_Queue_Create();
    current_time = 0;

    // Create the emulator structure:

    adlib_init(ESP32_MIXER_SAMPLE_RATE);

    if (callback_mutex == NULL)
        callback_mutex = xSemaphoreCreateMutex();
    if (callback_queue_mutex == NULL)
        callback_queue_mutex = xSemaphoreCreateMutex();

    esp32_mixer_init();
    mixer_handle = esp32_mixer_register_audio_requested_cb(OPL_Audio_Callback, 2);

    return 1;
}

static unsigned int OPL_ESP32_PortRead(opl_port_t port)
{
    unsigned int result = 0;

    if (port == OPL_REGISTER_PORT_OPL3)
    {
        return 0xff;
    }

    if (timer1.enabled && current_time > timer1.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x40;   // Timer 1 has expired
    }

    if (timer2.enabled && current_time > timer2.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x20;   // Timer 2 has expired
    }

    return result | adlib_reg_read(port);
}

static void OPLTimer_CalculateEndTime(opl_timer_t *timer)
{
    int tics;

    // If the timer is enabled, calculate the time when the timer
    // will expire.

    if (timer->enabled)
    {
        tics = 0x100 - timer->value;
        timer->expire_time = current_time
                           + ((uint64_t) tics * OPL_SECOND) / timer->rate;
    }
}

static void WriteRegister(unsigned int reg_num, unsigned int value)
{
    switch (reg_num)
    {
        case OPL_REG_TIMER1:
            timer1.value = value;
            OPLTimer_CalculateEndTime(&timer1);
            break;

        case OPL_REG_TIMER2:
            timer2.value = value;
            OPLTimer_CalculateEndTime(&timer2);
            break;

        case OPL_REG_TIMER_CTRL:
            if (value & 0x80)
            {
                timer1.enabled = 0;
                timer2.enabled = 0;
            }
            else
            {
                if ((value & 0x40) == 0)
                {
                    timer1.enabled = (value & 0x01) != 0;
                    OPLTimer_CalculateEndTime(&timer1);
                }

                if ((value & 0x20) == 0)
                {
                    timer1.enabled = (value & 0x02) != 0;
                    OPLTimer_CalculateEndTime(&timer2);
                }
            }

            break;

        case OPL_REG_NEW:
        default:
            adlib_write(reg_num, value);
            break;
    }
}

static void OPL_ESP32_PortWrite(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT)
    {
        register_num = value;
    }
    else if (port == OPL_REGISTER_PORT_OPL3)
    {
        register_num = value | 0x100;
    }
    else if (port == OPL_DATA_PORT)
    {
        WriteRegister(register_num, value);
    }
}

static void OPL_ESP32_SetCallback(uint64_t us, opl_callback_t callback,
                                void *data)
{
    xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);
    OPL_Queue_Push(callback_queue, callback, data,
                   current_time - pause_offset + us);
    xSemaphoreGive(callback_queue_mutex);
}

static void OPL_ESP32_ClearCallbacks(void)
{
    xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);
    OPL_Queue_Clear(callback_queue);
    xSemaphoreGive(callback_queue_mutex);
}

static void OPL_ESP32_Lock(void)
{
    xSemaphoreTake(callback_mutex, portMAX_DELAY);
}

static void OPL_ESP32_Unlock(void)
{
    xSemaphoreGive(callback_mutex);
}

static void OPL_ESP32_SetPaused(int paused)
{
    opl_esp32_paused = paused;
}

static void OPL_ESP32_AdjustCallbacks(float factor)
{
    xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);
    OPL_Queue_AdjustCallbacks(callback_queue, current_time, factor);
    xSemaphoreGive(callback_queue_mutex);
}

opl_driver_t opl_esp32_driver =
{
    "ESP32_woody",
    OPL_ESP32_Init,
    OPL_ESP32_Shutdown,
    OPL_ESP32_PortRead,
    OPL_ESP32_PortWrite,
    OPL_ESP32_SetCallback,
    OPL_ESP32_ClearCallbacks,
    OPL_ESP32_Lock,
    OPL_ESP32_Unlock,
    OPL_ESP32_SetPaused,
    OPL_ESP32_AdjustCallbacks,
};
