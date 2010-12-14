/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/cmn_err.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <umem.h>

#define MAX_PREFIX_SIZE 10

static char ce_prefix[CE_IGNORE][MAX_PREFIX_SIZE] = { "", "NOTICE: ", "WARNING: ", "ERROR: " };
static int ce_level[CE_IGNORE] = { LOG_INFO, LOG_NOTICE, LOG_WARNING, LOG_ERR};

void
vpanic(const char *fmt, va_list adx)
{
	int slen = strlen(fmt) + MAX_PREFIX_SIZE + 1;
	char *real_fmt = umem_alloc(slen, UMEM_NOFAIL);

	(void) strcpy(real_fmt, ce_prefix[CE_PANIC]);
	(void) strcat(real_fmt, fmt);

	vsyslog(LOG_DAEMON | ce_level[CE_PANIC], real_fmt, adx);

	umem_free(real_fmt, slen);

	abort();	/* think of it as a "user-level crash dump" */
}

void
panic(const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vpanic(fmt, adx);
	va_end(adx);
}

void
vcmn_err(int ce, const char *fmt, va_list adx)
{
	int slen;
	char *real_fmt;

	if (ce == CE_PANIC)
		vpanic(fmt, adx);

	slen = strlen(fmt) + MAX_PREFIX_SIZE + 1;
	real_fmt = umem_alloc(slen, UMEM_NOFAIL);

	(void) strcpy(real_fmt, ce_prefix[ce]);
	(void) strcat(real_fmt, fmt);

	vsyslog(LOG_DAEMON | ce_level[ce], real_fmt, adx);

	umem_free(real_fmt, slen);
}

/*PRINTFLIKE2*/
void
cmn_err(int ce, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_err(ce, fmt, adx);
	va_end(adx);
}
