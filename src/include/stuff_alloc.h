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

#ifdef _NO_BUDDY_SYSTEM

#include <errno.h>

#define Mem_Alloc( a )       malloc( a )
#define Mem_Calloc( s1, s2 ) calloc( s1, s2 )
#define Mem_Realloc( p, s )  realloc( p, s )
#define Mem_Free( a )        free( a )
#define Mem_Errno            errno

#define GetPreferedPool( _n, _s )  (_n)

#else

#include "BuddyMalloc.h"

#ifdef _DEBUG_MEMLEAKS
#  define Mem_Alloc( a )       BuddyMalloc_Autolabel( a , __FILE__, __FUNCTION__, __LINE__ )
#  define Mem_Calloc( s1, s2 ) BuddyCalloc_Autolabel( s1, s2, __FILE__, __FUNCTION__, __LINE__ )
#  define Mem_Realloc( p, s)   BuddyRealloc_Autolabel( (caddr_t)(p), s, __FILE__, __FUNCTION__, __LINE__ )
#else
#  define Mem_Alloc( a )       BuddyMallocExit( a )
#  define Mem_Calloc( s1, s2 ) BuddyCalloc( s1, s2 )
#  define Mem_Realloc( p, s)   BuddyRealloc( (caddr_t)(p), s )
#endif

#define Mem_Free( a )        BuddyFree((caddr_t) (a))
#define Mem_Errno            BuddyErrno

#define GetPreferedPool( _n, _s )  BuddyPreferedPoolCount( _n, _s)

#endif

#ifndef _NO_BLOCK_PREALLOC

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
#define STUFF_PREALLOC( _pool, _nb, _type, _name_next )                       \
do                                                                            \
{                                                                             \
  unsigned int _i = 0 ;                                                       \
  unsigned int _prefered = 0 ;                                                \
                                                                              \
  _prefered = GetPreferedPool( _nb, sizeof(_type) );                          \
  _pool= NULL ;                                                               \
                                                                              \
  if( ( _pool = ( _type *)Mem_Alloc( sizeof( _type ) * _prefered ) ) != NULL ) \
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
  _prefered = GetPreferedPool( _nb, sizeof(_type) );                       \
                                                                          \
  _pool = ( _type *)Mem_Calloc( _prefered, sizeof( _type ) ) ;            \
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

#define STUFF_PREALLOC( pool, nb, type, name_next )                       \
              do {                                                        \
                /* No pool management in this mode */                     \
                pool = NULL;                                              \
              } while(0)

#define GET_PREALLOC( entry, pool, nb, type, name_next )                  \
do                                                                        \
{                                                                         \
  entry = (type *)Mem_Alloc( sizeof( type ));                             \
  entry->name_next = NULL;                                                \
} while( 0 )

#define RELEASE_PREALLOC( entry, pool, name_next )     Mem_Free( entry )

#define STUFF_PREALLOC_CONSTRUCT( pool, nb, type, name_next, construct )   \
              do {                                                        \
                /* No pool management in this mode */                     \
                pool = NULL;                                              \
              } while(0)

#define GET_PREALLOC_CONSTRUCT( entry, pool, nb, type, name_next, construct ) \
do                                                                        \
{                                                                         \
  entry = (type *)Mem_Alloc( sizeof( type ));                             \
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
