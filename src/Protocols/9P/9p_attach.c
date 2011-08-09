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
 * \file    9p_attach.c
 * \brief   9P version
 *
 * 9p_attach.c : _9P_interpretor, request ATTACH
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



int _9p_attach( _9p_request_data_t * preq9p, u32 * plenout, char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;

  u16 * msgtag = NULL ;
  u32 * fid = NULL ;
  u32 * afid = NULL ;
  u16 * uname_len = NULL ;
  char * uname_str = NULL ;
  u16 * aname_len = NULL ;
  char * aname_str = NULL ;
  u32 * n_aname = NULL ;

  struct _9p_qid qid ;

  if ( !preq9p || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, fid,    u32 ) ; 
  _9p_getptr( cursor, afid,   u32 ) ; 
  _9p_getstr( cursor, uname_len, uname_str ) ;
  _9p_getstr( cursor, aname_len, aname_str ) ;
  _9p_getptr( cursor, n_aname, u32 ) ; 

  LogDebug( COMPONENT_9P, "TATTACH: tag=%ufid=%u afid=%d uname='%.*s' aname='%.*s' n_uname=%d", 
            (u32)*msgtag, *fid, *afid, (int)*uname_len, uname_str, (int)*aname_len, aname_str, *n_aname ) ;

  /* Compute the qid */
  qid.type = _9P_QTDIR ;
  qid.version = 0 ;
  qid.path = 0LL ; /* use the fileid here */

  /* Build the reply */
  _9p_setinitptr( cursor, preply, _9P_RATTACH ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setptr( cursor, &qid.type,      u8 ) ;
  _9p_setptr( cursor, &qid.version,  u32 ) ;
  _9p_setptr( cursor, &qid.path,     u64 ) ;

  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RATTACH: qid=(type=%u,version=%u,path=%llu)", 
            (u32)qid.type, qid.version, (unsigned long long)qid.path ) ;

  return 1 ;
}

