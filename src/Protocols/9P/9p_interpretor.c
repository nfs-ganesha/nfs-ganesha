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
#include "nfs_core.h"
#include "stuff_alloc.h"
#include "log_macros.h"
#include "9p.h"

static inline char * _9p_msgtype2str( u8 msgtype )
{
  switch( msgtype )
   {
     case _9P_TLERROR:
        return "_9P_TLERROR" ;
        break ;
     case _9P_RLERROR:
        return "_9P_RLERROR" ;
        break ;
     case _9P_TSTATFS:
        return "_9P_TSTATFS" ;
        break ;
     case _9P_RSTATFS:
        return "_9P_RSTATFS" ;
        break ;
     case _9P_TLOPEN:
        return "_9P_TLOPEN" ;
        break ;
     case _9P_RLOPEN:
        return "_9P_RLOPEN" ;
        break ;
     case _9P_TLCREATE:
        return "_9P_TLCREATE" ;
        break ;
     case _9P_RLCREATE:
        return "_9P_RLCREATE" ;
        break ;
     case _9P_TSYMLINK:
        return "_9P_TSYMLINK" ;
        break ;
     case _9P_RSYMLINK:
        return "_9P_RSYMLINK" ;
        break ;
     case _9P_TMKNOD:
        return "_9P_TMKNOD" ;
        break ;
     case _9P_RMKNOD:
        return "_9P_RMKNOD" ;
        break ;
     case _9P_TRENAME:
        return "_9P_TRENAME" ;
        break ;
     case _9P_RRENAME:
        return "_9P_RRENAME" ;
        break ;
     case _9P_TREADLINK:
        return "_9P_TREADLINK" ;
        break ;
     case _9P_RREADLINK:
        return "_9P_RREADLINK" ;
        break ;
     case _9P_TGETATTR:
        return "_9P_TGETATTR" ;
        break ;
     case _9P_RGETATTR:
        return "_9P_RGETATTR" ;
        break ;
     case _9P_TSETATTR:
        return "_9P_TSETATTR" ;
        break ;
     case _9P_RSETATTR:
        return "_9P_RSETATTR" ;
        break ;
     case _9P_TXATTRWALK:
        return "_9P_TXATTRWALK" ;
        break ;
     case _9P_RXATTRWALK:
        return "_9P_RXATTRWALK" ;
        break ;
     case _9P_TXATTRCREATE:
        return "_9P_TXATTRCREATE" ;
        break ;
     case _9P_RXATTRCREATE:
        return "_9P_RXATTRCREATE" ;
        break ;
     case _9P_TREADDIR:
        return "_9P_TREADDIR" ;
        break ;
     case _9P_RREADDIR:
        return "_9P_RREADDIR" ;
        break ;
     case _9P_TFSYNC:
        return "_9P_TFSYNC" ;
        break ;
     case _9P_RFSYNC:
        return "_9P_RFSYNC" ;
        break ;
     case _9P_TLOCK:
        return "_9P_TLOCK" ;
        break ;
     case _9P_RLOCK:
        return "_9P_RLOCK" ;
        break ;
     case _9P_TGETLOCK:
        return "_9P_TGETLOCK" ;
        break ;
     case _9P_RGETLOCK:
        return "_9P_RGETLOCK" ;
        break ;
     case _9P_TLINK:
        return "_9P_TLINK" ;
        break ;
     case _9P_RLINK:
        return "_9P_RLINK" ;
        break ;
     case _9P_TMKDIR:
        return "_9P_TMKDIR" ;
        break ;
     case _9P_RMKDIR:
        return "_9P_RMKDIR" ;
        break ;
     case _9P_TRENAMEAT:
        return "_9P_TRENAMEAT" ;
        break ;
     case _9P_RRENAMEAT:
        return "_9P_RRENAMEAT" ;
        break ;
     case _9P_TUNLINKAT:
        return "_9P_TUNLINKAT" ;
        break ;
     case _9P_RUNLINKAT:
        return "_9P_RUNLINKAT" ;
        break ;
     case _9P_TVERSION:
        return "_9P_TVERSION" ;
        break ;
     case _9P_RVERSION:
        return "_9P_RVERSION" ;
        break ;
     case _9P_TAUTH:
        return "_9P_TAUTH" ;
        break ;
     case _9P_RAUTH:
        return "_9P_RAUTH" ;
        break ;
     case _9P_TATTACH:
        return "_9P_TATTACH" ;
        break ;
     case _9P_RATTACH:
        return "_9P_RATTACH" ;
        break ;
     case _9P_TERROR:
        return "_9P_TERROR" ;
        break ;
     case _9P_RERROR:
        return "_9P_RERROR" ;
        break ;
     case _9P_TFLUSH:
        return "_9P_TFLUSH" ;
        break ;
     case _9P_RFLUSH:
        return "_9P_RFLUSH" ;
        break ;
     case _9P_TWALK:
        return "_9P_TWALK" ;
        break ;
     case _9P_RWALK:
        return "_9P_RWALK" ;
        break ;
     case _9P_TOPEN:
        return "_9P_TOPEN" ;
        break ;
     case _9P_ROPEN:
        return "_9P_ROPEN" ;
        break ;
     case _9P_TCREATE:
        return "_9P_TCREATE" ;
        break ;
     case _9P_RCREATE:
        return "_9P_RCREATE" ;
        break ;
     case _9P_TREAD:
        return "_9P_TREAD" ;
        break ;
     case _9P_RREAD:
        return "_9P_RREAD" ;
        break ;
     case _9P_TWRITE:
        return "_9P_TWRITE" ;
        break ;
     case _9P_RWRITE:
        return "_9P_RWRITE" ;
        break ;
     case _9P_TCLUNK:
        return "_9P_TCLUNK" ;
        break ;
     case _9P_RCLUNK:
        return "_9P_RCLUNK" ;
        break ;
     case _9P_TREMOVE:
        return "_9P_TREMOVE" ;
        break ;
     case _9P_RREMOVE:
        return "_9P_RREMOVE" ;
        break ;
     case _9P_TSTAT:
        return "_9P_TSTAT" ;
        break ;
     case _9P_RSTAT:
        return "_9P_RSTAT" ;
        break ;
     case _9P_TWSTAT:
        return "_9P_TWSTAT" ;
        break ;
     case _9P_RWSTAT:
        return "_9P_RWSTAT" ;
        break ;
     default:
	return "unknown 9p msgtype" ;
	break ;
  }
} /*_9p_msgtype2str */

void _9p_process_request( _9p_request_data_t * preq9p ) 
{
  char * msgdata ;
  u32 * pmsglen = NULL ;
  u8 * pmsgtype = NULL ;
  u32 outdatalen = 0 ;
  int rc = 0 ; 

  char replydata[_9P_MSG_SIZE] ;
  char *replypayload = NULL ;

  msgdata =  preq9p->_9pmsg;
  replypayload = replydata + _9P_HDR_SIZE + _9P_TYPE_SIZE + _9P_TAG_SIZE ;

  /* Get message's length */
  pmsglen = (u32 *)msgdata ;
  msgdata += _9P_HDR_SIZE;

  /* Get message's type */
  pmsgtype = (u8 *)msgdata ;
  msgdata += _9P_TYPE_SIZE ;

  outdatalen = _9P_MSG_SIZE  -  _9P_HDR_SIZE ;

  LogFullDebug( COMPONENT_9P, "9P msg: length=%u type (%u|%s)",  *pmsglen, (u32)*pmsgtype, _9p_msgtype2str( *pmsgtype ) ) ;

  switch( *pmsgtype )
   {
     case _9P_TLERROR:
        break ;
     case _9P_TSTATFS:
        break ;
     case _9P_TLOPEN:
        break ;
     case _9P_TLCREATE:
        break ;
     case _9P_TSYMLINK:
        break ;
     case _9P_TMKNOD:
        break ;
     case _9P_TRENAME:
        break ;
     case _9P_TREADLINK:
        break ;
     case _9P_TGETATTR:
        break ;
     case _9P_TSETATTR:
        break ;
     case _9P_TXATTRWALK:
        break ;
     case _9P_TXATTRCREATE:
        break ;
     case _9P_TREADDIR:
        break ;
     case _9P_TFSYNC:
        break ;
     case _9P_TLOCK:
        break ;
     case _9P_TGETLOCK:
        break ;
     case _9P_TLINK:
        break ;
     case _9P_TMKDIR:
        break ;
     case _9P_TRENAMEAT:
        break ;
     case _9P_TUNLINKAT:
        break ;
     case _9P_TVERSION:
        if(  ( ( rc = _9p_version( msgdata, &outdatalen, replypayload ) ) < 0 )  ||
             ( send( preq9p->_9psockfd, replypayload, outdatalen, 0 ) != outdatalen ) )
           printf( "VERSION: Error \n" ) ;
        break ;

     case _9P_TAUTH:
        break ;
     case _9P_TATTACH:
        if( ( rc = _9p_attach( msgdata, &outdatalen, replypayload ) ) < 0 )
	  printf( "ATTACH: Got rc = %d\n", rc ) ;
        else if( send( preq9p->_9psockfd, replypayload, outdatalen, 0 ) != outdatalen )
           printf( "ATTACH: BADSIZE sent\n" ) ;

        break ;
     case _9P_TERROR:
        break ;
     case _9P_TFLUSH:
        break ;
     case _9P_TWALK:
        break ;
     case _9P_TOPEN:
        break ;
     case _9P_TCREATE:
        break ;
     case _9P_TREAD:
        break ;
     case _9P_TWRITE:
        break ;
     case _9P_TCLUNK:
        break ;
     case _9P_TREMOVE:
        break ;
     case _9P_TSTAT:
        break ;
     case _9P_TWSTAT:
        break ;
     default:
	break ;
   }
  return ;
}
