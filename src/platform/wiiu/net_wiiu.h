/*
net_wiiu.h - Wii U (nsysnet) network compatibility
Copyright (C) 2026 Half-Life Wii U port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#pragma once
#ifndef NET_WIIU_H
#define NET_WIIU_H

#include <stdint.h>
#include "platform/posix/net.h"

// The Wii U network stack is IPv4 only. The engine still references IPv6 types, so define enough
// of them to compile; IPv6 is switched off at runtime (net.allow_ip6 = false in net_ws.c).
#define XASH_NO_IPV6_RESOLVE 1

// wut's sockaddr_storage is only 16 bytes (it only knows sockaddr_in), but the engine copies
// sockaddr_in6 in and out of it. Use a full size one instead; nsysnet only ever sees it cast to
// struct sockaddr together with the real address length.
struct xash_sockaddr_storage
{
	sa_family_t ss_family;
	char ss_padding[126];
} __attribute__(( aligned( 8 )));
#define sockaddr_storage xash_sockaddr_storage

#ifndef AF_INET6
#define AF_INET6 10
#define PF_INET6 AF_INET6
#endif

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif

#ifndef IPV6_MULTICAST_LOOP
#define IPV6_MULTICAST_LOOP 19
#endif

#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 26
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

struct in6_addr
{
	uint8_t s6_addr[16];
};

struct sockaddr_in6
{
	sa_family_t     sin6_family;
	in_port_t       sin6_port;
	uint32_t        sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t        sin6_scope_id;
};

static const struct in6_addr in6addr_any;

#ifndef IN6_IS_ADDR_V4MAPPED
#define IN6_IS_ADDR_V4MAPPED( p ) ( 0 )
#endif

// No receive timeouts in nsysnet. The engine only uses SO_RCVTIMEO as a fallback when making a
// socket non-blocking fails, which ioctlsocket below doesn't do; an unknown option just fails.
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 0x1006
#endif

// ioctlsocket() is only used to make sockets non-blocking: use the nsysnet socket option for it
static inline int ioctl_wiiu( int fd, long req, unsigned long *arg )
{
	if( req == (long)FIONBIO )
	{
		int nonblock = *arg ? 1 : 0;
		return setsockopt( fd, SOL_SOCKET, SO_NONBLOCK, &nonblock, sizeof( nonblock ));
	}
	return ioctl( fd, req, arg );
}

#undef ioctlsocket
#define ioctlsocket ioctl_wiiu

#endif // NET_WIIU_H
