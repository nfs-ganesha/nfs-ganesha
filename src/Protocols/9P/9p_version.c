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
 * \file    9p_version.c
 * \brief   9P version
 *
 * 9p_version.c : _9P_interpretor, request VERSION
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

static char version_9p200l[] = "9P2000.L" ;

int _9p_version( _9p_request_data_t * preq9p, 
                 void * pworker_data,
                 u32 * plenout, char * preply)
{
  char * cursor = preq9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE ;

  u16 * msgtag = NULL ;
  u32 * msize = NULL ;
  u16 * version_len = NULL ;
  char * version_str = NULL ;

  if ( !preq9p || !plenout || !preply )
   return -1 ;

  /* Get data */
  _9p_getptr( cursor, msgtag, u16 ) ; 
  _9p_getptr( cursor, msize,  u32 ) ;
  _9p_getstr( cursor, version_len, version_str ) ;

  LogDebug( COMPONENT_9P, "TVERSION: tag=%u msize=%u version='%.*s'", (u32)*msgtag, *msize, (int)*version_len, version_str ) ;

  if( strncmp( version_str, version_9p200l, *version_len ) )
   {
      LogEvent( COMPONENT_9P, "RVERSION: BAD VERSION" ) ;
      return -1 ;
   } 

  /* Good version, build the reply */
  _9p_setinitptr( cursor, preply, _9P_RVERSION ) ;
  _9p_setptr( cursor, msgtag, u16 ) ;

  _9p_setptr( cursor, msize,  u32 ) ;
  _9p_setstr( cursor, *version_len, version_str ) ;
  _9p_setendptr( cursor, preply ) ;
  _9p_checkbound( cursor, preply, plenout ) ;

  LogDebug( COMPONENT_9P, "RVERSION: msize=%u version='%.*s'", *msize, (int)*version_len, version_str ) ;

  return 1 ;
}

