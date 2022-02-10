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
 * Functions to generate various networks based on a CIDR
 */
#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "cidr.h"
#include "abstract_mem.h"

/* Get the CIDR's immediate supernet */
CIDR *cidr_net_supernet(const CIDR * addr)
{
	int i, j;
	int pflen;
	CIDR *toret;

	/* If it's already a /0 in its protocol, return nothing */
	pflen = cidr_get_pflen(addr);
	if (pflen == 0) {
		errno = 0;
		return (NULL);
	}

	toret = cidr_dup(addr);
	if (toret == NULL)
		return (NULL);	/* Preserve errno */

	/* Chop a bit off the netmask */
	/* This gets the last network bit */
	if (toret->proto == CIDR_IPV4)
		pflen += 96;
	pflen--;
	i = pflen / 8;
	j = 7 - (pflen % 8);

	/* Make that bit a host bit */
	(toret->mask)[i] &= ~(1 << j);

	/*
	 * Now zero out the host bits in the addr.  Do this manually instead
	 * of calling cidr_addr_network() to save some extra copies and
	 * allocationss and so forth.
	 */
	for ( /* i */ ; i <= 15; i++) {
		for ( /* j */ ; j >= 0; j--)
			(toret->addr)[i] &= ~(1 << j);
		j = 7;
	}

	/* And send it back */
	return (toret);
}

/* Get the CIDR's two children */
CIDR **cidr_net_subnets(const CIDR * addr)
{
	int i, j;
	int pflen;
	CIDR **toret;

	/* You can't split a host address! */
	pflen = cidr_get_pflen(addr);
	if ((addr->proto == CIDR_IPV4 && pflen == 32)
	    || (addr->proto == CIDR_IPV6 && pflen == 128)) {
		errno = 0;
		return (NULL);
	}

	toret = gsh_calloc(2, sizeof(CIDR *));

	/* Get a blank-ish slate for the first kid */
	toret[0] = cidr_addr_network(addr);
	if (toret[0] == NULL) {
		gsh_free(toret);
		return (NULL);	/* Preserve errno */
	}

	/* Find its first host bit */
	if (toret[0]->proto == CIDR_IPV4)
		pflen += 96;
	i = pflen / 8;
	j = 7 - (pflen % 8);

	/* Make it a network bit */
	(toret[0])->mask[i] |= 1 << j;

	/* Now dup the second kid off that */
	toret[1] = cidr_dup(toret[0]);
	if (toret[1] == NULL) {
		cidr_free(toret[0]);
		gsh_free(toret);
		return (NULL);	/* Preserve errno */
	}

	/* And set that first host bit */
	(toret[1])->addr[i] |= 1 << j;

	/* Return the pair */
	return (toret);
}
