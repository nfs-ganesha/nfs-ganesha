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

#define PNFS_LAYOUTFILE_NB_OP_OPEN_DS_FILE 4
#define PNFS_LAYOUTFILE_NB_OP_CLOSE_DS_FILE 3

#define PNFS_LAYOUTFILE_CREATE_VAL_BUFFER  1024

static int pnfs_create_ds_partfile(pnfs_ds_client_t * pnfsdsclient,
                                   component4 name, fattr4_fileid fileid,
                                   pnfs_part_file_t * ppartfile)
{
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  struct timeval timeout = { 25, 0 };
  nfs_argop4 argoparray_open_ds_file[PNFS_LAYOUTFILE_NB_OP_OPEN_DS_FILE];
  nfs_resop4 resoparray_open_ds_file[PNFS_LAYOUTFILE_NB_OP_OPEN_DS_FILE];
  nfs_argop4 argoparray_close_ds_file[PNFS_LAYOUTFILE_NB_OP_CLOSE_DS_FILE];
  nfs_resop4 resoparray_close_ds_file[PNFS_LAYOUTFILE_NB_OP_CLOSE_DS_FILE];
  char owner_val[PNFS_LAYOUTFILE_OWNER_LEN];
  unsigned int owner_len = 0;
  unsigned int bitmap_val[2];
  unsigned int bitmap_res[2];
  fattr4_mode buffmode;
  fattr4 inattr;
  unsigned int i;

  char tmp[1024];

#define PNFS_LAYOUTFILE_CREATE_IDX_OP_SEQUENCE 0
#define PNFS_LAYOUTFILE_CREATE_IDX_OP_PUTFH    1
#define PNFS_LAYOUTFILE_CREATE_IDX_OP_OPEN     2
#define PNFS_LAYOUTFILE_CREATE_IDX_OP_GETFH    3

  if(!pnfsdsclient || !ppartfile)
    return NFS4ERR_SERVERFAULT;

  /* Create the owner */
  snprintf(owner_val, PNFS_LAYOUTFILE_OWNER_LEN,
           "GANESHA/PNFS: pid=%u clnt=%p fileid=%llu", getpid(), pnfsdsclient,
           (unsigned long long)fileid);
  owner_len = strnlen(owner_val, PNFS_LAYOUTFILE_OWNER_LEN);

  inattr.attrmask.bitmap4_len = 2;
  inattr.attrmask.bitmap4_val = bitmap_val;
  bitmap_val[0] = 0;
  bitmap_val[1] = 2;            /* FATTR4_MODE == 33 = 32+1 */
  inattr.attr_vals.attrlist4_len = sizeof(fattr4_mode);
  buffmode = htonl(0644);
  inattr.attr_vals.attrlist4_val = (char *)&buffmode;

  /* Step 1 OP4_OPEN as OPEN4_CREATE */
  argnfs4.argarray.argarray_val = argoparray_open_ds_file;
  resnfs4.resarray.resarray_val = resoparray_open_ds_file;
  argnfs4.minorversion = 1;

  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  resnfs4.resarray.resarray_val[PNFS_LAYOUTFILE_CREATE_IDX_OP_GETFH].nfs_resop4_u.
      opgetfh.GETFH4res_u.resok4.object.nfs_fh4_val =
      (char *)Mem_Alloc(PNFS_LAYOUTFILE_FILEHANDLE_MAX_LEN);

  resnfs4.resarray.resarray_val[PNFS_LAYOUTFILE_CREATE_IDX_OP_OPEN].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.attrset.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[PNFS_LAYOUTFILE_CREATE_IDX_OP_OPEN].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.attrset.bitmap4_len = 2;

  COMPOUNDV41_ARG_ADD_OP_SEQUENCE(argnfs4, pnfsdsclient->session, pnfsdsclient->sequence);
  pnfsdsclient->sequence += 1;  /* In all cases, failure or not, increment the sequence counter */
  COMPOUNDV41_ARG_ADD_OP_PUTFH(argnfs4, pnfsdsclient->ds_rootfh);
  COMPOUNDV41_ARG_ADD_OP_OPEN_CREATE(argnfs4, name, inattr, owner_val, owner_len);
  COMPOUNDV41_ARG_ADD_OP_GETFH(argnfs4);

  /* Call the NFSv4 function */
  if(clnt_call(pnfsdsclient->rpc_client, NFSPROC4_COMPOUND,
               (xdrproc_t) xdr_COMPOUND4args, (caddr_t) & argnfs4,
               (xdrproc_t) xdr_COMPOUND4res, (caddr_t) & resnfs4, timeout) != RPC_SUCCESS)
    {
      return NFS4ERR_IO;        /* @todo: For wanting of something more appropriate */
    }

  /* Get information from reply */
  ppartfile->stateid.seqid =
      resnfs4.resarray.resarray_val[PNFS_LAYOUTFILE_CREATE_IDX_OP_OPEN].
      nfs_resop4_u.opopen.OPEN4res_u.resok4.stateid.seqid;
  memcpy((char *)ppartfile->stateid.other,
         (char *)resnfs4.resarray.
         resarray_val[PNFS_LAYOUTFILE_CREATE_IDX_OP_OPEN].nfs_resop4_u.opopen.OPEN4res_u.
         resok4.stateid.other, 12);
  ppartfile->handle.nfs_fh4_len =
      resnfs4.resarray.resarray_val[PNFS_LAYOUTFILE_CREATE_IDX_OP_GETFH].
      nfs_resop4_u.opgetfh.GETFH4res_u.resok4.object.nfs_fh4_len;
  ppartfile->handle.nfs_fh4_val =
      resnfs4.resarray.resarray_val[PNFS_LAYOUTFILE_CREATE_IDX_OP_GETFH].
      nfs_resop4_u.opgetfh.GETFH4res_u.resok4.object.nfs_fh4_val;

  ppartfile->deviceid = 1;
  ppartfile->is_ganesha = FALSE;

  if(resnfs4.status != NFS4_OK)
    {
      return resnfs4.status;
    }

  /* Close the file */
  argnfs4.argarray.argarray_val = argoparray_close_ds_file;
  resnfs4.resarray.resarray_val = resoparray_close_ds_file;
  argnfs4.argarray.argarray_len = i;

  COMPOUNDV41_ARG_ADD_OP_SEQUENCE(argnfs4, pnfsdsclient->session, pnfsdsclient->sequence);
  pnfsdsclient->sequence += 1;  /* In all cases, failure or not, increment the sequence counter */
  COMPOUNDV41_ARG_ADD_OP_PUTFH(argnfs4, ppartfile->handle);
  COMPOUNDV41_ARG_ADD_OP_CLOSE(argnfs4, ppartfile->stateid);

  /* Call the NFSv4 function */
  if(clnt_call(pnfsdsclient->rpc_client, NFSPROC4_COMPOUND,
               (xdrproc_t) xdr_COMPOUND4args, (caddr_t) & argnfs4,
               (xdrproc_t) xdr_COMPOUND4res, (caddr_t) & resnfs4, timeout) != RPC_SUCCESS)
    {
      return NFS4ERR_IO;        /* @todo: For wanting of something more appropriate */
    }

  if(resnfs4.status != NFS4_OK)
    return resnfs4.status;

  return NFS4_OK;
}                               /* pnfs_create_ds_partfile */

int pnfs_create_ds_file(pnfs_client_t * pnfsclient,
                        fattr4_fileid fileid, pnfs_ds_file_t * pfile)
{
  component4 name;
  char nameval[MAXNAMLEN];
  char filename[MAXNAMLEN];
  int rc;
  unsigned int i = 0;

  if(!pnfsclient || !pfile)
    return NFS4ERR_SERVERFAULT;

  name.utf8string_val = nameval;
  name.utf8string_len = 0;

  snprintf(filename, MAXNAMLEN, "fileid=%llu", (unsigned long long)fileid);

  if(str2utf8(filename, &name) == -1)
    return NFS4ERR_SERVERFAULT;

  for(i = 0; i < pnfsclient->nb_ds; i++)
    {

      if((rc = pnfs_create_ds_partfile(&(pnfsclient->ds_client[i]),
                                       name, fileid, &(pfile->filepart[i]))) != NFS4_OK)
        return rc;

    }                           /* for */

  pfile->allocated = TRUE;
  pfile->stripe = pnfsclient->nb_ds;

  return NFS4_OK;
}                               /* pnfs_create_ds_file */
