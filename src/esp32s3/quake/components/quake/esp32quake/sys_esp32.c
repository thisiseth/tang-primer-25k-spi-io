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
// sys_null.h -- null system driver to aid porting efforts

#include "quakedef.h"
#include "errno.h"

#include "esp_timer.h"
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"

#define MAX_HANDLES 10

typedef struct 
{
	bool isOpen;
	FILE *file;
	uint32_t mmapOffset;
} file_handle_t;

static file_handle_t sys_handles[MAX_HANDLES];

static bool sys_quit = false;

static const char *mmap_pak_path;
static uint32_t mmap_pak_size;
static const void *mmap_pak;

/*
===============================================================================

FILE IO

===============================================================================
*/

static int findhandle(void)
{
	int i;
	
	for (i = 1; i < MAX_HANDLES; ++i)
		if (!sys_handles[i].isOpen)
			return i;

	Sys_Error("out of handles");
	return -1;
}

static int filelength(FILE *f)
{
	int pos;
	int end;

	pos = ftell(f);
	fseek(f, 0, SEEK_END);
	end = ftell(f);
	fseek(f, pos, SEEK_SET);

	return end;
}

int Sys_FileOpenRead(char *path, int *hndl)
{
	FILE *f;
	int i;
	
	printf("Sys_FileOpenRead call: %s\n", path);

	i = findhandle();
	
	//mmap hook
	if (!strcmp(path, mmap_pak_path))
	{
		sys_handles[i].isOpen = true;
		sys_handles[i].mmapOffset = 0;
		
		*hndl = i;
		return mmap_pak_size;
	}

	f = fopen(path, "rb");
	if (!f)
	{
		*hndl = -1;
		return -1;
	}

	sys_handles[i].isOpen = true;
	sys_handles[i].file = f;

	*hndl = i;
	
	return filelength(f);
}

int Sys_FileOpenWrite(char *path)
{
	FILE *f;
	int i;
	
	printf("Sys_FileOpenWrite call: %s\n", path);

	if (!strcmp(path, mmap_pak_path))
		Sys_Error("attempt to write to mmaped pak file");

	i = findhandle();

	f = fopen(path, "wb");

	if (!f)
		Sys_Error ("Error opening %s: %s", path,strerror(errno));

	sys_handles[i].isOpen = true;
	sys_handles[i].file = f;
	
	return i;
}

void Sys_FileClose(int handle)
{
	if (sys_handles[handle].file != NULL)
	{
		fclose(sys_handles[handle].file);
		sys_handles[handle].file = NULL;
	}

	sys_handles[handle].isOpen = false;
}

void Sys_FileSeek(int handle, int position)
{
	assert(sys_handles[handle].isOpen);

	if (sys_handles[handle].file == NULL)
	{
		sys_handles[handle].mmapOffset = position;
		return;
	}

	fseek(sys_handles[handle].file, position, SEEK_SET);
}

int Sys_FileRead(int handle, void *dest, int count)
{
	assert(sys_handles[handle].isOpen);
	
	if (sys_handles[handle].file == NULL)
	{
		if (sys_handles[handle].mmapOffset >= mmap_pak_size)
			return 0;

		if ((sys_handles[handle].mmapOffset + count) > mmap_pak_size)
			count = mmap_pak_size - sys_handles[handle].mmapOffset;

		memcpy(dest, (const uint8_t*)mmap_pak + sys_handles[handle].mmapOffset, count);
		sys_handles[handle].mmapOffset += count;
		return count;
	}

	return fread(dest, 1, count, sys_handles[handle].file);
}

int Sys_FileWrite(int handle, void *data, int count)
{
	assert(sys_handles[handle].isOpen && sys_handles[handle].file != NULL);

	return fwrite(data, 1, count, sys_handles[handle].file);
}

int Sys_FileTime(char *path)
{
	FILE *f;
	
	if (!strcmp(path, mmap_pak_path))
		return 1;

	f = fopen(path, "rb");
	if (f)
	{
		fclose(f);
		return 1;
	}
	
	return -1;
}

void Sys_mkdir(char *path)
{    
    if (mkdir(path, 0755) != 0)
        printf("Sys_mkdir: unable to create %s: %s\n", path, strerror(errno));
}

/*
===============================================================================

SYSTEM IO

===============================================================================
*/

void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length)
{
	//something from very early days
}

void Sys_Error(char *error, ...)
{
	va_list         argptr;

	printf ("Sys_Error: ");   
	va_start (argptr,error);
	vprintf (error,argptr);
	va_end (argptr);
	printf ("\n");

	exit (1);
}

void Sys_Printf(char *fmt, ...)
{
	va_list         argptr;
	
	va_start (argptr,fmt);
	vprintf (fmt,argptr);
	va_end (argptr);
}

void Sys_Quit(void)
{	
	sys_quit = true;
}

double Sys_FloatTime(void)
{
	return ((uint64_t)esp_timer_get_time()) / 1000000.0;
}

char *Sys_ConsoleInput(void)
{
	return NULL;
}

void Sys_Sleep(void)
{
}

void Sys_SendKeyEvents(void)
{
}

void Sys_HighFPPrecision(void)
{
	//not a 486
}

void Sys_LowFPPrecision(void)
{
	//not a 486
}

FILE* quake_fopen(const char *name, const char *type)
{
	if (!strcmp(name, mmap_pak_path))
	{
		if (strcmp(type, "rb"))
			Sys_Error("quake_fopen: invalid mode %s for mmapped %s\n", type, name);

		return fmemopen(mmap_pak, mmap_pak_size, "rb");
	}

	return fopen(name, type);
}

//=============================================================================

static quakeparms_t parms;

void esp32_quake_main(int argc, char **argv, const char *pakPath, uint32_t pakSize, const void *pakMmap)
{
	printf("starting quake, heap at %lu, stack at %u\n",
			esp_get_free_heap_size(),
			uxTaskGetStackHighWaterMark(NULL));

	mmap_pak_path = pakPath;
	mmap_pak_size = pakSize;
	mmap_pak = pakMmap;

	parms.memsize = 6500*1024;
	parms.membase = malloc (parms.memsize);
	parms.basedir = "/";

	COM_InitArgv (argc, argv);

	parms.argc = com_argc;
	parms.argv = com_argv;

	printf ("Host_Init\n");
	Host_Init (&parms);

	double oldtime = Sys_FloatTime();

	while (!sys_quit)
	{
		double newtime = Sys_FloatTime();
		double time = newtime - oldtime;

		Host_Frame (time);

		oldtime = newtime;

		// printf("frame drawn, heap at %lu, stack at %u\n",
		// 	esp_get_free_heap_size(),
		// 	uxTaskGetStackHighWaterMark(NULL));

		//vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}


