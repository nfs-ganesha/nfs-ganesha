/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    pnfs_ds_create_file.c
 * \brief   pNFS objects creation functions.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <inttypes.h>

#include <string.h>
#include <signal.h>

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#else
#include <rpc/rpc.h>
#endif

#include "stuff_alloc.h"
#include "nfs_proto_functions.h"

#include "PNFS/LAYOUT4_NFSV4_1_FILES/pnfs_layout4_nfsv4_1_files.h"
#include "pnfs_nfsv41_macros.h"

int pnfs_ds_get_location( pnfs_client_t      * pnfsclient,
                          fsal_handle_t      * phandle, 
                          pnfs_ds_hints_t    * phints,
	                  pnfs_ds_loc_t      * plocation ) 
{
  if( !phandle || !plocation )
    return 0 ;
 
  snprintHandle( plocation->str_mds_handle, MAXNAMLEN, phandle ) ;

  return 1 ;
} /* pnfs_ds_get_location */

