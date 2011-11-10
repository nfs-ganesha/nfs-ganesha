/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_fsinfo.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/16 08:20:22 $
 * \version $Revision: 1.12 $
 * \brief   functions for retrieving filesystem info.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <string.h>
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/xdr.h>
#else
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#endif
#include "nfs4.h"

#include "stuff_alloc.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"

#include "nfs_proto_functions.h"
#include "fsal_nfsv4_macros.h"

/**
 * FSAL_dynamic_fsinfo:
 * Return dynamic filesystem info such as
 * used size, free size, number of objects...
 *
 * \param filehandle (input):
 *        Handle of an object in the filesystem
 *        whom info is to be retrieved.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param dynamicinfo (output):
 *        Pointer to the static info of the filesystem.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t PROXYFSAL_dynamic_fsinfo(fsal_handle_t * filehandle, /* IN */
                                       fsal_op_context_t * context,      /* IN */
                                       fsal_dynamicfsinfo_t * dynamicinfo       /* OUT */
    )
{

  int rc;

  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  bitmap4 bitmap;
  uint32_t bitmap_val[2];
  uint32_t bitmap_res[2];
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

#define FSAL_FSINFO_NB_OP_ALLOC 2
  nfs_argop4 argoparray[FSAL_FSINFO_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_FSINFO_NB_OP_ALLOC];

  fsal_proxy_internal_fattr_t fattr_internal;
  struct timeval timeout = TIMEOUTRPC;

  /* sanity checks. */
  if(!filehandle || !dynamicinfo || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_dynamic_fsinfo);

  /* >> get attributes from your filesystem << */
  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  fsal_internal_proxy_setup_fattr(&fattr_internal);
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Getattr" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  bitmap.bitmap4_val = bitmap_val;
  bitmap.bitmap4_len = 2;

  fsal_internal_proxy_create_fattr_fsinfo_bitmap(&bitmap);

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, filehandle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

#define FSAL_FSINFO_IDX_OP_PUTFH     0
#define FSAL_FSINFO_IDX_OP_GETATTR   1
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);

  resnfs4.resarray.resarray_val[FSAL_FSINFO_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[FSAL_FSINFO_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_FSINFO_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_FSINFO_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, 0, INDEX_FSAL_dynamic_fsinfo);
    }

  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_dynamic_fsinfo);

  /* Use NFSv4 service function to build the FSAL_attr */
  if(proxy_Fattr_To_FSAL_dynamic_fsinfo(dynamicinfo,
                                        &resnfs4.resarray.resarray_val
                                        [FSAL_FSINFO_IDX_OP_GETATTR].
                                        nfs_resop4_u.opgetattr.GETATTR4res_u.resok4.
                                        obj_attributes) != 1)
    {
      memset((char *)dynamicinfo, 0, sizeof(fsal_dynamicfsinfo_t));
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_dynamic_fsinfo);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_dynamic_fsinfo);

}                               /* FSAL_dynamic_fsinfo */
