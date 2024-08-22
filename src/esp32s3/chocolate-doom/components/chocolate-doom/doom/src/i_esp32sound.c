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

#include "samplerate.h"
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

#define ESP32_DOOM_CHEAP_UPSAMPLE

#ifndef ESP32_DOOM_CHEAP_UPSAMPLE
    #define ESP32SOUND_RESAMPLE_FREQ ESP32_MIXER_SAMPLE_RATE
#else
    #define ESP32SOUND_RESAMPLE_FREQ (ESP32_MIXER_SAMPLE_RATE/2)
#endif

int use_libsamplerate = 1;

// Scale factor used when converting libsamplerate floating point numbers
// to integers. Too high means the sounds can clip; too low means they
// will be too quiet. This is an amount that should avoid clipping most
// of the time: with all the Doom IWAD sound effects, at least. If a PWAD
// is used, clipping might occur.

float libsamplerate_scale = 0.65f;

#define NUM_CHANNELS 16

#define ESP32SOUND_STEP 1024

typedef struct allocated_sound_s allocated_sound_t;

struct allocated_sound_s
{
    sfxinfo_t *sfxinfo;
    uint8_t *samples;
    int samplesLength;
    int use_count;
    allocated_sound_t *prev, *next;
};

typedef struct 
{   
    allocated_sound_t *sound;
    int16_t *samples;
    uint32_t samplesCount; //sample count 0 also indicates that playback has finished
    uint32_t offset; //position in samples * ESP32SOUND_STEP
    uint32_t step; //one sample step * ESP32SOUND_STEP
    uint8_t left, right; // volume 0-255
} channel_t;

static boolean sound_initialized = false;

static channel_t channels[NUM_CHANNELS];

static boolean use_sfx_prefix;

// Doubly-linked list of allocated sounds.
// When a sound is played, it is moved to the head, so that the oldest
// sounds not used recently are at the tail.

static allocated_sound_t *allocated_sounds_head = NULL;
static allocated_sound_t *allocated_sounds_tail = NULL;

//static int allocated_sounds_size = 0;
int allocated_sounds_size = 0;

static esp32_mixer_callback_handle_t mixer_handle = NULL;

static SemaphoreHandle_t sound_mutex = NULL;

// Hook a sound into the linked list at the head.

static inline void AllocatedSoundLink(allocated_sound_t *snd)
{
    snd->prev = NULL;

    snd->next = allocated_sounds_head;
    allocated_sounds_head = snd;

    if (allocated_sounds_tail == NULL)
        allocated_sounds_tail = snd;
    else
        snd->next->prev = snd;
}

// Unlink a sound from the linked list.

static inline void AllocatedSoundUnlink(allocated_sound_t *snd)
{
    if (snd->prev == NULL)
        allocated_sounds_head = snd->next;
    else
        snd->prev->next = snd->next;

    if (snd->next == NULL)
        allocated_sounds_tail = snd->prev;
    else
        snd->next->prev = snd->prev;
}

static void FreeAllocatedSound(allocated_sound_t *snd)
{
    // Unlink from linked list.

    AllocatedSoundUnlink(snd);

    // Keep track of the amount of allocated sound data:

    allocated_sounds_size -= snd->samplesLength;

    free(snd);
}

// Search from the tail backwards along the allocated sounds list, find
// and free a sound that is not in use, to free up memory.  Return true
// for success.

static boolean FindAndFreeSound(void)
{
    allocated_sound_t *snd;

    snd = allocated_sounds_tail;

    while (snd != NULL)
    {
        if (snd->use_count <= 0)
        {
            FreeAllocatedSound(snd);
            return true;
        }

        snd = snd->prev;
    }

    // No available sounds to free...

    return false;
}

// Enforce SFX cache size limit.  We are just about to allocate "len"
// bytes on the heap for a new sound effect, so free up some space
// so that we keep allocated_sounds_size < snd_cachesize

static void ReserveCacheSpace(size_t len)
{
    if (snd_cachesize <= 0)
        return;

    // Keep freeing sound effects that aren't currently being played,
    // until there is enough space for the new sound.

    while (allocated_sounds_size + len > snd_cachesize)
        if (!FindAndFreeSound()) // Free a sound. If there is nothing more to free, stop.
            break;
}

// Allocate a block for a new sound effect.

static allocated_sound_t *AllocateSound(sfxinfo_t *sfxinfo, size_t len)
{
    allocated_sound_t *snd;

    // Keep allocated sounds within the cache size.

    ReserveCacheSpace(len);

    // Allocate the sound structure and data.  The data will immediately
    // follow the structure, which acts as a header.

    do
    {
        snd = malloc(sizeof(allocated_sound_t) + len);

        // Out of memory?  Try to free an old sound, then loop round
        // and try again.

        if (snd == NULL && !FindAndFreeSound())
            return NULL;
    } while (snd == NULL);

    // Skip past the chunk structure for the audio buffer

    snd->samples = (byte *) (snd + 1);
    snd->samplesLength = len;
    snd->sfxinfo = sfxinfo;
    snd->use_count = 0;

    // Keep track of how much memory all these cached sounds are using...

    allocated_sounds_size += len;

    AllocatedSoundLink(snd);

    return snd;
}

// Lock a sound, to indicate that it may not be freed.

static inline void LockAllocatedSound(allocated_sound_t *snd)
{
    // Increase use count, to stop the sound being freed.

    ++snd->use_count;

    // When we use a sound, re-link it into the list at the head, so
    // that the oldest sounds fall to the end of the list for freeing.

    AllocatedSoundUnlink(snd);
    AllocatedSoundLink(snd);
}

// Unlock a sound to indicate that it may now be freed.

static inline void UnlockAllocatedSound(allocated_sound_t *snd)
{
    if (snd->use_count <= 0)
        I_Error("Sound effect released more times than it was locked...");
    
    --snd->use_count;
}

// Search through the list of allocated sounds and return the one that matches
// the supplied sfxinfo entry and pitch level.

static allocated_sound_t* GetAllocatedSoundBySfxInfo(sfxinfo_t *sfxinfo)
{
    allocated_sound_t * p = allocated_sounds_head;

    while (p != NULL)
    {
        if (p->sfxinfo == sfxinfo)
            return p;

        p = p->next;
    }

    return NULL;
}

// When a sound stops, check if it is still playing.  If it is not,
// we can mark the sound data as CACHE to be freed back for other
// means.

static void ReleaseSoundOnChannel(int channel)
{
    allocated_sound_t *snd = channels[channel].sound;

    if (snd == NULL)
        return;

    channels[channel].sound = NULL;
    channels[channel].samples = NULL;
    channels[channel].samplesCount = 0;

    UnlockAllocatedSound(snd);
}

// Returns the conversion mode for libsamplerate to use.

static int SRC_ConversionMode(void)
{
    switch (use_libsamplerate)
    {
        // 0 = disabled

        default:
        case 0:
            return -1;

        // Ascending numbers give higher quality

        case 1:
            return SRC_LINEAR;
        case 2:
            return SRC_ZERO_ORDER_HOLD;
        case 3:
            return SRC_SINC_FASTEST;
        case 4:
            return SRC_SINC_MEDIUM_QUALITY;
        case 5:
            return SRC_SINC_BEST_QUALITY;
    }
}

static allocated_sound_t* ExpandSoundData_SRC(sfxinfo_t *sfxinfo,
                                             byte *data,
                                             int samplerate,
                                             int length)
{
    SRC_DATA src_data;
    float *data_in;
    uint32_t i, abuf_index=0, clipped=0;
    int retn;
    int16_t *expanded;
    allocated_sound_t *snd;

    src_data.input_frames = length;
    data_in = malloc(length * sizeof(float));
    src_data.data_in = data_in;
    src_data.src_ratio = (double)ESP32SOUND_RESAMPLE_FREQ / samplerate;

    // We include some extra space here in case of rounding-up.
    src_data.output_frames = src_data.src_ratio * length + (ESP32SOUND_RESAMPLE_FREQ / 4);
    src_data.data_out = malloc(src_data.output_frames * sizeof(float));

    assert(src_data.data_in != NULL && src_data.data_out != NULL);

    // Convert input data to floats

    for (i=0; i<length; ++i)
        // Unclear whether 128 should be interpreted as "zero" or whether a
        // symmetrical range should be assumed.  The following assumes a
        // symmetrical range.
        data_in[i] = data[i] / 127.5f - 1.0f;
    
    // Do the sound conversion

    retn = src_simple(&src_data, SRC_ConversionMode(), 1);
    assert(retn == 0);

    // Allocate the new chunk.

    snd = AllocateSound(sfxinfo, src_data.output_frames_gen * 2);

    if (snd == NULL)
        return NULL;
    
    expanded = (int16_t *)snd->samples;

    // Convert the result back into 16-bit integers.

    for (i=0; i<src_data.output_frames_gen; ++i)
    {
        // libsamplerate does not limit itself to the -1.0 .. 1.0 range on
        // output, so a multiplier less than INT16_MAX (32767) is required
        // to avoid overflows or clipping.  However, the smaller the
        // multiplier, the quieter the sound effects get, and the more you
        // have to turn down the music to keep it in balance.

        // 22265 is the largest multiplier that can be used to resample all
        // of the Vanilla DOOM sound effects to 48 kHz without clipping
        // using SRC_SINC_BEST_QUALITY.  It is close enough (only slightly
        // too conservative) for SRC_SINC_MEDIUM_QUALITY and
        // SRC_SINC_FASTEST.  PWADs with interestingly different sound
        // effects or target rates other than 48 kHz might still result in
        // clipping--I don't know if there's a limit to it.

        // As the number of clipped samples increases, the signal is
        // gradually overtaken by noise, with the loudest parts going first.
        // However, a moderate amount of clipping is often tolerated in the
        // quest for the loudest possible sound overall.  The results of
        // using INT16_MAX as the multiplier are not all that bad, but
        // artifacts are noticeable during the loudest parts.

        float cvtval_f =
            src_data.data_out[i] * libsamplerate_scale * INT16_MAX;
        int32_t cvtval_i = cvtval_f + (cvtval_f < 0 ? -0.5 : 0.5);

        // Asymmetrical sound worries me, so we won't use -32768.
        if (cvtval_i < -INT16_MAX)
        {
            cvtval_i = -INT16_MAX;
            ++clipped;
        }
        else if (cvtval_i > INT16_MAX)
        {
            cvtval_i = INT16_MAX;
            ++clipped;
        }

        // mono

        expanded[abuf_index++] = cvtval_i;
    }

    free(data_in);
    free(src_data.data_out);

    if (clipped > 0)
    {
        fprintf(stderr, "Sound '%s': clipped %lu samples (%0.2f %%)\n", 
                        sfxinfo->name, clipped,
                        400.0 * clipped / snd->samplesLength);
    }

    return snd;
}

// Load and convert a sound effect
// Returns true if successful

static allocated_sound_t* CacheSFX(sfxinfo_t *sfxinfo)
{
    int lumpnum;
    unsigned int lumplen;
    int samplerate;
    unsigned int length;
    byte *data;
    allocated_sound_t *sound;

    // need to load the sound

    lumpnum = sfxinfo->lumpnum;
    data = W_CacheLumpNum(lumpnum, PU_STATIC);
    lumplen = W_LumpLength(lumpnum);

    // Check the header, and ensure this is a valid sound

    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
        return NULL; // Invalid sound
    
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
        return NULL;
    
    // The DMX sound library seems to skip the first 16 and last 16
    // bytes of the lump - reason unknown.

    data += 16;
    length -= 32;

    // Sample rate conversion

    if ((sound = ExpandSoundData_SRC(sfxinfo, data + 8, samplerate, length)) == NULL)
        return NULL;
    
    // don't need the original lump any more
  
    W_ReleaseLumpNum(lumpnum);

    return sound;
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

static inline uint32_t GetStepForPitch(int pitch)
{
    return (uint32_t)(ESP32SOUND_STEP * (2.0f - (float)pitch / NORM_PITCH));
}

// Preload all the sound effects - stops nasty ingame freezes

static void I_ESP32_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    char namebuf[9];
    int i;

    printf("I_ESP32_PrecacheSounds: Precaching all sound effects..");

    for (i=0; i<num_sounds; ++i)
    {
        if ((i % 6) == 0)
        {
            printf(".");
            fflush(stdout);
        }

        GetSfxLumpName(&sounds[i], namebuf, sizeof(namebuf));

        sounds[i].lumpnum = W_CheckNumForName(namebuf);

        if (sounds[i].lumpnum != -1)
            CacheSFX(&sounds[i]);
    }

    printf("\n");
}

// Load a SFX chunk into memory and ensure that it is locked.

static allocated_sound_t* LockSound(sfxinfo_t *sfxinfo)
{
    allocated_sound_t *sound;

    // If the sound isn't loaded, load it now
    if ((sound = GetAllocatedSoundBySfxInfo(sfxinfo)) == NULL)
        if ((sound = CacheSFX(sfxinfo)) == NULL)
            return NULL;

    LockAllocatedSound(sound);

    return sound;
}

static inline void UpdateSoundParams(int handle, int vol, int sep)
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

    channels[handle].left = left;
    channels[handle].right = right;
}

static inline boolean SoundIsPlaying(int handle)
{
    return channels[handle].samplesCount > 0 && (channels[handle].offset / channels[handle].step) >= channels[handle].samplesCount;
}

#ifdef ESP32_DOOM_CHEAP_UPSAMPLE
static int32_t prev_left, prev_right;
#endif

static void Mixer_Audio_Callback(uint32_t *buffer, int sampleCount)
{
    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    for (int i = 0; i < sampleCount; ++i)
    {
        int32_t left = 0, right = 0;



        if (left < -INT16_MAX)
            left = -INT16_MAX;
        else if (left > INT16_MAX)
            left = INT16_MAX;

        if (right < -INT16_MAX)
            right = -INT16_MAX;
        else if (right > INT16_MAX)
            right = INT16_MAX;

        
    }

    for (int channel = 0; channel < NUM_CHANNELS; ++channel)
    {
        if (!SoundIsPlaying(channel))
            continue;

        
    }

    xSemaphoreGive(sound_mutex);
}

//
// Retrieve the raw data lump index
//  for a given SFX name.
//

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

//
// Starting a sound means adding it
//  to the current list of active sounds
//  in the internal channels.
// As the SFX info struct contains
//  e.g. a pointer to the raw data,
//  it is ignored.
// As our sound handling does not handle
//  priority, it is ignored.
// Pitching (that is, increased speed of playback)
//  is set, but currently not used by mixing.
//

static int I_ESP32_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep, int pitch)
{
    allocated_sound_t *snd;

    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
        return -1;
    
    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    // Release a sound effect if there is already one playing
    // on this channel

    ReleaseSoundOnChannel(channel);

    xSemaphoreGive(sound_mutex);

    // Get the sound data

    if ((snd = LockSound(sfxinfo)) == NULL)
        return -1;
    
    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    channels[channel].sound = snd;
    channels[channel].samples = (int16_t*)snd->samples;
    channels[channel].samplesCount = snd->samplesLength / 2;
    channels[channel].offset = 0;
    channels[channel].step = GetStepForPitch(pitch);

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

    ReleaseSoundOnChannel(handle);

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
        if (channels[i].sound != NULL && !SoundIsPlaying(i))
            // Sound has finished playing on this channel,
            // but sound data has not been released to cache
            ReleaseSoundOnChannel(i);
        
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

    if (SRC_ConversionMode() < 0)
    {
        I_Error("I_SDL_InitSound: Invalid value for use_libsamplerate: %i", use_libsamplerate);
        return false;
    }

    use_sfx_prefix = (mission == doom || mission == strife);
    
    if (sound_mutex == NULL)
        sound_mutex = xSemaphoreCreateMutex();

#ifdef ESP32_DOOM_CHEAP_UPSAMPLE
    prev_left = 0;
    prev_right = 0;
#endif

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
