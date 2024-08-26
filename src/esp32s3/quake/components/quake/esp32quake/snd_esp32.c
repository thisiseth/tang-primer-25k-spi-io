/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "fpga_driver.h"

#define MAX_SFX 512

//static sfx info allocation pool
static sfx_t known_sfx[MAX_SFX];
static int num_sfx;

static sfx_t *ambient_sfx[NUM_AMBIENTS];

static bool	snd_ambient = 1;

static SemaphoreHandle_t sound_mutex = NULL;

static DMA_ATTR channel_t channels_render_copy[MAX_CHANNELS];

// sound.h visible stuff
//

cvar_t bgmvolume = {"bgmvolume", "1", true};
cvar_t volume = {"volume", "0.7", true};

cvar_t nosound = {"nosound", "0"};
cvar_t ambient_level = {"ambient_level", "0.3"};
cvar_t ambient_fade = {"ambient_fade", "100"};

//will be used by audio task - do not touch without mutex
DMA_ATTR channel_t channels[MAX_CHANNELS];
int total_channels;

qboolean snd_initialized = false;

vec3_t listener_origin;
vec3_t listener_forward;
vec3_t listener_right;
vec3_t listener_up;
vec_t sound_nominal_clip_dist = 1000.0;

int paintedtime; // sample PAIRS

// private or non-private but not called from other code functions
//

// from snd_mem.c
static bool LoadSound(sfx_t *s)
{
    char namebuffer[256];

    // load it in
    Q_strcpy(namebuffer, "sound/");
    Q_strcat(namebuffer, s->name);

    //we dont have free ram for sfx - working from flash directly
    const byte *data = COM_LoadMmapFile(namebuffer);

    if (!data)
    {
        Con_Printf ("Couldn't load %s\n", namebuffer);
        return false;
    }

    wavinfo_t info = GetWavinfo(s->name, (byte*)data, com_filesize);

    if (info.channels != 1)
    {
        Con_Printf ("%s is a stereo sample\n",s->name);
        return false;
    }

    assert(info.rate > 0);
    assert(info.width == 1 || info.width == 2);
    assert(info.loopstart >= -1);
    assert(info.samples > 0);

    s->cache.data = data + info.dataofs;
    s->cache.sampleRate = info.rate;
    s->cache.sampleWidth = info.width;
    s->cache.loopStart = info.loopstart;
    s->cache.sampleCount = info.samples;
    
    s->cache.stepFixedPoint = (float)info.rate * ESP32_SOUND_STEP / FPGA_DRIVER_AUDIO_SAMPLE_RATE;
    s->cache.effectiveLength = (s->cache.sampleCount * ESP32_SOUND_STEP) / s->cache.stepFixedPoint;

    return true;
}

// from snd_dma.c
// searches for AND loads a sound if not loaded
static sfx_t *FindSfxName(char *name)
{
    if (name == NULL)
        Sys_Error("S_FindName: NULL\n");

    if (Q_strlen(name) >= MAX_QPATH)
        Sys_Error("Sound name too long: %s", name);

    int i;

    // see if already loaded
    for (i = 0; i < num_sfx; ++i)
        if (!Q_strcmp(known_sfx[i].name, name))
            return &known_sfx[i];

    if (num_sfx == MAX_SFX)
        Sys_Error("S_FindName: out of sfx_t");
    
    sfx_t *sfx = &known_sfx[i];
    strcpy(sfx->name, name);

    if (!LoadSound(sfx))
        return NULL;

    num_sfx++;
    
    return sfx;
}

// from snd_dma.c
static void UpdateAmbientSounds(void)
{
    mleaf_t		*l;
    float		vol;
    int			ambient_channel;
    channel_t	*chan;

    if (!snd_ambient)
        return;

    // calc ambient sound levels
    if (!cl.worldmodel)
        return;

    l = Mod_PointInLeaf(listener_origin, cl.worldmodel);
    if (!l || !ambient_level.value)
    {
        for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++)
            channels[ambient_channel].sfx = NULL;
        return;
    }

    for (ambient_channel = 0 ; ambient_channel< NUM_AMBIENTS ; ambient_channel++)
    {
        chan = &channels[ambient_channel];	
        chan->sfx = ambient_sfx[ambient_channel];
    
        vol = ambient_level.value * l->ambient_sound_level[ambient_channel];
        if (vol < 8)
            vol = 0;

        // don't adjust volume too fast
        if (chan->master_vol < vol)
        {
            chan->master_vol += host_frametime * ambient_fade.value;
            if (chan->master_vol > vol)
                chan->master_vol = vol;
        }
        else if (chan->master_vol > vol)
        {
            chan->master_vol -= host_frametime * ambient_fade.value;
            if (chan->master_vol < vol)
                chan->master_vol = vol;
        }
        
        chan->leftvol = chan->rightvol = chan->master_vol;
    }
}

// from snd_dma.c
channel_t *SND_PickChannel(int entnum, int entchannel)
{
    int ch_idx;
    int first_to_die;
    int life_left;

    // Check for replacement sound, or find the best one to replace
    first_to_die = -1;
    life_left = 0x7fffffff;

    for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ++ch_idx)
    {
        if (entchannel != 0		// channel 0 never overrides
            && channels[ch_idx].entnum == entnum
            && (channels[ch_idx].entchannel == entchannel || entchannel == -1))
        {	// allways override sound from same entity
            first_to_die = ch_idx;
            break;
        }

        // don't let monster sounds override player sounds
        if (channels[ch_idx].entnum == cl.viewentity && entnum != cl.viewentity && channels[ch_idx].sfx)
            continue;

        if (channels[ch_idx].end - paintedtime < life_left)
        {
            life_left = channels[ch_idx].end - paintedtime;
            first_to_die = ch_idx;
        }
   }

    if (first_to_die == -1)
        return NULL;

    if (channels[first_to_die].sfx)
        channels[first_to_die].sfx = NULL;

    return &channels[first_to_die];    
} 

// from snd_dma.c
void SND_Spatialize(channel_t *ch)
{
    vec_t dot;
    vec_t ldist, rdist, dist;
    vec_t lscale, rscale, scale;
    vec3_t source_vec;
    sfx_t *snd;

    // anything coming from the view entity will allways be full volume
    if (ch->entnum == cl.viewentity)
    {
        ch->leftvol = ch->master_vol;
        ch->rightvol = ch->master_vol;
        return;
    }

    // calculate stereo seperation and distance attenuation

    snd = ch->sfx;
    VectorSubtract(ch->origin, listener_origin, source_vec);
    
    dist = VectorNormalize(source_vec) * ch->dist_mult;
    
    dot = DotProduct(listener_right, source_vec);

    rscale = 1.0 + dot;
    lscale = 1.0 - dot;

    // add in distance effect
    scale = (1.0 - dist) * rscale;
    ch->rightvol = (int) (ch->master_vol * scale);
    if (ch->rightvol < 0)
        ch->rightvol = 0;

    scale = (1.0 - dist) * lscale;
    ch->leftvol = (int) (ch->master_vol * scale);
    if (ch->leftvol < 0)
        ch->leftvol = 0;
}

static IRAM_ATTR void audio_callback(uint32_t *buffer, int *sampleCount, int maxSampleCount)
{
    int32_t mixBuffer[FPGA_DRIVER_AUDIO_BUFFER_WRITE_MAX_SAMPLES*2] = {0};

    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    memcpy(channels_render_copy, channels, sizeof(channels));
    int totalChannelsCopy = total_channels;
    int paintedTimeCopy = paintedtime;
    int volumeInt = volume.value * 256;

    xSemaphoreGive(sound_mutex);

    //to keep track if the channel was modified during sound processing
    int channelPositions[MAX_CHANNELS];

    for (int i = 0; i < totalChannelsCopy; ++i)
        channelPositions[i] = channels_render_copy[i].pos;

    //this loop order to lower spi flash access contention with kind of bulk access
    //i.e. read each pcm file in one pass
    for (int i = 0; i < totalChannelsCopy; ++i)
    {
        if (channels_render_copy[i].sfx == NULL || 
            (channels_render_copy[i].leftvol == 0 && channels_render_copy[i].rightvol == 0))
            continue;

        sfxcache_t cache = channels_render_copy[i].sfx->cache;

        int pos = channels_render_copy[i].pos;
        int length = channels_render_copy[i].sfx->cache.sampleCount * ESP32_SOUND_STEP;

        for (int j = 0; j < maxSampleCount; ++j)
        {
            if (pos >= length)
            {
                if (channels_render_copy[i].sfx->cache.loopStart < 0)
                    break; //no loop - thats all

                pos = cache.loopStart * ESP32_SOUND_STEP + pos % length;
                channels_render_copy[i].end = (paintedTimeCopy + j) + ((length - pos) / cache.stepFixedPoint);

                assert (pos < length);
            }

            int32_t left, right;

            if (cache.sampleWidth == 1) //8bit
            {
                int32_t sample = ((uint8_t*)cache.data)[pos / ESP32_SOUND_STEP];
                sample -= 128;

                left = sample * channels_render_copy[i].leftvol;
                right = sample * channels_render_copy[i].rightvol;
            }
            else //16bit
            {
                int32_t sample = ((int16_t*)cache.data)[pos / ESP32_SOUND_STEP];

                left = (sample * channels_render_copy[i].leftvol) >> 8;
                right = (sample * channels_render_copy[i].rightvol) >> 8;
            }

            pos += cache.stepFixedPoint;

            //my monitor is NOT happy with clipping quake sounds
#define MONITOR_NOT_HAPPY 8

#ifdef MONITOR_NOT_HAPPY
            left /= MONITOR_NOT_HAPPY;
            right /= MONITOR_NOT_HAPPY;
#endif

            mixBuffer[2*j] += (left*volumeInt) >> 8;
            mixBuffer[2*j + 1] += (right*volumeInt) >> 8;
        }

        channels_render_copy[i].pos = pos;
    }

    for (int i = 0; i < maxSampleCount; ++i)
    {
        int32_t left = mixBuffer[2*i], right = mixBuffer[2*i + 1];

        if (left > 32767)
            left = 32767;
        else if (left < -32767)
            left = -32767;

        if (right > 32767)
            right = 32767;
        else if (right < -32767)
            right = -32767;

        buffer[i] = (left & 0xFFFF) | (right << 16);
    }

    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    for (int i = 0; i < totalChannelsCopy; ++i)
        if (channels_render_copy[i].sfx != NULL && 
            channels[i].sfx == channels_render_copy[i].sfx &&
            channels[i].pos == channelPositions[i]) //if channel was modified - ignore
        {
            channels[i].pos = channels_render_copy[i].pos;
            channels[i].end = channels_render_copy[i].end;
        }

    paintedtime += maxSampleCount;

    xSemaphoreGive(sound_mutex);

    *sampleCount = maxSampleCount;
}

// main sound inteface
//

void S_Init(void)
{
    Cvar_RegisterVariable(&bgmvolume);
    Cvar_RegisterVariable(&volume);
    Cvar_RegisterVariable(&nosound);
    Cvar_RegisterVariable(&ambient_level);
    Cvar_RegisterVariable(&ambient_fade);

    sound_mutex = xSemaphoreCreateMutex();

    fpga_driver_register_audio_requested_cb(audio_callback);

    snd_initialized = true;

    ambient_sfx[AMBIENT_WATER] = S_PrecacheSound("ambience/water1.wav");
    ambient_sfx[AMBIENT_SKY] = S_PrecacheSound("ambience/wind2.wav");
}

void S_AmbientOff(void)
{
    snd_ambient = false;
}

void S_AmbientOn(void)
{
    snd_ambient = true;
}

void S_Shutdown(void)
{
    if (!snd_initialized)
        return;
    
    fpga_driver_register_audio_requested_cb(NULL);
    snd_initialized = 0;
}

void S_StartSound(int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation)
{
    if (!snd_initialized || nosound.value)
        return;

    if (sfx == NULL)
        return;

    int vol = fvol*255;

    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    // pick a channel to play on
    channel_t *target_chan = SND_PickChannel(entnum, entchannel);

    if (!target_chan)
    {
        xSemaphoreGive(sound_mutex);
        return;
    }
        
    // spatialize
    memset(target_chan, 0, sizeof(*target_chan));
    VectorCopy(origin, target_chan->origin);
    target_chan->dist_mult = attenuation / sound_nominal_clip_dist;
    target_chan->master_vol = vol;
    target_chan->entnum = entnum;
    target_chan->entchannel = entchannel;
    SND_Spatialize(target_chan);

    if (!target_chan->leftvol && !target_chan->rightvol)
    {
        xSemaphoreGive(sound_mutex);
        return;	// not audible at all
    }

    target_chan->sfx = sfx;
    target_chan->pos = 0;
    target_chan->end = paintedtime + sfx->cache.effectiveLength;	

    // if an identical sound has also been started this frame, offset the pos
    // a bit to keep it from just making the first one louder
    channel_t *check = &channels[NUM_AMBIENTS];

    for (int ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ++ch_idx, ++check)
    {
        if (check == target_chan)
            continue;

        if (check->sfx == sfx && check->pos == 0)
        {
            int skip = rand () % (int)(0.1*FPGA_DRIVER_AUDIO_SAMPLE_RATE);

            if (skip >= target_chan->end)
                skip = target_chan->end - 1;

            target_chan->pos += skip*ESP32_SOUND_STEP;
            target_chan->end -= skip;
            break;
        }
    }

    xSemaphoreGive(sound_mutex);
}

void S_StaticSound(sfx_t *sfx, vec3_t origin, float vol, float attenuation)
{
    if (sfx == NULL)
        return;

    if (total_channels == MAX_CHANNELS)
    {
        Con_Printf ("total_channels == MAX_CHANNELS\n");
        return;
    }

    if (sfx->cache.loopStart == -1)
    {
        Con_Printf ("Sound %s not looped\n", sfx->name);
        return;
    }

    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    channel_t *ss = &channels[total_channels];
    total_channels++;
    
    ss->sfx = sfx;
    VectorCopy (origin, ss->origin);
    ss->master_vol = vol;
    ss->dist_mult = (attenuation/64) / sound_nominal_clip_dist;
    ss->end = paintedtime + sfx->cache.effectiveLength;	
    //most likely pos also should be set to 0, 
    //however static sounds are likely destroyed only by stopall which also memsets to 0

    SND_Spatialize(ss);

    xSemaphoreGive(sound_mutex);
}

void S_LocalSound(char *name)
{
    if (!snd_initialized || nosound.value)
        return;
        
    sfx_t *sfx = FindSfxName(name);

    if (!sfx)
    {
        Con_Printf("S_LocalSound: can't cache %s\n", name);
        return;
    }

    S_StartSound(cl.viewentity, -1, sfx, vec3_origin, 1, 1);
}

void S_StopSound(int entnum, int entchannel)
{
    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    for (int i=0; i < MAX_DYNAMIC_CHANNELS; ++i)
    {
        if (channels[i].entnum == entnum && channels[i].entchannel == entchannel)
        {
            channels[i].end = 0;
            channels[i].sfx = NULL;
            break;;
        }
    }

    xSemaphoreGive(sound_mutex);
}

sfx_t *S_PrecacheSound(char *name)
{
    if (!snd_initialized || nosound.value)
        return NULL;

    return FindSfxName(name);
}

void S_TouchSound(char *name)
{
    S_PrecacheSound(name);
}

void S_Update(vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    if (!snd_initialized)
        return;

    VectorCopy(origin, listener_origin);
    VectorCopy(forward, listener_forward);
    VectorCopy(right, listener_right);
    VectorCopy(up, listener_up);

    xSemaphoreTake(sound_mutex, portMAX_DELAY);

	if (paintedtime > 0x40000000)
	{ // time to chop things off to avoid 32 bit limits
		paintedtime = 0;

        //S_StopAllSounds
        total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; 
        memset(channels, 0, MAX_CHANNELS * sizeof(channel_t));
	}

    // update general area ambient sound sources
    UpdateAmbientSounds();

    channel_t *combine = NULL;

    // update spatialization for static and dynamic sounds	
    channel_t *ch = channels + NUM_AMBIENTS;

    for (int i = NUM_AMBIENTS; i < total_channels; ++i, ++ch)
    {
        if (!ch->sfx)
            continue;

        SND_Spatialize(ch); // respatialize channel

        if (!ch->leftvol && !ch->rightvol)
            continue;

        // try to combine static sounds with a previous channel of the same
        // sound effect so we don't mix five torches every frame
    
        if (i >= MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS)
        {
            // see if it can just use the last one
            if (combine && combine->sfx == ch->sfx)
            {
                combine->leftvol += ch->leftvol;
                combine->rightvol += ch->rightvol;
                ch->leftvol = ch->rightvol = 0;
                continue;
            }

            // search for one
            int j;
            combine = channels + MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;

            for (j = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; j < i; ++j, ++combine)
                if (combine->sfx == ch->sfx)
                    break;
                    
            if (j == total_channels)			
                combine = NULL;			
            else
            {
                if (combine != ch)
                {
                    combine->leftvol += ch->leftvol;
                    combine->rightvol += ch->rightvol;
                    ch->leftvol = ch->rightvol = 0;
                }
                continue;
            }
        }
    }

    xSemaphoreGive(sound_mutex);
}

void S_StopAllSounds(qboolean clear)
{
    if (!snd_initialized)
        return;

    xSemaphoreTake(sound_mutex, portMAX_DELAY);

    total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; // no statics
    memset(channels, 0, MAX_CHANNELS * sizeof(channel_t));

    xSemaphoreGive(sound_mutex);
}

void S_ClearBuffer(void)
{
    //noop
}

void S_BeginPrecaching(void)
{
    //noop
}

void S_EndPrecaching(void)
{
    //noop
}

void S_ClearPrecache(void)
{
    //noop
}

void S_ExtraUpdate(void)
{
    //noop
}

