/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_unlink.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.9 $
 * \brief   object removing function.
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
#include <gssrpc/clnt.h>
#include <gssrpc/xdr.h>
#else
#include <rpc/rpc.h>
#include <rpc/clnt.h>
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
 * FSAL_unlink:
 * Remove a filesystem object .
 *
 * \param parentdir_handle (input):
 *        Handle of the parent directory of the object to be deleted.
 * \param p_object_name (input):
 *        Name of the object to be removed.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param parentdir_attributes (optionnal input/output): 
 *        Post operation attributes of the parent directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parentdir_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parentdir_handle does not address a directory)
 *        - ERR_FSAL_NOENT        (the object designated by p_object_name does not exist)
 *        - ERR_FSAL_NOTEMPTY     (tried to remove a non empty directory)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t PROXYFSAL_unlink(fsal_handle_t * parentdir_handle,   /* IN */
                               fsal_name_t * p_object_name,     /* IN */
                               fsal_op_context_t *context,      /* IN */
                               fsal_attrib_list_t * parentdir_attributes        /* [IN/OUT ] */
    )
{
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  bitmap4 bitmap;
  uint32_t bitmap_val[2];
  component4 name;
  char nameval[MAXNAMLEN];
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

#define FSAL_UNLINK_NB_OP_ALLOC 3
  nfs_argop4 argoparray[FSAL_UNLINK_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_UNLINK_NB_OP_ALLOC];
  uint32_t bitmap_res[2];

  fsal_proxy_internal_fattr_t fattr_internal;
  struct timeval timeout = TIMEOUTRPC;

  /* sanity checks.
   * note : parentdir_attributes are optional.
   *        parentdir_handle is mandatory,
   *        because, we do not allow to delete FS root !
   */
  if(!parentdir_handle || !p_context || !p_object_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlink);

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  fsal_internal_proxy_setup_fattr(&fattr_internal);
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Unlink" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  /* >> retrieve root handle filehandle here << */
  bitmap.bitmap4_val = bitmap_val;
  bitmap.bitmap4_len = 2;

  fsal_internal_proxy_create_fattr_bitmap(&bitmap);

  if(fsal_internal_proxy_extract_fh(&nfs4fh, parentdir_handle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlink);

  memset((char *)&name, 0, sizeof(component4));
  name.utf8string_val = nameval;
  if(fsal_internal_proxy_fsal_name_2_utf8(p_object_name, &name) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlink);

#define FSAL_UNLINK_IDX_OP_PUTFH      0
#define FSAL_UNLINK_IDX_OP_REMOVE     1
#define FSAL_UNLINK_IDX_OP_GETATTR    2

  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_REMOVE(argnfs4, name);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);

  resnfs4.resarray.resarray_val[FSAL_UNLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[FSAL_UNLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_UNLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_UNLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_unlink);
    }

  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_unlink);

  /* >> get post op attributes for the parent, if they are asked,
   * and your filesystem didn't return them << */

  if(parentdir_attributes)
    {

      if(nfs4_Fattr_To_FSAL_attr(parentdir_attributes,
                                 &resnfs4.resarray.
                                 resarray_val[FSAL_UNLINK_IDX_OP_GETATTR].nfs_resop4_u.
                                 opgetattr.GETATTR4res_u.resok4.obj_attributes) != NFS4_OK)
        {
          FSAL_CLEAR_MASK(parentdir_attributes->asked_attributes);
          FSAL_SET_MASK(parentdir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_unlink);
        }

    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlink);

}
