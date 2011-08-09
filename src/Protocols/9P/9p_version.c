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
#include "9p.h"

static char version_9p200l[] = "9P2000.L" ;

int _9p_version( char *pmsg, u32 * plenout, char * preply)
{
  char * cursor = pmsg ;
  u16 * pmsgtag = NULL ;
  u32 * pmsize = NULL ;
  u16 * pstrlen = NULL ;
  char * strdata = NULL ;

  struct _9p_tversion * tversion = NULL ;


  if ( !pmsg || !plenout || !preply )
   return -1 ;

  /* Get message's tag */
  pmsgtag = (u16 *)cursor ;
  cursor += _9P_TAG_SIZE ;
 
  LogDebug( COMPONENT_9P, "VERSION: The 9P message has tag %u", (u32)*pmsgtag ) ;

  tversion = (struct _9p_tversion *)(pmsgtag+_9P_TAG_SIZE) ;

  /* Get the version size */
  pmsize = (u32 *)cursor ;
  cursor += sizeof( u32 ) ;


  /* Get the length of the string containing the version */
  pstrlen = (u16 *)cursor ;
  cursor += sizeof( u16 ) ;
  strdata = cursor ;

  LogDebug( COMPONENT_9P, "TVERSION: msize = %u version='%s'\n", *pmsize, strdata ) ;

  if( strncmp( strdata, version_9p200l, *pstrlen ) )
   {
      LogEvent( COMPONENT_9P, "RVERSION: BAD VERSION\n" ) ;
      return -1 ;
   } 

  /* Good version, build the reply */
  cursor = preply ;
  cursor += _9P_HDR_SIZE ; /* to be set at the end */

  /* Set reply type */
  *((u8 *)cursor) = _9P_RVERSION ;
  cursor += sizeof( u8 ) ;

  /* Set the tag */
  *((u16 *)cursor) =  *pmsgtag ;
  cursor += sizeof( u16 ) ;

  /* Set the msize */
  *((u32 *)cursor) = *pmsize ;  
  cursor += sizeof( u32 ) ;

  /* set the string */
  *((u16 *)cursor) = *pstrlen ;
  cursor += sizeof( u16 ) ;

  memcpy( cursor, strdata, *pstrlen ) ;
  cursor += *pstrlen ;

  /* Now that the full size if computable, fill in the header size */
  if( ( (u32)(cursor - preply) )  > *plenout )
    return -1 ;

  *((u32 *)preply) =  (u32)(cursor - preply) ;
  *plenout =  (u32)(cursor - preply) ;

  LogDebug( COMPONENT_9P, "RVERSION: msize = %u version='%s'\n", *pmsize, strdata ) ;

  return 1 ;
}

