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

#define gsh_malloc(n) gsh_malloc__(n, __FILE__, __LINE__, __func__)

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

#define gsh_malloc_aligned(a, n) \
	gsh_malloc_aligned__(a, n, __FILE__, __LINE__, __func__)

/**
 * @brief Allocate zeroed memory
 *
 * This function allocates a block of memory that is guaranteed to be
 * zeroed. The block of memory allocated must be released with gsh_free.
 *
 * This function aborts if no memory is available.
 *
 * @param[in] s Size of object
 * @param[in] n Number of objects in block
 *
 * @return Pointer to a block of zeroed memory.
 */
static inline void *
gsh_calloc__(size_t s, size_t n,
	     const char *file, int line, const char *function)
{
	void *p = calloc(s, n);

	if (p == NULL) {
		LogMallocFailure(file, line, function, "gsh_calloc");
		abort();
	}

	return p;
}

#define gsh_calloc(s, n) gsh_calloc__(s, n, __FILE__, __LINE__, __func__)

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

#define gsh_realloc(p, n) gsh_realloc__(p, n, __FILE__, __LINE__, __func__)

/**
 * @brief Duplicate a string to newly allocated memory
 *
 * This function allocates a new block of memory sufficient to contain
 * the supplied string, then copies the string into that buffer.
 *
 * This function aborts if no memory is available.
 *
 * @param[in] s  String to duplicate
 * @param[in] file Calling source file
 * @param[in] line Calling source line
 * @param[in] function Calling source function
 *
 * @return Pointer to new copy of string.
 */
static inline char *
gsh_strdup__(const char *s,
	     const char *file, int line, const char *function)
{
	void *p = strdup(s);

	if (p == NULL) {
		LogMallocFailure(file, line, function, "gsh_strdup");
		abort();
	}

	return p;
}

#define gsh_strdup(s) gsh_strdup__(s, __FILE__, __LINE__, __func__)

/**
 * @brief Duplicate a string to newly allocated memory (bounded)
 *
 * This function allocates a new block of memory sufficient to contain
 * the supplied string, then copies the string into that buffer.
 *
 * This function aborts if no memory is available.
 *
 * @param[in] s String to duplicate
 * @param[in] length Size of the returned string shall be <= length+1
 * @param[out] copied Number of bytes copied
 * @param[in] file Calling source file
 * @param[in] line Calling source line
 * @param[in] function Calling source function
 *
 * @return Pointer to new copy of string.
 */
static inline char *
gsh_strldup__(const char *s, size_t length, size_t *copied,
	     const char *file, int line, const char *function)
{
	char *p = (char *) gsh_malloc__(length+1, file, line, function);

	if (p == NULL) {
		LogMallocFailure(file, line, function, "gsh_strldup");
		abort();
	}

	memcpy(p, s, length);
	p[length] = '\0';
	*copied = length;

	return p;
}

#define gsh_strldup(s, l, n) gsh_strldup__(s, l, n, __FILE__, __LINE__, \
						__func__)

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
 * @page PoolAllocator Pool allocator shim
 *
 * These functions provide an abstract interface to memory pools.
 * Since multiple implementations of pooling may be useful within a
 * single running image, the underlying substrate can be changed using
 * by passing a constant (specifying an allocator) and parameters to
 * pool_init.
 *
 * By design, things are separated out so one can add a new pool
 * substrate without editing this file.  One can create, for example,
 * static_pool.h, define a function vector and a parameter structure,
 * and any functions wishing to use the static_pool could include it.
 */

typedef struct pool pool_t;

/**
 * @brief Abstract type of pool initializer
 *
 * This type represents that of a function to perform the
 * initialization of the underlying pool.  It should allocate the
 * pool_t and other memory it needs in a contiguous block, initialize
 * its own substrate-specific data, and return the pointer to the
 * pool_t (without filling out any of the fields in the pool_t
 * proper.)
 *
 * This function is expected to abort if it fails.
 *
 * @param[in] size  Size of the objects to be allocated
 * @param[in] param Substrate specific parameters
 * @param[in] file Calling source file
 * @param[in] line Calling source line
 * @param[in] function Calling source function
 *
 * @return the pointer to the pool_t
 */

typedef pool_t *(*pool_initializer_t)(size_t size, void *param,
				      const char *file, int line,
				      const char *function);

/**
 * @brief Abstract type of pool destroyer
 *
 * This type represents that of a function to perform the destruction
 * of an underlying pool.  It should do any substrate specific cleanup
 * and then deallocate the memory pointed to by the pool argment.
 *
 * @param[in] pool The pool to destroy
 */

typedef void (*pool_destroyer_t)(pool_t *pool);

/**
 * @brief Abstract type of pool allocator
 *
 * This type represents that of a function to perform the allocation
 * of an object in the underlying pool implementation.  It must not
 * call the constructor function, leaving that to the wrapper.
 *
 * This function is expected to abort if it fails.
 *
 * @param[in] pool The pool from which to allocate.
 * @param[in] file Calling source file
 * @param[in] line Calling source line
 * @param[in] function Calling source function
 *
 * @return the allocated object.
 */

typedef void *(*pool_allocator_t)(pool_t *pool,
				  const char *file, int line,
				  const char *function);

/**
 * @brief Abstract type of pool freer
 *
 * This type represents that of a function to free an object in the
 * underlying pool implementation.  It must not call the destructor
 * function, leaving that to the wrapper.
 *
 * @param[in] pool   The pool to which to return the object
 * @param[in] object The object to free
 *
 */

typedef void (*pool_freer_t)(pool_t *pool, void *object);

/**
 * @brief A function vector defining a pool substrate
 *
 * This structure provides the set of functions that defines the
 * underlying pool substrate.
 */

struct pool_substrate_vector {
	pool_initializer_t initializer;	/*< Create an underlying pool */
	pool_destroyer_t destroyer; /*< Destroy an underlying pool */
	pool_allocator_t allocator; /*< Allocate an object */
	pool_freer_t freer; /*< Free an object */
};

/**
 * @brief Object constructor
 *
 * Functions of this type are used to initialize objects newly
 * allocated from the pool.
 *
 * @param[out] object     The newly allocated object to be initialized
 * @param[in]  parameters Parameters to be used in initialization
 */

typedef void (*pool_constructor_t)(void *object, void *parameters);

/**
 * @brief Object destructor
 *
 * Functions of this type are used to finalize objects before they are
 * returned to the pool.
 *
 * @param[in,out] object The object to be finalized
 */

typedef void (*pool_destructor_t)(void *object);

/**
 * @brief Type representing a pool
 *
 * This type represents a memory pool.  it should be treated, by all
 * callers, as a completely abstract type.  The pointer should only be
 * stored or passed to pool functions.  The pointer should never be
 * referenced.  No assumptions about the size of the pointed-to type
 * should be made.
 */

struct pool {
	char *name; /*< The name of the pool */
	size_t object_size; /*< The size of the objects created */
	pool_constructor_t constructor; /*< The object constructor */
	pool_destructor_t destructor; /*< The object destructor */
	struct pool_substrate_vector *substrate_vector;	/*< Pool operations */
	char substrate_data[]; /*< The beginning of any substrate-specific
				   data */
};

/**
 * @brief Create an object pool
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
 * @param[in] substrate        The function vector specifying the
 *                             substrate to use for this pool
 * @param[in] substrate_params The substrate-specific parameters for
 *                             this pool
 * @param[in] constructor      Function to be called on each new
 *                             object
 * @param[in] destructor       Function to be called on each object
 *                             before destruction
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
pool_init__(const char *name, size_t object_size,
	    const struct pool_substrate_vector *substrate,
	    void *substrate_params,
	    pool_constructor_t constructor,
	    pool_destructor_t destructor,
	    const char *file, int line, const char *function)
{
	pool_t *pool = substrate->initializer(object_size, substrate_params,
					      file, line, function);

	pool->substrate_vector = (struct pool_substrate_vector *)substrate;
	pool->object_size = object_size;
	pool->constructor = constructor;
	pool->destructor = destructor;

	if (name)
		pool->name = gsh_strdup__(name, file, line, function);
	else
		pool->name = NULL;

	return pool;
}

#define pool_init(name, object_size, substrate, substrate_params, \
		  constructor, destructor) \
	pool_init__(name, object_size, substrate, substrate_params, \
		    constructor, destructor, __FILE__, __LINE__, __func__)

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
	pool->substrate_vector->destroyer(pool);
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
 * @param[in] parameters Parameters to be supplied to the constructor
 * @param[in] file       Calling source file
 * @param[in] line       Calling source line
 * @param[in] function   Calling source function
 *
 * @return A pointer to the allocated pool item.
 */

static inline void *
pool_alloc__(pool_t *pool, void *parameters,
	     const char *file, int line, const char *function)
{
	void *object;

	object = pool->substrate_vector->allocator(pool, file, line, function);

	if (pool->constructor)
		(pool->constructor) (object, parameters);

	return object;
}

#define pool_alloc(pool, parameters) \
	pool_alloc__(pool, parameters, __FILE__, __LINE__, __func__)

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
 * @param[in] object Object to return.  This is a void pointer.
 *                   Programmers wishing more type safety could create
 *                   a static inline wrapper taking an object of a
 *                   specific type (and omitting the pool parameter.)
 */

static inline void
pool_free(pool_t *pool, void *object)
{
	if (pool->destructor)
		(pool->destructor) (object);

	pool->substrate_vector->freer(pool, object);
}

/**
 * @page BasicPoolSubstrate The Basic Pool Substrate
 *
 * These functions provide a non-preallocated, general-purpose pool
 * abstraction to Ganesha.  Currently it is only a wrapper around
 * gsh_malloc and gsh_free, but there are plans to, at a later date,
 * rewrite it in terms of an exposed pool interface in the underlying
 * allocator.
 */

/**
 * @brief Initialize a basic pool
 *
 * This function simply allocates space for the pool_t structure and
 * returns it.
 *
 * This function aborts if no memory is available.
 *
 * @param[in] size  Size of the object (unused)
 * @param[in] param Parameters (there are no parameters, must be
 *                  NULL.)
 * @param[in] file Calling source file
 * @param[in] line Calling source line
 * @param[in] function Calling source function
 *
 * @return the allocated pool_t structure.
 */

static inline pool_t *
pool_basic_initializer(size_t size __attribute__ ((unused)),
		       void *param __attribute__ ((unused)),
		       const char *file, int line, const char *function)
{
	assert(param == NULL);	/* We take no parameters */
	return (pool_t *) gsh_malloc__(sizeof(pool_t), file, line, function);
}

/**
 * @brief Destroy a basic pool
 *
 * This function just frees the space of the pool_t structure.
 *
 * @param[in] pool The pool to destroy
 */

static inline void
pool_basic_destroy(pool_t *pool)
{
	if (pool->name)
		gsh_free(pool->name);
	gsh_free(pool);
}

/**
 * @brief Allocate an object from a basic pool
 *
 * This function just calls gsh_malloc to get an object of the
 * appropriate size.
 *
 * This function aborts if no memory is available.
 *
 * @param[in] pool The pool from which to allocate.
 * @param[in] file Calling source file
 * @param[in] line Calling source line
 * @param[in] function Calling source function
 *
 * @return the allocated object or NULL.
 */

static inline void *
pool_basic_alloc(pool_t *pool,
		 const char *file, int line, const char *function)
{
	if (pool->constructor)
		return gsh_malloc__(pool->object_size, file, line, function);
	else
		return gsh_calloc__(1, pool->object_size, file, line, function);
}

/**
 * @brief Free an object in a basic pool
 *
 * This function just calls gsh_free on the supplied object.
 *
 * @param[in] pool   The pool to which to return the object (unused)
 * @param[in] object The object to free
 */

static inline void
pool_basic_free(pool_t *pool __attribute__ ((unused)), void *object)
{
	gsh_free(object);
}

static const struct pool_substrate_vector pool_basic_substrate[] = {
	{
		.initializer = pool_basic_initializer,
		.destroyer = pool_basic_destroy,
		.allocator = pool_basic_alloc,
		.freer = pool_basic_free
	}
};

#endif /* ABSTRACT_MEM_H */
