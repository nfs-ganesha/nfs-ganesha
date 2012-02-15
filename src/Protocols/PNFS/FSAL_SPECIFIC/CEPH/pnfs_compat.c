/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    pnfs_layoutcommit.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * pnfs_lock.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <stdint.h>
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
#include "nfs_tools.h"
#include "nfs_proto_tools.h"
#ifdef _USE_FSALMDS
#include "fsal.h"
#include "fsal_pnfs.h"
#include "sal_data.h"
#include "sal_functions.h"
#endif /* _USE_FSALMDS */

#include "pnfs_internal.h"

pnfs_functions_t pnfs_fsal_functions = {
  .getdevicelist  = FSAL_pnfs_getdevicelist,
  .getdeviceinfo  = FSAL_pnfs_getdeviceinfo,
  .layoutget      = FSAL_pnfs_layoutget,
  .layoutcommit   = FSAL_pnfs_layoutcommit,
  .layoutreturn   = FSAL_pnfs_layoutreturn,
  .layoutget_Free = COMMON_pnfs_layoutget_Free,
  .layoutcommit_Free = COMMON_pnfs_layoutcommit_Free,
  .layoutreturn_Free = COMMON_pnfs_layoutreturn_Free,
  .getdevicelist_Free  = COMMON_pnfs_getdevicelist_Free,
  .getdeviceinfo_Free  = COMMON_pnfs_getdeviceinfo_Free
} ;

pnfs_functions_t pNFS_GetFunctions( void )
{
  return pnfs_fsal_functions ;
}
