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
 * Various libcidr memory-related functions
 */
#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "abstract_mem.h"
#include "cidr.h"

/* Allocate a struct cidr_addr */
CIDR *cidr_alloc(void)
{
	return gsh_calloc(1, sizeof(CIDR));
}

/* Duplicate a CIDR */
CIDR *cidr_dup(const CIDR * src)
{
	CIDR *toret;

	toret = cidr_alloc();

	memcpy(toret, src, sizeof(CIDR));

	return (toret);
}

/* Free a struct cidr_addr */
void cidr_free(CIDR * tofree)
{
	gsh_free(tofree);
}
