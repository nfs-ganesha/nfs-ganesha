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
 * Misc pieces
 */
#include "config.h"

#include "cidr.h"

static const char *__libcidr_version = CIDR_VERSION_STR;

/* Library version info */
const char *cidr_version(void)
{
	return (__libcidr_version);
}

/* Is a CIDR a v4-mapped IPv6 address? */
int cidr_is_v4mapped(const CIDR * addr)
{
	int i;

	if (addr->proto != CIDR_IPV6)
		return (-1);

	/* First 10 octets should be 0 */
	for (i = 0; i <= 9; i++)
		if (addr->addr[i] != 0)
			return (-1);

	/* Next 2 should be 0xff */
	for (i = 10; i <= 11; i++)
		if (addr->addr[i] != 0xff)
			return (-1);

	/* Then it is */
	return (0);
}
