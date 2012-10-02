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
#include "log.h"
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
  u8   * pmsgtype =  preq9p->_9pmsg + _9P_HDR_SIZE ;
  nfs_worker_data_t * pwkrdata = (nfs_worker_data_t *)pworker_data ;

  u16 * msgtag = NULL ;
  u32 * fid    = NULL ;
  u64 * offset = NULL ;
  u32 * count  = NULL ;
  u32 outcount = 0 ;

  _9p_fid_t * pfid = NULL ;

  size_t read_size = 0;
  bool_t eof_met;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  // uint64_t stable_flag = CACHE_INODE_SAFE_WRITE_TO_FS;
  uint64_t stable_flag = CACHE_INODE_UNSAFE_WRITE_TO_FS_BUFFER;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getptr( cursor, offset, u64 ) ; 
  _9p_getptr( cursor, count,  u32 ) ; 
  
  LogDebug( COMPONENT_9P, "TREAD: tag=%u fid=%u offset=%llu count=%u",
            (u32)*msgtag, *fid, (unsigned long long)*offset, *count  ) ;

  if( *fid >= _9P_FID_PER_CONN )
   return  _9p_rerror( preq9p, pworker_data,  msgtag, ERANGE, plenout, preply ) ;

  pfid = &preq9p->pconn->fids[*fid] ;

  /* Check that it is a valid fid */
  if (pfid->pentry == NULL) 
  {
    LogDebug( COMPONENT_9P, "request on invalid fid=%u", *fid ) ;
    return  _9p_rerror( preq9p, pworker_data,  msgtag, EIO, plenout, preply ) ;
  }

  /* Do the job */
  if( pfid->specdata.xattr.xattr_content != NULL )
    {
      /* Copy the value cached during xattrwalk */
      memcpy( databuffer, pfid->specdata.xattr.xattr_content, *count ) ;

      outcount = (u32)*count ;
    }
  else
    {
       cache_status = cache_inode_rdwr( pfid->pentry,
					CACHE_INODE_READ,
					*offset,
					*count,
					&read_size,
					databuffer,
					&eof_met,
					&pfid->op_context,
					stable_flag);
       if(cache_status != CACHE_INODE_SUCCESS)
         return  _9p_rerror( preq9p, pworker_data,  msgtag, _9p_tools_errno( cache_status ), plenout, preply ) ;

       outcount = (u32)read_size ;
   }
  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RREAD ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setbuffer( cursor, outcount, databuffer ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RREAD: tag=%u fid=%u offset=%llu count=%u",
            (u32)*msgtag, *fid , (unsigned long long)*offset, *count ) ;

  _9p_stat_update( *pmsgtype, TRUE, &pwkrdata->stats._9p_stat_req ) ;
  return 1 ;
}

