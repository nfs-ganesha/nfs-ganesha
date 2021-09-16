/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 */

/**
 * @file common_utils.h
 * @brief Common tools for printing, parsing, ....
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

#if defined(GCC_ATOMIC_FUNCTIONS) | defined(GCC_SYNC_FUNCTIONS)
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
#endif

/**
 * @brief Decrement an uint64_t refcounter and take mutex if zero.
 *
 * @param[in,out] var  The refcounter
 * @param[in,out] _mtx The mutex to acquire
 *
 * @return true if the counter was decremented to zero and the mutex locked.
 */

#if defined(GCC_ATOMIC_FUNCTIONS) | defined(GCC_SYNC_FUNCTIONS)
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
#endif

/**
 * @brief Decrement an int32_t refcounter and take mutex if zero.
 *
 * @param[in,out] var  The refcounter
 * @param[in,out] _mtx The mutex to acquire
 *
 * @return true if the counter was decremented to zero and the mutex locked.
 */

#if defined(GCC_ATOMIC_FUNCTIONS) | defined(GCC_SYNC_FUNCTIONS)
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
#endif

/**
 * @brief Decrement an uint32_t refcounter and take mutex if zero.
 *
 * @param[in,out] var  The refcounter
 * @param[in,out] _mtx The mutex to acquire
 *
 * @return true if the counter was decremented to zero and the mutex locked.
 */

#if defined(GCC_ATOMIC_FUNCTIONS) | defined(GCC_SYNC_FUNCTIONS)
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
#endif

#endif				/* !ATOMIC_UTILS_H */
