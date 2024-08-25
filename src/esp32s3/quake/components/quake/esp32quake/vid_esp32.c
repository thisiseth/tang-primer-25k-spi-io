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
// vid_null.c -- null video driver to aid porting efforts

#include "quakedef.h"
#include "d_local.h"

#include "esp_attr.h"
#include "fpga_driver.h"

viddef_t	vid;				// global video state

#define	BASEWIDTH	FPGA_DRIVER_FRAME_WIDTH
#define	BASEHEIGHT	FPGA_DRIVER_FRAME_HEIGHT

static short zbuffer[BASEWIDTH*BASEHEIGHT];
static uint8_t surfcache[256*1024];

//not used
const unsigned short * const d_8to16table = NULL;
const unsigned * const d_8to24table = NULL;

static uint8_t *fpga_framebuffer, *fpga_palette;

static DMA_ATTR uint8_t current_palette[FPGA_DRIVER_PALETTE_SIZE_BYTES];
static int palette_set_count;

void VID_SetPalette (unsigned char *palette)
{
    memcpy(current_palette, palette, FPGA_DRIVER_PALETTE_SIZE_BYTES);
    palette_set_count = 2;
}

void VID_ShiftPalette(unsigned char *p)
{
    VID_SetPalette(p);
}

void VID_Init(unsigned char *palette)
{
    fpga_driver_get_framebuffer(&fpga_palette, &fpga_framebuffer);

    vid.width = vid.conwidth = BASEWIDTH;
    vid.height = vid.conheight = BASEHEIGHT;
    vid.rowbytes = vid.conrowbytes = BASEWIDTH;
    vid.aspect = 1.0;
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
    vid.buffer = vid.conbuffer = fpga_framebuffer;
    vid.maxwarpwidth = WARP_WIDTH; //dont know why but its like this for all drivers
    vid.maxwarpheight = WARP_HEIGHT;
    
    d_pzbuffer = zbuffer;
    D_InitCaches (surfcache, sizeof(surfcache));
}

void VID_Shutdown (void)
{
}

void VID_Update (vrect_t *rects)
{
    if (palette_set_count > 0)
    {
        memcpy(fpga_palette, current_palette, sizeof(current_palette));
        --palette_set_count;
    }

    const void *oldBuffer = fpga_framebuffer;

    fpga_driver_present_frame(&fpga_palette, &fpga_framebuffer, FPGA_DRIVER_VSYNC_WAIT_IF_PREVIOUS_NOT_PRESENTED);

    memcpy(fpga_framebuffer, oldBuffer, FPGA_DRIVER_FRAMEBUFFER_SIZE_BYTES); //well.. i dont want to understand how to hook up proper buffer switching here
    vid.buffer = vid.conbuffer = fpga_framebuffer;
}

/*
================
D_BeginDirectRect
================
*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
}

/*
================
D_EndDirectRect
================
*/
void D_EndDirectRect (int x, int y, int width, int height)
{
}


