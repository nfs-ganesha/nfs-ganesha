/*
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file    stuff_alloc.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 15:06:30 $
 * \version $Revision: 1.18 $
 * \brief   Stuff allocators macros.
 *
 * stuff_alloc.h : Stuff allocators macros.
 *
 *
 */

#ifndef _STUFF_ALLOC_H
#define _STUFF_ALLOC_H

#include <stdlib.h>

typedef void (*constructor)(void *entry);
struct prealloc_pool;

#ifdef _NO_BUDDY_SYSTEM

#include <malloc.h>
#include <errno.h>

#define Mem_Alloc( a )                  malloc( a )
#define Mem_Calloc( s1, s2 )            calloc( s1, s2 )
#define Mem_Realloc( p, s )             realloc( p, s )
#define Mem_Alloc_Label( a, lbl )       malloc( a )
#define Mem_Calloc_Label( s1, s2, lbl ) calloc( s1, s2 )
#define Mem_Realloc_Label( p, s, lbl)   realloc( p, s )
#define Mem_Free( a )                   free( a )
#define Mem_Free_Label( a, lbl )        free( a )
#define Mem_Errno                       errno
#define Str_Dup( a )                    strdup( a )

#define GetPreferedPool( _n, _s )  (_n)

#else

#include "BuddyMalloc.h"

#ifdef _DEBUG_MEMLEAKS
#  define Mem_Alloc( a )                  BuddyMalloc_Autolabel( a , __FILE__, __FUNCTION__, __LINE__, "BuddyMalloc" )
#  define Mem_Calloc( s1, s2 )            BuddyCalloc_Autolabel( s1, s2, __FILE__, __FUNCTION__, __LINE__, "BuddyCalloc" )
#  define Mem_Realloc( p, s)              BuddyRealloc_Autolabel( (caddr_t)(p), s, __FILE__, __FUNCTION__, __LINE__, "BuddyRealloc" )
#  define Mem_Alloc_Label( a, lbl )       BuddyMalloc_Autolabel( a , __FILE__, __FUNCTION__, __LINE__, lbl )
#  define Mem_Calloc_Label( s1, s2, lbl ) BuddyCalloc_Autolabel( s1, s2, __FILE__, __FUNCTION__, __LINE__, lbl )
#  define Mem_Realloc_Label( p, s, lbl)   BuddyRealloc_Autolabel( (caddr_t)(p), s, __FILE__, __FUNCTION__, __LINE__, lbl )
#  define Mem_Free( a )                   BuddyFree_Autolabel( (caddr_t) (a), __FILE__, __FUNCTION__, __LINE__, "BuddyFree" )
#  define Mem_Free_Label( a, lbl )        BuddyFree_Autolabel( (caddr_t) (a), __FILE__, __FUNCTION__, __LINE__, lbl )
#  define Str_Dup( a )                    BuddyStr_Dup_Autolabel( a , __FILE__, __FUNCTION__, __LINE__, "BuddyMalloc" )
#  define Str_Dup_Label( a, lbl )         BuddyStr_Dup_Autolabel( a , __FILE__, __FUNCTION__, __LINE__, lbl )
#else
#  define Mem_Alloc( a )                  BuddyMallocExit( a )
#  define Mem_Calloc( s1, s2 )            BuddyCalloc( s1, s2 )
#  define Mem_Realloc( p, s)              BuddyRealloc( (caddr_t)(p), s )
#  define Mem_Alloc_Label( a, lbl )       BuddyMallocExit( a )
#  define Mem_Calloc_Label( s1, s2, lbl ) BuddyCalloc( s1, s2 )
#  define Mem_Realloc_Label( p, s, lbl)   BuddyRealloc( (caddr_t)(p), s )
#  define Mem_Free( a )                   BuddyFree( (caddr_t) (a) )
#  define Mem_Free_Label( a, lbl )        BuddyFree( (caddr_t) (a) )
#  define Str_Dup( a )                    BuddyStr_Dup_Exit( a )
#  define Str_Dup_Label( a, lbl )         BuddyStr_Dup_Exit( a )
#endif

#define Mem_Errno            BuddyErrno

#define GetPreferedPool( _n, _s )  BuddyPreferedPoolCount( _n, _s)

#endif

/*******************************************************************************
 *
 * block preallocation
 *
 ******************************************************************************/

#ifndef _NO_BLOCK_PREALLOC

typedef struct prealloc_header
{
  struct prealloc_header *pa_next;  // next pool entry in free lists (or self if in use)
#ifdef _DEBUG_MEMLEAKS
  int                     pa_inuse; // flag indicating block is in use
  struct prealloc_header *pa_nextb; // next pool entry in buddy block
  struct prealloc_pool   *pa_pool;  // owning pool
#endif
} prealloc_header;

#define size_prealloc_header64 ((ptrdiff_t) ( (sizeof(prealloc_header) + 7) & ~7 ))
#define get_prealloc_entry(header, type) ((type *) ((ptrdiff_t) header + size_prealloc_header64))
#define get_prealloc_header(entry) ((prealloc_header *) ((ptrdiff_t) entry - size_prealloc_header64))

typedef struct prealloc_pool
{
#ifdef _DEBUG_MEMLEAKS
  struct prealloc_pool   *pa_next_pool;   // next pool
  char                    pa_name[256];   // name of pool
  char                   *pa_type;        // data type stored in pool
  int                     pa_used;        // number of entries in use
  int                     pa_high;        // high water mark of used entries
#endif
  struct prealloc_header *pa_free;        // free list
  constructor             pa_constructor; // constructor
  constructor             pa_destructor;  // destructor
  size_t                  pa_size;        // size of entry
  int                     pa_num;         // optimized number of entries per block
  int                     pa_blocks;      // number of blocks allocated
  int                     pa_allocated;   // number of entries preallocated
} prealloc_pool;

#define IsPoolPreallocated(pool) ((pool)->pa_num == 0 || (pool)->pa_allocated > 0)

/*******************************************
 *
 * No Buddy System or No debug of memleaks
 *
 ******************************************/
 
#if defined(_NO_BUDDY_SYSTEM) || !defined(_DEBUG_MEMLEAKS)

/**
 *
 * FillPool: Allocates entries for a pool of pre-allocated entries.
 *
 * This macro Allocates a pool of pre-allocated entries. It calls malloc to get
 * the pool as an arry and then chains all the entries together. If a
 * constructor has been defined for the pool, it will be invoked on each entry.
 *
 * If BuddyMalloc is used, it is supposed to be already initialised.
 *
 * @param pool the preallocted pool that we want to fill.
 * @param fi   dummy parameter for the file
 * @param fu   dummy parameter for the function
 * @param li   dummy parameter for the line number
 * @param str  dummy parameter for the string version of the type
 *
 * @return  nothing (this is a macro)
 *
 */
#define FillPool(pool, fi, fu, li, str)                      \
do {                                                         \
  int size = (pool)->pa_size + size_prealloc_header64;       \
  char *mem = (char *) Mem_Calloc((pool)->pa_num, size);     \
  int num = (pool)->pa_num;                                  \
                                                             \
  if (mem != NULL)                                           \
    {                                                        \
      (pool)->pa_allocated += num;                           \
      (pool)->pa_blocks++;                                   \
      while (num > 0)                                        \
        {                                                    \
          prealloc_header *h = (prealloc_header *) mem;      \
          h->pa_next  = (pool)->pa_free;                     \
          (pool)->pa_free = h;                               \
          mem += size;                                       \
          if((pool)->pa_constructor != NULL)                 \
            (pool)->pa_constructor(get_prealloc_entry(h, void)); \
          num--;                                             \
        }                                                    \
    }                                                        \
} while (0)

/**
 *
 * InitPool: Initializes a pool of pre-allocated entries.
 *
 * @param pool      the preallocted pool that we want to init.
 * @param num_alloc the number of entries to be allocated at once
 * @param type      the type of the entries to be allocated.
 * @param ctor      the constructor for the objects
 * @param dtor      the destructor for the entries
 *
 * @return  nothing (this is a macro)
 *
 */
#define InitPool(pool, num_alloc, type, ctor, dtor)          \
do {                                                         \
  int size;                                                  \
  memset((pool), 0, sizeof(*(pool)));                        \
  (pool)->pa_free        = NULL;                             \
  (pool)->pa_constructor = ctor;                             \
  (pool)->pa_destructor  = dtor;                             \
  (pool)->pa_size        = sizeof(type);                     \
  size = (pool)->pa_size + size_prealloc_header64;           \
  (pool)->pa_num         = GetPreferedPool(num_alloc, size); \
  (pool)->pa_allocated   = 0;                                \
} while (0)

/**
 *
 * NamePool: Names a pool of pre-allocated entries (for debug)
 *
 * @param pool the preallocted pool that we want to init.
 * @param fmt  sprintf format
 * @param args sprintf args
 *
 * @return  nothing (this is a macro)
 *
 */
#define NamePool(pool, fmt, args...)

/**
 *
 * GetFromPool: Gets an entry in a preallocated pool. 
 *
 * This macro is used to get an entry from a pre-allocated pool. If the pool is
 * empty, the macro FillPool will be called with the same type to extend the
 * pool. If pa_num is set to zero during this call, FillPool is not called and
 * no entry is get from the pool that is empty.
 *
 * @param entry the entry we need.
 * @param pool the preallocted pool that we want to fetch from.
 * @param type the type of the entries to be allocated.
 *
 * @return  nothing (this is a macro), but entry will be NULL if an error occurs. 
 *
 */
#define GetFromPool(entry, pool, type)                       \
do {                                                         \
  if ((pool)->pa_free == NULL)                               \
    FillPool(pool, __FILE__, __FUNCTION__, __LINE__, # type);\
  if ((pool)->pa_free != NULL)                               \
    {                                                        \
      prealloc_header *h = (pool)->pa_free;                  \
      (pool)->pa_free = h->pa_next;                          \
      h->pa_next = h;                                        \
      entry = get_prealloc_entry(h, type);                   \
    }                                                        \
  else                                                       \
    entry = NULL;                                            \
} while (0)

/**
 *
 * ReleaseToPool: Releases an entry and puts it back to the pool.
 *
 * When an entry is no used any more, this macro is used to put it back to the
 * pool, so that it could be reuse later.
 *
 * @param entry the entry to be released.
 * @param pool the pool to which the entry belongs.
 *
 * @return nothing (this is a macro).
 *
 */
#define ReleaseToPool(entry, pool)                           \
do {                                                         \
  prealloc_header *h = get_prealloc_header(entry);           \
  if ((pool)->pa_destructor != NULL)                         \
    (pool)->pa_destructor(entry);                            \
  h->pa_next = (pool)->pa_free;                              \
  (pool)->pa_free = h;                                       \
} while (0)

#else

/*******************************************
 *
 * Buddy System with debug of memleaks
 *
 ******************************************/

/**
 *
 * FillPool: Allocates entries for a pool of pre-allocated entries.
 *
 * This macro Allocates a pool of pre-allocated entries. It calls malloc to get
 * the pool as an arry and then chains all the entries together. If a
 * constructor has been defined for the pool, it will be invoked on each entry.
 *
 * If BuddyMalloc is used, it is supposed to be already initialised.
 *
 * @param pool     the preallocted pool that we want to fill.
 * @param file     file FillPool call is in
 * @param function function FillPool call is in
 * @param line     line FillPool call is on
 * @param str      parameter for the string version of the type
 *
 * @return  nothing (this is a macro)
 *
 */
void FillPool(struct prealloc_pool *pool,
              const char           *file,
              const char           *function,
              const unsigned int    line,
              const char           *str);

/**
 *
 * InitPool: Initializes a pool of pre-allocated entries.
 *
 * @param pool      the preallocted pool that we want to init.
 * @param num_alloc the number of entries to be allocated at once
 * @param type      the type of the entries to be allocated.
 * @param ctor      the constructor for the objects
 * @param dtor      the destructor for the entries
 *
 * @return  nothing (this is a macro)
 *
 */
void _InitPool(struct prealloc_pool *pool,
               int                   num_alloc,
               int                   size_type,
               constructor           ctor,
               constructor           dtor,
               char                 *type);

#define InitPool(pool, num_alloc, type, ctor, dtor)          \
  _InitPool(pool, num_alloc, sizeof(type), ctor, dtor, # type)

/**
 *
 * NamePool: Names a pool of pre-allocated entries (for debug)
 *
 * @param pool the preallocted pool that we want to init.
 * @param fmt  sprintf format
 * @param args sprintf args
 *
 * @return  nothing (this is a macro)
 *
 */
#define NamePool(pool, fmt, args...)                         \
  snprintf((pool)->pa_name, 256, fmt, ## args)

/**
 *
 * GetFromPool: Gets an entry in a preallocated pool. 
 *
 * This macro is used to get an entry from a pre-allocated pool. If the pool is
 * empty, the macro FillPool will be called with the same type to extend the
 * pool. If pa_num is set to zero during this call, FillPool is not called and
 * no entry is get from the pool that is empty.
 *
 * @param entry the entry we need.
 * @param pool the preallocted pool that we want to fetch from.
 * @param type the type of the entries to be allocated.
 *
 * @return  nothing (this is a macro), but entry will be NULL if an error occurs. 
 *
 */
#define GetFromPool(entry, pool, type)                       \
do {                                                         \
  if ((pool)->pa_free == NULL && (pool)->pa_num != 0)        \
    FillPool(pool, __FILE__, __FUNCTION__, __LINE__, # type);\
  if ((pool)->pa_free != NULL)                               \
    {                                                        \
      prealloc_header *h = (pool)->pa_free;                  \
      (pool)->pa_free = h->pa_next;                          \
      h->pa_next = h;                                        \
      h->pa_inuse = 1;                                       \
      entry = get_prealloc_entry(h, type);                   \
      (pool)->pa_used++;                                     \
      if ((pool)->pa_used > (pool)->pa_high)                 \
        (pool)->pa_high = (pool)->pa_used;                   \
    }                                                        \
  else                                                       \
    entry = NULL;                                            \
} while (0)

/**
 *
 * ReleaseToPool: Releases an entry and puts it back to the pool.
 *
 * When an entry is no used any more, this macro is used to put it back to the
 * pool, so that it could be reuse later.
 *
 * @param entry the entry to be released.
 * @param pool the pool to which the entry belongs.
 *
 * @return nothing (this is a macro).
 *
 */
#define ReleaseToPool(entry, pool)                           \
do {                                                         \
  prealloc_header *h = get_prealloc_header(entry);           \
  if ((pool)->pa_destructor != NULL)                         \
    (pool)->pa_destructor(entry);                            \
  h->pa_next = (pool)->pa_free;                              \
  h->pa_inuse = 0;                                           \
  (pool)->pa_free = h;                                       \
  (pool)->pa_used--;                                         \
} while (0)

#endif

/**
 *
 * MakePool: Initializes and fills a pool of pre-allocated entries.
 *
 * @param pool      the preallocted pool that we want to init.
 * @param num_alloc the number of entries to be allocated at once
 * @param type      the type of the entries to be allocated.
 * @param ctor      the constructor for the objects
 * @param dtor      the destructor for the entries
 *
 * @return  nothing (this is a macro)
 *
 */
#define MakePool(pool, num_alloc, type, ctor, dtor)          \
do {                                                         \
  InitPool(pool, num_alloc, type, ctor, dtor);               \
  FillPool(pool, __FILE__, __FUNCTION__, __LINE__, # type);  \
} while (0)

#else 

/*******************************************************************************
 *
 * no block preallocation
 *
 ******************************************************************************/

typedef struct prealloc_pool
{
  constructor             pa_constructor;
  constructor             pa_destructor;
} prealloc_pool;

/* Don't care if pool is pre-allocated */
#define IsPoolPreallocated(pool) (1)
    
#define InitPool(pool, num_alloc, type, ctor, dtor)          \
do {                                                         \
  (pool)->pa_constructor = ctor;                             \
  (pool)->pa_destructor  = dtor;                             \
} while (0)

#define MakePool(pool, num_alloc, type, ctor, dtor)          \
  InitPool(pool, num_alloc, type, ctor, dtor)

#define NamePool(pool, fmt, args...)

#define GetFromPool(entry, pool, type)                       \
do {                                                         \
  entry = (type *)Mem_Alloc_Label(sizeof(type), # type);     \
  if ((pool)->pa_constructor != NULL)                        \
    (pool)->pa_constructor(entry);                           \
} while (0)

#define ReleaseToPool(entry, pool)                           \
do {                                                         \
  if ((pool)->pa_destructor != NULL)                         \
    (pool)->pa_destructor(entry);                            \
  Mem_Free(entry);                                           \
} while (0)

#endif                          /* no block preallocation */

#endif                          /* _STUFF_ALLOC_H */
