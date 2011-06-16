/*
 *
 *
 * Copyright CEA/DAM/DIF  (2010)
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

#ifndef _PNFS_LAYOUT4_NFSV4_1_FILES_H
#define _PNFS_LAYOUT4_NFSV4_1_FILES_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include "rpc.h"
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
#include "PNFS/SPNFS_LIKE/pnfs_layout4_nfsv4_1_files_types.h"

/* Mandatory functions */
nfsstat4 pnfs_spnfs_getdevicelist( GETDEVICELIST4args * pargs, 
				   compound_data_t   * data,
				   GETDEVICELIST4res  * pres ) ;

nfsstat4 pnfs_spnfs_getdeviceinfo( GETDEVICEINFO4args * pargs,
				   compound_data_t   * data,
				   GETDEVICEINFO4res  * pres ) ;

nfsstat4 pnfs_spnfs_layoutcommit( LAYOUTCOMMIT4args * pargs, 
				  compound_data_t   * data,
	    		          LAYOUTCOMMIT4res  * pres ) ;

nfsstat4 pnfs_spnfs_layoutget( LAYOUTGET4args  * pargs, 
			       compound_data_t * data,
			       LAYOUTGET4res   * pres ) ;

nfsstat4 pnfs_spnfs_layoutreturn( LAYOUTRETURN4args * pargs, 
				  compound_data_t * data,
				  LAYOUTRETURN4res  * pres ) ; 

/* SPNFS specific functions */
int pnfs_ds_get_location( pnfs_client_t    * pnfsclient,
                          fsal_handle_t    * phandle, 
                          pnfs_ds_hints_t  * phints,
	                  pnfs_ds_loc_t    * plocation ) ; 

int pnfs_ds_init( pnfs_client_t * pnfsclient,
                  pnfs_layoutfile_parameter_t * pnfs_layout_param);

int pnfs_ds_create_file( pnfs_client_t * pnfsclient,
                          pnfs_ds_loc_t * plocation, pnfs_ds_file_t * pfile);

int pnfs_ds_lookup_file( pnfs_client_t * pnfsclient,
                         pnfs_ds_loc_t * plocation, pnfs_ds_file_t * pfile);

int pnfs_ds_unlink_file( pnfs_client_t * pnfsclient,
                         pnfs_ds_file_t * pfile);

int pnfs_ds_open_file( pnfs_client_t * pnfsdsclient,
                       pnfs_ds_loc_t * plocation, pnfs_ds_file_t * pfile);

int pnfs_ds_truncate_file( pnfs_client_t * pnfsclient,
                           size_t newsize,
                           pnfs_ds_file_t * pfile);

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

nfsstat4 pnfs_terminate();

/* Internal functions */
int pnfs_connect(pnfs_ds_client_t * pnfsdsclient, pnfs_ds_parameter_t * pnfs_ds_param);

int pnfs_do_mount(pnfs_ds_client_t * pnfsclient, pnfs_ds_parameter_t * pds_param);

int pnfs_lookupPath(pnfs_ds_client_t * pnfsdsclient, char *p_path,
                    nfs_fh4 * object_handle);


#endif                          /* _PNFS_LAYOUT4_NFSV4_1_FILES_H */
