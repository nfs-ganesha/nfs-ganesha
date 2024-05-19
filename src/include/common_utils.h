/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * @file common_utils.h
 * @brief Common tools for printing, parsing, ....
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

#ifndef COMMON_UTILS_H
#define COMMON_UTILS_H

#include <sys/mman.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <netdb.h>

#include "abstract_atomic.h"
#include "gsh_types.h"
#include "log.h"

extern pthread_mutexattr_t default_mutex_attr;
extern pthread_rwlockattr_t default_rwlock_attr;

/**
 * BUILD_BUG_ON - break compile if a condition is true.
 * @condition: the condition which the compiler should know is false.
 *
 * If you have some code which relies on certain constants being equal, or
 * other compile-time-evaluated condition, you should use BUILD_BUG_ON to
 * detect if someone changes it.
 *
 * The implementation uses gcc's reluctance to create a negative array, but
 * gcc (as of 4.4) only emits that error for obvious cases (eg. not arguments
 * to inline functions).  So as a fallback we use the optimizer; if it can't
 * prove the condition is false, it will cause a link error on the undefined
 * "__build_bug_on_failed".  This error message can be harder to track down
 * though, hence the two different methods.
 *
 * Blatantly stolen from kernel source, include/linux/kernel.h:651
 */
#ifndef __OPTIMIZE__
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#else
extern int __build_bug_on_failed;
#define BUILD_BUG_ON(condition)					\
	do {							\
		((void)sizeof(char[1 - 2*!!(condition)]));      \
		if (condition)					\
			__build_bug_on_failed = 1;		\
	} while (0)
#endif

/* Most machines scandir callback requires a const. But not all */
#define SCANDIR_CONST const

/* Most machines have mntent.h. */
#define HAVE_MNTENT_H 1

/* String parsing functions */

#ifndef HAVE_STRLCPY
extern size_t strlcpy(char *dst, const char *src, size_t siz);
#endif

#ifndef HAVE_STRNLEN
#define strnlen(a, b) gsh_strnlen(a, b)
/* prefix with gsh_ to prevent library conflict -- will fix properly
   with new build system */
extern size_t gsh_strnlen(const char *s, size_t max);
#endif

#if defined(__APPLE__)
#define pthread_yield() pthread_yield_np()
#undef SCANDIR_CONST
#define SCANDIR_CONST
#undef HAVE_MNTENT_H
#endif

#if defined(__FreeBSD__)
#undef SCANDIR_CONST
#define SCANDIR_CONST
#endif

#ifndef ARRAY_SIZE
/* Assumes a is an array */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#endif

#ifndef CONCAT
#define CONCAT_(A, B) A##B
/** Macro-concatenate A and B (using ##, avoids macro expansion order issues) */
#define CONCAT(A, B)  CONCAT_(A, B)
#endif

extern unsigned long PTHREAD_stack_size;

static inline int PTHREAD_create(pthread_t *thread,
				 pthread_attr_t *attr,
				 void *(*start_routine)(void *),
				 void *arg)
{
	pthread_attr_t scratch;
	pthread_attr_t *pattr = (attr == NULL ? &scratch : attr);

	if (attr == NULL)
		pthread_attr_init(&scratch);

	/*
	 * glibc's current default stacksize is 8M
	 */
	(void) pthread_attr_setstacksize(pattr, PTHREAD_stack_size);
	return pthread_create(thread, pattr, start_routine, arg);
}


/**
 * @brief Logging pthread attribute initialization
 *
 * @param[in,out] _attr The attributes to initialize
 */
#define PTHREAD_ATTR_init(_attr)					\
	do {								\
		int rc;							\
									\
		rc = pthread_attr_init(_attr);				\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Init pthread attr %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, pthread attr init %p (%s) "	\
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging pthread attribute destruction
 *
 * @param[in,out] _attr The attributes to initialize
 */
#define PTHREAD_ATTR_destroy(_attr)					\
	do {								\
		int rc;							\
									\
		rc = pthread_attr_destroy(_attr);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Destroy pthread attr %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, pthread attr destroy %p (%s) " \
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging pthread attr set scope
 *
 * @param[in,out] _attr   The attributes to update
 * @param[in]     _scope  The scope to set
 */
#define PTHREAD_ATTR_setscope(_attr, _scope)				\
	do {								\
		int rc;							\
									\
		rc = pthread_attr_setscope(_attr, _scope);		\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "pthread_attr_setscope %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, pthread_attr_setscope %p (%s) " \
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging pthread attr set detach state
 *
 * @param[in,out] _attr    The attributes to update
 * @param[in]     _detach  The detach type to set
 */
#define PTHREAD_ATTR_setdetachstate(_attr, _detach)			\
	do {								\
		int rc;							\
									\
		rc = pthread_attr_setdetachstate(_attr, _detach);	\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "pthread_attr_setdetachstate %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, pthread_attr_setdetachstate %p (%s) " \
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging pthread attr set stack size
 *
 * @param[in,out] _attr    The attributes to update
 * @param[in]     _detach  The detach type to set
 */
#define PTHREAD_ATTR_setstacksize(_attr, _detach)			\
	do {								\
		int rc;							\
									\
		rc = pthread_attr_setstacksize(_attr, _detach);	\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "pthread_attr_setstacksize %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, pthread_attr_setstacksize %p (%s) " \
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging pthread rwlock attribute initialization
 *
 * @param[in,out] _attr The attributes to initialize
 */
#define PTHREAD_RWLOCKATTR_init(_attr)					\
	do {								\
		int rc;							\
									\
		rc = pthread_rwlockattr_init(_attr);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Init rwlockattr %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, rwlockattr init %p (%s) "	\
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging pthread rwlock set kind
 *
 * @param[in,out] _attr The attributes to update
 * @param[in]     _kind The kind to set
 */
#if __GLIBC__
#define PTHREAD_RWLOCKATTR_setkind_np(_attr, _kind)			\
	do {								\
		int rc;							\
									\
		rc = pthread_rwlockattr_setkind_np(_attr, _kind);	\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "pthread_rwlockattr_setkind_np %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, rwlockattr setkind_np %p (%s) " \
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)
#else
#define PTHREAD_RWLOCKATTR_setkind_np(_attr, _kind) /**/
#endif

/**
 * @brief Logging pthread rwlock attribute destruction
 *
 * @param[in,out] _attr The attributes to initialize
 */
#define PTHREAD_RWLOCKATTR_destroy(_attr)				\
	do {								\
		int rc;							\
									\
		rc = pthread_rwlockattr_destroy(_attr);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Destroy rwlockattr %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, rwlockattr destroy %p (%s) " \
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging rwlock initialization
 *
 * @param[in,out] _lock The rwlock to initialize
 * @param[in,out] _attr The attributes used while initializing the lock
 */
#define PTHREAD_RWLOCK_init(_lock, _attr)				\
	do {								\
		int rc;							\
		pthread_rwlockattr_t *attr = _attr;			\
									\
		if (attr == NULL)					\
			attr = &default_rwlock_attr;			\
									\
		rc = pthread_rwlock_init(_lock, attr);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Init rwlock %p (%s) at %s:%d",	\
				     _lock, #_lock,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Init rwlock %p (%s) "	\
				"at %s:%d", rc, _lock, #_lock,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging rwlock destroy
 *
 * @param[in,out] _lock The rwlock to destroy
 */
#define PTHREAD_RWLOCK_destroy(_lock)					\
	do {								\
		int rc;							\
									\
		rc = pthread_rwlock_destroy(_lock);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Destroy mutex %p (%s) at %s:%d",	\
				     _lock, #_lock,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Destroy mutex %p (%s) "	\
				"at %s:%d", rc, _lock, #_lock,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging write-lock
 *
 * @param[in,out] _lock Read-write lock
 */

#define PTHREAD_RWLOCK_wrlock(_lock)					\
	do {								\
		int rc;							\
									\
		rc = pthread_rwlock_wrlock(_lock);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Got write lock on %p (%s) "	\
				     "at %s:%d", _lock, #_lock,		\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, write locking %p (%s) "	\
				"at %s:%d", rc, _lock, #_lock,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)							\

/**
 * @brief Logging read-lock
 *
 * @param[in,out] _lock Read-write lock
 */

#define PTHREAD_RWLOCK_rdlock(_lock)					\
	do {								\
		int rc;							\
									\
		rc = pthread_rwlock_rdlock(_lock);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Got read lock on %p (%s) "	\
				     "at %s:%d", _lock, #_lock,		\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, read locking %p (%s) "	\
				"at %s:%d", rc, _lock, #_lock,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)							\

/**
 * @brief Logging read-write lock unlock
 *
 * @param[in,out] _lock Read-write lock
 */

#define PTHREAD_RWLOCK_unlock(_lock)					\
	do {								\
		int rc;							\
									\
		rc = pthread_rwlock_unlock(_lock);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Unlocked %p (%s) at %s:%d",       \
				     _lock, #_lock,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, unlocking %p (%s) at %s:%d",	\
				rc, _lock, #_lock,			\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)							\

/**
 * @brief Logging pthread mutex attribute initialization
 *
 * @param[in,out] _attr The attributes to initialize
 */
#define PTHREAD_MUTEXATTR_init(_attr)					\
	do {								\
		int rc;							\
									\
		rc = pthread_mutexattr_init(_attr);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Init mutexattr %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, mutexattr init %p (%s) "	\
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging pthread mutex set type
 *
 * @param[in,out] _attr The attributes to update
 * @param[in]     _type The type to set
 */
#define PTHREAD_MUTEXATTR_settype(_attr, _type)				\
	do {								\
		int rc;							\
									\
		rc = pthread_mutexattr_settype(_attr, _type);		\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "pthread_mutexattr_settype %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, mutexattr settype %p (%s) "	\
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging pthread mutex attribute destruction
 *
 * @param[in,out] _attr The attributes to initialize
 */
#define PTHREAD_MUTEXATTR_destroy(_attr)				\
	do {								\
		int rc;							\
									\
		rc = pthread_mutexattr_destroy(_attr);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Destroy mutexattr %p (%s) at %s:%d", \
				     _attr, #_attr,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, mutexattr destroy %p (%s) "	\
				"at %s:%d", rc, _attr, #_attr,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging mutex lock
 *
 * @param[in,out] _mtx The mutex to acquire
 */

#define PTHREAD_MUTEX_lock(_mtx)					\
	do {								\
		int rc;							\
									\
		rc = pthread_mutex_lock(_mtx);				\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Acquired mutex %p (%s) at %s:%d",	\
				     _mtx, #_mtx,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, acquiring mutex %p (%s) "	\
				"at %s:%d", rc, _mtx, #_mtx,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

static inline int PTHREAD_mutex_trylock(pthread_mutex_t *mtx,
					const char *mtx_name)
{
	int rc;

	rc = pthread_mutex_trylock(mtx);
	if (rc == 0) {
		LogFullDebug(COMPONENT_RW_LOCK,
			     "Acquired mutex %p (%s) at %s:%d",
			     mtx, mtx_name,
			     __FILE__, __LINE__);
	} else if (rc == EBUSY) {
		LogFullDebug(COMPONENT_RW_LOCK,
			     "Busy mutex %p (%s) at %s:%d",
			     mtx, mtx_name,
			     __FILE__, __LINE__);
	} else {
		LogCrit(COMPONENT_RW_LOCK,
			"Error %d, acquiring mutex %p (%s) at %s:%d",
			rc, mtx, mtx_name,
			__FILE__, __LINE__);
		abort();
	}

	return rc;
}

#define PTHREAD_MUTEX_trylock(_mtx)  PTHREAD_mutex_trylock(_mtx, #_mtx)

/**
 * @brief Logging mutex unlock
 *
 * @param[in,out] _mtx The mutex to relinquish
 */

#define PTHREAD_MUTEX_unlock(_mtx)					\
	do {								\
		int rc;							\
									\
		rc = pthread_mutex_unlock(_mtx);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Released mutex %p (%s) at %s:%d",	\
				     _mtx, #_mtx,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, releasing mutex %p (%s) "	\
				"at %s:%d", rc, _mtx, #_mtx,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging mutex initialization
 *
 * @param[in,out] _mtx The mutex to initialize
 * @param[in,out] _attr The attributes used while initializing the mutex
 */
#define PTHREAD_MUTEX_init(_mtx, _attr)					\
	do {								\
		int rc;							\
		pthread_mutexattr_t *attr = _attr;			\
									\
		if (attr == NULL)					\
			attr = &default_mutex_attr;			\
									\
		rc = pthread_mutex_init(_mtx, attr);			\
									\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Init mutex %p (%s) at %s:%d",	\
				     _mtx, #_mtx,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Init mutex %p (%s) "		\
				"at %s:%d", rc, _mtx, #_mtx,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging mutex destroy
 *
 * @param[in,out] _mtx The mutex to destroy
 */

#define PTHREAD_MUTEX_destroy(_mtx)					\
	do {								\
		int rc;							\
									\
		rc = pthread_mutex_destroy(_mtx);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Destroy mutex %p (%s) at %s:%d",	\
				     _mtx, #_mtx,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Destroy mutex %p (%s) "	\
				"at %s:%d", rc, _mtx, #_mtx,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging spin lock
 *
 * @param[in,out] _spin The spin lock to acquire
 */

#define PTHREAD_SPIN_lock(_spin)					\
	do {								\
		int rc;							\
									\
		rc = pthread_spin_lock(_spin);				\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Acquired spin lock %p (%s) at %s:%d", \
				     _spin, #_spin,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, acquiring spin lock %p (%s) " \
				"at %s:%d", rc, _spin, #_spin,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

#define PTHREAD_SPIN_unlock(_spin)					\
	do {								\
		int rc;							\
									\
		rc = pthread_spin_unlock(_spin);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Released spin lock %p (%s) at %s:%d", \
				     _spin, #_spin,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, releasing spin lock %p (%s) " \
				"at %s:%d", rc, _spin, #_spin,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging spin lock initialization
 *
 * @param[in,out] _spin    The spin lock to initialize
 * @param[in,out] _pshared The sharing type for the spin lock
 */
#define PTHREAD_SPIN_init(_spin, _pshared)				\
	do {								\
		int rc;							\
									\
		rc = pthread_spin_init(_spin, _pshared);		\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Init spin lock %p (%s) at %s:%d",	\
				     _spin, #_spin,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Init spin lock %p (%s) "	\
				"at %s:%d", rc, _spin, #_spin,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging spin lock destroy
 *
 * @param[in,out] _spin The spin lock to destroy
 */

#define PTHREAD_SPIN_destroy(_spin)					\
	do {								\
		int rc;							\
									\
		rc = pthread_spin_destroy(_spin);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Destroy spin lock %p (%s) at %s:%d", \
				     _spin, #_spin,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Destroy spin lock %p (%s) "	\
				"at %s:%d", rc, _spin, #_spin,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging condition variable initialization
 *
 * @param[in,out] _cond The condition variable to initialize
 * @param[in,out] _attr The attributes used while initializing the
 *			condition variable
 */
#define PTHREAD_COND_init(_cond, _attr)					\
	do {								\
		int rc;							\
									\
		rc = pthread_cond_init(_cond, _attr);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Init cond %p (%s) at %s:%d",	\
				     _cond, #_cond,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Init cond %p (%s) "		\
				"at %s:%d", rc, _cond, #_cond,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging condition variable destroy
 *
 * @param[in,out] _cond The condition variable to destroy
 */

#define PTHREAD_COND_destroy(_cond)					\
	do {								\
		int rc;							\
									\
		rc = pthread_cond_destroy(_cond);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Destroy cond %p (%s) at %s:%d",	\
				     _cond, #_cond,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Destroy cond %p (%s) "	\
				"at %s:%d", rc, _cond, #_cond,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging condtion variable wait
 *
 * @param[in,out] _cond The condition variable to wait for
 * @param[in,out] _mutex The mutex associated with the condition variable
 */

#define PTHREAD_COND_wait(_cond, _mutex)				\
	do {								\
		int rc;							\
									\
		rc = pthread_cond_wait(_cond, _mutex);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Wait cond %p (%s) at %s:%d",	\
				     _cond, #_cond,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Wait cond %p (%s) "		\
				"at %s:%d", rc, _cond, #_cond,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging condtion variable signal
 *
 * @param[in,out] _cond The condition variable to destroy
 */

#define PTHREAD_COND_signal(_cond)					\
	do {								\
		int rc;							\
									\
		rc = pthread_cond_signal(_cond);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Signal cond %p (%s) at %s:%d",	\
				     _cond, #_cond,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Signal cond %p (%s) "	\
				"at %s:%d", rc, _cond, #_cond,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Logging condition variable broadcast
 *
 * @param[in,out] _cond The condition variable to destroy
 */

#define PTHREAD_COND_broadcast(_cond)					\
	do {								\
		int rc;							\
									\
		rc = pthread_cond_broadcast(_cond);			\
		if (rc == 0) {						\
			LogFullDebug(COMPONENT_RW_LOCK,			\
				     "Broadcast cond %p (%s) at %s:%d",	\
				     _cond, #_cond,			\
				     __FILE__, __LINE__);		\
		} else {						\
			LogCrit(COMPONENT_RW_LOCK,			\
				"Error %d, Broadcast cond %p (%s) "	\
				"at %s:%d", rc, _cond, #_cond,		\
				__FILE__, __LINE__);			\
			abort();					\
		}							\
	} while (0)

/**
 * @brief Inline functions for timespec math
 *
 * This is for timespec math.  If you want to
 * do the same kinds of math on timeval values,
 * See timeradd(3) in GLIBC.
 *
 * The primary purpose of nsecs_elapsed_t is for a compact
 * and quick way to handle time issues relative to server
 * start and server EPOCH (which is not quite the same thing
 * but too complicated to explain here).
 */

#ifdef __APPLE__
/* For accessing timespec values on 'struct stat' */
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
#endif

/**
 * @brief Get the abs difference between two timespecs in nsecs
 *
 * useful for cheap time calculation. Works with Dr. Who...
 *
 * @param[in] start timespec of before end
 * @param[in] end   timespec after start time
 *
 * @return Elapsed time in nsecs
 */

static inline nsecs_elapsed_t
timespec_diff(const struct timespec *start,
	      const struct timespec *end)
{
	if ((end->tv_sec > start->tv_sec)
	    || (end->tv_sec == start->tv_sec
		&& end->tv_nsec >= start->tv_nsec)) {
		return (end->tv_sec - start->tv_sec) * NS_PER_SEC +
		    (end->tv_nsec - start->tv_nsec);
	} else {
		return (start->tv_sec - end->tv_sec) * NS_PER_SEC +
		    (start->tv_nsec - end->tv_nsec);
	}
}

/**
 * @brief update timespec fields atomically.
 *
 */
static inline void
timespec_update(const struct timespec *dest,
		const struct timespec *src)
{
	(void)atomic_store_uint64_t((uint64_t *)&dest->tv_sec,
				    (uint64_t)src->tv_sec);
	(void)atomic_store_uint64_t((uint64_t *)&dest->tv_nsec,
				    (uint64_t)src->tv_nsec);
}

/**
 * @brief Convert a timespec to an elapsed time interval
 *
 * This will work for wallclock time until 2554.
 */

static
inline nsecs_elapsed_t timespec_to_nsecs(struct timespec *timespec)
{
	return timespec->tv_sec * NS_PER_SEC + timespec->tv_nsec;
}

/**
 * @brief Convert an elapsed time interval to a timespec
 */

static
inline void nsecs_to_timespec(nsecs_elapsed_t interval,
			      struct timespec *timespec)
{
	timespec->tv_sec = interval / NS_PER_SEC;
	timespec->tv_nsec = interval % NS_PER_SEC;
}

/**
 * @brief Add an interval to a timespec
 *
 * @param[in]     interval Nanoseconds to add
 * @param[in,out] timespec Time
 */

static inline void
timespec_add_nsecs(nsecs_elapsed_t interval,
		   struct timespec *timespec)
{
	timespec->tv_sec += (interval / NS_PER_SEC);
	timespec->tv_nsec += (interval % NS_PER_SEC);
	if ((nsecs_elapsed_t)timespec->tv_nsec > NS_PER_SEC) {
		timespec->tv_sec += (timespec->tv_nsec / NS_PER_SEC);
		timespec->tv_nsec = timespec->tv_nsec % NS_PER_SEC;
	}
}

/**
 * @brief Subtract an interval from a timespec
 *
 * @param[in]     interval Nanoseconds to subtract
 * @param[in,out] timespec Time
 */

static inline void
timespec_sub_nsecs(nsecs_elapsed_t interval, struct timespec *t)
{
	struct timespec ts;

	nsecs_to_timespec(interval, &ts);

	if (ts.tv_nsec > t->tv_nsec) {
		t->tv_sec -= (ts.tv_sec + 1);
		t->tv_nsec = ts.tv_nsec - t->tv_nsec;
	} else {
		t->tv_sec -= ts.tv_sec;
		t->tv_nsec -= ts.tv_nsec;
	}
}

/**
 * @brief Compare two times
 *
 * Determine if @c t1 is less-than, equal-to, or greater-than @c t2.
 *
 * @param[in] t1 First time
 * @param[in] t2 Second time
 *
 * @retval -1 @c t1 is less-than @c t2
 * @retval 0 @c t1 is equal-to @c t2
 * @retval 1 @c t1 is greater-than @c t2
 */

static inline int gsh_time_cmp(const struct timespec *t1,
			       const struct timespec *t2)
{
	if (t1->tv_sec < t2->tv_sec) {
		return -1;
	} else if (t1->tv_sec > t2->tv_sec) {
		return 1;
	} else {
		if (t1->tv_nsec < t2->tv_nsec)
			return -1;
		else if (t1->tv_nsec > t2->tv_nsec)
			return 1;
	}

	return 0;
}

/**
 * @brief Compare two buffers
 *
 * Handle the case where one buffer is a left sub-buffer of another
 * buffer by counting the longer one as larger.
 *
 * @param[in] buff1 A buffer
 * @param[in] buffa Another buffer
 *
 * @retval -1 if buff1 is less than buffa
 * @retval 0 if buff1 and buffa are equal
 * @retval 1 if buff1 is greater than buffa
 */

static inline int gsh_buffdesc_comparator(const struct gsh_buffdesc *buffa,
	const struct gsh_buffdesc *buff1)
{
	int mr = memcmp(buff1->addr, buffa->addr, MIN(buff1->len, buffa->len));

	if (unlikely(mr == 0)) {
		if (buff1->len < buffa->len)
			return -1;
		else if (buff1->len > buffa->len)
			return 1;
		else
			return 0;
	} else {
		return mr;
	}
}

/**
 * @brief Get the time right now as a timespec
 *
 * @param[out] ts Timespec struct
 */

static inline void now(struct timespec *ts)
{
	int rc;

	rc = clock_gettime(CLOCK_REALTIME, ts);
	if (rc != 0) {
		LogCrit(COMPONENT_MAIN, "Failed to get timestamp");
		assert(0);	/* if this is broken, we are toast so die */
	}
}

/* The following definitions require some of the following */
#include "idmapper.h"

/*wrapper for gethostname to capture auth stats, if required */
static inline int gsh_gethostname(char *name, size_t len, bool stats)
{
	int ret;
	struct timespec s_time, e_time;

	if (stats)
		now(&s_time);

	ret = gethostname(name, len);

	if (!ret && stats) {
		now(&e_time);
		dns_stats_update(&s_time, &e_time);
	}
	return ret;
}

/*wrapper for getaddrinfo to capture auth stats, if required */
static inline int gsh_getaddrinfo(const char *node,
				  const char *service,
				  const struct addrinfo *hints,
				  struct addrinfo **res,
				  bool stats)
{
	int ret;
	struct timespec s_time, e_time;

	if (stats)
		now(&s_time);

	ret = getaddrinfo(node, service, hints, res);

	if (!ret && stats) {
		now(&e_time);
		dns_stats_update(&s_time, &e_time);
	}
	return ret;
}

/*wrapper for getnameinfo to capture auth stats, if required */
static inline int gsh_getnameinfo(const struct sockaddr *addr,
				  socklen_t addrlen,
				  char *host, socklen_t hostlen,
				  char *serv, socklen_t servlen,
				  int flags, bool stats)
{
	int ret;
	struct timespec s_time, e_time;

	if (stats)
		now(&s_time);

	ret = getnameinfo(addr, addrlen, host, hostlen, serv, servlen, flags);

	if (!ret && stats) {
		now(&e_time);
		dns_stats_update(&s_time, &e_time);
	}
	return ret;
}

#endif				/* !COMMON_UTILS_H */
