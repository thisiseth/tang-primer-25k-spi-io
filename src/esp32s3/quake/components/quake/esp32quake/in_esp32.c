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
#include "fpga_driver.h"

static int32_t prev_mouse_x, prev_mouse_y, prev_mouse_wheel;

void IN_Init(void)
{
    fpga_driver_hid_status_t hidStatus;

    fpga_driver_hid_get_status(&hidStatus);

    prev_mouse_x = hidStatus.mouseX;
    prev_mouse_y = hidStatus.mouseY;
    prev_mouse_wheel = hidStatus.mouseWheel;
}

void IN_Shutdown (void)
{
}

void IN_Commands (void)
{
}

void IN_Move (usercmd_t *cmd)
{
    fpga_driver_hid_status_t hidStatus;

    fpga_driver_hid_get_status(&hidStatus);

    float mouse_x = hidStatus.mouseX - prev_mouse_x, 
          mouse_y = hidStatus.mouseY - prev_mouse_y;

    prev_mouse_x = hidStatus.mouseX;
    prev_mouse_y = hidStatus.mouseY;
    
    mouse_x *= sensitivity.value;
    mouse_y *= sensitivity.value;

    // add mouse X/Y movement to cmd
    if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
        cmd->sidemove += m_side.value * mouse_x;
    else
        cl.viewangles[YAW] -= m_yaw.value * mouse_x;
    
    if (in_mlook.state & 1)
        V_StopPitchDrift ();
        
    if ((in_mlook.state & 1) && !(in_strafe.state & 1))
    {
        cl.viewangles[PITCH] += m_pitch.value * mouse_y;
        if (cl.viewangles[PITCH] > 80)
            cl.viewangles[PITCH] = 80;
        if (cl.viewangles[PITCH] < -70)
            cl.viewangles[PITCH] = -70;
    }
    else
    {
        if ((in_strafe.state & 1) && noclip_anglehack)
            cmd->upmove -= m_forward.value * mouse_y;
        else
            cmd->forwardmove -= m_forward.value * mouse_y;
    }
}

