/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Author Charles Hardin <ckhardin@gmail.com> - Public Domain
 * The author hereby disclaims copyright to this source code.
 *
 * GCC support functions for atomic operations that are not supported
 * by the builtin. When this happens, an reference to an external
 * implementation is attempted by the compiler so that this provides
 * the external function
 */

#include <stdint.h>
#include <pthread.h>

static	pthread_mutex_t	global_mutex = PTHREAD_MUTEX_INITIALIZER;


uint64_t __attribute__((weak))
__atomic_fetch_add_8(uint64_t *ptr, uint64_t val, int memmodel)
{
	uint64_t tmp;

	pthread_mutex_lock(&global_mutex);
	tmp = *ptr;
	*ptr += val;
	pthread_mutex_unlock(&global_mutex);

	return tmp;
}


uint64_t __attribute__((weak))
__atomic_fetch_sub_8(uint64_t *ptr, uint64_t val, int memmodel)
{
	uint64_t tmp;

	pthread_mutex_lock(&global_mutex);
	tmp = *ptr;
	*ptr -= val;
	pthread_mutex_unlock(&global_mutex);

	return tmp;
}


void __attribute__((weak))
__atomic_store_8(uint64_t *ptr, uint64_t val, int memmodel)
{
	pthread_mutex_lock(&global_mutex);
	*ptr = val;
	pthread_mutex_unlock(&global_mutex);
}


uint64_t __attribute__((weak))
__atomic_load_8(uint64_t *ptr, int memmodel)
{
	uint64_t ret;

	pthread_mutex_lock(&global_mutex);
	ret = *ptr;
	pthread_mutex_unlock(&global_mutex);

	return ret;
}
