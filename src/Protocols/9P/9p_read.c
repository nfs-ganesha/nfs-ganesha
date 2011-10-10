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
 * \file    9p_read.c
 * \brief   9P version
 *
 * 9p_read.c : _9P_interpretor, request READ
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

char __thread databuffer[_9p_READ_BUFFER_SIZE] ;

int _9p_read( _9p_request_data_t * preq9p, 
              void  * pworker_data,
              u32 * plenout, 
              char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  int rc = 0 ;
  u32 err = 0 ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u64 * offset = NULL ;
  u32 * count  = NULL ;
  u32 outcount = 0 ;

  _9p_fid_t * pfid = NULL ;

  fsal_seek_t seek_descriptor;
  fsal_size_t size;
  fsal_size_t read_size = 0;
  fsal_attrib_list_t attr;
  fsal_boolean_t eof_met;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  uint64_t stable_flag = FSAL_SAFE_WRITE_TO_FS;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getptr( cursor, offset, u64 ) ; 
  _9p_getptr( cursor, count,  u32 ) ; 
  
  LogDebug( COMPONENT_9P, "TREAD: tag=%u fid=%u offset=%llu count=%u",
            (u32)*msgtag, *fid, (unsigned long long)*offset, *count  ) ;

  if( *fid >= _9P_FID_PER_CONN )
    {
      err = ERANGE ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

  pfid = &preq9p->pconn->fids[*fid] ;

  /* Do the job */
  seek_descriptor.whence = FSAL_SEEK_SET ;
  seek_descriptor.offset = *offset;

  size = *count ;
   
  if(cache_inode_rdwr( pfid->pentry,
                       CACHE_INODE_READ,
                       &seek_descriptor,
                       size,
                       &read_size,
                       &attr,
                       databuffer,
                       &eof_met,
                       pwkrdata->ht,
                       &pwkrdata->cache_inode_client,
                       &pfid->fsal_op_context,
                       stable_flag,
                       &cache_status ) != CACHE_INODE_SUCCESS )
    {
      err = _9p_tools_errno( cache_status ) ; ;
      rc = _9p_rerror( preq9p, msgtag, &err, plenout, preply ) ;
      return rc ;
    }

  outcount = (u32)read_size ;

  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RREAD ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setbuffer( cursor, outcount, databuffer ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RREAD: tag=%u fid=%u offset=%llu count=%u",
            (u32)*msgtag, *fid , (unsigned long long)*offset, *count ) ;

  return 1 ;
}

