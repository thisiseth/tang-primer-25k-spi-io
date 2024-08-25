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

uint8_t vid_buffer[BASEWIDTH*BASEHEIGHT];
short zbuffer[BASEWIDTH*BASEHEIGHT];
uint8_t surfcache[256*1024];

unsigned short d_8to16table[256];
unsigned d_8to24table[256];

static uint8_t *fpga_framebuffer, *fpga_palette;

static uint8_t current_palette[FPGA_DRIVER_PALETTE_SIZE_BYTES];
static int palette_set_count;

void VID_SetPalette (unsigned char *palette)
{
	memcpy(current_palette, palette, sizeof(current_palette));
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
	vid.buffer = vid.conbuffer = vid_buffer;
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

	memcpy(fpga_framebuffer, vid_buffer, sizeof(vid_buffer));
	fpga_driver_present_frame(&fpga_palette, &fpga_framebuffer, FPGA_DRIVER_VSYNC_WAIT_IF_PREVIOUS_NOT_PRESENTED);
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


