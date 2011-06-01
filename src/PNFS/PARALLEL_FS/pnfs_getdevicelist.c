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
 * pnfs_parallel_fs_getdevicelist: manages the OP4_GETDEVICELIST operation for pNFS/File on top of PARALLEL_FS
 *
 * Manages the OP4_GETDEVICELIST operation for pNFS/File on top of PARALLEL_FS
 *
 * @param pgetdevicelistargs [IN]  pointer to getdevicelist's arguments
 * @param data           [INOUT]  pointer to related compoud request
 * @param pgetdevicelistres [OUT] pointer to getdevicelistt's results
 *
 * @return  NFSv4 status (with NFSv4 error code)
 *
 */

nfsstat4 pnfs_parallel_fs_getdevicelist( GETDEVICELIST4args  * pgetdevicelistargs,
			            compound_data_t     * data,
				    GETDEVICELIST4res   * pgetdevicelistres )
{
  
  pgetdevicelistres->gdlr_status = NFS4_OK;

  return pgetdevicelistres->gdlr_status  ;
}                               /* pnfs_parallel_fs_getdevicelist */
