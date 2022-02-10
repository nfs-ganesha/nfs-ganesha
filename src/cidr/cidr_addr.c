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
 * Functions to generate various addresses based on a CIDR
 */
#include "config.h"

#include <errno.h>
#include <string.h>
#include "../include/cidr.h"

/* Create a network address */
CIDR *cidr_addr_network(const CIDR * addr)
{
	int i, j;
	CIDR *toret;

	toret = cidr_alloc();
	toret->proto = addr->proto;

	/* The netmask is the same */
	memcpy(toret->mask, addr->mask, (16 * sizeof(toret->mask[0])));

	/* Now just figure out the network address and spit it out */
	for (i = 0; i <= 15; i++) {
		for (j = 7; j >= 0; j--) {
			/* If we're into host bits, hop out */
			if ((addr->mask[i] & 1 << j) == 0)
				return (toret);

			/* Else, copy this network bit */
			toret->addr[i] |= (addr->addr[i] & 1 << j);
		}
	}

	/*
	 * We only get here on host (/32 or /128) addresses; shorter masks
	 * return earlier.  But it's as correct as can be to just say the
	 * same here, so...
	 */
	return (toret);
}

/* And a broadcast */
CIDR *cidr_addr_broadcast(const CIDR * addr)
{
	int i, j;
	CIDR *toret;

	toret = cidr_alloc();
	toret->proto = addr->proto;

	/* The netmask is the same */
	memcpy(toret->mask, addr->mask, (16 * sizeof(toret->mask[0])));

	/* Copy all the network bits */
	for (i = 0; i <= 15; i++) {
		for (j = 7; j >= 0; j--) {
			/* If we're into host bits, hop out */
			if ((addr->mask[i] & 1 << j) == 0)
				goto post;

			/* Else, copy this network bit */
			toret->addr[i] |= (addr->addr[i] & 1 << j);

		}
	}

 post:
	/* Now set the remaining bits to 1 */
	for ( /* i */ ; i <= 15; i++) {
		for ( /* j */ ; j >= 0; j--)
			toret->addr[i] |= (1 << j);

		j = 7;
	}

	/* And send it back */
	return (toret);
}

/* Get the first host in a CIDR block */
CIDR *cidr_addr_hostmin(const CIDR * addr)
{
	CIDR *toret;

	toret = cidr_addr_network(addr);
	if (toret == NULL)
		return (NULL);	/* Preserve errno */

	toret->addr[15] |= 1;

	return (toret);
}

/* Get the last host in a CIDR block */
CIDR *cidr_addr_hostmax(const CIDR * addr)
{
	CIDR *toret;

	toret = cidr_addr_broadcast(addr);
	if (toret == NULL)
		return (NULL);	/* Preserve errno */

	toret->addr[15] &= 0xfe;

	return (toret);
}
