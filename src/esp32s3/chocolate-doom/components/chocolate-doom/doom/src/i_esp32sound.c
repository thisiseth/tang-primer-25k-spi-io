//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2008 David Flater
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
//	System interface for sound.
//

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "deh_str.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_swap.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include "doomtype.h"

#include "freertos/freertos.h"
#include "freertos/semphr.h"
#include "esp32_mixer.h"

#define NUM_CHANNELS 16

#define ESP32SOUND_STEP 4096

typedef struct 
{
    uint8_t *samples;
    uint32_t samplesCount;
    uint32_t sampleRate;
} sfxinfo_parsed_t;

typedef struct 
{
    uint8_t *samples;
    uint32_t samplesCount; //sample count 0 also indicates that playback has finished
    uint32_t offset; //position in samples * ESP32SOUND_STEP
    uint32_t step; //one sample step * ESP32SOUND_STEP
    float left, right; // volume 0.0-32767.0
} channel_t;

static boolean sound_initialized = false;
static boolean use_sfx_prefix;

static channel_t channels[NUM_CHANNELS];

static esp32_mixer_callback_handle_t mixer_handle = NULL;
static SemaphoreHandle_t sound_mutex = NULL;

static inline void StopChannel(int channel)
{
    channels[channel].samples = NULL;
    channels[channel].samplesCount = 0;
}

static inline sfxinfo_parsed_t ParseSfxInfo(sfxinfo_t *sfxinfo)
{
    sfxinfo_parsed_t info = {0};

    int lumpnum;
    unsigned int lumplen;
    int samplerate;
    unsigned int length;
    byte *data;
    
    // need to load the sound

    lumpnum = sfxinfo->lumpnum;
    data = W_CacheLumpNum(lumpnum, PU_STATIC);
    lumplen = W_LumpLength(lumpnum);

    // Check the header, and ensure this is a valid sound

    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
        return info; // Invalid sound
    
    // 16 bit sample rate field, 32 bit length field

    samplerate = (data[3] << 8) | data[2];
    length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];

    // If the header specifies that the length of the sound is greater than
    // the length of the lump itself, this is an invalid sound lump

    // We also discard sound lumps that are less than 49 samples long,
    // as this is how DMX behaves - although the actual cut-off length
    // seems to vary slightly depending on the sample rate.  This needs
    // further investigation to better understand the correct
    // behavior.

    if (length > lumplen - 8 || length <= 48)
        return info;
    
    // The DMX sound library seems to skip the first 16 and last 16
    // bytes of the lump - reason unknown.

    data += 16;
    length -= 32;

    info.samples = data + 8;
    info.samplesCount = length;
    info.sampleRate = samplerate;
    
    return info;
}

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    // Linked sfx lumps? Get the lump number for the sound linked to.

    if (sfx->link != NULL)
        sfx = sfx->link;
    
    // Doom adds a DS* prefix to sound lumps; Heretic and Hexen don't
    // do this.

    if (use_sfx_prefix)
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    else
        M_StringCopy(buf, DEH_String(sfx->name), buf_len);
}

static inline uint32_t GetStep(int pitch, int sampleRate)
{
    return (uint32_t)(ESP32SOUND_STEP * (2.0f - (float)NORM_PITCH / pitch) * ((float)sampleRate / ESP32_MIXER_SAMPLE_RATE));
}

static inline void UpdateSoundParams(int channel, int vol, int sep)
{
    int left, right;

    left = ((254 - sep) * vol) / 127;
    right = ((sep) * vol) / 127;

    if (left < 0) 
        left = 0;
    else if (left > 255) 
        left = 255;

    if (right < 0) 
        right = 0;
    else if (right > 255) 
        right = 255;

    channels[channel].left = 32767.0f * left / 255.0f;
    channels[channel].right = 32767.0f * right / 255.0f;
}

static inline boolean SoundIsPlaying(int handle)
{
    return (channels[handle].samplesCount > 0) && (channels[handle].offset / ESP32SOUND_STEP) < channels[handle].samplesCount;
}

static IRAM_ATTR void Mixer_Audio_Callback(uint32_t *buffer, int sampleCount)
{
    int32_t left, right;
    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    for (int i = 0; i < sampleCount; ++i)
    {
        left = right = 0;

        for (int j = 0; j < NUM_CHANNELS; ++j)
        {
            if (!SoundIsPlaying(j))
                continue;
            
            left += ((channels[j].samples[channels[j].offset / ESP32SOUND_STEP]) / 127.5f - 1.0f) * channels[j].left;
            right += ((channels[j].samples[channels[j].offset / ESP32SOUND_STEP]) / 127.5f - 1.0f) * channels[j].right;

            channels[j].offset += channels[j].step;
        }

        if (left < -INT16_MAX)
            left = -INT16_MAX;
        else if (left > INT16_MAX)
            left = INT16_MAX;

        if (right < -INT16_MAX)
            right = -INT16_MAX;
        else if (right > INT16_MAX)
            right = INT16_MAX;

        buffer[i] = (left & 0xFFFF) | (right << 16);
    }

    xSemaphoreGive(sound_mutex);
}

//
// Retrieve the raw data lump index
//  for a given SFX name.
//

static void I_ESP32_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    // :(
}

static int I_ESP32_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];

    GetSfxLumpName(sfx, namebuf, sizeof(namebuf));

    return W_GetNumForName(namebuf);
}

static void I_ESP32_UpdateSoundParams(int handle, int vol, int sep)
{
    if (!sound_initialized || handle < 0 || handle >= NUM_CHANNELS)
        return;

    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    UpdateSoundParams(handle, vol, sep);

    xSemaphoreGive(sound_mutex);
}

static int I_ESP32_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep, int pitch)
{
    sfxinfo_parsed_t snd;

    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
        return -1;
    
    assert(pitch > 0);

    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    StopChannel(channel);

    // Get the sound data

    snd = ParseSfxInfo(sfxinfo);

    if (snd.samplesCount == 0)
    {
        xSemaphoreGive(sound_mutex);
        return -1;
    }

    channels[channel].samples = snd.samples;
    channels[channel].samplesCount = snd.samplesCount;
    channels[channel].offset = 0;
    channels[channel].step = GetStep(pitch, snd.sampleRate);

    UpdateSoundParams(channel, vol, sep);

    xSemaphoreGive(sound_mutex);

    return channel;
}

static void I_ESP32_StopSound(int handle)
{
    if (!sound_initialized || handle < 0 || handle >= NUM_CHANNELS)
        return;
    
    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    // Sound data is no longer needed; release the
    // sound data being used for this channel

    StopChannel(handle);

    xSemaphoreGive(sound_mutex);
}

static boolean I_ESP32_SoundIsPlaying(int handle)
{
    if (!sound_initialized || handle < 0 || handle >= NUM_CHANNELS)
        return false;
    
    boolean playing;

    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    playing = SoundIsPlaying(handle);

    xSemaphoreGive(sound_mutex);

    return playing;
}

//
// Periodically called to update the sound system
//

static void I_ESP32_UpdateSound(void)
{
    int i;

    // Check all channels to see if a sound has finished

    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    for (i=0; i<NUM_CHANNELS; ++i)
        if (!SoundIsPlaying(i))
            StopChannel(i);
        
    xSemaphoreGive(sound_mutex);
}

static void I_ESP32_ShutdownSound(void)
{
    if (!sound_initialized)
        return;
    
    esp32_mixer_unregister_audio_requested_cb(mixer_handle);
    mixer_handle = NULL;

    sound_initialized = false;
}

static boolean I_ESP32_InitSound(GameMission_t mission)
{
    int i;

    if (snd_samplerate != ESP32_MIXER_SAMPLE_RATE)
    {
        printf("esp32sound supports only samplerate %d while %d is requested\n", ESP32_MIXER_SAMPLE_RATE, snd_samplerate);
        return false;
    }

    use_sfx_prefix = (mission == doom || mission == strife);
    
    if (sound_mutex == NULL)
        sound_mutex = xSemaphoreCreateMutex();

    esp32_mixer_init();
    mixer_handle = esp32_mixer_register_audio_requested_cb(Mixer_Audio_Callback, 1);

    sound_initialized = true;

    return true;
}

static const snddevice_t sound_esp32_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

const sound_module_t sound_esp32_module =
{
    sound_esp32_devices,
    arrlen(sound_esp32_devices),
    I_ESP32_InitSound,
    I_ESP32_ShutdownSound,
    I_ESP32_GetSfxLumpNum,
    I_ESP32_UpdateSound,
    I_ESP32_UpdateSoundParams,
    I_ESP32_StartSound,
    I_ESP32_StopSound,
    I_ESP32_SoundIsPlaying,
    I_ESP32_PrecacheSounds,
};
