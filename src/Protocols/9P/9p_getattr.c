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
 * \file    9p_getattr.c
 * \brief   9P version
 *
 * 9p_getattr.c : _9P_interpretor, request ATTACH
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

u32 zero32 = 0 ;
u64 zero64 = 0LL ;

int _9p_getattr( _9p_request_data_t * preq9p, 
                  void  * pworker_data,
                  u32 * plenout, 
                  char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u64 * request_mask = NULL ;

  _9p_fid_t * pfid = NULL ;

  int rc = 0 ; 
  int err = 0 ;


  u64 * valid        = NULL ;
  u32 * mode         = NULL ;
  u32 * uid          = NULL ;
  u32 * gid          = NULL ;
  u64 * nlink        = NULL ;
  u64 * rdev         = NULL ;
  u64 * size         = NULL ;
  u64 *blksize       = NULL ; 
  u64 * blocks       = NULL ; 
  u64 * atime_sec    = NULL ;
  u64 * atime_nsec   = NULL ;
  u64 * mtime_sec    = NULL ;
  u64 * mtime_nsec   = NULL ;
  u64 * ctime_sec    = NULL ;
  u64 * ctime_nsec   = NULL ;
  u64 * btime_sec    = NULL;
  u64 * btime_nsec   = NULL;
  u64 * gen          = NULL;
  u64 * data_version = NULL;


  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;
  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getptr( cursor, request_mask, u64 ) ; 

  LogDebug( COMPONENT_9P, "TGETATTR: tag=%u fid=%u request_mask=0x%llx",
            (u32)*msgtag, *fid, (unsigned long long)*request_mask ) ;
 
  if( *fid >= _9P_FID_PER_CONN )
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

  pfid = &preq9p->pconn->fids[*fid] ;

  /* Attach point is found, build the requested attributes */
  
  valid = request_mask ; /* FSAL covers all 9P attributes */

  mode       = (*request_mask & _9P_GETATTR_RDEV)?(u32 *)&pfid->attr.st_mode:&zero32 ;
  uid        = (*request_mask & _9P_GETATTR_UID)?(u32 *)&pfid->attr.st_uid:&zero32 ;
  gid        = (*request_mask & _9P_GETATTR_GID)?(u32 *)&pfid->attr.st_gid:&zero32 ;
  nlink      = (*request_mask & _9P_GETATTR_NLINK)?(u64 *)&pfid->attr.st_nlink:&zero64 ;  
  rdev       = (*request_mask & _9P_GETATTR_RDEV)?(u64 *)&pfid->attr.st_rdev:&zero64 ; 
  size       = (*request_mask & _9P_GETATTR_SIZE)?(u64 *)&pfid->attr.st_size:&zero64 ; 
  blksize    = (*request_mask & _9P_GETATTR_BLOCKS)?(u64 *)&pfid->attr.st_blksize:&zero64 ; 
  blocks     = (*request_mask & _9P_GETATTR_BLOCKS)?(u64 *)&pfid->attr.st_blocks:&zero64 ; 
  atime_sec  = (*request_mask & _9P_GETATTR_ATIME )?(u64 *)&pfid->attr.st_atime:&zero64 ;
  atime_nsec = &zero64 ;
  mtime_sec  = (*request_mask & _9P_GETATTR_MTIME )?(u64 *)&pfid->attr.st_mtime:&zero64 ;
  mtime_nsec = &zero64 ;
  ctime_sec  = (*request_mask & _9P_GETATTR_CTIME )?(u64 *)&pfid->attr.st_ctime:&zero64 ;
  ctime_nsec = &zero64 ;

  /* Not yet supported attributes */
  btime_sec    = &zero64 ;
  btime_nsec   = &zero64 ;
  gen          = &zero64 ;
  data_version = &zero64 ;

   /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RGETATTR ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setptr( cursor, valid,               u64 ) ;
  _9p_setqid( cursor, pfid->qid ) ;
  _9p_setptr( cursor, mode,                u32 ) ;
  _9p_setptr( cursor, uid,                 u32 ) ;
  _9p_setptr( cursor, gid,                 u32 ) ;
  _9p_setptr( cursor, nlink,               u64 ) ;
  _9p_setptr( cursor, rdev,                u64 ) ;
  _9p_setptr( cursor, size,                u64 ) ;
  _9p_setptr( cursor, blksize,             u64 ) ;
  _9p_setptr( cursor, blocks,              u64 ) ;
  _9p_setptr( cursor, atime_sec,           u64 ) ;
  _9p_setptr( cursor, atime_nsec,          u64 ) ;
  _9p_setptr( cursor, mtime_sec,           u64 ) ;
  _9p_setptr( cursor, mtime_nsec,          u64 ) ;
  _9p_setptr( cursor, ctime_sec,           u64 ) ;
  _9p_setptr( cursor, ctime_nsec,          u64 ) ;
  _9p_setptr( cursor, btime_sec,           u64 ) ;
  _9p_setptr( cursor, btime_nsec,          u64 ) ;
  _9p_setptr( cursor, gen,                 u64 ) ;
  _9p_setptr( cursor, data_version,        u64 ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, 
            "RGETATTR: tag=%u valid=0x%llx qid=(type=%u,version=%u,path=%llu) mode=0%o uid=%u gid=%u nlink=%llu"
            " rdev=%llu size=%llu blksize=%llu blocks=%llu atime=(%llu,%llu) mtime=(%llu,%llu) ctime=(%llu,%llu)"
            " btime=(%llu,%llu) gen=%llu, data_version=%llu", 
            *msgtag, (unsigned long long)*valid, (u32)pfid->qid.type, pfid->qid.version, (unsigned long long)pfid->qid.path,
            *mode, *uid, *gid, (unsigned long long)*nlink, (unsigned long long)*rdev, (unsigned long long)*size,
            (unsigned long long)*blksize, (unsigned long long)*blocks,
            (unsigned long long)*atime_sec, (unsigned long long)*atime_nsec,
            (unsigned long long)*mtime_sec, (unsigned long long)*mtime_nsec, 
            (unsigned long long)*ctime_sec, (unsigned long long)*ctime_nsec,    
            (unsigned long long)*btime_sec, (unsigned long long)*btime_nsec, 
            (unsigned long long)*gen, (unsigned long long)*data_version )  ;
  return 1 ;
}

