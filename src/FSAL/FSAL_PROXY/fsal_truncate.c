/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_truncate.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/29 09:39:05 $
 * \version $Revision: 1.4 $
 * \brief   Truncate function.
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

fsal_status_t FSAL_proxy_truncate_stateless(fsal_handle_t * file_hdl,    /* IN */
                                            fsal_op_context_t * context, /* IN */
                                            fsal_size_t length, /* IN */
                                            fsal_attrib_list_t * object_attributes      /* [ IN/OUT ] */
    )
{
  int rc;

  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  fsal_status_t fsal_status;
  fsal_attrib_list_t open_attrs;
  bitmap4 inbitmap;
  bitmap4 convert_bitmap;
  uint32_t inbitmap_val[2];
  uint32_t bitmap_res[2];
  uint32_t bitmap_set[2];
  uint32_t bitmap_conv_val[2];
  proxyfsal_handle_t * filehandle = (proxyfsal_handle_t *)file_hdl;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

#define FSAL_TRUNCATE_STATELESS_NB_OP_ALLOC 3
  nfs_argop4 argoparray[FSAL_TRUNCATE_STATELESS_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_TRUNCATE_STATELESS_NB_OP_ALLOC];

  fsal_attrib_list_t fsal_attr_set;
  fattr4 fattr_set;
  fsal_proxy_internal_fattr_t fattr_internal;
  struct timeval timeout = TIMEOUTRPC;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!filehandle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  if(filehandle->data.object_type_reminder != FSAL_TYPE_FILE)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_truncate);
    }

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  fsal_internal_proxy_setup_fattr(&fattr_internal);
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Truncate" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, file_hdl) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  /* Get prepared for truncate */
  fsal_attr_set.asked_attributes = FSAL_ATTR_SIZE;
  fsal_attr_set.filesize = length;

  convert_bitmap.bitmap4_val = bitmap_conv_val;
  convert_bitmap.bitmap4_len = 2;

  fsal_interval_proxy_fsalattr2bitmap4(&fsal_attr_set, &convert_bitmap);

  if(nfs4_FSALattr_To_Fattr(NULL,       /* no exportlist required here */
                            &fsal_attr_set, &fattr_set, NULL,   /* no compound data required here */
                            NULL,       /* No fh here, filehandle is not a settable attribute */
                            &convert_bitmap) == -1)
    Return(ERR_FSAL_INVAL, -1, INDEX_FSAL_truncate);

  inbitmap.bitmap4_val = inbitmap_val;
  inbitmap.bitmap4_len = 2;
  fsal_internal_proxy_create_fattr_bitmap(&inbitmap);

#define FSAL_TRUNCATE_STATELESS_IDX_OP_PUTFH     0
#define FSAL_TRUNCATE_STATELESS_IDX_OP_SETATTR   1
#define FSAL_TRUNCATE_STATELESS_IDX_OP_GETATTR   2
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_SETATTR(argnfs4, fattr_set);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, inbitmap);

  /* For ATTR_SIZE, stateid is needed, we use the stateless "all-0's" stateid here */
  argnfs4.argarray.argarray_val[FSAL_TRUNCATE_STATELESS_IDX_OP_SETATTR].nfs_argop4_u.
      opsetattr.stateid.seqid = 0;
  memset(argnfs4.argarray.argarray_val[FSAL_TRUNCATE_STATELESS_IDX_OP_SETATTR].
         nfs_argop4_u.opsetattr.stateid.other, 0, 12);

  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_STATELESS_IDX_OP_GETATTR].nfs_resop4_u.
      opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_STATELESS_IDX_OP_GETATTR].nfs_resop4_u.
      opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_STATELESS_IDX_OP_GETATTR].nfs_resop4_u.
      opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_STATELESS_IDX_OP_GETATTR].nfs_resop4_u.
      opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_STATELESS_IDX_OP_SETATTR].nfs_resop4_u.
      opsetattr.attrsset.bitmap4_val = bitmap_set;
  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_STATELESS_IDX_OP_SETATTR].nfs_resop4_u.
      opsetattr.attrsset.bitmap4_len = 2;

  TakeTokenFSCall();

  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, 0, INDEX_FSAL_truncate);
    }

  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_truncate);

  /* >> interpret error code << */

  /* >> Optionnaly retrieve post op attributes
   * If your filesystem truncate call can't return them,
   * you can proceed like this : <<
   */
  if(object_attributes)
    {
      if(nfs4_Fattr_To_FSAL_attr(object_attributes,
                                 &resnfs4.resarray.resarray_val
                                 [FSAL_TRUNCATE_STATELESS_IDX_OP_GETATTR].
                                 nfs_resop4_u.opgetattr.GETATTR4res_u.resok4.
                                 obj_attributes) != NFS4_OK)
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_truncate);
        }

    }

  /* No error occured */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_truncate);
}                               /* FSAL_proxy_truncate_stateless */

/**
 * FSAL_truncate:
 * Modify the data length of a regular file.
 *
 * \param filehandle (input):
 *        Handle of the file is to be truncated.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param length (input):
 *        The new data length for the file.
 * \param object_attributes (optionnal input/output): 
 *        The post operation attributes of the file.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (filehandle does not address an existing object)
 *        - ERR_FSAL_INVAL        (filehandle does not address a regular file)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t PROXYFSAL_truncate(fsal_handle_t * file_hdl,       /* IN */
                                 fsal_op_context_t * context,    /* IN */
                                 fsal_size_t length,    /* IN */
                                 fsal_file_t * file_descriptor, /* [IN|OUT] */
                                 fsal_attrib_list_t * object_attributes /* [ IN/OUT ] */
    )
{

  int rc;

  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  uint64_t fileid;
  fsal_status_t fsal_status;
  fsal_attrib_list_t open_attrs;
  bitmap4 inbitmap;
  bitmap4 convert_bitmap;
  uint32_t inbitmap_val[2];
  uint32_t bitmap_res[2];
  uint32_t bitmap_set[2];
  uint32_t bitmap_conv_val[2];
  proxyfsal_handle_t * filehandle = (proxyfsal_handle_t *)file_hdl;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

#define FSAL_TRUNCATE_NB_OP_ALLOC 3
  nfs_argop4 argoparray[FSAL_TRUNCATE_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_TRUNCATE_NB_OP_ALLOC];

  fsal_attrib_list_t fsal_attr_set;
  fattr4 fattr_set;
  fsal_proxy_internal_fattr_t fattr_internal;
  struct timeval timeout = TIMEOUTRPC;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!filehandle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  if(filehandle->data.object_type_reminder != FSAL_TYPE_FILE)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_truncate);
    }

  if(file_descriptor == NULL)
    {
      /* Use the stateless version */
      fsal_status = FSAL_proxy_truncate_stateless(file_hdl,
                                                  context, length, object_attributes);
      Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_truncate);
    }

  /* First, we need to get the fileid on a filehandle base */
  fsal_status = FSAL_DigestHandle(context->export_context,
                                  FSAL_DIGEST_FILEID4, file_hdl, (caddr_t) & fileid);
  if(FSAL_IS_ERROR(fsal_status))
    {
      Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_truncate);
    }

  /* Then we have of open the file by fileid */
  open_attrs.asked_attributes = FSAL_ATTRS_POSIX;
  fsal_status = FSAL_open_by_fileid(file_hdl,
                                    fileid,
                                    context, FSAL_O_RDWR, file_descriptor, &open_attrs);
  if(FSAL_IS_ERROR(fsal_status))
    {
      Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_truncate);
    }

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  fsal_internal_proxy_setup_fattr(&fattr_internal);
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Truncate" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, file_hdl) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  /* Get prepared for truncate */
  fsal_attr_set.asked_attributes = FSAL_ATTR_SIZE;
  fsal_attr_set.filesize = length;

  convert_bitmap.bitmap4_val = bitmap_conv_val;
  convert_bitmap.bitmap4_len = 2;

  fsal_interval_proxy_fsalattr2bitmap4(&fsal_attr_set, &convert_bitmap);

  if(nfs4_FSALattr_To_Fattr(NULL,       /* no exportlist required here */
                            &fsal_attr_set, &fattr_set, NULL,   /* no compound data required here */
                            NULL,       /* No fh here, filehandle is not a settable attribute */
                            &convert_bitmap) == -1)
    Return(ERR_FSAL_INVAL, -1, INDEX_FSAL_truncate);

  inbitmap.bitmap4_val = inbitmap_val;
  inbitmap.bitmap4_len = 2;
  fsal_internal_proxy_create_fattr_bitmap(&inbitmap);

#define FSAL_TRUNCATE_IDX_OP_PUTFH     0
#define FSAL_TRUNCATE_IDX_OP_SETATTR   1
#define FSAL_TRUNCATE_IDX_OP_GETATTR   2
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_SETATTR(argnfs4, fattr_set);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, inbitmap);

  /* For ATTR_SIZE, stateid is needed */
  argnfs4.argarray.argarray_val[FSAL_TRUNCATE_IDX_OP_SETATTR].nfs_argop4_u.opsetattr.
      stateid.seqid = ((proxyfsal_file_t *)file_descriptor)->stateid.seqid;
  memcpy(argnfs4.argarray.argarray_val[FSAL_TRUNCATE_IDX_OP_SETATTR].nfs_argop4_u.
         opsetattr.stateid.other, ((proxyfsal_file_t *)file_descriptor)->stateid.other, 12);

  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_IDX_OP_SETATTR].nfs_resop4_u.opsetattr.
      attrsset.bitmap4_val = bitmap_set;
  resnfs4.resarray.resarray_val[FSAL_TRUNCATE_IDX_OP_SETATTR].nfs_resop4_u.opsetattr.
      attrsset.bitmap4_len = 2;

  TakeTokenFSCall();

  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, 0, INDEX_FSAL_truncate);
    }

  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_truncate);

  /* >> interpret error code << */

  /* >> Optionnaly retrieve post op attributes
   * If your filesystem truncate call can't return them,
   * you can proceed like this : <<
   */
  if(object_attributes)
    {
      if(nfs4_Fattr_To_FSAL_attr(object_attributes,
                                 &resnfs4.resarray.
                                 resarray_val[FSAL_TRUNCATE_IDX_OP_GETATTR].nfs_resop4_u.
                                 opgetattr.GETATTR4res_u.resok4.obj_attributes) != NFS4_OK)
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_truncate);
        }

    }

  /* Close the previously opened filedescriptor */
  fsal_status = FSAL_close_by_fileid(file_descriptor, fileid);
  if(FSAL_IS_ERROR(fsal_status))
    {
      Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_truncate);
    }

  /* No error occured */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_truncate);
}
