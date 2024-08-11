//
// Copyright(C) 1993-1996 Id Software, Inc.
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
//      Timer functions.
//

#ifndef ESP32_DOOM
    #include "SDL.h"
#endif

#include "i_timer.h"
#include "doomtype.h"

//
// I_GetTime
// returns time in 1/35th second tics
//
#ifdef ESP32_DOOM

#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#include "esp_err.h"

static esp_timer_handle_t timer;
static int64_t basetime = 0;

//
// I_GetTime
// returns time in 1/35th second tics
//
int  I_GetTime (void)
{
    int64_t ticks = esp_timer_get_time();

    if (basetime == 0)
        basetime = ticks;

    ticks -= basetime;

    return (ticks * TICRATE) / 1000000;    
}

//
// Same as I_GetTime, but returns time in milliseconds
//

int I_GetTimeMS(void)
{
    int64_t ticks = esp_timer_get_time();

    if (basetime == 0)
        basetime = ticks;

    return (ticks - basetime) / 1000;
}

// Sleep for a specified number of ms

void I_Sleep(int ms)
{
    int waitUntil = I_GetTimeMS() + ms;

    while (I_GetTimeMS() < waitUntil)
        vTaskDelay(0);
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}

void I_InitTimer(void)
{
    // initialize timer

}

#else

static Uint32 basetime = 0;

//
// I_GetTime
// returns time in 1/35th second tics
//
int  I_GetTime (void)
{
    Uint32 ticks;

    ticks = SDL_GetTicks();

    if (basetime == 0)
        basetime = ticks;

    ticks -= basetime;

    return (ticks * TICRATE) / 1000;    
}

//
// Same as I_GetTime, but returns time in milliseconds
//

int I_GetTimeMS(void)
{
    Uint32 ticks;

    ticks = SDL_GetTicks();

    if (basetime == 0)
        basetime = ticks;

    return ticks - basetime;
}

// Sleep for a specified number of ms

void I_Sleep(int ms)
{
    SDL_Delay(ms);
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}


void I_InitTimer(void)
{
    // initialize timer

    SDL_SetHint(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING, "1");

    SDL_Init(SDL_INIT_TIMER);
}

#endif