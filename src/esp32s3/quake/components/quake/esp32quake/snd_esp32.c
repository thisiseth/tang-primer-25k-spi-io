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

#define MAX_SFX 512

static sfx_t known_sfx[MAX_SFX];
static int num_sfx;

static sfx_t *ambient_sfx[NUM_AMBIENTS];

static bool	snd_ambient = 1;

static SemaphoreHandle_t sound_mutex = NULL;

// sound.h visible stuff
//

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

// private or non-private but not called from other code functions
//

// from snd_dma.c
static sfx_t *FindSfxName(char *name)
{
    int		i;
    sfx_t	*sfx;

    if (!name)
        Sys_Error("S_FindName: NULL\n");

    if (Q_strlen(name) >= MAX_QPATH)
        Sys_Error("Sound name too long: %s", name);

    // see if already loaded
    for (i=0 ; i < num_sfx ; i++)
        if (!Q_strcmp(known_sfx[i].name, name))
            return &known_sfx[i];

    if (num_sfx == MAX_SFX)
        Sys_Error("S_FindName: out of sfx_t");
    
    sfx = &known_sfx[i];
    strcpy(sfx->name, name);

    //////// load!!!

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

static void audio_callback(uint32_t *buffer, int *sampleCount, int maxSampleCount)
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
    target_chan->pos = 0.0;
    target_chan->end = paintedtime + sfx->cache.length;	

    // if an identical sound has also been started this frame, offset the pos
    // a bit to keep it from just making the first one louder
    channel_t *check = &channels[NUM_AMBIENTS];

    for (int ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ++ch_idx, ++check)
    {
        if (check == target_chan)
            continue;

        if (check->sfx == sfx && !check->pos)
        {
            int skip = rand () % (int)(0.1*shm->speed);

            if (skip >= target_chan->end)
                skip = target_chan->end - 1;

            target_chan->pos += skip;
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

    if (sfx->cache.loopstart == -1)
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
    ss->end = paintedtime + sfx->cache.length;	
    
    SND_Spatialize(ss);

    xSemaphoreGive(sound_mutex);
}

void S_LocalSound(char *name)
{
    if (!snd_initialized || nosound.value)
        return NULL;
        
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
            combine = channels+MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;

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

    ///////
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

