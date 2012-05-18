/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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

#ifndef _ABSTRACT_MEM_H
#define _ABSTRACT_MEM_H

#include <stdlib.h>
#include <string.h>

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
     void *p = NULL;
     posix_memalign(&p, a, n);
     return p;
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
 * @param[in] p  Block of memory to free.
 */
static inline void
gsh_free(void *p)
{
     free(p);
}

/**
 * @brief Object constructor
 *
 * Functions of this type are used to initialize objects newly
 * allocated from the pool.
 *
 * @param[out] object     The newly allocated object to be initialized
 * @param[in]  parameters Parameters to be used in initialization
 */

typedef void(*pool_constructor_t)(void *object,
                                  void *parameters);

/**
 * @brief Object destructor
 *
 * Functions of this type are used to finalize objects before they are
 * returned to the pool.
 *
 * @param[in,out] object The object to be finalized
 */

typedef void(*pool_destructor_t)(void *object);

/**
 * @brief Type representing a pool
 *
 * This type represents a memory pool.  it should be treated, by all
 * callers, as a completely abstract type.  The pointer should only be
 * stored or passed to pool functions.  The pointer should never be
 * referenced.  No assumptions about the size of the pointed-to type
 * should be made.
 */

typedef struct pool {
     size_t object_size;
     pool_constructor_t constructor;
     pool_destructor_t destructor;
} pool_t;

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
 * @param[in] name        The name of this pool
 * @param[in] object_size The size of objects to allocate
 * @param[in] constructor Function to be called on each new object
 * @param[in] destructor  Function to be called on each object before
 *                        destruction
 *
 * @return A pointer to the pool object.  This pointer must not be
 *         dereferenced.  It may be stored or supplied as an argument
 *         to the other pool functions.  It must not be supplied as an
 *         argument to gsh_free, rather it must be disposed of with
 *         pool_destroy.  NULL is returned on error.
 */

static inline pool_t*
pool_init(const char* name __attribute__((unused)),
          size_t object_size,
          pool_constructor_t constructor,
          pool_destructor_t destructor)
{
     pool_t *pool = gsh_calloc(1, sizeof(pool_t));
     if (pool) {
          pool->object_size = object_size;
          pool->constructor = constructor;
          pool->destructor = destructor;
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
destroy_pool(pool_t *pool)
{
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

static inline void*
pool_alloc(pool_t *pool, void *parameters)
{
     void *object = gsh_calloc(1, pool->object_size);
     if (object && (pool->constructor)) {
          (pool->constructor)(object, parameters);
     }
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
pool_free(pool_t *pool, void* object)
{
     if (pool->destructor) {
          (pool->destructor)(object);
     }
     gsh_free(object);
}

#endif /* _ABSTRACT_MEM_H */
