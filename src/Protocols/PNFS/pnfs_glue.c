/**
 * \file    pnfs_glue.c
 * \brief   pNFS glue functions
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

#ifdef _USE_PNFS_PARALLEL_FS
nfsstat4 pnfs_getdevicelist( GETDEVICELIST4args * pargs,  compound_data_t * data, GETDEVICELIST4res * pres ) 
{
   return pnfs_parallel_fs_getdevicelist( pargs, data, pres ) ;
}

nfsstat4 pnfs_getdeviceinfo( GETDEVICEINFO4args * pargs, compound_data_t * data, GETDEVICEINFO4res * pres ) 
{
   return pnfs_parallel_fs_getdeviceinfo( pargs, data, pres ) ;
}

nfsstat4 pnfs_layoutget( LAYOUTGET4args   * pargs,
	                 compound_data_t  * data, 
			 LAYOUTGET4res    * pres ) 
{
   return pnfs_parallel_fs_layoutget( pargs, data, pres ) ;
}

nfsstat4 pnfs_layoutcommit( LAYOUTCOMMIT4args * pargs, 
                            compound_data_t   * data,
                            LAYOUTCOMMIT4res  * pres ) 
{
   return pnfs_parallel_fs_layoutcommit( pargs, data, pres ) ;
}

nfsstat4 pnfs_layoutreturn( LAYOUTRETURN4args * pargs, 
                            compound_data_t   * data, 
                            LAYOUTRETURN4res  * pres ) 
{
   return pnfs_parallel_fs_layoutreturn( pargs, data, pres ) ;
}
#endif

nfsstat4 pnfs_nit( pnfs_client_t               * pnfsclient,
                   pnfs_layoutfile_parameter_t * pnfs_layout_param)
{
   return NFS4_OK;
}

nfsstat4 pnfs_terminate()
{
   return ;
}


