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
// snd_null.c -- include this instead of all the other snd_* files to have
// no sound code whatsoever

#include "quakedef.h"

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "fpga_driver.h"

typedef struct 
{
    int 	length;
    int 	loopstart;
    int 	speed;
    int 	width;
    int 	stereo;
    byte	data[1];		// variable sized
} sfx_cache_esp32_t;

cvar_t bgmvolume = {"bgmvolume", "1", true};
cvar_t volume = {"volume", "0.7", true};

cvar_t nosound = {"nosound", "0"};
cvar_t ambient_level = {"ambient_level", "0.3"};
cvar_t ambient_fade = {"ambient_fade", "100"};

DMA_ATTR channel_t channels[MAX_CHANNELS];
int total_channels;

qboolean snd_initialized = false;

vec3_t		listener_origin;
vec3_t		listener_forward;
vec3_t		listener_right;
vec3_t		listener_up;
vec_t		sound_nominal_clip_dist=1000.0;

static bool	snd_ambient = 1;

static SemaphoreHandle_t sound_mutex = NULL;

// private or non-private but not called from other code functions
//

static sfx_cache_esp32_t* S_LoadSound_ESP32(sfx_t *s)
{
    char namebuffer[256];
    byte *data;
    wavinfo_t info;
    int len;
    float stepscale;
    sfx_cache_esp32_t *sc;

    // see if still in memory
    sc = Cache_Check (&s->cache);
    if (sc)
        return sc;

    //Con_Printf ("S_LoadSound: %x\n", (int)stackbuf);
    // load it in
    Q_strcpy(namebuffer, "sound/");
    Q_strcat(namebuffer, s->name);

    //we dont have free ram for sfx - working from flash directly
    data = COM_LoadMmapFile(namebuffer);

    if (!data)
    {
        Con_Printf ("Couldn't load %s\n", namebuffer);
        return NULL;
    }

    info = GetWavinfo(s->name, data, com_filesize);
    if (info.channels != 1)
    {
        Con_Printf ("%s is a stereo sample\n",s->name);
        return NULL;
    }

    stepscale = (float)info.rate / shm->speed;	
    len = info.samples / stepscale;

    len = len * info.width * info.channels;

    sc = Cache_Alloc ( &s->cache, len + sizeof(sfxcache_t), s->name);
    if (!sc)
        return NULL;
    
    sc->length = info.samples;
    sc->loopstart = info.loopstart;
    sc->speed = info.rate;
    sc->width = info.width;
    sc->stereo = info.channels;

    ResampleSfx (s, sc->speed, sc->width, data + info.dataofs);

    return sc;
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
    for (ch_idx=NUM_AMBIENTS ; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS ; ch_idx++)
    {
        if (entchannel != 0		// channel 0 never overrides
        && channels[ch_idx].entnum == entnum
        && (channels[ch_idx].entchannel == entchannel || entchannel == -1) )
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

    if (shm->channels == 1)
    {
        rscale = 1.0;
        lscale = 1.0;
    }
    else
    {
        rscale = 1.0 + dot;
        lscale = 1.0 - dot;
    }

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

void audio_callback(uint32_t *buffer, int *sampleCount, int maxSampleCount)
{

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

void S_TouchSound(char *sample)
{
    //noop
}

void S_ClearBuffer(void)
{
}

void S_StaticSound(sfx_t *sfx, vec3_t origin, float vol, float attenuation)
{
}

void S_StartSound(int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation)
{
    channel_t *target_chan, *check;
    sfxcache_t	*sc;
    int		vol;
    int		ch_idx;
    int		skip;

    if (!snd_initialized || nosound.value)
        return;

    if (sfx == NULL)
        return;

    vol = fvol*255;

    // pick a channel to play on
    target_chan = SND_PickChannel(entnum, entchannel);
    if (!target_chan)
        return;
        
    // spatialize
    memset (target_chan, 0, sizeof(*target_chan));
    VectorCopy(origin, target_chan->origin);
    target_chan->dist_mult = attenuation / sound_nominal_clip_dist;
    target_chan->master_vol = vol;
    target_chan->entnum = entnum;
    target_chan->entchannel = entchannel;
    SND_Spatialize(target_chan);

    if (!target_chan->leftvol && !target_chan->rightvol)
        return;	// not audible at all

    //////
}

void S_StopSound(int entnum, int entchannel)
{
}

sfx_t *S_PrecacheSound(char *name)
{
    sfx_t *sfx;

    if (!snd_initialized || nosound.value)
        return NULL;

    sfx = S_FindName(name);
    
    // cache it in
    if (precache.value)
        S_LoadSound (sfx);
    
    return sfx;
}


void S_ClearPrecache(void)
{
    //noop
}

void S_Update(vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
	if (!snd_initialized)
		return;

	VectorCopy(origin, listener_origin);
	VectorCopy(forward, listener_forward);
	VectorCopy(right, listener_right);
	VectorCopy(up, listener_up);

    ///////
}

void S_StopAllSounds(qboolean clear)
{
    if (!snd_initialized)
        return;

    total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; // no statics
    memset(channels, 0, MAX_CHANNELS * sizeof(channel_t));

    //
}

void S_BeginPrecaching(void)
{
    //noop
}

void S_EndPrecaching(void)
{
    //noop
}

void S_ExtraUpdate(void)
{
    //noop
}

void S_LocalSound(char *name)
{	
    sfx_t *sfx;

    if (!snd_initialized || nosound.value)
        return NULL;
        
    sfx = S_PrecacheSound(name);

    if (!sfx)
    {
        Con_Printf("S_LocalSound: can't cache %s\n", name);
        return;
    }

    S_StartSound(cl.viewentity, -1, sfx, vec3_origin, 1, 1);
}

