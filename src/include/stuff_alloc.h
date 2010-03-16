/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
