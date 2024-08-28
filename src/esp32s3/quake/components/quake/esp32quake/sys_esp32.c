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
#include "errno.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "fpga_driver.h"
#include "fatfs_proxy.h"

#define MAX_HANDLES 10

//keys missing from quake keys.h ?
#define K_CAPSLOCK 0
#define K_PRTSCR 0
#define K_SCRLCK 0
#define K_NUMLOCK 0

#define KEYP_DIVIDE 0
#define KEYP_MULTIPLY 0
#define KEYP_MINUS 0
#define KEYP_PLUS 0
#define KEYP_ENTER 0
#define KEYP_1 0
#define KEYP_2 0
#define KEYP_3 0
#define KEYP_4 0
#define KEYP_5 0
#define KEYP_6 0
#define KEYP_7 0
#define KEYP_8 0
#define KEYP_9 0
#define KEYP_0 0
#define KEYP_PERIOD 0
#define KEYP_EQUALS 0

//borrowed from chocolate doom code
static const int scancode_to_quake_table[] = 
{                                            
    0,   0,   0,   0,   'a',                                  /* 0-9 */     
    'b', 'c', 'd', 'e', 'f',                                                
    'g', 'h', 'i', 'j', 'k',                                  /* 10-19 */   
    'l', 'm', 'n', 'o', 'p',                                                
    'q', 'r', 's', 't', 'u',                                  /* 20-29 */   
    'v', 'w', 'x', 'y', 'z',                                                
    '1', '2', '3', '4', '5',                                  /* 30-39 */   
    '6', '7', '8', '9', '0',                                                
    K_ENTER, K_ESCAPE, K_BACKSPACE, K_TAB, K_SPACE,           /* 40-49 */   
    '-', '=', '[', ']', '\\',                                  
    0,   ';', '\'', '`', ',',                                 /* 50-59 */   
    '.', '/', K_CAPSLOCK, K_F1, K_F2,                                 
    K_F3, K_F4, K_F5, K_F6, K_F7,                             /* 60-69 */   
    K_F8, K_F9, K_F10, K_F11, K_F12,                              
    K_PRTSCR, K_SCRLCK, K_PAUSE, K_INS, K_HOME,               /* 70-79 */   
    K_PGUP, K_DEL, K_END, K_PGDN, K_RIGHTARROW,                   
    K_LEFTARROW, K_DOWNARROW, K_UPARROW,                      /* 80-89 */   
    K_NUMLOCK, KEYP_DIVIDE,                                               
    KEYP_MULTIPLY, KEYP_MINUS, KEYP_PLUS, KEYP_ENTER, KEYP_1,               
    KEYP_2, KEYP_3, KEYP_4, KEYP_5, KEYP_6,                   /* 90-99 */  
    KEYP_7, KEYP_8, KEYP_9, KEYP_0, KEYP_PERIOD,                            
    0, 0, 0, KEYP_EQUALS                                      /* 100-103 */ 
};

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

static RingbufHandle_t hid_ringbuf;

static void hid_callback(fpga_driver_hid_event_t hidEvent)
{
    switch (hidEvent.type)
    {
        case FPGA_DRIVER_HID_EVENT_KEY_DOWN:
        case FPGA_DRIVER_HID_EVENT_KEY_UP:
        case FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_DOWN:
        case FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_UP:
            if (xRingbufferSend(hid_ringbuf, &hidEvent, sizeof(fpga_driver_hid_event_t), 0) != pdPASS)
                printf("hid ringbuf full!\n");

            break;
        default:
            break;
    }
}

static int translate_hid_key(uint8_t keyCode)
{
    switch (keyCode)
    {
        case FPGA_DRIVER_HID_KEY_CODE_LEFT_CTRL:
        case FPGA_DRIVER_HID_KEY_CODE_RIGHT_CTRL:
            return K_CTRL;
        case FPGA_DRIVER_HID_KEY_CODE_LEFT_SHIFT:
        case FPGA_DRIVER_HID_KEY_CODE_RIGHT_SHIFT:
            return K_SHIFT;
        case FPGA_DRIVER_HID_KEY_CODE_LEFT_ALT:
        case FPGA_DRIVER_HID_KEY_CODE_RIGHT_ALT:
            return K_ALT;
        default:
            if (keyCode < (sizeof(scancode_to_quake_table)/sizeof(scancode_to_quake_table[0])))
                return scancode_to_quake_table[keyCode];
            else
                return 0;
    }
}

// quake stdio wrappers - had to do these to allow proxying the calls to fatfs_proxy 
//- separate 'thread' (task) that does work in cache-disable context
//as opposed to quake main task which has stack on PSRAM - spiflash operations are not allowed in this case

struct QUAKE_FILE
{
    FILE *file;
    bool isMemoryFile;
};

QUAKE_FILE* quake_fopen(const char* restrict name, const char* restrict type)
{
    FILE *file;
    bool isMemoryFile;

    if (strcmp(name, mmap_pak_path) == 0)
    {
        if (strcmp(type, "rb") != 0)
            Sys_Error("quake_fopen: invalid mode %s for mmapped %s\n", type, name);

        isMemoryFile = true;
        file = fmemopen((void*)mmap_pak, mmap_pak_size, "rb");
    }
    else
    {
        isMemoryFile = false;
        file = fatfs_proxy_fopen(name, type);
    }

    if (file == NULL)
        return NULL;

    QUAKE_FILE *qfile = malloc(sizeof(QUAKE_FILE));

    if (qfile == NULL)
        Sys_Error("quake_fopen: malloc for QUAKE_FILE failed");

    qfile->file = file;
    qfile->isMemoryFile = isMemoryFile;

    return qfile;
}

int	quake_fclose(QUAKE_FILE *qfile)
{
    int ret;

    if (qfile->isMemoryFile)
        ret = fclose(qfile->file);
    else   
        ret = fatfs_proxy_fclose(qfile->file);

    free(qfile);
    return ret;
}

int quake_fseek(QUAKE_FILE *qfile, long pos, int type)
{
    int ret;

    if (qfile->isMemoryFile)
        ret = fseek(qfile->file, pos, type);
    else   
        ret = fatfs_proxy_fseek(qfile->file, pos, type);

    return ret;
}

size_t quake_fread(void* restrict buf, size_t size, size_t n, QUAKE_FILE* restrict qfile)
{
    size_t ret;

    if (qfile->isMemoryFile)
        ret = fread(buf, size, n, qfile->file);
    else   
        ret = fatfs_proxy_fread(buf, size, n, qfile->file);

    return ret;
}

size_t quake_fwrite(const void* restrict buf, size_t size, size_t n, QUAKE_FILE *qfile)
{
    size_t ret;
    
    if (qfile->isMemoryFile)
        ret = fwrite(buf, size, n, qfile->file);
    else   
        ret = fatfs_proxy_fwrite(buf, size, n, qfile->file);

    return ret;
}

int quake_fprintf(QUAKE_FILE* restrict qfile, const char* restrict fmt, ...)
{
    int ret;
    va_list args;

    va_start(args, fmt);

    if (qfile->isMemoryFile)
        ret = vfprintf(qfile->file, fmt, args);
    else   
        ret = fatfs_proxy_vfprintf(qfile->file, fmt, args);

    va_end(args);

    return ret;
}

int quake_fscanf(QUAKE_FILE* restrict qfile, const char* restrict fmt, ...)
{
    int ret;
    va_list args;

    va_start(args, fmt);

    if (qfile->isMemoryFile)
        ret = vfscanf(qfile->file, fmt, args);
    else   
        ret = fatfs_proxy_vfscanf(qfile->file, fmt, args);

    va_end(args);

    return ret;
}

int quake_fgetc(QUAKE_FILE *qfile)
{
    int ret;

    if (qfile->isMemoryFile)
        ret = fgetc(qfile->file);
    else
        ret = fatfs_proxy_fgetc(qfile->file);

    return ret;
}

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

    pos = fatfs_proxy_ftell(f);
    fatfs_proxy_fseek(f, 0, SEEK_END);
    end = fatfs_proxy_ftell(f);
    fatfs_proxy_fseek(f, pos, SEEK_SET);

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

    f = fatfs_proxy_fopen(path, "rb");
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

    f = fatfs_proxy_fopen(path, "wb");

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
        fatfs_proxy_fclose(sys_handles[handle].file);
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

    fatfs_proxy_fseek(sys_handles[handle].file, position, SEEK_SET);
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

    return fatfs_proxy_fread(dest, 1, count, sys_handles[handle].file);
}

int Sys_FileWrite(int handle, void *data, int count)
{
    assert(sys_handles[handle].isOpen && sys_handles[handle].file != NULL);

    return fatfs_proxy_fwrite(data, 1, count, sys_handles[handle].file);
}

int Sys_FileTime(char *path)
{
    FILE *f;
    
    if (!strcmp(path, mmap_pak_path))
        return 1;

    f = fatfs_proxy_fopen(path, "rb");
    if (f)
    {
        fatfs_proxy_fclose(f);
        return 1;
    }
    
    return -1;
}

void Sys_mkdir(char *path)
{    
    if (fatfs_proxy_mkdir(path, 0755) != 0)
        printf("Sys_mkdir: unable to create %s: %s\n", path, strerror(errno));
}

const void* Sys_FileGetMmapBase(int handle)
{
    assert(sys_handles[handle].isOpen);

    return sys_handles[handle].file == NULL ? mmap_pak : NULL;
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

    abort();
}

void Sys_Printf(char *fmt, ...)
{
    va_list argptr;
    
    va_start(argptr,fmt);
    vprintf(fmt,argptr);
    va_end(argptr);
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
    fpga_driver_hid_event_t* hid_event;
    size_t item_size;

    while ((hid_event = (fpga_driver_hid_event_t*)xRingbufferReceive(hid_ringbuf, &item_size, 0)) != NULL)
    {
        assert(item_size == sizeof(fpga_driver_hid_event_t));

        switch (hid_event->type)
        {
            case FPGA_DRIVER_HID_EVENT_KEY_DOWN:
            case FPGA_DRIVER_HID_EVENT_KEY_UP:
                Key_Event(translate_hid_key(hid_event->keyEvent.keyCode), 
                          hid_event->type == FPGA_DRIVER_HID_EVENT_KEY_DOWN);
                break;
            case FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_DOWN:
            case FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_UP:  
                Key_Event(K_MOUSE1 + __builtin_ctz(hid_event->mouseButtonEvent.buttonCode), 
                          hid_event->type == FPGA_DRIVER_HID_EVENT_MOUSE_BUTTON_DOWN);
                break;

            default:
                break;
        }

        vRingbufferReturnItem(hid_ringbuf, hid_event);
    }
}

void Sys_HighFPPrecision(void)
{
    //not a 486
}

void Sys_LowFPPrecision(void)
{
    //not a 486
}

//=============================================================================

static quakeparms_t parms;

void esp32_quake_main(int argc, char **argv, const char *basedir, const char *pakPath, uint32_t pakSize, const void *pakMmap)
{
    mmap_pak_path = pakPath;
    mmap_pak_size = pakSize;
    mmap_pak = pakMmap;

    parms.memsize = 6800*1024;
    parms.membase = malloc(parms.memsize);
    parms.basedir = (char*)basedir;

    if (parms.membase == NULL)
        Sys_Error("membase allocation failed\n");

    COM_InitArgv(argc, argv);

    parms.argc = com_argc;
    parms.argv = com_argv;

    printf ("Host_Init\n");
    Host_Init(&parms);
    
    hid_ringbuf = xRingbufferCreateWithCaps((sizeof(fpga_driver_hid_event_t)+8)*128, RINGBUF_TYPE_NOSPLIT, MALLOC_CAP_INTERNAL);

    if (hid_ringbuf == NULL)
        Sys_Error("unable to allocate hid_ringbuf\n");

    fpga_driver_register_hid_event_cb(hid_callback);

    ////////////////

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

    fpga_driver_register_hid_event_cb(NULL);
}


