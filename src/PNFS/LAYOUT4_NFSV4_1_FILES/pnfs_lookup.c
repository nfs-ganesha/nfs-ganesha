/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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

#include "nfs_proto_functions.h"

#include "PNFS/LAYOUT4_NFSV4_1_FILES/pnfs_layout4_nfsv4_1_files.h"
#include "pnfs_nfsv41_macros.h"

#define PNFS_LAYOUTFILE_NB_OP_EXCHANGEID 2
#define PNFS_LAYOUTFILE_NB_OP_CREATESESSION 2

/**
 * \file    pnfs_lookup.c
 * \brief   Lookup operations.
 *
 */

/**
 *
 * pnfs_lookup: looks up for a path's component.
 *
 * Looks up for a path's component.
 *
 * @param pnfsclient              [IN]  pointer to the pnfsclient structure (client to the ds).
 * @param parent_directory_handle [IN]  the NFSv4 file handle for the parent directory
 * @param filename                [IN]  the path's component to be looked up
 * @param object_handle           [OUT] the resulting NFSv4 file handle
 *
 * @return NFS4_OK if successful
 * @return a NFSv4 error (positive value) if failed.
 *
 */
int pnfs_lookup(pnfs_ds_client_t * pnfsdsclient, nfs_fh4 * parent_directory_handle,     /* IN */
                char *filename, /* IN */
                nfs_fh4 * object_handle)
{

  int rc;
  nfs_fh4 nfs4fh;
  component4 name;
  char nameval[MAXNAMLEN];
  unsigned int index_getfh = 0;

  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  struct timeval timeout = { 25, 0 };

#define  PNFS_LAYOUTFILE_NB_OP_ALLOC 4
  nfs_argop4 argoparray[PNFS_LAYOUTFILE_NB_OP_ALLOC];
  nfs_resop4 resoparray[PNFS_LAYOUTFILE_NB_OP_ALLOC];
  uint32_t bitmap_res[2];

  char padfilehandle[PNFS_LAYOUTFILE_FILEHANDLE_MAX_LEN];

  /* sanity checks
   * note : object_attributes is optionnal
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!object_handle || !filename || !object_handle)
    return NFS4ERR_INVAL;

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 1;
  argnfs4.argarray.argarray_len = 0;

  name.utf8string_val = nameval;
  name.utf8string_len = 0;

  if(!parent_directory_handle)
    {
      /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Lookup Root" ; */
      argnfs4.tag.utf8string_val = NULL;
      argnfs4.tag.utf8string_len = 0;

#define PNFS_LOOKUP_IDX_OP_SEQUENCE       0
#define PNFS_LOOKUP_IDX_OP_PUTROOTFH      1
#define PNFS_LOOKUP_IDX_OP_GETFH_ROOT     2
      COMPOUNDV41_ARG_ADD_OP_SEQUENCE(argnfs4, pnfsdsclient->session,
                                      pnfsdsclient->sequence);
      COMPOUNDV41_ARG_ADD_OP_PUTROOTFH(argnfs4);
      COMPOUNDV41_ARG_ADD_OP_GETFH(argnfs4);

      index_getfh = PNFS_LOOKUP_IDX_OP_GETFH_ROOT;

      resnfs4.resarray.resarray_val[PNFS_LOOKUP_IDX_OP_GETFH_ROOT].nfs_resop4_u.opgetfh.
          GETFH4res_u.resok4.object.nfs_fh4_val = (char *)padfilehandle;
      resnfs4.resarray.resarray_val[PNFS_LOOKUP_IDX_OP_GETFH_ROOT].nfs_resop4_u.opgetfh.
          GETFH4res_u.resok4.object.nfs_fh4_len = PNFS_LAYOUTFILE_FILEHANDLE_MAX_LEN;
    }
  else                          /* this is a real lookup(parent, name)  */
    {
      /* the filename should not be null */
      if(filename == NULL)
        return NFS4ERR_INVAL;

      if(str2utf8(filename, &name) == -1)
        return NFS4ERR_SERVERFAULT;

      nfs4fh.nfs_fh4_len = parent_directory_handle->nfs_fh4_len;
      nfs4fh.nfs_fh4_val = parent_directory_handle->nfs_fh4_val;

      /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Lookup name" ; */
      argnfs4.tag.utf8string_val = NULL;
      argnfs4.tag.utf8string_len = 0;

#define PNFS_LOOKUP_IDX_OP_SEQUENCE  0
#define PNFS_LOOKUP_IDX_OP_PUTFH     1
#define PNFS_LOOKUP_IDX_OP_LOOKUP    2
#define PNFS_LOOKUP_IDX_OP_GETFH     3
      COMPOUNDV41_ARG_ADD_OP_SEQUENCE(argnfs4, pnfsdsclient->session,
                                      pnfsdsclient->sequence);
      COMPOUNDV41_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
      COMPOUNDV41_ARG_ADD_OP_LOOKUP(argnfs4, name);
      COMPOUNDV41_ARG_ADD_OP_GETFH(argnfs4);

      index_getfh = PNFS_LOOKUP_IDX_OP_GETFH;

      resnfs4.resarray.resarray_val[PNFS_LOOKUP_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
          GETFH4res_u.resok4.object.nfs_fh4_val = (char *)padfilehandle;
      resnfs4.resarray.resarray_val[PNFS_LOOKUP_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
          GETFH4res_u.resok4.object.nfs_fh4_len = PNFS_LAYOUTFILE_FILEHANDLE_MAX_LEN;

    }

  /* Call the NFSv4 function */
  if(clnt_call(pnfsdsclient->rpc_client, NFSPROC4_COMPOUND,
               (xdrproc_t) xdr_COMPOUND4args, (caddr_t) & argnfs4,
               (xdrproc_t) xdr_COMPOUND4res, (caddr_t) & resnfs4, timeout) != RPC_SUCCESS)
    {
      return NFS4ERR_IO;        /* @todo: For wanting of something more appropriate */
    }

  /* Increment the sequence */
  pnfsdsclient->sequence += 1;

  if(resnfs4.status != NFS4_OK)
    {
      return resnfs4.status;
    }

  object_handle->nfs_fh4_len =
      resnfs4.resarray.resarray_val[index_getfh].nfs_resop4_u.opgetfh.GETFH4res_u.resok4.
      object.nfs_fh4_len;
  memcpy((char *)object_handle->nfs_fh4_val,
         (char *)resnfs4.resarray.resarray_val[index_getfh].nfs_resop4_u.opgetfh.
         GETFH4res_u.resok4.object.nfs_fh4_val,
         resnfs4.resarray.resarray_val[index_getfh].nfs_resop4_u.opgetfh.GETFH4res_u.
         resok4.object.nfs_fh4_len);

  /* lookup complete ! */
  return NFS4_OK;
}

/**
 *
 * pnfs_lookupPath: looks up for a full path
 *
 * Looks up for a full path.
 *
 * @param pnfsdsclient        [IN]  pointer to the pnfdssclient structure (client to the ds).
 * @param path              [IN]  the path to be looked up
 * @param object_handle     [OUT] the resulting NFSv4 file handle
 *
 * @return NFS4_OK if successful
 * @return a NFSv4 error (positive value) if failed.
 *
 */
int pnfs_lookupPath(pnfs_ds_client_t * pnfsdsclient, char *path, nfs_fh4 * object_handle)
{
  char *ptr_str;
  nfs_fh4 out_hdl;
  unsigned int status;
  int b_is_last = FALSE;        /* is it the last lookup ? */

  char padfilehandle[PNFS_LAYOUTFILE_FILEHANDLE_MAX_LEN];

  if(!object_handle || !pnfsdsclient || !path)
    return NFS4ERR_INVAL;

  out_hdl.nfs_fh4_len = 0;
  out_hdl.nfs_fh4_val = padfilehandle;

  /* test whether the path begins with a slash */
  if(path[0] != '/')
    return NFS4ERR_INVAL;

  /* the pointer now points on the next name in the path,
   * skipping slashes.
   */

  ptr_str = path + 1;
  while(ptr_str[0] == '/')
    ptr_str++;

  /* is the next name empty ? */

  if(ptr_str[0] == '\0')
    b_is_last = TRUE;

  /* retrieves root directory */

  if((status = pnfs_lookup(pnfsdsclient, NULL,  /* looking up for root */
                           "",  /* empty string to get root handle */
                           &out_hdl)) != NFS4_OK)       /* output root handle */
    return status;

  /* exits if this was the last lookup */

  if(b_is_last)
    {
      object_handle->nfs_fh4_len = out_hdl.nfs_fh4_len;
      memcpy((char *)object_handle->nfs_fh4_val, (char *)out_hdl.nfs_fh4_val,
             out_hdl.nfs_fh4_len);
      return NFS4_OK;
    }

  /* proceed a step by step lookup */

  while(ptr_str[0])
    {

      nfs_fh4 in_hdl;
      char padfilehandle_out[PNFS_LAYOUTFILE_FILEHANDLE_MAX_LEN];
      char obj_name[MAXPATHLEN];

      char *dest_ptr;

      /* preparing lookup */

      in_hdl = out_hdl;
      out_hdl.nfs_fh4_val = padfilehandle_out;

      /* compute next name */
      dest_ptr = obj_name;
      while(ptr_str[0] != '\0' && ptr_str[0] != '/')
        {
          dest_ptr[0] = ptr_str[0];
          dest_ptr++;
          ptr_str++;
        }
      /* final null char */
      dest_ptr[0] = '\0';

      /* skip multiple slashes */
      while(ptr_str[0] == '/')
        ptr_str++;

      /* is the next name empty ? */
      if(ptr_str[0] == '\0')
        b_is_last = TRUE;

      /*call to FSAL_lookup */
      if((status = pnfs_lookup(pnfsdsclient, &in_hdl, obj_name, &out_hdl)) != NFS4_OK)
        return status;

      /* ptr_str is ok, we are ready for next loop */
    }

  /* Successful exit */
  object_handle->nfs_fh4_len = out_hdl.nfs_fh4_len;
  memcpy((char *)object_handle->nfs_fh4_val, (char *)out_hdl.nfs_fh4_val,
         out_hdl.nfs_fh4_len);

  return NFS4_OK;

}                               /* pnfs_lookupPath */
