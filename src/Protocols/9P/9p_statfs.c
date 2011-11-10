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
 * \file    9p_statfs.c
 * \brief   9P version
 *
 * 9p_statfs.c : _9P_interpretor, request ATTACH
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



int _9p_statfs( _9p_request_data_t * preq9p, 
                void  * pworker_data,
                u32 * plenout, 
                char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u64 * request_mask = NULL ;

  _9p_fid_t * pfid = NULL ;

  u32 type      = 0x6969 ; /* NFS_SUPER_MAGIC for wanting of better, FSAL do not return this information */
  u32 bsize     = DEV_BSIZE ;
  u64 * blocks  = NULL ;
  u64 * bfree   = NULL ;
  u64 * bavail  = NULL ;
  u64 * files   = NULL ;
  u64 * ffree   = NULL ;
  u64  fsid     = 0LL ;

  u32 namelen = MAXNAMLEN ;

  int rc = 0 ; 
  int err = 0 ;

  fsal_dynamicfsinfo_t dynamicinfo;
  cache_inode_status_t cache_status;

  if ( !preq9p || !pworker_data || !plenout || !preply )
   return -1 ;
  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 

  LogDebug( COMPONENT_9P, "TSTATFS: tag=%u fid=%u",
            (u32)*msgtag, *fid ) ;
 
  if( *fid >= _9P_FID_PER_CONN )
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

  pfid = &preq9p->pconn->fids[*fid] ;

  /* Get the FS's stats */
  if( cache_inode_statfs( pfid->pentry,
                          &dynamicinfo,
                          &pfid->fsal_op_context, 
                          &cache_status ) != CACHE_INODE_SUCCESS )
    {
       err = _9p_tools_errno( cache_status ) ; ;
       rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
       return rc ;
    }

  blocks  = (u64 *)&dynamicinfo.total_bytes ;
  bfree   = (u64 *)&dynamicinfo.free_bytes ;
  bavail  = (u64 *)&dynamicinfo.avail_bytes ;
  files   = (u64 *)&dynamicinfo.total_files ;
  ffree   = (u64 *)&dynamicinfo.free_files ;
  fsid    = (u64 )pfid->attr.st_dev ;

  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RSTATFS ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setvalue( cursor, type,    u32 ) ;
  _9p_setvalue( cursor, bsize,   u32 ) ;
  _9p_setptr( cursor, blocks,    u64 ) ;
  _9p_setptr( cursor, bfree,     u64 ) ;
  _9p_setptr( cursor, bavail,    u64 ) ;
  _9p_setptr( cursor, files,     u64 ) ;
  _9p_setptr( cursor, ffree,     u64 ) ;
  _9p_setvalue( cursor, fsid,    u64 ) ;
  _9p_setvalue( cursor, namelen, u32 ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RSTATFS: tag=%u fid=%u",
            (u32)*msgtag, *fid ) ;
 
  return 1 ;
}

