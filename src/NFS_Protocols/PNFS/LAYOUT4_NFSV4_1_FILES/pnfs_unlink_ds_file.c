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
static int pnfs_unlink_ds_partfile(pnfs_ds_client_t * pnfsdsclient,
                                   component4 name, fattr4_fileid fileid,
                                   pnfs_part_file_t * ppartfile)
{
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  struct timeval timeout = { 25, 0 };
  nfs_argop4 argoparray[PNFS_LAYOUTFILE_NB_OP_UNLINK_DS_FILE];
  nfs_resop4 resoparray[PNFS_LAYOUTFILE_NB_OP_UNLINK_DS_FILE];
  unsigned int i;

  if(!pnfsdsclient || !ppartfile)
    return NFS4ERR_SERVERFAULT;

  /* Step 1 OP4_OPEN as OPEN4_CREATE */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 1;

  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Mkdir" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  COMPOUNDV41_ARG_ADD_OP_SEQUENCE(argnfs4, pnfsdsclient->session, pnfsdsclient->sequence);
  COMPOUNDV41_ARG_ADD_OP_PUTFH(argnfs4, pnfsdsclient->ds_rootfh);
  COMPOUNDV41_ARG_ADD_OP_REMOVE(argnfs4, name);

  /* Call the NFSv4 function */
  if(clnt_call(pnfsdsclient->rpc_client, NFSPROC4_COMPOUND,
               (xdrproc_t) xdr_COMPOUND4args, (caddr_t) & argnfs4,
               (xdrproc_t) xdr_COMPOUND4res, (caddr_t) & resnfs4, timeout) != RPC_SUCCESS)
    {
      return NFS4ERR_IO;        /* @todo: For wanting of something more appropriate */
    }

  if(resnfs4.status != NFS4_OK)
    return resnfs4.status;

  pnfsdsclient->sequence += 1;

  /* Free the ressources */
  Mem_Free((char *)ppartfile->handle.nfs_fh4_val);

  return NFS4_OK;
}                               /* pnfs_unlink_ds_file */

int pnfs_unlink_ds_file(pnfs_client_t * pnfsclient,
                        fattr4_fileid fileid, pnfs_ds_file_t * pfile)
{
  component4 name;
  char nameval[MAXNAMLEN];
  char filename[MAXNAMLEN];
  unsigned int i;
  int rc = 0;

  if(!pnfsclient || !pfile)
    return NFS4ERR_SERVERFAULT;

  name.utf8string_val = nameval;
  name.utf8string_len = 0;
  snprintf(filename, MAXNAMLEN, "fileid=%llu", (unsigned long long)fileid);
  if(str2utf8(filename, &name) == -1)
    return NFS4ERR_SERVERFAULT;

  for(i = 0; i < pnfsclient->nb_ds; i++)
    {
      if((rc =
          pnfs_unlink_ds_partfile(&(pnfsclient->ds_client[i]), name, fileid,
                                  &(pfile->filepart[i]))) != NFS4_OK)
        return rc;
    }

  return NFS4_OK;
}                               /* pnfs_unlink_ds_file */
