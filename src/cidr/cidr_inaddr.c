// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2005, 2006
 * Matthew D. Fuller <fullermd@over-yonder.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Functions to convert to/from in[6]_addr structs
 */
#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "abstract_mem.h"
#include "cidr.h"

/* Create a struct in_addr with the given v4 address */
struct in_addr *cidr_to_inaddr(const CIDR * addr, struct in_addr *uptr)
{
	struct in_addr *toret;

	/* Better be a v4 address... */
	if (addr->proto != CIDR_IPV4) {
		errno = EPROTOTYPE;
		return (NULL);
	}

	/*
	 * Use the user's struct if possible, otherwise allocate one.  It's
	 * _their_ responsibility to give us the right type of struct to not
	 * stomp all over the address space...
	 */
	toret = uptr;
	if (toret == NULL)
		toret = gsh_calloc(1, sizeof(struct in_addr));

	/* Add 'em up and stuff 'em in */
	toret->s_addr = ((addr->addr)[12] << 24)
	    + ((addr->addr)[13] << 16)
	    + ((addr->addr)[14] << 8)
	    + ((addr->addr)[15]);

	/*
	 * in_addr's are USUALLY used inside sockaddr_in's to do socket
	 * stuff.  The upshot of this is that they generally need to be in
	 * network byte order.  We'll do that transition here; if the user
	 * wants to be different, they'll have to manually convert.
	 */
	toret->s_addr = htonl(toret->s_addr);

	return (toret);
}

/* Build up a CIDR struct from a given in_addr */
CIDR *cidr_from_inaddr(const struct in_addr * uaddr)
{
	int i;
	CIDR *toret;
	in_addr_t taddr;

	toret = cidr_alloc();
	toret->proto = CIDR_IPV4;

	/*
	 * For IPv4, pretty straightforward, except that we need to jump
	 * through a temp variable to convert into host byte order.
	 */
	taddr = ntohl(uaddr->s_addr);

	/* Mask these just to be safe */
	toret->addr[15] = (taddr & 0xff);
	toret->addr[14] = ((taddr >> 8) & 0xff);
	toret->addr[13] = ((taddr >> 16) & 0xff);
	toret->addr[12] = ((taddr >> 24) & 0xff);

	/* Give it a single-host mask */
	toret->mask[15] = toret->mask[14] = toret->mask[13] = toret->mask[12] =
	    0xff;

	/* Standard v4 overrides of addr and mask for mapped form */
	for (i = 0; i <= 9; i++)
		toret->addr[i] = 0;
	for (i = 10; i <= 11; i++)
		toret->addr[i] = 0xff;
	for (i = 0; i <= 11; i++)
		toret->mask[i] = 0xff;

	/* That's it */
	return (toret);
}

/* Create a struct in5_addr with the given v6 address */
struct in6_addr *cidr_to_in6addr(const CIDR * addr, struct in6_addr *uptr)
{
	struct in6_addr *toret;
	int i;

	/*
	 * Note: We're allowing BOTH IPv4 and IPv6 addresses to go through
	 * this function.  The reason is that this allows us to build up an
	 * in6_addr struct to be used to connect to a v4 host (via a
	 * v4-mapped address) through a v6 socket connection.  A v4
	 * cidr_address, when built, has the upper bits of the address set
	 * correctly for this to work.  We don't support "compat"-mode
	 * addresses here, though, and won't.
	 */
	if (addr->proto != CIDR_IPV6 && addr->proto != CIDR_IPV4) {
		errno = EPROTOTYPE;
		return (NULL);
	}

	/* Use their struct if they gave us one */
	toret = uptr;
	if (toret == NULL)
		toret = gsh_calloc(1, sizeof(struct in6_addr));

	/*
	 * The in6_addr is defined to store it in 16 octets, just like we do.
	 * But just to be safe, we're not going to stuff a giant copy in.
	 * Most systems also use some union trickery to force alignment, but
	 * we don't need to worry about that.
	 * Now, this is defined to be in network byte order, which is
	 * MSB-first.  Since this is a structure of bytes, and we're filling
	 * them in from the MSB onward ourself, we don't actually have to do
	 * any conversions.
	 */
	for (i = 0; i <= 15; i++)
		toret->s6_addr[i] = addr->addr[i];

	return (toret);
}

/* And create up a CIDR struct from a given in6_addr */
CIDR *cidr_from_in6addr(const struct in6_addr * uaddr)
{
	int i;
	CIDR *toret;

	toret = cidr_alloc();
	toret->proto = CIDR_IPV6;

	/*
	 * For v6, just iterate over the arrays and return.  Set all 1's in
	 * the mask while we're at it, since this is a single host.
	 */
	for (i = 0; i <= 15; i++) {
		toret->addr[i] = uaddr->s6_addr[i];
		toret->mask[i] = 0xff;
	}

	return (toret);
}
