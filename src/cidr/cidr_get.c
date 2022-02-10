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
 * cidr_get - Get and return various semi-raw bits of info
 */
#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "cidr.h"
#include "abstract_mem.h"

/* Get the prefix length */
int cidr_get_pflen(const CIDR * block)
{
	int i, j;
	int foundnmh;
	int pflen;

	/* Where do we start? */
	if (block->proto == CIDR_IPV4)
		i = 12;
	else if (block->proto == CIDR_IPV6)
		i = 0;
	else {
		errno = ENOENT;	/* Bad errno */
		return (-1);	/* Unknown */
	}

	/*
	 * We're intentionally not supporting non-contiguous netmasks.  So,
	 * if we find one, bomb out.
	 */
	foundnmh = 0;
	pflen = 0;
	for ( /* i */ ; i <= 15; i++) {
		for (j = 7; j >= 0; j--) {
			if ((block->mask)[i] & (1 << j)) {
				/*
				 * This is a network bit (1).  If we've already seen a
				 * host bit (0), we need to bomb.
				 */
				if (foundnmh == 1) {
					errno = EINVAL;
					return (-1);
				}

				pflen++;
			} else
				foundnmh = 1;	/* A host bit */
		}
	}

	/* If we get here, return the length */
	return (pflen);
}

/* Get the address bits */
uint8_t *cidr_get_addr(const CIDR * addr)
{
	uint8_t *toret;

	toret = gsh_calloc(16, sizeof(uint8_t));

	/* Copy 'em in */
	memcpy(toret, addr->addr, sizeof(addr->addr));

	return (toret);
}

/* Get the netmask bits */
uint8_t *cidr_get_mask(const CIDR * addr)
{
	uint8_t *toret;

	toret = gsh_calloc(16, sizeof(uint8_t));

	/* Copy 'em in */
	memcpy(toret, addr->mask, sizeof(addr->mask));

	return (toret);
}

/* Get the protocol */
int cidr_get_proto(const CIDR * addr)
{
	return (addr->proto);
}
