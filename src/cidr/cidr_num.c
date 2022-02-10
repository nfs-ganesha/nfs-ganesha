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
 * Show some numbers
 */
#include "config.h"

#include <errno.h>
#include <string.h>

#include "cidr.h"
#include "cidr_pow2_p.h"

/* Number of total addresses in a given prefix length */
const char *cidr_numaddr_pflen(int pflen)
{

	if (pflen < 0 || pflen > 128) {
		errno = EINVAL;
		return (NULL);
	}
	return (__cidr_pow2[128 - pflen]);
}

/* Addresses in a CIDR block */
const char *cidr_numaddr(const CIDR * addr)
{
	int pflen;

	pflen = cidr_get_pflen(addr);
	if (addr->proto == CIDR_IPV4)
		pflen += 96;

	return (cidr_numaddr_pflen(pflen));
}

/* Hosts in a prefix length */
const char *cidr_numhost_pflen(int pflen)
{

	if (pflen < 0 || pflen > 128) {
		errno = EINVAL;
		return (NULL);
	}
	return (__cidr_pow2m2[128 - pflen]);
}

/* Addresses in a CIDR block */
const char *cidr_numhost(const CIDR * addr)
{
	int pflen;

	pflen = cidr_get_pflen(addr);
	if (addr->proto == CIDR_IPV4)
		pflen += 96;

	return (cidr_numhost_pflen(pflen));
}
