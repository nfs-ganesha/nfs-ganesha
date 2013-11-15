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
 * size.  On failure, it returns NULL.  If a block of memory is
 * returned, it must be released with gsh_free.
 *
 * @param[in] n Number of bytes to allocate
 *
 * @return Pointer to a block of memory or NULL.
 */
static inline void *
gsh_malloc(size_t n)
{
	return malloc(n);
}

/**
 * @brief Allocate aligned memory
 *
 * This function allocates a block of memory to the given alignment.
 * Failure may indicate either insufficient memory or an invalid
 * alignment.
 *
 * @param[in] a Block alignment
 * @param[in] n Number of bytes to allocate
 *
 * @return Pointer to a block of memory or NULL.
 */
static inline void *
gsh_malloc_aligned(size_t a, size_t n)
{
#ifdef __APPLE__
	return valloc(n);
#else
	void *p;
	if (posix_memalign(&p, a, n) != 0)
		p = NULL;
	return p;
#endif
}

/**
 * @brief Allocate zeroed memory
 *
 * This function allocates a block of memory that is guaranteed to be
 * zeroed. On failure, it returns NULL.  If a block of memory is
 * returned, it must be released with gsh_free.
 *
 * @param[in] s Size of object
 * @param[in] n Number of objects in block
 *
 * @return Pointer to a block of zeroed memory or NULL.
 */
static inline void *
gsh_calloc(size_t s, size_t n)
{
	return calloc(s, n);
}

/**
 * @brief Resize a block of memory
 *
 * This function resizes the buffer indicated by the supplied pointer
 * to the given size.  The block may be moved in this process.  On
 * failure, the original block is retained at its original address.
 *
 * @param[in] p Block of memory to resize
 * @param[in] n New size
 *
 * @return Pointer to the address of the resized block.  NULL if
 *         resize failed.
 */
static inline void *
gsh_realloc(void *p, size_t n)
{
	return realloc(p, n);
}

/**
 * @brief Duplicate a string to newly allocated memory
 *
 * This function allocates a new block of memory sufficient to contain
 * the supplied string, then copies the string into that buffer.
 *
 * @param[in] s  String to duplicate
 *
 * @return Pointer to new copy of string or NULL on failure.
 */
static inline char *
gsh_strdup(const char *s)
{
	return strdup(s);
}

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
 * @param[in] size  Size of the objects to be allocated
 * @param[in] param Substrate specific parameters
 *
 * @return the pointer to the pool_t or NULL on failure
 */

typedef pool_t *(*pool_initializer_t)(size_t size, void *param);

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
 * @param[in] pool The pool from which to allocate.
 *
 * @return the allocated object or NULL.
 */

typedef void *(*pool_allocator_t)(pool_t *pool);

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
 *
 * @return A pointer to the pool object.  This pointer must not be
 *         dereferenced.  It may be stored or supplied as an argument
 *         to the other pool functions.  It must not be supplied as an
 *         argument to gsh_free, rather it must be disposed of with
 *         pool_destroy.  NULL is returned on error.
 */

static inline pool_t *
pool_init(const char *name, size_t object_size,
	  const struct pool_substrate_vector *substrate,
	  void *substrate_params,
	  pool_constructor_t constructor,
	  pool_destructor_t destructor)
{
	pool_t *pool = substrate->initializer(object_size,
					      substrate_params);
	if (pool) {
		pool->substrate_vector =
		    (struct pool_substrate_vector *)substrate;
		pool->object_size = object_size;
		pool->constructor = constructor;
		pool->destructor = destructor;
		if (name)
			pool->name = gsh_strdup(name);
		else
			pool->name = NULL;
	}
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
 * @param[in] pool       The pool from which to allocate
 * @param[in] parameters Parameters to be supplied to the
 *                       constructor
 *
 * @return A pointer on success or NULL on failure.  This function
 *         returns void pointers.  Programmers who wish for more type
 *         safety can easily create static inline wrappers
 *         (alloc_client or similar) to return pointers of a specific
 *         type (and omitting the pool parameter).
 */

static inline void *
pool_alloc(pool_t *pool, void *parameters)
{
	void *object = pool->substrate_vector->allocator(pool);
	if (object && (pool->constructor))
		(pool->constructor) (object, parameters);

	return object;
}

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
 * @param[in] size  Size of the object (unused)
 * @param[in] param Parameters (there are no parameters, must be
 *                  NULL.)
 *
 * @return the allocated pool_t structure.
 */

static inline pool_t *
pool_basic_initializer(size_t size __attribute__ ((unused)),
		       void *param __attribute__ ((unused)))
{
	assert(param == NULL);	/* We take no parameters */
	return (pool_t *) gsh_malloc(sizeof(pool_t));
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
	gsh_free(pool);
}

/**
 * @brief Allocate an object from a basic pool
 *
 * This function just calls gsh_malloc to get an object of the
 * appropriate size.
 *
 * @param[in] pool The pool from which to allocate.
 *
 * @return the allocated object or NULL.
 */

static inline void *
pool_basic_alloc(pool_t *pool)
{
	if (pool->constructor)
		return gsh_malloc(pool->object_size);
	else
		return gsh_calloc(1, pool->object_size);
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
