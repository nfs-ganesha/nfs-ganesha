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
#else
#  define Mem_Alloc( a )                  BuddyMallocExit( a )
#  define Mem_Calloc( s1, s2 )            BuddyCalloc( s1, s2 )
#  define Mem_Realloc( p, s)              BuddyRealloc( (caddr_t)(p), s )
#  define Mem_Alloc_Label( a, lbl )       BuddyMallocExit( a )
#  define Mem_Calloc_Label( s1, s2, lbl ) BuddyCalloc( s1, s2 )
#  define Mem_Realloc_Label( p, s, lbl)   BuddyRealloc( (caddr_t)(p), s )
#  define Mem_Free( a )                   BuddyFree( (caddr_t) (a) )
#  define Mem_Free_Label( a, lbl )        BuddyFree( (caddr_t) (a) )
#endif

#ifdef _DEBUG_MEMLEAKS
void FillPool(struct prealloc_pool *pool,
              const char           *file,
              const char           *function,
              const unsigned int    line,
              const char           *str);
void _InitPool(struct prealloc_pool *pool,
               int                   num_alloc,
               int                   size_type,
               constructor           ctor,
               constructor           dtor,
               char                 *type);
#endif

#define Mem_Errno            BuddyErrno

#define GetPreferedPool( _n, _s )  BuddyPreferedPoolCount( _n, _s)

#endif

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

#define IsPoolPreallocated(pool) ((pool)->pa_allocated > 0)

#if defined(_NO_BUDDY_SYSTEM) || !defined(_DEBUG_MEMLEAKS)
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
            (pool)->pa_constructor(h + 1);                   \
          num--;                                             \
        }                                                    \
    }                                                        \
} while (0)

#define InitPool(pool, num_alloc, type, ctor, dtor)          \
do {                                                         \
  int size;                                                  \
  (pool)->pa_free        = NULL;                             \
  (pool)->pa_constructor = ctor;                             \
  (pool)->pa_destructor  = dtor;                             \
  (pool)->pa_size        = sizeof(type);                     \
  size = (pool)->pa_size + size_prealloc_header64;           \
  (pool)->pa_num         = GetPreferedPool(num_alloc, size); \
} while (0)

#define NamePool(pool, fmt, args...)

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

#define ReleaseToPool(entry, pool)                           \
do {                                                         \
  prealloc_header *h = get_prealloc_header(entry);           \
  if ((pool)->pa_destructor != NULL)                         \
    (pool)->pa_destructor(entry);                            \
  h->pa_next = (pool)->pa_free;                              \
  (pool)->pa_free = h;                                       \
} while (0)

#define LogPoolData(component, pool)

#else

#define InitPool(pool, num_alloc, type, ctor, dtor)          \
  _InitPool(pool, num_alloc, sizeof(type), ctor, dtor, # type)

#define NamePool(pool, fmt, args...)                         \
  snprintf((pool)->pa_name, 256, fmt, ## args)

#define GetFromPool(entry, pool, type)                       \
do {                                                         \
  if ((pool)->pa_free == NULL)                               \
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

#define LogPoolData(component, pool)                                \
  LogDebug(component,                                               \
           "Pool %s NumBlocks=%d Num/Block=%d SizeOfEntry=%d NumAllocated=%d NumInUse=%d MaxInUse=%d", \
           (pool)->pa_type,                                         \
           (pool)->pa_blocks, (pool)->pa_num, (pool)->pa_size,      \
           (pool)->pa_allocated, (pool)->pa_used, (pool)->pa_high);

#endif

#define MakePool(pool, num_alloc, type, ctor, dtor)          \
do {                                                         \
  InitPool(pool, num_alloc, type, ctor, dtor);               \
  FillPool(pool, __FILE__, __FUNCTION__, __LINE__, # type);  \
} while (0)

/**
 *
 * STUFF_PREALLOC: Allocates a pool of pre-allocated entries.
 *
 * This macro Allocates a pool of pre-allocated entries. It calls
 * malloc to get the spool as an arry and then chains all the
 * entries together. Each entry is supposed to have a specific
 * 'next' field, a pointer to an object of its own type, to be
 * used as a pointer to the next entry in the pool.
 *
 * If BuddyMalloc is used, it is supposed to be already initialised.
 *
 * @param pool the preallocted pool that we want to init.
 * @param nb the number of entries to be allocated.
 * @param type the type of the entries to be allocated.
 * @param name_next the name of the field, in structure of type 'type' which pointer to the next entry.
 *
 * @return  nothing (this is a macro), but pool will be NULL if an error occures. 
 *
 */
#define STUFF_PREALLOC( _pool, _nb, _type, _name_next)                        \
do                                                                            \
{                                                                             \
  unsigned int _i = 0 ;                                                       \
  unsigned int _prefered = 0 ;                                                \
                                                                              \
  _prefered = GetPreferedPool( _nb, sizeof(_type) );                          \
  _pool= NULL ;                                                               \
                                                                              \
  if( ( _pool = ( _type *)Mem_Calloc_Label( _prefered, sizeof( _type ), # _type ) ) != NULL ) \
    {                                                                         \
      for( _i = 0 ; _i < ( unsigned int)_prefered ; _i++ )                    \
        {                                                                     \
          if( _i != _prefered -1 )                                            \
            _pool[_i]._name_next = &(_pool[_i+1]) ;                           \
          else                                                                \
            _pool[_i]._name_next = NULL ;                                     \
        }                                                                     \
    }                                                                         \
} while( 0 )


/**
 *
 * GET_PREALLOC: Gets an entry in a preallocated pool. 
 *
 * This macro is used to get an entry from a pre-allocated pool. If the pool is empty,
 * the macro STUFF_PREALLOC will be called with the same last four arguments to extend 
 * the pool. If nb is set to zero during this call, STUFF_PREALLOC is not called and no 
 * entry is get from the pool that is empty.
 *
 * @param entry the entry we need.
 * @param pool the preallocted pool that we want to init.
 * @param nb the number of entries to be allocated.
 * @param type the type of the entries to be allocated.
 * @param name_next the name of the field, in structure of type 'type' which pointer to the next entry.
 *
 * @return  nothing (this is a macro), but entry will be NULL if an error occures. 
 *
 */
#define GET_PREALLOC( _entry, _pool, _nb, _type, _name_next )             \
do                                                                        \
{                                                                         \
                                                                          \
  if( ( _pool == NULL ) && ( _nb != 0 ) )                                 \
    STUFF_PREALLOC( _pool, _nb, _type, _name_next ) ;                     \
                                                                          \
  if( _pool != NULL )                                                     \
    {                                                                     \
      _entry = _pool ;                                                    \
      _pool = _entry->_name_next ;                                        \
    }                                                                     \
  else                                                                    \
   _entry = NULL ;                                                        \
} while( 0 )

/**
 *
 * RELEASE_PREALLOC: Releases an entry and puts it back to the pool.
 *
 * When an entry is no used any more, this macro is used to put it 
 * back to the pool, so that it could be reuse later. The released 
 * entry is chained to the pool, through the 'name_next' field.
 *
 * @param entry the entry to be released.
 * @param pool the pool to which the entry belongs.
 * @param name_next the name of the field, in structure of type 'type' which pointer to the next entry.
 *
 * @return nothing (this is a macro).
 *
 */
#define RELEASE_PREALLOC( _entry, _pool, _name_next )                     \
do                                                                        \
{                                                                         \
  _entry->_name_next = _pool ;                                            \
  _pool = _entry ;                                                        \
} while( 0 )

/**
 *
 * STUFF_PREALLOC_CONSTRUCT: Allocates a pool of pre-allocated entries with a call to a constructor.
 *
 * This macro Allocates a pool of pre-allocated entries. It calls
 * malloc to get the spool as an arry and then chains all the
 * entries together. Each entry is supposed to have a specific
 * 'next' field, a pointer to an object of its own type, to be
 * used as a pointer to the next entry in the pool.
 *
 * If BuddyMalloc is used, it is supposed to be already initialised.
 *
 * @param pool the preallocted pool that we want to init.
 * @param nb the number of entries to be allocated.
 * @param type the type of the entries to be allocated.
 * @param name_next the name of the field, in structure of type 'type' which pointer to the next entry.
 * @param construct the constructor for the object: takes a pointer to void as argument and returns void
 *
 * @return  nothing (this is a macro), but pool will be NULL if an error occures.
 *
 */
#define STUFF_PREALLOC_CONSTRUCT( _pool, _nb, _type, _name_next, _construct )  \
do                                                                        \
{                                                                         \
  unsigned int _i = 0 ;                                                   \
  unsigned int _prefered = 0 ;                                            \
                                                                          \
  _prefered = GetPreferedPool( _nb, sizeof(_type) );                      \
                                                                          \
  _pool = ( _type *)Mem_Calloc_Label( _prefered, sizeof( _type ), # _type ) ; \
                                                                          \
  if( _pool != NULL )                                                     \
    {                                                                     \
      for( _i = 0 ; _i < _prefered ; _i++ )                               \
        {                                                                 \
          if( _i != _prefered -1 )                                        \
            _pool[_i]._name_next = &(_pool[_i+1]) ;                       \
          else                                                            \
            _pool[_i]._name_next = NULL ;                                 \
                                                                          \
          if( _construct != NULL )                                        \
            _construct( (void *)&(_pool[_i]) )  ;                         \
                                                                          \
        }                                                                 \
    }                                                                     \
} while( 0 )

/**
 *
 * GET_PREALLOC_CONSTRUCT: Gets an entry in a preallocated pool.
 *
 * This macro is used to get an entry from a pre-allocated pool. If the pool is empty,
 * the macro STUFF_PREALLOC will be called with the same last four arguments to extend
 * the pool. If nb is set to zero during this call, STUFF_PREALLOC is not called and no
 * entry is get from the pool that is empty.
 *
 * @param entry the entry we need.
 * @param pool the preallocted pool that we want to init.
 * @param nb the number of entries to be allocated.
 * @param type the type of the entries to be allocated.
 * @param name_next the name of the field, in structure of type 'type' which pointer to the next entry.
 * @param construct the constructor for the object: takes a pointer to void as argument and returns void
 *
 * @return  nothing (this is a macro), but entry will be NULL if an error occures.
 *
 */
#define GET_PREALLOC_CONSTRUCT( _entry, _pool, _nb, _type, _name_next, _construct )  \
do                                                                             \
{                                                                              \
  if( ( _pool == NULL ) && ( _nb != 0 ) )                                      \
    STUFF_PREALLOC_CONSTRUCT( _pool, _nb, _type, _name_next, _construct ) ;    \
                                                                               \
  if( _pool != NULL )                                                          \
    {                                                                          \
      _entry = _pool ;                                                         \
      _pool = _entry->_name_next ;                                             \
    }                                                                          \
  else                                                                         \
   _entry = NULL ;                                                             \
} while( 0 )

/**
 *
 * RELEASE_PREALLOC_DESTRUCT: Releases an entry and puts it back to the pool with a destructor.
 *
 * When an entry is no used any more, this macro is used to put it
 * back to the pool, so that it could be reuse later. The released
 * entry is chained to the pool, through the 'name_next' field.
 *
 * @param entry the entry to be released.
 * @param pool the pool to which the entry belongs.
 * @param name_next the name of the field, in structure of type 'type' which pointer to the next entry.
 * @param destruct the destructor for the object: takes a pointer to void as argument and returns void
 *
 * @return nothing (this is a macro).
 *
 */
#define RELEASE_PREALLOC_DESTRUCT( entry, pool, name_next, destruct )     \
do                                                                        \
{                                                                         \
  destruct( (void *)entry ) ;                                             \
  entry->name_next = pool ;                                               \
  pool = entry ;                                                          \
} while( 0 )

#else                           /* no block preallocation */

typedef struct prealloc_pool
{
  constructor             pa_constructor;
  constructor             pa_destructor;
}

/* Don't care if pool is pre-allocated */
#define IsPoolPreallocated(pool) (1)

#define LogPoolData(component, pool)
    
#define InitPool(pool, num_alloc, type, ctor, dtor)          \
do {                                                         \
  (pool)->pa_constructor = ctor;                             \
  (pool)->pa_destructor  = dtor;                             \
} while (0)

#define MakePool(pool, num_alloc, type, ctor, dtor)          \
  InitPool(pool, num_alloc, type, ctor, dtor)

#define GetFromPool(entry, pool, type)                       \
do {                                                         \
  entry = (type *)Mem_Alloc_Label(sizeof(type), # type);     \
  if ((pool)->pa_constructor != NULL)                        \
    (pool)->pa_constructor(entry);                           \
} while (0)

#define ReleaseToPool(entry, pool, type)                     \
do {                                                         \
  if ((pool)->pa_destructor != NULL)                         \
    (pool)->pa_destructor(entry);                            \
  Mem_Free_Label(entry, # type);                             \
} while (0)

#define STUFF_PREALLOC( pool, nb, type, name_next )                       \
              do {                                                        \
                /* No pool management in this mode */                     \
                pool = NULL;                                              \
              } while(0)

#define GET_PREALLOC( entry, pool, nb, type, name_next )                  \
do                                                                        \
{                                                                         \
  entry = (type *)Mem_Alloc_Label( sizeof( type ), # _type );             \
  entry->name_next = NULL;                                                \
} while( 0 )

#define RELEASE_PREALLOC( entry, pool, name_next ) Mem_Free( entry )

#define STUFF_PREALLOC_CONSTRUCT( pool, nb, type, name_next, construct )  \
              do {                                                        \
                /* No pool management in this mode */                     \
                pool = NULL;                                              \
              } while(0)

#define GET_PREALLOC_CONSTRUCT( entry, pool, nb, type, name_next, construct ) \
do                                                                        \
{                                                                         \
  entry = (type *)Mem_Alloc_Label( sizeof( type ), # _type );             \
  construct( (void *)(entry) );                                           \
  entry->name_next = NULL;                                                \
} while( 0 )

#define RELEASE_PREALLOC_DESTRUCT( entry, pool, name_next, destruct )     \
do                                                                        \
{                                                                         \
  destruct( (void *)(entry) ) ;                                           \
  Mem_Free( entry );                                                      \
} while( 0 )

#endif                          /* no block preallocation */

#endif                          /* _STUFF_ALLOC_H */
