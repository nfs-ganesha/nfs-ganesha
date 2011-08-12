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
#include "log_macros.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"

#include "PNFS/PARALLEL_FS/pnfs_layout4_nfsv4_1_files_types.h"

/* Mandatory functions */
nfsstat4 pnfs_parallel_fs_getdevicelist( GETDEVICELIST4args * pargs, 
				         compound_data_t   * data,
				         GETDEVICELIST4res  * pres ) ;

nfsstat4 pnfs_parallel_fs_getdeviceinfo( GETDEVICEINFO4args * pargs,
				         compound_data_t   * data,
				         GETDEVICEINFO4res  * pres ) ;

nfsstat4 pnfs_parallel_fs_layoutcommit( LAYOUTCOMMIT4args * pargs, 
				        compound_data_t   * data,
	    		                LAYOUTCOMMIT4res  * pres ) ;

nfsstat4 pnfs_parallel_fs_layoutget( LAYOUTGET4args  * pargs, 
				     compound_data_t * data,
				     LAYOUTGET4res   * pres ) ;

nfsstat4 pnfs_parallel_fs_layoutreturn( LAYOUTRETURN4args * pargs, 
				        compound_data_t * data,
				        LAYOUTRETURN4res  * pres ) ; 

#endif 
