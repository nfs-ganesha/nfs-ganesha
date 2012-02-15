/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * \file    pnfs.h
 * \author  $Author: deniel $
 * \date    $Date: 2010/01/27 12:44:15 $
 * \brief   Management of the pNFS features.
 *
 * pnfs.h : Management of the pNFS features.
 *
 *
 */

#ifndef _PNFS_H
#define _PNFS_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "RW_Lock.h"
#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "log.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_exports.h"
#include "fsal_pnfs.h"

typedef struct pnfs_functions__
{
  nfsstat4(*getdevicelist)( GETDEVICELIST4args * pargs, 
  	  	            compound_data_t * data,
			    GETDEVICELIST4res  * pres ) ;

  nfsstat4(*getdeviceinfo)( GETDEVICEINFO4args * pargs, 
			    compound_data_t * data,
			    GETDEVICEINFO4res  * pres ) ;

  nfsstat4(*layoutcommit)( LAYOUTCOMMIT4args * pargs, 
			   compound_data_t * data,
			   LAYOUTCOMMIT4res  * pres ) ;

  nfsstat4(*layoutget)( LAYOUTGET4args   * pargs, 
			compound_data_t  * data,
			LAYOUTGET4res    * pres ) ;

  nfsstat4(*layoutreturn)( LAYOUTRETURN4args * pargs, 
			   compound_data_t   * data,
			   LAYOUTRETURN4res  * pres ) ; 

  void(*layoutget_Free)( LAYOUTGET4res * pres ) ;

  void(*layoutcommit_Free)( LAYOUTCOMMIT4res * pres ) ;

  void(*layoutreturn_Free)( LAYOUTRETURN4res * pres ) ;

  void(*getdevicelist_Free)(  GETDEVICELIST4res  * pres ) ;

  void(*getdeviceinfo_Free)(  GETDEVICEINFO4res  * pres ) ;

} pnfs_functions_t ;

nfsstat4 pnfs_getdevicelist( GETDEVICELIST4args * pargs, 
			     compound_data_t * data,
			     GETDEVICELIST4res  * pres ) ;

nfsstat4 pnfs_getdeviceinfo( GETDEVICEINFO4args * pargs, 
			     compound_data_t * data,
			     GETDEVICEINFO4res  * pres ) ;

nfsstat4 pnfs_layoutcommit( LAYOUTCOMMIT4args * pargs, 
			    compound_data_t * data,
			    LAYOUTCOMMIT4res  * pres ) ;

nfsstat4 pnfs_layoutget( LAYOUTGET4args   * pargs, 
			 compound_data_t  * data,
			 LAYOUTGET4res    * pres ) ;

nfsstat4 pnfs_layoutreturn( LAYOUTRETURN4args * pargs, 
			    compound_data_t   * data,
			    LAYOUTRETURN4res  * pres ) ; 

void pnfs_layoutget_Free( LAYOUTGET4res * pres ) ;
void pnfs_layoutcommit_Free( LAYOUTCOMMIT4res * pres ) ;
void pnfs_layoutreturn_Free( LAYOUTRETURN4res * pres ) ;

void pnfs_getdevicelist_Free(  GETDEVICELIST4res  * pres ) ;
void pnfs_getdeviceinfo_Free(  GETDEVICEINFO4res  * pres ) ;


/* Common functions */
void COMMON_pnfs_layoutget_Free( LAYOUTGET4res * pres ) ;
void COMMON_pnfs_layoutcommit_Free( LAYOUTCOMMIT4res * pres ) ;
void COMMON_pnfs_layoutreturn_Free( LAYOUTRETURN4res * pres ) ;

void COMMON_pnfs_getdevicelist_Free(  GETDEVICELIST4res  * pres ) ;
void COMMON_pnfs_getdeviceinfo_Free(  GETDEVICEINFO4res  * pres ) ;

pnfs_functions_t pNFS_GetFunctions( void ) ;


#endif                          /* _PNFS_H */
