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
//	Main program, simply calls D_DoomMain high level loop.
//

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef ESP32_DOOM
    #include "SDL.h"
#endif

#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"


//
// D_DoomMain()
// Not a globally visible function, just included for source reference,
// calls all startup code, parses command line options.
//

void D_DoomMain (void);

#ifdef ESP32_DOOM

const char *esp32_mmap_wad_name;
uint32_t esp32_mmap_wad_size;
const void *esp32_mmap_wad;

int esp32_doom_main(int argc, const char **argv, const char *wadName, uint32_t wadSize, const void *wadMmap)
{
    // save arguments
    esp32_mmap_wad_name = wadName;
    esp32_mmap_wad_size = wadSize;
    esp32_mmap_wad = wadMmap;

    myargc = argc;
    myargv = malloc(argc * sizeof(char *));
    assert(myargv != NULL);

    for (int i = 0; i < argc; i++)
    {
        myargv[i] = M_StringDuplicate(argv[i]);
    }

    //!
    // Print the program version and exit.
    //
    if (M_ParmExists("-version") || M_ParmExists("--version")) {
        puts("123");
        exit(0);
    }

    // start doom

    D_DoomMain ();

    return 0;
}

#else

int main(int argc, char **argv)
{
    // save arguments

    myargc = argc;
    myargv = malloc(argc * sizeof(char *));
    assert(myargv != NULL);

    for (int i = 0; i < argc; i++)
    {
        myargv[i] = M_StringDuplicate(argv[i]);
    }

    //!
    // Print the program version and exit.
    //
    if (M_ParmExists("-version") || M_ParmExists("--version")) {
        puts(PACKAGE_STRING);
        exit(0);
    }

#if defined(_WIN32)
    // compose a proper command line from loose file paths passed as arguments
    // to allow for loading WADs and DEHACKED patches by drag-and-drop
    M_AddLooseFiles();
#endif

    M_FindResponseFile();
    M_SetExeDir();

    #ifdef SDL_HINT_NO_SIGNAL_HANDLERS
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    #endif

    // start doom

    D_DoomMain ();

    return 0;
}

#endif
