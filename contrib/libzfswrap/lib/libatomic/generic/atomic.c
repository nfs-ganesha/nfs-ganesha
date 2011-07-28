/*-
 * Copyright (c) 2008 Mike Hommey <glandium@debian.org>
 *
 * Derived from opensolaris_atomic.c
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdint.h>

#include <pthread.h>

static pthread_mutex_t atomic_mtx;

static __attribute__((constructor)) void
atomic_init(void)
{
	pthread_mutex_init(&atomic_mtx, NULL);
}

void
atomic_add_64(volatile uint64_t *target, int64_t delta)
{
	pthread_mutex_lock(&atomic_mtx);
	*target += delta;
	pthread_mutex_unlock(&atomic_mtx);
}

void
atomic_inc_64(volatile uint64_t *target)
{
	pthread_mutex_lock(&atomic_mtx);
	*target += 1;
	pthread_mutex_unlock(&atomic_mtx);
}

void
atomic_dec_64(volatile uint64_t *target)
{

	pthread_mutex_lock(&atomic_mtx);
	*target -= 1;
	pthread_mutex_unlock(&atomic_mtx);
}

uint64_t
atomic_add_64_nv(volatile uint64_t *target, int64_t delta)
{
	uint64_t newval;

	pthread_mutex_lock(&atomic_mtx);
	newval = (*target += delta);
	pthread_mutex_unlock(&atomic_mtx);
	return (newval);
}

void
atomic_add_32(volatile uint32_t *target, int32_t delta)
{
	pthread_mutex_lock(&atomic_mtx);
	*target += delta;
	pthread_mutex_unlock(&atomic_mtx);
}

void
atomic_inc_32(volatile uint32_t *target)
{
	pthread_mutex_lock(&atomic_mtx);
	*target += 1;
	pthread_mutex_unlock(&atomic_mtx);
}

void
atomic_dec_32(volatile uint32_t *target)
{
	pthread_mutex_lock(&atomic_mtx);
	*target -= 1;
	pthread_mutex_unlock(&atomic_mtx);
}

uint32_t
atomic_add_32_nv(volatile uint32_t *target, int32_t delta)
{
	uint32_t newval;

	pthread_mutex_lock(&atomic_mtx);
	newval = (*target += delta);
	pthread_mutex_unlock(&atomic_mtx);
	return (newval);
}

uint32_t
atomic_inc_32_nv(volatile uint32_t *target)
{
	uint32_t newval;

	pthread_mutex_lock(&atomic_mtx);
	newval = (*target += 1);
	pthread_mutex_unlock(&atomic_mtx);
	return (newval);
}

uint32_t
atomic_dec_32_nv(volatile uint32_t *target)
{
	uint32_t newval;

	pthread_mutex_lock(&atomic_mtx);
	newval = (*target -= 1);
	pthread_mutex_unlock(&atomic_mtx);
	return (newval);
}

void *
atomic_cas_ptr(volatile void *target, void *cmp,  void *newval)
{
	void *oldval, **trg;

	pthread_mutex_lock(&atomic_mtx);
	trg = ((void **)(uintptr_t)(volatile void *)(target));
	oldval = *trg;
	if (oldval == cmp)
		*trg = newval;
	pthread_mutex_unlock(&atomic_mtx);
	return (oldval);
}

uint64_t
atomic_cas_64(volatile uint64_t *target, uint64_t cmp, uint64_t newval)
{
	uint64_t oldval;

	pthread_mutex_lock(&atomic_mtx);
	oldval = *target;
	if (oldval == cmp)
		*target = newval;
	pthread_mutex_unlock(&atomic_mtx);
	return (oldval);
}

void
membar_producer(void)
{
	/* nothing */
}
