/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_symlinks.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/29 09:39:04 $
 * \version $Revision: 1.15 $
 * \brief   symlinks operations.
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
 * FSAL_readlink:
 * Read the content of a symbolic link.
 *
 * \param linkhandle (input):
 *        Handle of the link to be read.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param p_link_content (output):
 *        Pointer to an fsal path structure where
 *        the link content is to be stored..
 * \param link_attributes (optionnal input/output): 
 *        The post operation attributes of the symlink link.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (linkhandle does not address an existing object)
 *        - ERR_FSAL_INVAL        (linkhandle does not address a symbolic link)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 * */

fsal_status_t PROXYFSAL_readlink(fsal_handle_t * linkhandle,       /* IN */
                                 fsal_op_context_t *context,    /* IN */
                                 fsal_path_t * p_link_content,  /* OUT */
                                 fsal_attrib_list_t * link_attributes   /* [ IN/OUT ] */
    )
{

  int rc;

  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  bitmap4 bitmap;
  uint32_t bitmap_val[2];
  fsal_attrib_list_t attributes;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

#define FSAL_READLINK_NB_OP_ALLOC 4
  nfs_argop4 argoparray[FSAL_READLINK_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_READLINK_NB_OP_ALLOC];
  uint32_t bitmap_res[2];

  fsal_proxy_internal_fattr_t fattr_internal;
  char padpath[FSAL_MAX_PATH_LEN];

  struct timeval timeout = TIMEOUTRPC;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!linkhandle || !p_context || !p_link_content)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readlink);

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  fsal_internal_proxy_setup_fattr(&fattr_internal);
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Readlink" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  /* >> retrieve root handle filehandle here << */
  bitmap.bitmap4_val = bitmap_val;
  bitmap.bitmap4_len = 2;
  fsal_internal_proxy_create_fattr_bitmap(&bitmap);

  if(fsal_internal_proxy_extract_fh(&nfs4fh, linkhandle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readlink);

#define FSAL_READLINK_IDX_OP_PUTFH     0
#define FSAL_READLINK_IDX_OP_READLINK  1
#define FSAL_READLINK_IDX_OP_GETATTR   2

  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_READLINK(argnfs4);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);

  resnfs4.resarray.resarray_val[FSAL_READLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[FSAL_READLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_READLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_READLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  resnfs4.resarray.resarray_val[FSAL_READLINK_IDX_OP_READLINK].nfs_resop4_u.opreadlink.
      READLINK4res_u.resok4.link.utf8string_val = (char *)padpath;
  resnfs4.resarray.resarray_val[FSAL_READLINK_IDX_OP_READLINK].nfs_resop4_u.opreadlink.
      READLINK4res_u.resok4.link.utf8string_len = FSAL_MAX_PATH_LEN;

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_readlink);
    }

  ReleaseTokenFSCall();

  /* >> convert error code and return on error << */
  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_readlink);

  /* >> convert fs output to fsal_path_t
   * for example, if this is a char * (link_content_out) :
   */

  if(fsal_internal_proxy_fsal_utf8_2_path(p_link_content,
                                          &(resnfs4.resarray.resarray_val
                                            [FSAL_READLINK_IDX_OP_READLINK].
                                            nfs_resop4_u.opreadlink.READLINK4res_u.resok4.
                                            link)) == FALSE)

    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readlink);

  /* retrieves object attributes, if asked */
  if(link_attributes)
    {
      /* Use NFSv4 service function to build the FSAL_attr */
      if(nfs4_Fattr_To_FSAL_attr(&attributes,
                                 &resnfs4.resarray.
                                 resarray_val[FSAL_READLINK_IDX_OP_GETATTR].nfs_resop4_u.
                                 opgetattr.GETATTR4res_u.resok4.obj_attributes) != NFS4_OK)
        {
          FSAL_CLEAR_MASK(link_attributes->asked_attributes);
          FSAL_SET_MASK(link_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_readlink);
        }

      memcpy(link_attributes, &attributes, sizeof(attributes));

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readlink);

}

/**
 * FSAL_symlink:
 * Create a symbolic link.
 *
 * \param parent_directory_handle (input):
 *        Handle of the parent directory where the link is to be created.
 * \param p_linkname (input):
 *        Name of the link to be created.
 * \param p_linkcontent (input):
 *        Content of the link to be created.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (ignored input):
 *        Mode of the link to be created.
 *        It has no sense in HPSS nor UNIX filesystems.
 * \param link_handle (output):
 *        Pointer to the handle of the created symlink.
 * \param link_attributes (optionnal input/output): 
 *        Attributes of the newly created symlink.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parent_directory_handle does not address a directory)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t PROXYFSAL_symlink(fsal_handle_t * parent_directory_handle,   /* IN */
                                fsal_name_t * p_linkname,       /* IN */
                                fsal_path_t * p_linkcontent,    /* IN */
                                fsal_op_context_t *context,     /* IN */
                                fsal_accessmode_t accessmode,   /* IN (ignored) */
                                fsal_handle_t * link_handle,       /* OUT */
                                fsal_attrib_list_t * link_attributes    /* [ IN/OUT ] */
    )
{

  int rc;

  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  bitmap4 bitmap;
  uint32_t bitmap_val[2];
  uint32_t bitmap_res[2];
  uint32_t bitmap_mkdir[2];
  uint32_t bitmap_getattr_res[2];
  uint32_t bitmap_conv_val[2];
  fattr4 input_attr;
  bitmap4 convert_bitmap;
  component4 name;
  char nameval[MAXNAMLEN];
  component4 linkname;
  char linknameval[MAXNAMLEN];
  char padfilehandle[FSAL_PROXY_FILEHANDLE_MAX_LEN];

  fsal_proxy_internal_fattr_t fattr_internal;
  fsal_attrib_list_t create_mode_attr;
  fsal_attrib_list_t attributes;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

#define FSAL_SYMLINK_NB_OP_ALLOC 4
#define FSAL_SYMLINK_VAL_BUFFER  1024

  nfs_argop4 argoparray[FSAL_SYMLINK_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_SYMLINK_NB_OP_ALLOC];

  char fattr_val[FSAL_SYMLINK_VAL_BUFFER];
  struct timeval timeout = TIMEOUTRPC;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!parent_directory_handle ||
     !p_context || !link_handle || !p_linkname || !p_linkcontent)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_symlink);

  /* Tests if symlinking is allowed by configuration. */

  if(!global_fs_info.symlink_support)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_symlink);

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Symlink" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  input_attr.attrmask.bitmap4_val = bitmap_val;
  input_attr.attrmask.bitmap4_len = 2;

  input_attr.attr_vals.attrlist4_val = fattr_val;
  input_attr.attr_vals.attrlist4_len = FSAL_SYMLINK_VAL_BUFFER;

  fsal_internal_proxy_setup_fattr(&fattr_internal);

  convert_bitmap.bitmap4_val = bitmap_conv_val;
  convert_bitmap.bitmap4_len = 2;

  memset((char *)&name, 0, sizeof(component4));
  name.utf8string_val = nameval;
  if(fsal_internal_proxy_fsal_name_2_utf8(p_linkname, &name) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_symlink);

  memset((char *)&linkname, 0, sizeof(component4));
  linkname.utf8string_val = linknameval;
  if(fsal_internal_proxy_fsal_path_2_utf8(p_linkcontent, &linkname) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_symlink);

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, parent_directory_handle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_symlink);

  bitmap.bitmap4_val = bitmap_mkdir;
  bitmap.bitmap4_len = 2;
  fsal_internal_proxy_create_fattr_bitmap(&bitmap);

  create_mode_attr.asked_attributes = FSAL_ATTR_MODE;
  create_mode_attr.mode = accessmode;
  fsal_interval_proxy_fsalattr2bitmap4(&create_mode_attr, &convert_bitmap);

  if(nfs4_FSALattr_To_Fattr(NULL,       /* no exportlist required here */
                            &create_mode_attr, &input_attr, NULL,       /* no compound data required here */
                            NULL,       /* No fh here, filehandle is not a settable attribute */
                            &convert_bitmap) == -1)
    Return(ERR_FSAL_INVAL, -1, INDEX_FSAL_symlink);

#define FSAL_SYMLINK_IDX_OP_PUTFH     0
#define FSAL_SYMLINK_IDX_OP_SYMLINK   1
#define FSAL_SYMLINK_IDX_OP_GETFH     2
#define FSAL_SYMLINK_IDX_OP_GETATTR   3
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_SYMLINK(argnfs4, name, linkname, input_attr);
  COMPOUNDV4_ARG_ADD_OP_GETFH(argnfs4);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);

  resnfs4.resarray.resarray_val[FSAL_SYMLINK_IDX_OP_SYMLINK].nfs_resop4_u.opcreate.
      CREATE4res_u.resok4.attrset.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[FSAL_SYMLINK_IDX_OP_SYMLINK].nfs_resop4_u.opcreate.
      CREATE4res_u.resok4.attrset.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_SYMLINK_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
      GETFH4res_u.resok4.object.nfs_fh4_val = (char *)padfilehandle;
  resnfs4.resarray.resarray_val[FSAL_SYMLINK_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
      GETFH4res_u.resok4.object.nfs_fh4_len = FSAL_PROXY_FILEHANDLE_MAX_LEN;

  resnfs4.resarray.resarray_val[FSAL_SYMLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_getattr_res;
  resnfs4.resarray.resarray_val[FSAL_SYMLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_SYMLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_SYMLINK_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, 0, INDEX_FSAL_symlink);
    }

  ReleaseTokenFSCall();

  /* >> convert error code, and return on error << */
  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_symlink);

  /* Use NFSv4 service function to build the FSAL_attr */
  if(nfs4_Fattr_To_FSAL_attr(&attributes,
                             &resnfs4.resarray.resarray_val[FSAL_SYMLINK_IDX_OP_GETATTR].
                             nfs_resop4_u.opgetattr.GETATTR4res_u.resok4.
                             obj_attributes) != NFS4_OK)
    {
      FSAL_CLEAR_MASK(attributes.asked_attributes);
      FSAL_SET_MASK(attributes.asked_attributes, FSAL_ATTR_RDATTR_ERR);

      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_symlink);
    }

  if(link_attributes)
    {
      memcpy(link_attributes, &attributes, sizeof(attributes));
    }

  if(fsal_internal_proxy_create_fh
     (&
      (resnfs4.resarray.resarray_val[FSAL_SYMLINK_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
       GETFH4res_u.resok4.object), FSAL_TYPE_LNK, attributes.fileid,
      link_handle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_symlink);

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_symlink);
}
