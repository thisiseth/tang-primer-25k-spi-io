//
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
//     Networking module which uses SDL_net
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "doomtype.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"
#include "net_defs.h"
#include "net_io.h"
#include "net_packet.h"
#include "net_sdl.h"
#include "z_zone.h"

//
// NETWORKING
//


// no-op implementation


static boolean NET_NULL_InitClient(void)
{
    return false;
}


static boolean NET_NULL_InitServer(void)
{
    return false;
}


static void NET_NULL_SendPacket(net_addr_t *addr, net_packet_t *packet)
{
}


static boolean NET_NULL_RecvPacket(net_addr_t **addr, net_packet_t **packet)
{
    return false;
}


static void NET_NULL_AddrToString(net_addr_t *addr, char *buffer, int buffer_len)
{

}


static void NET_NULL_FreeAddress(net_addr_t *addr)
{
}


net_addr_t *NET_NULL_ResolveAddress(const char *address)
{
    return NULL;
}


net_module_t net_esp32_module =
{
    NET_NULL_InitClient,
    NET_NULL_InitServer,
    NET_NULL_SendPacket,
    NET_NULL_RecvPacket,
    NET_NULL_AddrToString,
    NET_NULL_FreeAddress,
    NET_NULL_ResolveAddress,
};