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

int _9p_version( u32 * plenin, char *pmsg, u32 * plenout, char * preply)
{
  char * cursor = pmsg ;
  u16 * pmsgtag = NULL ;
  u32 * pmsize = NULL ;
  u16 * pstrlen = NULL ;
  char * strdata = NULL ;

  u32 lenout ; 

  char version_9p200l[] = "9P2000.L" ;

  if ( !plenin || !pmsg || !plenout || !preply )
   return -1 ;

  /* Get message's tag */
  pmsgtag = (u16 *)cursor ;
  cursor += _9P_TAG_SIZE ;
 
  printf( "VERSION: The 9P message is of tag %u\n", (u32)*pmsgtag ) ;

  /* Get the version size */
  pmsize = (u32 *)cursor ;
  cursor += sizeof( u32 ) ;

  printf( "VERSION: msize = %u\n", *pmsize ) ;

  /* Get the length of the string containing the version */
  pstrlen = (u16 *)cursor ;
  cursor += sizeof( u16 ) ;

  printf( "VERSION: version length = %u\n", *pstrlen ) ;
  
  strdata = cursor ;

  printf( "VERSION: version string = %s\n", strdata ) ;

  if( strncmp( strdata, version_9p200l, *pstrlen ) )
   {
      printf( "VERSION: BAD VERSION\n" ) ;
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
  lenout = (u32)(cursor - preply) ;
  *((u32 *)preply) = lenout ;
 
  if( lenout > *plenout )
    return -1 ;
  else 
    *plenout = lenout ;
 
  return 1 ;
}

