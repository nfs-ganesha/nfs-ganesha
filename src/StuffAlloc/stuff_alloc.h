/**
 *
 * \file    stuff_alloc.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/05/03 07:24:33 $
 * \version $Revision: 1.4 $
 * \brief   Stuff allocators macros.
 *
 * stuff_alloc.h : Stuff allocators macros.
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/StuffAlloc/stuff_alloc.h,v 1.4 2005/05/03 07:24:33 deniel Exp $
 *
 * $Log: stuff_alloc.h,v $
 * Revision 1.4  2005/05/03 07:24:33  deniel
 * Using Buddy System instead of regular malloc/free
 *
 * Revision 1.3  2004/10/20 16:51:25  deniel
 * Comments added to stuuf allocator macros
 *
 * Revision 1.2  2004/10/19 14:40:26  deniel
 * Ok for UDP and TCP processing
 *
 * Revision 1.2  2004/10/18 08:43:49  deniel
 * Many changes in protootypes ans data structures
 * Adding some functions in preallocator
 *
 * Revision 1.1  2004/10/13 13:01:44  deniel
 * Now using the stuff allocator
 *
 *
 */

#ifndef _STUFF_ALLOC_H
#define _STUFF_ALLOC_H

#include <stdlib.h>

#ifdef _NO_BUDDY_SYSTEM
#define Mem_Alloc( a )  malloc( a )
#else
#include "BuddyMalloc.h"
#define Mem_Alloc( a ) BuddyMalloc( a )
#endif

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
#define STUFF_PREALLOC( pool, nb, type, name_next )                       \
do                                                                        \
{                                                                         \
  int i = 0 ;                                                             \
                                                                          \
  pool= NULL ;                                                            \
                                                                          \
  if( ( pool = ( type *)Mem_Alloc( sizeof( type ) * nb ) ) != NULL )         \
    {                                                                     \
      for( i = 0 ; i < nb ; i++ )                                         \
        {                                                                 \
          if( i != nb -1 )                                                \
            pool[i].name_next = &(pool[i+1]) ;                            \
          else                                                            \
            pool[i].name_next = NULL ;                                    \
        }                                                                 \
    }                                                                     \
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
#define GET_PREALLOC( entry, pool, nb, type, name_next )                  \
do                                                                        \
{                                                                         \
                                                                          \
  if( ( pool == NULL ) && ( nb != 0 ) )                                   \
    STUFF_PREALLOC( pool, nb, type, name_next ) ;                         \
                                                                          \
  if( pool != NULL )                                                      \
    {                                                                     \
      entry = pool ;                                                      \
      pool = entry->name_next ;                                           \
    }                                                                     \
  else                                                                    \
   entry = NULL ;                                                         \
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
#define RELEASE_PREALLOC( entry, pool, name_next )                        \
do                                                                        \
{                                                                         \
  entry->name_next = pool ;                                               \
  pool = entry ;                                                          \
} while( 0 )

#endif				/* _STUFF_ALLOC_H */
