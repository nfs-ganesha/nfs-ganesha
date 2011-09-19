/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_attrs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/09/09 15:22:49 $
 * \version $Revision: 1.19 $
 * \brief   Attributes functions.
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

extern fsal_staticfsinfo_t default_proxy_info;

/**
 * PROXYFSAL_getattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user, export...).
 * \param object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (object_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument) 
 *        - Another error code if an error occured.
 */
fsal_status_t PROXYFSAL_getattrs(fsal_handle_t * filehandle,       /* IN */
                                 fsal_op_context_t * context,    /* IN */
                                 fsal_attrib_list_t * object_attributes /* IN/OUT */
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

#define FSAL_GETATTR_NB_OP_ALLOC 2
  nfs_argop4 argoparray[FSAL_GETATTR_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_GETATTR_NB_OP_ALLOC];

  fsal_proxy_internal_fattr_t fattr_internal;
  struct timeval timeout = TIMEOUTRPC;
  /* sanity checks.
   * note : object_attributes is mandatory in PROXYFSAL_getattrs.
   */
  if(!filehandle || !p_context || !object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

  PRINT_HANDLE("PROXYFSAL_getattrs", filehandle);

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
  fsal_internal_proxy_create_fattr_bitmap(&bitmap);

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, filehandle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

#define FSAL_GETATTR_IDX_OP_PUTFH     0
#define FSAL_GETATTR_IDX_OP_GETATTR   1
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);

  resnfs4.resarray.resarray_val[FSAL_GETATTR_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[FSAL_GETATTR_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_GETATTR_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_GETATTR_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, resnfs4.status, INDEX_FSAL_getattrs);
    }

  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_getattrs);

  /* Use NFSv4 service function to build the FSAL_attr */
  if(nfs4_Fattr_To_FSAL_attr(object_attributes,
                             &resnfs4.resarray.resarray_val[FSAL_GETATTR_IDX_OP_GETATTR].
                             nfs_resop4_u.opgetattr.GETATTR4res_u.resok4.
                             obj_attributes) != NFS4_OK)
    {
      FSAL_CLEAR_MASK(object_attributes->asked_attributes);
      FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_getattrs);
    }

  /** @todo BUGAZOMEU Cleaner supported_attributes to be put here */
  object_attributes->supported_attributes = default_proxy_info.supported_attrs;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs);
}

/**
 * PROXYFSAL_setattrs:
 * Set attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param attrib_set (mandatory input):
 *        The attributes to be set for the object.
 *        It defines the attributes that the caller
 *        wants to set and their values.
 * \param object_attributes (optionnal input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (object_handle does not address an existing object)
 *        - ERR_FSAL_INVAL        (tried to modify a read-only attribute)
 *        - ERR_FSAL_ATTRNOTSUPP  (tried to modify a non-supported attribute)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Another error code if an error occured.
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */

fsal_status_t PROXYFSAL_setattrs(fsal_handle_t * filehandle,       /* IN */
                                 fsal_op_context_t * context,    /* IN */
                                 fsal_attrib_list_t * attrib_set,       /* IN */
                                 fsal_attrib_list_t * object_attributes /* [ IN/OUT ] */
    )
{

  int rc;
  fsal_attrib_list_t attrs;

  nfs_fh4 nfs4fh;
  uint32_t bitmap_val[2];
  uint32_t bitmap_conv_val[2];
  fattr4 input_attr;
  bitmap4 convert_bitmap;
  bitmap4 output_bitmap;
  uint32_t bitmap_res_getattr[2];
  uint32_t bitmap_val_getattr[2];
  COMPOUND4res resnfs4;
  COMPOUND4args argnfs4;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

  fsal_proxy_internal_fattr_t fattr_internal_getattr;

#define FSAL_SETATTR_NB_OP_ALLOC 3
#define FSAL_SETATTR_VAL_BUFFER  1024

  nfs_argop4 argoparray[FSAL_SETATTR_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_SETATTR_NB_OP_ALLOC];

  char fattr_val[FSAL_SETATTR_VAL_BUFFER];
  struct timeval timeout = TIMEOUTRPC;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!filehandle || !p_context || !attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  PRINT_HANDLE("FSAL_setattr", filehandle);

  memset((char *)&argnfs4, 0, sizeof(COMPOUND4args));
  memset((char *)&resnfs4, 0, sizeof(COMPOUND4res));

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /*argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Setattr" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  input_attr.attrmask.bitmap4_val = bitmap_val;
  input_attr.attrmask.bitmap4_len = 2;

  input_attr.attr_vals.attrlist4_val = fattr_val;
  input_attr.attr_vals.attrlist4_len = FSAL_SETATTR_VAL_BUFFER;

  convert_bitmap.bitmap4_val = bitmap_conv_val;
  convert_bitmap.bitmap4_len = 2;

  /* local copy of attributes */
  attrs = *attrib_set;

  output_bitmap.bitmap4_val = bitmap_val_getattr;
  output_bitmap.bitmap4_len = 2;

  fsal_internal_proxy_create_fattr_bitmap(&output_bitmap);
  fsal_internal_proxy_setup_fattr(&fattr_internal_getattr);

  /* Is it allowed to change times ? */

  if(!global_fs_info.cansettime)
    {

      if(attrs.asked_attributes
         & (FSAL_ATTR_ATIME | FSAL_ATTR_CREATION | FSAL_ATTR_CTIME | FSAL_ATTR_MTIME))
        {

          /* handled as an unsettable attribute. */
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_setattrs);
        }

    }

  /* apply umask, if mode attribute is to be changed */

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    {
      attrs.mode &= (~global_fs_info.umask);
    }

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, filehandle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  fsal_interval_proxy_fsalattr2bitmap4(attrib_set, &convert_bitmap);

  /* Create the fattr4 for the request */
  /* We use the function that is used to convert NFSv4 read attributes, only a subset of it will be used */
  if(nfs4_FSALattr_To_Fattr(NULL,       /* no exportlist required here */
                            attrib_set, &input_attr, NULL,      /* no compound data required here */
                            NULL,       /* No fh here, filehandle is not a settable attribute */
                            &convert_bitmap) == -1)
    Return(ERR_FSAL_INVAL, -1, INDEX_FSAL_setattrs);

#define FSAL_SETATTR_IDX_OP_PUTFH     0
#define FSAL_SETATTR_IDX_OP_SETATTR   1
#define FSAL_SETATTR_IDX_OP_GETATTR   2
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_SETATTR(argnfs4, input_attr);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, output_bitmap);

  resnfs4.resarray.resarray_val[FSAL_SETATTR_IDX_OP_SETATTR].nfs_resop4_u.opsetattr.
      attrsset.bitmap4_val = bitmap_res_getattr;
  resnfs4.resarray.resarray_val[FSAL_SETATTR_IDX_OP_SETATTR].nfs_resop4_u.opsetattr.
      attrsset.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_SETATTR_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_res_getattr;
  resnfs4.resarray.resarray_val[FSAL_SETATTR_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_SETATTR_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal_getattr;
  resnfs4.resarray.resarray_val[FSAL_SETATTR_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal_getattr);

  /* Call to the server for which we act as a proxy */
  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, 0, INDEX_FSAL_setattrs);
    }

  ReleaseTokenFSCall();

  /* >> convert error code, and return on error << */
  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_setattrs);

  /* Optionaly fill output attributes */

  if(object_attributes)
    {

      /* Use NFSv4 service function to build the FSAL_attr */
      if(nfs4_Fattr_To_FSAL_attr(object_attributes,
                                 &resnfs4.resarray.
                                 resarray_val[FSAL_SETATTR_IDX_OP_GETATTR].nfs_resop4_u.
                                 opgetattr.GETATTR4res_u.resok4.obj_attributes) != NFS4_OK)
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_setattrs);
        }
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_setattrs);

}
