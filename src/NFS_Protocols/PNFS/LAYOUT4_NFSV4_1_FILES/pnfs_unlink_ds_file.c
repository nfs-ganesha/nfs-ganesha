/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    pnfs_create_ds_file.c
 * \brief   pNFS objects creation functions.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

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

#define PNFS_LAYOUTFILE_NB_OP_UNLINK_DS_FILE 3

#define PNFS_LAYOUTFILE_CREATE_VAL_BUFFER  1024

int pnfs_unlink_ds_file( pnfs_client_t  * pnfsclient, 
                         fattr4_fileid    fileid,
                         pnfs_ds_file_t * pfile ) 
{
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  struct timeval timeout = { 25, 0 };
  nfs_argop4 argoparray[PNFS_LAYOUTFILE_NB_OP_UNLINK_DS_FILE];
  nfs_resop4 resoparray[PNFS_LAYOUTFILE_NB_OP_UNLINK_DS_FILE];
  component4 name;
  char nameval[MAXNAMLEN];
  char filename[MAXNAMLEN] ;


  if( !pnfsclient || !pfile )
    return NFS4ERR_SERVERFAULT ;

  /* Step 1 OP4_OPEN as OPEN4_CREATE */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 1;

  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Mkdir" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  name.utf8string_val = nameval;
  name.utf8string_len = 0;
  snprintf( filename, MAXNAMLEN, "fileid=%llu", (unsigned long long)fileid ) ;
  if (str2utf8(filename, &name) == -1)
        return NFS4ERR_SERVERFAULT;

  COMPOUNDV41_ARG_ADD_OP_SEQUENCE( argnfs4, pnfsclient->session, pnfsclient->sequence ) ;
  pnfsclient->sequence += 1 ; /* In all cases, failure or not, increment the sequence counter */
  COMPOUNDV41_ARG_ADD_OP_PUTFH(argnfs4, pnfsclient->ds_rootfh[0] );
  COMPOUNDV41_ARG_ADD_OP_REMOVE(argnfs4, name ) ;

  /* Call the NFSv4 function */
  if (COMPOUNDV41_EXECUTE_SIMPLE(pnfsclient, argnfs4, resnfs4) != RPC_SUCCESS)
    {
      return NFS4ERR_IO;        /* @todo: For wanting of something more appropriate */
    }

  return resnfs4.status;
} /* pnfs_create_ds_file */ 

