/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
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
 * \file    9p_walk.c
 * \brief   9P version
 *
 * 9p_walk.c : _9P_interpretor, request WALK
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include "nfs_core.h"
#include "stuff_alloc.h"
#include "log_macros.h"
#include "cache_inode.h"
#include "fsal.h"
#include "9p.h"


int _9p_walk( _9p_request_data_t * preq9p, 
                  void  * pworker_data,
                  u32 * plenout, 
                  char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u32 * newfid = NULL ;
  u16 * nwname = NULL ;
  u16 * wnames_len[_9P_MAXWELEM] ;
  char * wnames_str[_9P_MAXWELEM] ;

  u16 * nwqid ;

  unsigned int i = 0 ;
  int rc = 0 ;
  u32 err = 0 ;

  _9p_fid_t * pfid = NULL ;
  _9p_fid_t * pnewfid = NULL ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getptr( cursor, newfid, u32 ) ; 
  _9p_getptr( cursor, nwname, u16 ) ; 

  for( i = 0 ; i < *nwname ; i++ )
    _9p_getstr( cursor, wnames_len[i], wnames_str[i] ) ;

  LogDebug( COMPONENT_9P, "TWALK: tag=%u fid=%u newfid=%u nwname=%u",
            (u32)*msgtag, *fid, *newfid, *nwname ) ;

  if( ( pfid = _9p_hash_fid_get( preq9p->pconn, 
                                 *fid,
                                 &rc ) ) == NULL )
   {
     err = ENOENT ;
     rc = _9p_rerror( preq9p, msgtag, &err, strerror( err ), plenout, preply ) ;
     return rc ;
   }

 /* Get a new FID from the pool */
  P( pwkrdata->_9pfid_pool_mutex ) ;
  GetFromPool( pnewfid, &pwkrdata->_9pfid_pool, _9p_fid_t ) ;
  V( pwkrdata->_9pfid_pool_mutex ) ;

  if( pnewfid == NULL )
    {
      err = ENOMEM ;
      rc = _9p_rerror( preq9p, msgtag, &err, strerror( err ), plenout, preply ) ;
      return rc ;
    }

  /* Is this a lookup or a fid cloning operation ? */
  if( *nwname == 0 )
   {
      /* Cloning operation */
      memcpy( (char *)pnewfid, (char *)pfid, sizeof( _9p_fid_t ) ) ;
  
      /* Set the new fid id */
      pnewfid->fid = *newfid ;
   }
  else
   {
 	/* Not yet implement */
     printf( "============> NOT YET IMPLEMENTED !!!!!!!!\n" ) ;
   }

  /* Had the new fid to the hash */
  if( ( err = _9p_hash_fid_update( preq9p->pconn, pnewfid ) ) != 0 )
   {
     P( pwkrdata->_9pfid_pool_mutex ) ;
     ReleaseToPool( pnewfid, &pwkrdata->_9pfid_pool ) ;
     V( pwkrdata->_9pfid_pool_mutex ) ;

     rc = _9p_rerror( preq9p, msgtag, &err, strerror( err ), plenout, preply ) ;
     return rc ;
   }

  /* As much qid as requested fid */
  nwqid = nwname ;

   /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RWALK ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  
  _9p_setptr( cursor, nwqid, u16 ) ;
  for( i = 0 ; i < *nwqid ; i++ )
    {
      /* Des choses a faire ici */
    }

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RWALK: tag=%u fid=%u newfid=%u nwqid=%u",
            (u32)*msgtag, *fid, *newfid, *nwqid ) ;

  return 1 ;
}

