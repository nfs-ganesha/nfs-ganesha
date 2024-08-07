/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright © 2012 Linux Box Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

#ifndef ATOMIC_UTILS_H
#define ATOMIC_UTILS_H

#include "common_utils.h"
#include "abstract_atomic.h"

/**
 * @brief Decrement an int64_t refcounter and take mutex if zero.
 *
 * @param[in,out] var  The refcounter
 * @param[in,out] _mtx The mutex to acquire
 *
 * @return true if the counter was decremented to zero and the mutex locked.
 */

static inline
bool PTHREAD_MUTEX_dec_int64_t_and_lock(int64_t *var,
					pthread_mutex_t *lock)
{
	if (atomic_add_unless_int64_t(var, -1, 1))
		return false;

	PTHREAD_MUTEX_lock(lock);
	if (atomic_add_int64_t(var, -1) == 0)
		return true;
	PTHREAD_MUTEX_unlock(lock);
	return false;
}

/**
 * @brief Decrement an uint64_t refcounter and take mutex if zero.
 *
 * @param[in,out] var  The refcounter
 * @param[in,out] _mtx The mutex to acquire
 *
 * @return true if the counter was decremented to zero and the mutex locked.
 */

static inline
bool PTHREAD_MUTEX_dec_uint64_t_and_lock(uint64_t *var,
					 pthread_mutex_t *lock)
{
	if (atomic_add_unless_uint64_t(var, -1, 1))
		return false;

	PTHREAD_MUTEX_lock(lock);
	if (atomic_add_uint64_t(var, -1) == 0)
		return true;
	PTHREAD_MUTEX_unlock(lock);
	return false;
}

/**
 * @brief Decrement an int32_t refcounter and take mutex if zero.
 *
 * @param[in,out] var  The refcounter
 * @param[in,out] _mtx The mutex to acquire
 *
 * @return true if the counter was decremented to zero and the mutex locked.
 */

static inline
bool PTHREAD_MUTEX_dec_int32_t_and_lock(int32_t *var,
					pthread_mutex_t *lock)
{
	if (atomic_add_unless_int32_t(var, -1, 1))
		return false;

	PTHREAD_MUTEX_lock(lock);
	if (atomic_add_int32_t(var, -1) == 0)
		return true;
	PTHREAD_MUTEX_unlock(lock);
	return false;
}

/**
 * @brief Decrement an uint32_t refcounter and take mutex if zero.
 *
 * @param[in,out] var  The refcounter
 * @param[in,out] _mtx The mutex to acquire
 *
 * @return true if the counter was decremented to zero and the mutex locked.
 */

static inline
bool PTHREAD_MUTEX_dec_uint32_t_and_lock(uint32_t *var,
					 pthread_mutex_t *lock)
{
	if (atomic_add_unless_uint32_t(var, -1, 1))
		return false;

	PTHREAD_MUTEX_lock(lock);
	if (atomic_add_uint32_t(var, -1) == 0)
		return true;
	PTHREAD_MUTEX_unlock(lock);
	return false;
}

#endif				/* !ATOMIC_UTILS_H */
