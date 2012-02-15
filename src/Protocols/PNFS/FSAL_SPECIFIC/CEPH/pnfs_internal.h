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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#include "rpc.h"
#include "log.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "pnfs.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#ifdef _USE_FSALMDS
#include "fsal_pnfs.h"
#endif /* _USE_FSALMDS */

#ifndef _PNFS_FSAL_INTERNAL_H
#define _PNFS_FSAL_INTERNAL_H

nfsstat4 FSAL_pnfs_getdevicelist( GETDEVICELIST4args * pargs,
                                  compound_data_t * data,
                                  GETDEVICELIST4res  * pres ) ;

nfsstat4 FSAL_pnfs_getdeviceinfo( GETDEVICEINFO4args * pargs,
                                  compound_data_t * data,
                                  GETDEVICEINFO4res  * pres ) ;

nfsstat4 FSAL_pnfs_layoutcommit( LAYOUTCOMMIT4args * pargs,
                                 compound_data_t * data,
                                 LAYOUTCOMMIT4res  * pres ) ;

nfsstat4 FSAL_pnfs_layoutget( LAYOUTGET4args   * pargs,
                              compound_data_t  * data,
                              LAYOUTGET4res    * pres ) ;

nfsstat4 FSAL_pnfs_layoutreturn( LAYOUTRETURN4args * pargs,
                                 compound_data_t   * data,
                                 LAYOUTRETURN4res  * pres ) ;

#endif /*  _PNFS_CEPH_INTERNAL_H */
