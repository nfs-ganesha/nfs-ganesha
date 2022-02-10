/*
 *
 *
 * Copyright © Linux box Corporation, 2012
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
 *
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

/**
 * @file   abstract_mem.h
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief  Abstract memory shims to allow swapping out allocators
 *
 * This file's purpose is to allow us to easily replace the memory
 * allocator used by Ganesha.  Further, it provides a pool abstraction
 * that may be implemented in terms of the normal allocator that may
 * be expanded at a later date.  These are intended to be thin
 * wrappers, but conditionally compiled trace information could be
 * added.
 */

#ifndef ABSTRACT_MEM_H
#define ABSTRACT_MEM_H

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "log.h"

/**
 * @page GeneralAllocator General Allocator Shim
 *
 * These functions provide an interface akin to the standard libc
 * allocation functions.  Currently they call the functions malloc,
 * free, and so forth, with changes in functionality being provided by
 * linking in alternate allocator libraries (tcmalloc and jemalloc, at
 * present.)  So long as the interface remains the same, these
 * functions can be switched out using ifdef for versions that do more
 * memory tracking or that call allocators with other names.
 */

/**
 * @brief Allocate memory
 *
 * This function allocates a block of memory no less than the given
 * size. The block of memory allocated must be released with gsh_free.
 *
 * This function aborts if no memory is available.
 *
 * @param[in] n Number of bytes to allocate
 * @param[in] file Calling source file
 * @param[in] line Calling source line
 * @param[in] function Calling source function
 *
 * @return Pointer to a block of memory.
 */
static inline void *
gsh_malloc__(size_t n,
	     const char *file, int line, const char *function)
{
	void *p = malloc(n);

	if (p == NULL) {
		LogMallocFailure(file, line, function, "gsh_malloc");
		abort();
	}

	return p;
}

#define gsh_malloc(n) ({ \
		void *p_ = malloc(n); \
		if (p_ == NULL) { \
			abort(); \
		} \
		p_; \
	})

/**
 * @brief Allocate aligned memory
 *
 * This function allocates a block of memory to the given alignment.
 * Failure may indicate either insufficient memory or an invalid
 * alignment.
 *
 * @param[in] a Block alignment
 * @param[in] n Number of bytes to allocate
 * @param[in] file Calling source file
 * @param[in] line Calling source line
 * @param[in] function Calling source function
 *
 * @return Pointer to a block of memory or NULL.
 */
static inline void *
gsh_malloc_aligned__(size_t a, size_t n,
		     const char *file, int line, const char *function)
{
	void *p;

#ifdef __APPLE__
	p = valloc(n);
#else
	if (posix_memalign(&p, a, n) != 0)
		p = NULL;
#endif
	if (p == NULL) {
		LogMallocFailure(file, line, function, "gsh_malloc_aligned");
		abort();
	}

	return p;
}

#define gsh_malloc_aligned(a, n) ({ \
		void *p_; \
		if (posix_memalign(&p_, a, n) != 0) { \
			abort(); \
		} \
		p_; \
	})

/**
 * @brief Allocate zeroed memory
 *
 * This function allocates a block of memory that is guaranteed to be
 * zeroed. The block of memory allocated must be released with gsh_free.
 *
 * This function aborts if no memory is available.
 *
 * @param[in] n Number of objects in block
 * @param[in] s Size of object
 *
 * @return Pointer to a block of zeroed memory.
 */
static inline void *
gsh_calloc__(size_t n, size_t s,
	     const char *file, int line, const char *function)
{
	void *p = calloc(n, s);

	if (p == NULL) {
		LogMallocFailure(file, line, function, "gsh_calloc");
		abort();
	}

	return p;
}

#define gsh_calloc(n, s) ({ \
		void *p_ = calloc(n, s); \
		if (p_ == NULL) { \
			abort(); \
		} \
		p_; \
	})

/**
 * @brief Resize a block of memory
 *
 * This function resizes the buffer indicated by the supplied pointer
 * to the given size.  The block may be moved in this process.  On
 * failure, the original block is retained at its original address.
 *
 * This function aborts if no memory is available to resize.
 *
 * @param[in] p Block of memory to resize
 * @param[in] n New size
 * @param[in] file Calling source file
 * @param[in] line Calling source line
 * @param[in] function Calling source function
 *
 * @return Pointer to the address of the resized block.
 */
static inline void *
gsh_realloc__(void *p, size_t n,
	      const char *file, int line, const char *function)
{
	void *p2 = realloc(p, n);

	if (n != 0 && p2 == NULL) {
		LogMallocFailure(file, line, function, "gsh_realloc");
		abort();
	}

	return p2;
}

#define gsh_realloc(p, n) ({ \
		void *p2_ = realloc(p, n); \
		if (n != 0 && p2_ == NULL) { \
			abort(); \
		} \
		p2_; \
	})

#define gsh_strdup(s) ({ \
		char *p_ = strdup(s); \
		if (p_ == NULL) { \
			abort(); \
		} \
		p_; \
	})

#define gsh_strldup(s, l, n) ({ \
		char *p_ = (char *) gsh_malloc(l+1); \
		memcpy(p_, s, l); \
		p_[l] = '\0'; \
		*n = l + 1; \
		p_; \
	})

#if defined(__GLIBC__) && defined(_GNU_SOURCE)
#define gsh_strdupa(src) strdupa(src)
#else
#define gsh_strdupa(src) ({\
		char *dest = alloca(strlen(src) + 1); \
		strcpy(dest, src); \
		dest; \
	})
#endif

#define gsh_memdup(s, l) ({ \
		void *p_ = gsh_malloc(l); \
		memcpy(p_, s, l); \
		p_; \
	})

/**
 * @brief Free a block of memory
 *
 * This function frees a block of memory allocated with gsh_malloc,
 * gsh_malloc_aligned, gsh_calloc, gsh_realloc, or gsh_strdup.
 *
 * @param[in] p Block of memory to free.
 */
static inline void
gsh_free(void *p)
{
	free(p);
}

/**
 * @brief Free a block of memory with size
 *
 * This function exists to be passed to TIRPC when setting
 * allocators.  It should not be used by anyone else.  New shim layers
 * should not redefine it.
 *
 * @param[in] p  Block of memory to free.
 * @param[in] n  Size of block (unused)
 */
static inline void
gsh_free_size(void *p, size_t n __attribute__ ((unused)))
{
	free(p);
}

/**
 * @brief Type representing a pool
 *
 * This type represents a memory pool.  it should be treated, by all
 * callers, as a completely abstract type.  The pointer should only be
 * stored or passed to pool functions.  The pointer should never be
 * referenced.  No assumptions about the size of the pointed-to type
 * should be made.
 *
 * This allows for flexible growth in the future.
 */

typedef struct pool {
	char *name; /*< The name of the pool */
	size_t object_size; /*< The size of the objects created */
} pool_t;

/**
 * @brief Create a basic object pool
 *
 * This function creates a new object pool, given a name, object size,
 * constructor and destructor.
 *
 * This particular implementation throws the name away, but other
 * implementations that do tracking or keep counts of allocated or
 * de-allocated objects will likely wish to use it in log messages.
 *
 * This initializer function is expected to abort if it fails.
 *
 * @param[in] name             The name of this pool
 * @param[in] object_size      The size of objects to allocate
 * @param[in] file             Calling source file
 * @param[in] line             Calling source line
 * @param[in] function         Calling source function
 *
 * @return A pointer to the pool object.  This pointer must not be
 *         dereferenced.  It may be stored or supplied as an argument
 *         to the other pool functions.  It must not be supplied as an
 *         argument to gsh_free, rather it must be disposed of with
 *         pool_destroy.
 */

static inline pool_t *
pool_basic_init(const char *name, size_t object_size)
{
	pool_t *pool = (pool_t *) gsh_malloc(sizeof(pool_t));

	pool->object_size = object_size;

	if (name)
		pool->name = gsh_strdup(name);
	else
		pool->name = NULL;

	return pool;
}

/**
 * @brief Destroy a memory pool
 *
 * This function destroys a memory pool.  All objects must be returned
 * to the pool before this function is called.
 *
 * @param[in] pool The pool to be destroyed.
 */

static inline void
pool_destroy(pool_t *pool)
{
	gsh_free(pool->name);
	gsh_free(pool);
}

/**
 * @brief Allocate an object from a pool
 *
 * This function allocates a single object from the pool and returns a
 * pointer to it.  If a constructor was specified at pool creation, it
 * is called on that pointer.  This function must be thread safe.  If
 * the underlying pool abstraction requires a lock, this function must
 * take and release it.
 *
 * This function returns void pointers.  Programmers who wish for more
 * type safety can easily create static inline wrappers (alloc_client
 * or similar) to return pointers of a specific type (and omitting the
 * pool parameter).
 *
 * This function aborts if no memory is available.
 *
 * @param[in] pool       The pool from which to allocate
 * @param[in] file       Calling source file
 * @param[in] line       Calling source line
 * @param[in] function   Calling source function
 *
 * @return A pointer to the allocated pool item.
 */

#define pool_alloc(pool) gsh_calloc(1, (pool)->object_size)

/**
 * @brief Return an entry to a pool
 *
 * This function returns a single object to the pool.  If a destructor
 * was defined at pool creation time, it is called before the object
 * is freed.  This function must be thread-safe.  If the underlying
 * pool abstract requires a lock, this function must take and release
 * it.
 *
 * @param[in] pool   Pool to which to return the object
 * @param[in] object The object to return.  This is a void pointer.
 *                   Programmers wishing more type safety could create
 *                   a static inline wrapper taking an object of a
 *                   specific type (and omitting the pool parameter.)
 */

static inline void
pool_free(pool_t *pool, void *object)
{
	gsh_free(object);
}

static inline char *gsh_concat(const char *p1, const char *p2)
{
	size_t len1 = strlen(p1);
	size_t len2 = strlen(p2);
	char *path = (char *)gsh_malloc(len1 + len2 + 1);

	memcpy(path, p1, len1);
	memcpy(path + len1, p2, len2 + 1);

	return path;
}

static inline char *gsh_concat_sep(const char *p1, char sep, const char *p2)
{
	size_t len1 = strlen(p1);
	size_t len2 = strlen(p2);
	char *path = (char *)gsh_malloc(len1 + 1 + len2 + 1);

	memcpy(path, p1, len1);
	path[len1] = sep;
	memcpy(path + len1 + 1, p2, len2 + 1);

	return path;
}

#endif /* ABSTRACT_MEM_H */
