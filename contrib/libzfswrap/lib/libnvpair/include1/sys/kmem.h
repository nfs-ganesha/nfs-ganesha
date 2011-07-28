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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Copyright 2006 Ricardo Correia.
 * Use is subject to license terms.
 */

#ifndef _SYS_KMEM_H
#define _SYS_KMEM_H

#include <sys/types.h>
#include <umem.h>

/* Maximum zfs-fuse memory usage */
#define ZFSFUSE_MAX_ARCSIZE (128<<20)

/*
 * Kernel memory
 */
#define KM_SLEEP    UMEM_NOFAIL
#define KM_PUSHPAGE KM_SLEEP
#define KM_NOSLEEP  UMEM_DEFAULT
#define KMC_NODEBUG UMC_NODEBUG

typedef umem_cache_t kmem_cache_t;

#if 0
extern void *kmem_alloc(size_t size, int kmflags);
extern void *kmem_zalloc(size_t size, int kmflags);
extern void kmem_free(void *buf, size_t size);
extern void *kmem_cache_alloc(kmem_cache_t *, int);
extern void kmem_cache_free(kmem_cache_t *, void *);
#endif

#define kmem_alloc(a,b) umem_alloc(a,b)
#define kmem_zalloc(a,b) umem_zalloc(a,b)
#define kmem_free(a,b) umem_free(a,b)
#define kmem_cache_alloc(a,b) umem_cache_alloc(a,b)
#define kmem_cache_free(a,b) umem_cache_free(a,b)

#define kmem_cache_create(_a, _b, _c, _d, _e, _f, _g, _h, _i) \
    umem_cache_create(_a, _b, _c, _d, _e, _f, _g, _h, _i)
#define kmem_cache_destroy(_c) umem_cache_destroy(_c)

#define kmem_debugging() 0
#define kmem_cache_reap_now(c)

extern uint64_t get_real_memusage();

#endif
