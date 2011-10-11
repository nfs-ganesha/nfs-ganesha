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
 * \file    9p_interpretor.c
 * \brief   9P interpretor
 *
 * 9p_interpretor.c : _9P_interpretor.
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


#include "stuff_alloc.h"
#include "nfs_core.h"
#include "9p.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "fsal.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "SemN.h"

/* This array maps a 9P Tmessage type to the 
 * related position in _9pfuncdesc array 
 * position=32 is "unknown function" */
const int _9ptabindex[] =
    {32, 
     32, 32, 32, 32, 32, 32, 32,
      0, 32, 32, 32,  1, 32,  2,
     32,  3, 32,  4, 32,  5, 32,
      6, 32,  7, 32,  8, 32, 32,
     32,  9, 32, 10, 32, 32, 32,
     32, 32, 32, 32, 11, 32, 32,
     32, 32, 32, 32, 32, 32, 32,
     12, 32, 13, 32, 14, 32, 32,
     32, 32, 32, 32, 32, 32, 32,
     32, 32, 32, 32, 32, 32, 15,
     32, 16, 32, 17, 32, 18, 32,
     32, 32, 32, 32, 32, 32, 32,
     32, 32, 32, 32, 32, 32, 32,
     32, 32, 32, 32, 32, 32, 32,
     32, 19, 32, 20, 32, 21, 32,
     32, 32, 22, 32, 23, 32, 24,
     32, 25, 32, 26, 32, 27, 32,
     28, 32, 29, 32, 30, 32, 31  
};

const _9p_function_desc_t _9pfuncdesc[] = {
        { _9p_statfs, "_9P_TSTATFS"  },
        { _9p_lopen, "_9P_TLOPEN" },
        { _9p_create, "_9P_TLCREATE" },
        { _9p_symlink, "_9P_TSYMLINK" },
        { _9p_mknod, "_9P_TMKNOD" },
        { _9p_rename, "_9P_TRENAME" },
        { _9p_readlink, "_9P_TREADLINK" },
        { _9p_getattr, "_9P_TGETATTR"},
        { _9p_setattr, "_9P_TSETATTR" },
        { _9p_dummy, "_9P_TXATTRWALK" },
        { _9p_dummy, "_9P_TXATTRCREATE" },
        { _9p_readdir, "_9P_TREADDIR" },
        { _9p_dummy, "_9P_TFSYNC" },
        { _9p_dummy, "_9P_TLOCK" },
        { _9p_dummy, "_9P_TGETLOCK" },
        { _9p_link, "_9P_TLINK" },
        { _9p_mkdir, "_9P_TMKDIR" },
        { _9p_renameat, "_9P_TRENAMEAT" },
        { _9p_unlinkat, "_9P_TUNLINKAT" },
        { _9p_version, "_9P_TVERSION" },
        { _9p_dummy, "_9P_TAUTH" },
        { _9p_attach, "_9P_TATTACH" },
        { _9p_flush, "_9P_TFLUSH" },
        { _9p_walk, "_9P_TWALK" },
        { _9p_dummy, "_9P_TOPEN" },
        { _9p_dummy, "_9P_TCREATE" },
        { _9p_read, "_9P_TREAD" },
        { _9p_write, "_9P_TWRITE" },
        { _9p_clunk, "_9P_TCLUNK" },
        { _9p_remove, "_9P_TREMOVE" },
        { _9p_dummy, "_9P_TSTAT" },
        { _9p_dummy, "_9P_TWSTAT" },
        { _9p_dummy, "no function" }
} ;

/* Will disappear when all work will have been done */
int _9p_dummy( _9p_request_data_t * preq9p, 
               void * pworker_data,
               u32 * plenout, 
               char * preply)
{
  char * msgdata = preq9p->_9pmsg + _9P_HDR_SIZE ;
  u8 * pmsgtype = NULL ;
  u16 msgtag = 0 ;
  int err = ENOTSUP ;

  /* Get message's type */
  pmsgtype = (u8 *)msgdata ;
  LogEvent( COMPONENT_9P,  "(%u|%s) not implemented yet, returning ENOTSUP", *pmsgtype,  _9pfuncdesc[_9ptabindex[*pmsgtype]].funcname  ) ;

  _9p_rerror( preq9p, &msgtag, &err, plenout, preply ) ;

  return -1 ;
} /* _9p_dummy */


void _9p_process_request( _9p_request_data_t * preq9p, nfs_worker_data_t * pworker_data)
{
  char * msgdata ;
  u32 * pmsglen = NULL ;
  u8 * pmsgtype = NULL ;
  u32 outdatalen = 0 ;
  int rc = 0 ; 

  char replydata[_9P_MSG_SIZE] ;

  msgdata =  preq9p->_9pmsg;

  /* Get message's length */
  pmsglen = (u32 *)msgdata ;
  msgdata += _9P_HDR_SIZE;

  /* Get message's type */
  pmsgtype = (u8 *)msgdata ;
  msgdata += _9P_TYPE_SIZE ;

  /* Check boundaries */
  if( *pmsgtype < _9P_TSTATFS || *pmsgtype > _9P_TWSTAT )
   return ;

  outdatalen = _9P_MSG_SIZE  -  _9P_HDR_SIZE ;

  LogFullDebug( COMPONENT_9P, "9P msg: length=%u type (%u|%s)",  *pmsglen, (u32)*pmsgtype, _9pfuncdesc[_9ptabindex[*pmsgtype]].funcname ) ;

  /* Call the 9P service function */  
  if(  ( ( rc = _9pfuncdesc[_9ptabindex[*pmsgtype]].service_function( preq9p, 
                                                                      (void *)pworker_data,
                                                                      &outdatalen, 
                                                                      replydata ) ) < 0 )  ||
             ( send( preq9p->pconn->sockfd, replydata, outdatalen, 0 ) != outdatalen ) )
     LogDebug( COMPONENT_9P, "%s: Error", _9pfuncdesc[_9ptabindex[*pmsgtype]].funcname ) ;

  return ;
} /* _9p_process_request */

