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
 * \file    9p_setattr.c
 * \brief   9P version
 *
 * 9p_setattr.c : _9P_interpretor, request SETATTR
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

int _9p_setattr( _9p_request_data_t * preq9p, 
                  void  * pworker_data,
                  u32 * plenout, 
                  char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  u16  * msgtag     = NULL ;
  u32  * fid        = NULL ;
  u32  * valid      = NULL ;
  u32  * mode       = NULL ;
  u32  * uid        = NULL ;
  u32  * gid        = NULL ;
  u64  * size       = NULL ;
  u64  * atime_sec  = NULL ;
  u64  * atime_nsec = NULL ;
  u64  * mtime_sec  = NULL ;
  u64  * mtime_nsec = NULL ;


  _9p_fid_t * pfid = NULL ;

  fsal_attrib_list_t    fsalattr ;
  cache_inode_status_t  cache_status ;

  int rc = 0 ; 
  int err = 0 ;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 

  _9p_getptr( cursor, fid,        u32 ) ; 
  _9p_getptr( cursor, valid,      u32 ) ;
  _9p_getptr( cursor, mode,       u32 ) ;
  _9p_getptr( cursor, uid,        u32 ) ;
  _9p_getptr( cursor, gid,        u32 ) ;
  _9p_getptr( cursor, size,       u64 ) ;
  _9p_getptr( cursor, atime_sec,  u64 ) ;
  _9p_getptr( cursor, atime_nsec, u64 ) ;
  _9p_getptr( cursor, mtime_sec,  u64 ) ;
  _9p_getptr( cursor, mtime_nsec, u64 ) ;

  LogDebug( COMPONENT_9P, "TSETATTR: tag=%u fid=%u mode=0%o uid=%u gid=%u size=%llu atime=(%llu|%llu) mtime=(%llu|%llu)",
            (u32)*msgtag, *fid, *mode, *uid, *gid, *size, *atime_sec, *atime_nsec, *mtime_sec, *mtime_nsec  ) ;

  if( *fid >= _9P_FID_PER_CONN )
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

   pfid = &preq9p->pconn->fids[*fid] ;

   /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RSETATTR ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  return 1 ;
}

