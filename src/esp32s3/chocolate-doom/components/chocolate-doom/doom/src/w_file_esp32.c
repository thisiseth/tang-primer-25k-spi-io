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
//	WAD I/O functions.
//

#include <stdio.h>
#include <string.h>

#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"

extern const char *esp32_mmap_wad_name;
extern uint32_t esp32_mmap_wad_size;
extern const void *esp32_mmap_wad;

typedef struct
{
    wad_file_t wad;
} esp32_wad_file_t;

static wad_file_t *W_esp32_OpenFile(const char *path)
{
    esp32_wad_file_t *result;

    if (strcmp(path, esp32_mmap_wad_name))
    {
        printf("W_esp32_OpenFile: %s does not match mmapped wad %s\n", path, esp32_mmap_wad_name);
        return NULL;
    }

    // Create a new stdc_wad_file_t to hold the file handle.

    result = Z_Malloc(sizeof(esp32_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &esp32_wad_file;
    result->wad.mapped = (byte*)esp32_mmap_wad;
    result->wad.length = esp32_mmap_wad_size;
    result->wad.path = M_StringDuplicate(path);

    printf("W_esp32_OpenFile: wad %s with size %lu opened\n", esp32_mmap_wad_name, esp32_mmap_wad_size);

    return &result->wad;
}

static void W_esp32_CloseFile(wad_file_t *wad)
{
    esp32_wad_file_t *esp32_wad;

    esp32_wad = (esp32_wad_file_t*) wad;

    Z_Free(esp32_wad);

    printf("W_esp32_OpenFile: wad %s closed\n", esp32_mmap_wad_name);
}

// Read data from the specified position in the file into the 
// provided buffer.  Returns the number of bytes read.

size_t W_esp32_Read(wad_file_t *wad, unsigned int offset,
                   void *buffer, size_t buffer_len)
{
    esp32_wad_file_t *esp32_wad;
    int32_t available_bytes;
    
    esp32_wad = (esp32_wad_file_t*) wad;

    // Read into the buffer.

    available_bytes = (int32_t)esp32_wad->wad.length - (int32_t)offset;

    if (available_bytes <= 0)
        return 0;

    if (available_bytes > buffer_len)
        available_bytes = buffer_len;

    memcpy(buffer, esp32_wad->wad.mapped + offset, available_bytes);

    return available_bytes;
}

wad_file_class_t esp32_wad_file = 
{
    W_esp32_OpenFile,
    W_esp32_CloseFile,
    W_esp32_Read,
};


