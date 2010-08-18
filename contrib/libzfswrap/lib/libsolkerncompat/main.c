/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2006 Ricardo Correia.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/policy.h>
#include <sys/kmem.h>
#include <sys/utsname.h>

#include <stdio.h>
#include <unistd.h>
#include <strings.h>

#include "vfs.h"

char hw_serial[11];

int ncpus;
uint64_t physmem;
unsigned long _pagesize;
unsigned int _pageshift;
kmem_cache_t *vnode_cache;
extern void system_taskq_init();

void libsolkerncompat_init()
{
	/* LINUX */
	ncpus = sysconf(_SC_NPROCESSORS_CONF);
	physmem = sysconf(_SC_PHYS_PAGES);
	_pagesize = sysconf(_SC_PAGESIZE);
	_pageshift = ffs(_pagesize) - 1;
	pwd_buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
	grp_buflen = sysconf(_SC_GETGR_R_SIZE_MAX);

	uname(&utsname);
	snprintf(hw_serial, sizeof(hw_serial), "%ld", gethostid());

	VERIFY(ncpus > 0 && physmem > 0);

#ifdef DEBUG
	printf("hostname = %s\n", utsname.nodename);
	printf("hw_serial = %s\n", hw_serial);
	printf("ncpus = %i\n", ncpus);
	printf("physmem = %llu pages (%.2f GB)\n", (unsigned long long) physmem, (double) physmem * sysconf(_SC_PAGE_SIZE) / (1ULL << 30));
	printf("pagesize = %li, pageshift: %i\n", _pagesize, _pageshift);
	printf("pwd_buflen = %li, grp_buflen = %li\n\n", pwd_buflen, grp_buflen);
#endif

	vnode_cache = kmem_cache_create("vnode_t", sizeof(vnode_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	VERIFY(vnode_cache != NULL);

	vfs_init();

	/* Carefull here : umem_init is called on another core when using a multi core cpu
	 * but it must have finished when calling taskq_init.
	 * My tests with my dual core laptop is ok, but I am not sure it works everywhere */
	taskq_init();
	system_taskq_init();
}

void libsolkerncompat_exit()
{
	kmem_cache_destroy(vnode_cache);

	vfs_exit();
	taskq_destroy(system_taskq);
}
