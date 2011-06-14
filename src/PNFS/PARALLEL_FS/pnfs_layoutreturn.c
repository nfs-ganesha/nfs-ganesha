/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    pnfs_encode_getdeviceinfo.c
 * \brief   encode the addr_body_val structure in GETDEVICEINFO
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"
#include "pnfs.h" 
#include "pnfs_service.h" 

/**
 *
 * pnfs_parallel_fs_layoutreturn: manages the OP4_LAYOUTRETURN operation for pNFS/File on top of PARALLE_FS
 *
 * Manages the OP4_LAYOUTRETURNT operation for pNFS/File on top of PARALLE_FS
 *
 * @param playoutreturnargs [IN]  pointer to layoutreturn's arguments
 * @param data           [INOUT]  pointer to related compoud request
 * @param playoutreturnres [OUT] pointer to layoutreturnt's results
 *
 * @return  NFSv4 status (with NFSv4 error code)
 *
 */

nfsstat4 pnfs_parallel_fs_layoutreturn( LAYOUTRETURN4args  * playoutreturnargs,
			           compound_data_t    * data,
				   LAYOUTRETURN4res   * playoutreturnres )
{
  
  playoutreturnres->lorr_status = NFS4_OK;

  return playoutreturnres->lorr_status  ;
}                               /* pnfs_parallel_fs_layoutreturn */
