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
#include "nfs_core.h"
#include "stuff_alloc.h"
#include "log_macros.h"
#include "cache_inode.h"
#include "fsal.h"
#include "9p.h"


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

  u32 zero32 = 0 ;
  u64 zero64 = 0LL ;

  u64 * valid = NULL ;
  u32 * mode  = NULL ;
  u32 * uid   = NULL ;
  u32 * gid   = NULL ;

  u64 dnlink = 0LL ;
  u64 * nlink = &dnlink ;

  u64 * rdev = NULL ;
  u64 * size = NULL ;

  u64 dblksize = 0ll ;
  u64 *blksize = &dblksize ;

  u64 dblocks = 0LL;
  u64 * blocks = &dblocks;

  u64 datime_sec = 0LL ;
  u64 * atime_sec = &datime_sec ;
  u64 datime_nsec = 0LL ;
  u64 * atime_nsec = &datime_nsec ;

  u64 dmtime_sec = 0LL ;
  u64 * mtime_sec = &dmtime_sec ;
  u64 dmtime_nsec = 0LL ;
  u64 * mtime_nsec = &dmtime_nsec ;

  u64 dctime_sec = 0LL ;
  u64 * ctime_sec = &dctime_sec ;
  u64 dctime_nsec = 0LL ;
  u64 * ctime_nsec = &dctime_nsec ;

  u64 * btime_sec  = NULL;
  u64 * btime_nsec  = NULL;
  u64 * gen  = NULL;
  u64 * data_version  = NULL;


  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;
  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getptr( cursor, request_mask, u64 ) ; 

  LogDebug( COMPONENT_9P, "TGETATTR: tag=%u fid=%u request_mask=0x%llx",
            (u32)*msgtag, *fid, (unsigned long long)*request_mask ) ;


  if( ( pfid = _9p_hash_fid_get( preq9p->pconn, 
                                 *fid,
                                 &rc ) ) == NULL )
   {
     err = ENOENT ;
     rc = _9p_rerror( preq9p, msgtag, &err, strerror( err ), plenout, preply ) ;
     return rc ;
   }

  /* Attach point is found, build the requested attributes */
  
  valid = request_mask ; /* FSAL covers all 9P attributes */
  mode  = (*request_mask && _9P_GETATTR_MODE)?&pfid->attr.mode:&zero32 ;
  uid   = (*request_mask && _9P_GETATTR_UID)?&pfid->attr.owner:&zero32 ;
  gid   = (*request_mask && _9P_GETATTR_GID)?&pfid->attr.group:&zero32 ;
  
  if(*request_mask && _9P_GETATTR_NLINK)
    dnlink = (u64)pfid->attr.numlinks ;
  else
    nlink = &zero64 ;
   
  rdev =  (*request_mask && _9P_GETATTR_RDEV)?(u64 *)&pfid->attr.rawdev:&zero64 ; 
  size =  (*request_mask && _9P_GETATTR_SIZE)?(u64 *)&pfid->attr.filesize:&zero64 ; 

  if( *request_mask && _9P_GETATTR_BLOCKS)
   {
     dblksize = (u64)4096 ;
     dblocks = (u64)((pfid->attr.filesize/4096)+1) ;
   }
  else
   {
      blksize = &zero64 ;
      blocks = &zero64 ;
   }

  if( *request_mask && _9P_GETATTR_ATIME )
   {
     datime_sec = (u64)&pfid->attr.atime.seconds ;
     datime_nsec = (u64)&pfid->attr.atime.nseconds ;
   }
  else
   {
      atime_sec = &zero64 ;
      atime_nsec = &zero64 ;
   }

  if( *request_mask && _9P_GETATTR_MTIME )
   {
     dmtime_sec = (u64)&pfid->attr.mtime.seconds ;
     dmtime_nsec = (u64)&pfid->attr.mtime.nseconds ;
   }
  else
   {
      mtime_sec = &zero64 ;
      mtime_nsec = &zero64 ;
   }

  if( *request_mask && _9P_GETATTR_CTIME )
   {
     dctime_sec = (u64)&pfid->attr.ctime.seconds ;
     dctime_nsec = (u64)&pfid->attr.ctime.nseconds ;
   }
  else
   {
      ctime_sec = &zero64 ;
      ctime_nsec = &zero64 ;
   }

  /* Not yet supported attributes */
  btime_sec = &zero64 ;
  btime_nsec = &zero64 ;
  gen = &zero64 ;
  data_version = &zero64 ;

   /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RATTACH ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setptr( cursor, valid,               u64 ) ;
  _9p_setptr( cursor, &pfid->qid.type,      u8 ) ;
  _9p_setptr( cursor, &pfid->qid.version,  u32 ) ;
  _9p_setptr( cursor, &pfid->qid.path,     u64 ) ;
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

