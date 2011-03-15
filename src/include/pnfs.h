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

#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#endif

#include "RW_Lock.h"
#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "fsal_types.h"
#include "log_macros.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"

#ifdef _USE_PNFS_PARALLEL_FS 
#include "PNFS/PARALLEL_FS/pnfs_layout4_nfsv4_1_files.h"
#endif

#ifdef _USE_PNFS_SPNFS_LIKE
#include "PNFS/SPNFS_LIKE/pnfs_layout4_nfsv4_1_files.h"
#endif

typedef union pnfs_parameter__
{
  pnfs_layoutfile_parameter_t layoutfile;
} pnfs_parameter_t;

#ifdef _USE_PNFS_SPNFS_LIKE
typedef union pnfs_file__
{
  pnfs_ds_file_t ds_file;
} pnfs_file_t;

typedef union pnfs_file_loc__
{
  pnfs_ds_loc_t ds_loc ;
} pnfs_fileloc_t ;

typedef union pnfs_hints__
{
  pnfs_ds_hints_t ds_hints ;
} pnfs_hints_t ;

int pnfs_get_location(  pnfs_client_t      * pnfsclient,
                        fsal_handle_t      * phandle, 
                        pnfs_hints_t       * phints,
	                pnfs_fileloc_t * pnfs_fileloc ) ;

int pnfs_create_file( pnfs_client_t  * pnfsclient,
	              pnfs_fileloc_t * pnfs_fileloc,
		      pnfs_file_t    * pnfs_file ) ;

int pnfs_remove_file( pnfs_client_t  * pnfsclient,
                      pnfs_file_t    * pfile ) ;

int pnfs_lookup_file( pnfs_client_t  * pnfsclient,
	              pnfs_fileloc_t * pnfs_fileloc,
		      pnfs_file_t    * pnfs_file ) ;

int pnfs_truncate_file( pnfs_client_t * pnfsclient,
			size_t newsize,
			pnfs_file_t * pnfs_file ) ;

int pnfs_init(pnfs_client_t * pnfsclient,
              pnfs_layoutfile_parameter_t * pnfs_layout_param) ;

void pnfs_terminate();
#else

typedef union pnfs_file__
{
  int nothing ;
} pnfs_file_t;
#endif

int pnfs_service_getdevicelist( char * buffin, unsigned int * plenin, char *buffout, unsigned int *plenout) ;
int pnfs_service_getdeviceinfo( char * buffin, unsigned int * plenin, char *buffout, unsigned int *plenout) ;
int pnfs_service_layoutcommit( char * buffin, unsigned int * plenin, char *buffout, unsigned int *plenout)  ;
int pnfs_service_layoutreturn( char * buffin, unsigned int * plenin, char *buffout, unsigned int *plenout)  ;
int pnfs_service_layoutget( char * buffin, unsigned int * plenin, char *buffout, unsigned int *plenout)     ;


#endif                          /* _PNFS_H */
