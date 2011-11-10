/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_rename.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.9 $
 * \brief   object renaming/moving function.
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
 * FSAL_rename:
 * Change name and/or parent dir of a filesystem object.
 *
 * \param old_parentdir_handle (input):
 *        Source parent directory of the object is to be moved/renamed.
 * \param p_old_name (input):
 *        Pointer to the current name of the object to be moved/renamed.
 * \param new_parentdir_handle (input):
 *        Target parent directory for the object.
 * \param p_new_name (input):
 *        Pointer to the new name for the object.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param src_dir_attributes (optionnal input/output): 
 *        Post operation attributes for the source directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 * \param tgt_dir_attributes (optionnal input/output): 
 *        Post operation attributes for the target directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (a parent directory handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (a parent directory handle does not address a directory)
 *        - ERR_FSAL_NOENT        (the object designated by p_old_name does not exist)
 *        - ERR_FSAL_NOTEMPTY     (the target object is a non empty directory)
 *        - ERR_FSAL_XDEV         (tried to move an object across different filesystems)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
  */

fsal_status_t PROXYFSAL_rename(fsal_handle_t * old_parent,       /* IN */
                               fsal_name_t * p_old_name,        /* IN */
                               fsal_handle_t * new_parent,       /* IN */
                               fsal_name_t * p_new_name,        /* IN */
                               fsal_op_context_t *context,      /* IN */
                               fsal_attrib_list_t * src_dir_attributes, /* [ IN/OUT ] */
                               fsal_attrib_list_t * tgt_dir_attributes  /* [ IN/OUT ] */
    )
{

  int rc;

  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh_old;
  nfs_fh4 nfs4fh_new;
  bitmap4 bitmap_old;
  uint32_t bitmap_val_old[2];
  bitmap4 bitmap_new;
  uint32_t bitmap_val_new[2];
  component4 oldname;
  char oldnameval[MAXNAMLEN];
  component4 newname;
  char newnameval[MAXNAMLEN];
  proxyfsal_handle_t * old_parentdir_handle = (proxyfsal_handle_t *)old_parent;
  proxyfsal_handle_t * new_parentdir_handle = (proxyfsal_handle_t *)new_parent;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

#define FSAL_RENAME_NB_OP_ALLOC 7
  nfs_argop4 argoparray[FSAL_RENAME_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_RENAME_NB_OP_ALLOC];
  uint32_t bitmap_res_old[2];
  uint32_t bitmap_res_new[2];

  fsal_proxy_internal_fattr_t fattr_internal_new;
  fsal_proxy_internal_fattr_t fattr_internal_old;
  struct timeval timeout = TIMEOUTRPC;

  /* sanity checks.
   * note : src/tgt_dir_attributes are optional.
   */
  if(!old_parentdir_handle ||
     !new_parentdir_handle || !p_old_name || !p_new_name || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  fsal_internal_proxy_setup_fattr(&fattr_internal_new);
  fsal_internal_proxy_setup_fattr(&fattr_internal_old);
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Rename" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  /* Prepare the structures */
  bitmap_old.bitmap4_val = bitmap_val_old;
  bitmap_old.bitmap4_len = 2;

  fsal_internal_proxy_create_fattr_bitmap(&bitmap_old);

  if(fsal_internal_proxy_extract_fh(&nfs4fh_old, old_parent) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

  memset((char *)&oldname, 0, sizeof(component4));
  oldname.utf8string_val = oldnameval;
  if(fsal_internal_proxy_fsal_name_2_utf8(p_old_name, &oldname) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

  bitmap_new.bitmap4_val = bitmap_val_new;
  bitmap_new.bitmap4_len = 2;
  fsal_internal_proxy_create_fattr_bitmap(&bitmap_new);

  if(fsal_internal_proxy_extract_fh(&nfs4fh_new, new_parent) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

  memset((char *)&newname, 0, sizeof(component4));
  newname.utf8string_val = newnameval;
  if(fsal_internal_proxy_fsal_name_2_utf8(p_new_name, &newname) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

#define FSAL_RENAME_IDX_OP_PUTFH_OLD      0
#define FSAL_RENAME_IDX_OP_SAVEFH         1
#define FSAL_RENAME_IDX_OP_PUTFH_NEW      2
#define FSAL_RENAME_IDX_OP_RENAME         3
#define FSAL_RENAME_IDX_OP_GETATTR_NEW    4
#define FSAL_RENAME_IDX_OP_RESTOREFH      5
#define FSAL_RENAME_IDX_OP_GETATTR_OLD    6

  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh_old);
  COMPOUNDV4_ARG_ADD_OP_SAVEFH(argnfs4);
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh_new);
  COMPOUNDV4_ARG_ADD_OP_RENAME(argnfs4, oldname, newname);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap_new);
  COMPOUNDV4_ARG_ADD_OP_RESTOREFH(argnfs4);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap_old);

  resnfs4.resarray.resarray_val[FSAL_RENAME_IDX_OP_GETATTR_NEW].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_res_new;
  resnfs4.resarray.resarray_val[FSAL_RENAME_IDX_OP_GETATTR_NEW].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_RENAME_IDX_OP_GETATTR_NEW].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal_new;
  resnfs4.resarray.resarray_val[FSAL_RENAME_IDX_OP_GETATTR_NEW].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal_new);

  resnfs4.resarray.resarray_val[FSAL_RENAME_IDX_OP_GETATTR_OLD].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_res_old;
  resnfs4.resarray.resarray_val[FSAL_RENAME_IDX_OP_GETATTR_OLD].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_RENAME_IDX_OP_GETATTR_OLD].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal_old;
  resnfs4.resarray.resarray_val[FSAL_RENAME_IDX_OP_GETATTR_OLD].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal_old);

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_rename);
    }

  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_rename);

  /* >> get last parent post op attributes if asked
   * For example : << */

  if(src_dir_attributes)
    {
      if(nfs4_Fattr_To_FSAL_attr(src_dir_attributes,
                                 &resnfs4.resarray.resarray_val
                                 [FSAL_RENAME_IDX_OP_GETATTR_OLD].nfs_resop4_u.
                                 opgetattr.GETATTR4res_u.resok4.obj_attributes) != NFS4_OK)
        {
          FSAL_CLEAR_MASK(src_dir_attributes->asked_attributes);
          FSAL_SET_MASK(src_dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_rename);
        }

    }

  /* >> get new parent post op attributes if asked
   * For example : << */

  if(tgt_dir_attributes)
    {
      if(nfs4_Fattr_To_FSAL_attr(tgt_dir_attributes,
                                 &resnfs4.resarray.resarray_val
                                 [FSAL_RENAME_IDX_OP_GETATTR_NEW].nfs_resop4_u.
                                 opgetattr.GETATTR4res_u.resok4.obj_attributes) != NFS4_OK)
        {
          FSAL_CLEAR_MASK(tgt_dir_attributes->asked_attributes);
          FSAL_SET_MASK(tgt_dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_rename);
        }

    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_rename);

}
