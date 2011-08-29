/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_lookup.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.18 $
 * \brief   Lookup operations.
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
 * PROXYFSAL_lookup :
 * Looks up for an object into a directory.
 *
 * Note : if parent handle and filename are NULL,
 *        this retrieves root's handle.
 *
 * \param parent_directory_handle (input)
 *        Handle of the parent directory to search the object in.
 * \param filename (input)
 *        The name of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parent_directory_handle does not address a directory)
 *        - ERR_FSAL_NOENT        (the object designated by p_filename does not exist)
 *        - ERR_FSAL_XDEV         (tried to operate a lookup on a filesystem junction.
 *                                 Use PROXYFSAL_lookupJunction instead)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 *          
 */
extern struct timeval timeout;

fsal_status_t PROXYFSAL_lookup(fsal_handle_t * parent_directory_handle,    /* IN */
                               fsal_name_t * p_filename,        /* IN */
                               fsal_op_context_t * context,      /* IN */
                               fsal_handle_t * object_handle,      /* OUT */
                               fsal_attrib_list_t * object_attributes   /* [ IN/OUT ] */
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
  fsal_attrib_list_t attributes;
  unsigned int index_getfh = 0;
  unsigned int index_getattr = 0;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

#define FSAL_LOOKUP_NB_OP_ALLOC 4
  nfs_argop4 argoparray[FSAL_LOOKUP_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_LOOKUP_NB_OP_ALLOC];
  uint32_t bitmap_res[2];

  fsal_proxy_internal_fattr_t fattr_internal;
  char padfilehandle[FSAL_PROXY_FILEHANDLE_MAX_LEN];

  struct timeval timeout = TIMEOUTRPC;
  /* sanity checks
   * note : object_attributes is optionnal
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  fsal_internal_proxy_setup_fattr(&fattr_internal);
  argnfs4.minorversion = 0;
  argnfs4.argarray.argarray_len = 0;

  /* >> retrieve root handle filehandle here << */
  bitmap.bitmap4_val = bitmap_val;
  bitmap.bitmap4_len = 2;
  fsal_internal_proxy_create_fattr_bitmap(&bitmap);

  if(!parent_directory_handle)
    {

      /* check that p_filename is NULL,
       * else, parent_directory_handle should not
       * be NULL.
       */
      if(p_filename != NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Lookup Root" ; */
      argnfs4.tag.utf8string_val = NULL;
      argnfs4.tag.utf8string_len = 0;

#define FSAL_LOOKUP_IDX_OP_PUTROOTFH      0
#define FSAL_LOOKUP_IDX_OP_GETATTR_ROOT   1
#define FSAL_LOOKUP_IDX_OP_GETFH_ROOT     2
      COMPOUNDV4_ARG_ADD_OP_PUTROOTFH(argnfs4);
      COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);
      COMPOUNDV4_ARG_ADD_OP_GETFH(argnfs4);

      index_getattr = FSAL_LOOKUP_IDX_OP_GETATTR_ROOT;
      index_getfh = FSAL_LOOKUP_IDX_OP_GETFH_ROOT;

      resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETATTR_ROOT].nfs_resop4_u.
          opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_res;
      resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETATTR_ROOT].nfs_resop4_u.
          opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

      resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETATTR_ROOT].nfs_resop4_u.
          opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
          (char *)&fattr_internal;
      resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETATTR_ROOT].nfs_resop4_u.
          opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
          sizeof(fattr_internal);

      resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETFH_ROOT].nfs_resop4_u.opgetfh.
          GETFH4res_u.resok4.object.nfs_fh4_val = (char *)padfilehandle;
      resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETFH_ROOT].nfs_resop4_u.opgetfh.
          GETFH4res_u.resok4.object.nfs_fh4_len = FSAL_PROXY_FILEHANDLE_MAX_LEN;
    }
  else                          /* this is a real lookup(parent, name)  */
    {
      PRINT_HANDLE("PROXYFSAL_lookup parent", parent_directory_handle);

      /* the filename should not be null */
      if(p_filename == NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      /* >> Be careful about junction crossing, symlinks, hardlinks,...
       * You may check the parent type if it's sored into the handle <<
       */

      switch (((proxyfsal_handle_t *)parent_directory_handle)->data.object_type_reminder)
        {
        case FSAL_TYPE_DIR:
          /* OK */
          break;

        case FSAL_TYPE_JUNCTION:
          /* This is a junction */
          Return(ERR_FSAL_XDEV, 0, INDEX_FSAL_lookup);

        case FSAL_TYPE_FILE:
        case FSAL_TYPE_LNK:
        case FSAL_TYPE_XATTR:
          /* not a directory */
          Return(ERR_FSAL_NOTDIR, 0, INDEX_FSAL_lookup);

        default:
          Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_lookup);
        }

      /* >> Call your filesystem lookup function here << */
      if(fsal_internal_proxy_extract_fh(&nfs4fh, parent_directory_handle) == FALSE)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      memset((char *)&name, 0, sizeof(component4));
      name.utf8string_val = nameval;
      if(fsal_internal_proxy_fsal_name_2_utf8(p_filename, &name) == FALSE)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      if(!FSAL_namecmp(p_filename, (fsal_name_t *) & FSAL_DOT))
        {
          /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Lookup current" ; */
          argnfs4.tag.utf8string_val = NULL;
          argnfs4.tag.utf8string_len = 0;

#define FSAL_LOOKUP_IDX_OP_DOT_PUTFH     0
#define FSAL_LOOKUP_IDX_OP_DOT_GETATTR   1
#define FSAL_LOOKUP_IDX_OP_DOT_GETFH     2
          COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
          COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);
          COMPOUNDV4_ARG_ADD_OP_GETFH(argnfs4);

          index_getattr = FSAL_LOOKUP_IDX_OP_DOT_GETATTR;
          index_getfh = FSAL_LOOKUP_IDX_OP_DOT_GETFH;

          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val =
              bitmap_res;
          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
              (char *)&fattr_internal;
          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
              sizeof(fattr_internal);

          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_GETFH].nfs_resop4_u.
              opgetfh.GETFH4res_u.resok4.object.nfs_fh4_val = (char *)padfilehandle;
          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_GETFH].nfs_resop4_u.
              opgetfh.GETFH4res_u.resok4.object.nfs_fh4_len =
              FSAL_PROXY_FILEHANDLE_MAX_LEN;
        }
      else if(!FSAL_namecmp(p_filename, (fsal_name_t *) & FSAL_DOT_DOT))
        {
          /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Lookup parent" ; */
          argnfs4.tag.utf8string_val = NULL;
          argnfs4.tag.utf8string_len = 0;

#define FSAL_LOOKUP_IDX_OP_DOT_DOT_PUTFH     0
#define FSAL_LOOKUP_IDX_OP_DOT_DOT_LOOKUPP   1
#define FSAL_LOOKUP_IDX_OP_DOT_DOT_GETATTR   2
#define FSAL_LOOKUP_IDX_OP_DOT_DOT_GETFH     3
          COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
          COMPOUNDV4_ARG_ADD_OP_LOOKUPP(argnfs4);
          COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);
          COMPOUNDV4_ARG_ADD_OP_GETFH(argnfs4);

          index_getattr = FSAL_LOOKUP_IDX_OP_DOT_DOT_GETATTR;
          index_getfh = FSAL_LOOKUP_IDX_OP_DOT_DOT_GETFH;

          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_DOT_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val =
              bitmap_res;
          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_DOT_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_DOT_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
              (char *)&fattr_internal;
          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_DOT_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
              sizeof(fattr_internal);

          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_DOT_GETFH].nfs_resop4_u.
              opgetfh.GETFH4res_u.resok4.object.nfs_fh4_val = (char *)padfilehandle;
          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_DOT_DOT_GETFH].nfs_resop4_u.
              opgetfh.GETFH4res_u.resok4.object.nfs_fh4_len =
              FSAL_PROXY_FILEHANDLE_MAX_LEN;
        }
      else
        {
          /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Lookup name" ; */
          argnfs4.tag.utf8string_val = NULL;
          argnfs4.tag.utf8string_len = 0;

#define FSAL_LOOKUP_IDX_OP_PUTFH     0
#define FSAL_LOOKUP_IDX_OP_LOOKUP    1
#define FSAL_LOOKUP_IDX_OP_GETATTR   2
#define FSAL_LOOKUP_IDX_OP_GETFH     3
          COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
          COMPOUNDV4_ARG_ADD_OP_LOOKUP(argnfs4, name);
          COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);
          COMPOUNDV4_ARG_ADD_OP_GETFH(argnfs4);

          index_getattr = FSAL_LOOKUP_IDX_OP_GETATTR;
          index_getfh = FSAL_LOOKUP_IDX_OP_GETFH;

          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val =
              bitmap_res;
          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
              (char *)&fattr_internal;
          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETATTR].nfs_resop4_u.
              opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
              sizeof(fattr_internal);

          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
              GETFH4res_u.resok4.object.nfs_fh4_val = (char *)padfilehandle;
          resnfs4.resarray.resarray_val[FSAL_LOOKUP_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
              GETFH4res_u.resok4.object.nfs_fh4_len = FSAL_PROXY_FILEHANDLE_MAX_LEN;
        }
    }

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_lookup);
    }
  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_lookup);

  /* Use NFSv4 service function to build the FSAL_attr */
  if(nfs4_Fattr_To_FSAL_attr(&attributes,
                             &resnfs4.resarray.resarray_val[index_getattr].nfs_resop4_u.
                             opgetattr.GETATTR4res_u.resok4.obj_attributes) != NFS4_OK)
    {
      FSAL_CLEAR_MASK(object_attributes->asked_attributes);
      FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookup);
    }

  if(object_attributes)
    {
      memcpy(object_attributes, &attributes, sizeof(attributes));
    }

  /* Build the handle */
  if(fsal_internal_proxy_create_fh
     (&resnfs4.resarray.resarray_val[index_getfh].nfs_resop4_u.opgetfh.GETFH4res_u.
      resok4.object, attributes.type, attributes.fileid, object_handle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  PRINT_HANDLE("PROXYFSAL_lookup object found", object_handle);

  /* Return attributes if asked */
  if(object_attributes)
    {
      memcpy(object_attributes, &attributes, sizeof(attributes));
    }

  /* lookup complete ! */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);

}

/**
 * PROXYFSAL_lookupJunction :
 * Get the fileset root for a junction.
 *
 * \param p_junction_handle (input)
 *        Handle of the junction to be looked up.
 * \param cred (input)
 *        Authentication context for the operation (user,...).
 * \param p_fsroot_handle (output)
 *        The handle of root directory of the fileset.
 * \param p_fsroot_attributes (optional input/output)
 *        Pointer to the attributes of the root directory
 *        for the fileset.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (p_junction_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 *          
 */
fsal_status_t PROXYFSAL_lookupJunction(fsal_handle_t * p_junction_handle,  /* IN */
                                       fsal_op_context_t * p_context,      /* IN */
                                       fsal_handle_t * p_fsoot_handle,     /* OUT */
                                       fsal_attrib_list_t * p_fsroot_attributes /* [ IN/OUT ] */
    )
{
  /* sanity checks
   * note : p_fsroot_attributes is optionnal
   */
  if(!p_junction_handle || !p_fsoot_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupJunction);

  /* >> you can also check object type if it is in stored in the handle << */

  if(((proxyfsal_handle_t *)p_junction_handle)->data.object_type_reminder != FSAL_TYPE_JUNCTION)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupJunction);

  TakeTokenFSCall();

  /* >> traverse the junction here << */

  ReleaseTokenFSCall();

  /* >> convert the error code and return on error << */

  /* >> set output handle << */

  if(p_fsroot_attributes)
    {

      /* >> fill output attributes if asked << */

    }

  /* lookup complete ! */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupJunction);
}

/**
 * PROXYFSAL_lookupPath :
 * Looks up for an object into the namespace.
 *
 * Note : if path equals "/",
 *        this retrieves root's handle.
 *
 * \param path (input)
 *        The path of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - ERR_FSAL_INVAL        (the path argument is not absolute)
 *        - ERR_FSAL_NOENT        (an element in the path does not exist)
 *        - ERR_FSAL_NOTDIR       (an element in the path is not a directory)
 *        - ERR_FSAL_XDEV         (tried to cross a filesystem junction,
 *                                 whereas is has not been authorized in the server
 *                                 configuration - FSAL::auth_xdev_export parameter)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t PROXYFSAL_lookupPath(fsal_path_t * p_path,        /* IN */
                                   fsal_op_context_t * p_context,  /* IN */
                                   fsal_handle_t * object_handle,  /* OUT */
                                   fsal_attrib_list_t * object_attributes       /* [ IN/OUT ] */
    )
{
  fsal_name_t obj_name = FSAL_NAME_INITIALIZER; /* empty string */
  char *ptr_str;
  proxyfsal_handle_t out_hdl;
  fsal_status_t status;
  int b_is_last = FALSE;        /* is it the last lookup ? */

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
 *  this function may be adapted to most FSALs
 *<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/

  /* sanity checks
   * note : object_attributes is optionnal.
   */

  if(!object_handle || !p_context || !p_path)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupPath);

  /* test whether the path begins with a slash */

  if(p_path->path[0] != '/')
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupPath);

  /* the pointer now points on the next name in the path,
   * skipping slashes.
   */

  ptr_str = p_path->path + 1;
  while(ptr_str[0] == '/')
    ptr_str++;

  /* is the next name empty ? */

  if(ptr_str[0] == '\0')
    b_is_last = TRUE;

  /* retrieves root directory */

  status = PROXYFSAL_lookup(NULL,       /* looking up for root */
                            NULL,       /* empty string to get root handle */
                            p_context,  /* user's credentials */
                            (fsal_handle_t *) &out_hdl,   /* output root handle */
                            /* retrieves attributes if this is the last lookup : */
                            (b_is_last ? object_attributes : NULL));

  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_lookupPath);

  /* exits if this was the last lookup */

  if(b_is_last)
    {
      (*(proxyfsal_handle_t *)object_handle) = out_hdl;
      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);
    }

  /* proceed a step by step lookup */

  while(ptr_str[0])
    {

      proxyfsal_handle_t in_hdl;
      char *dest_ptr;

      /* preparing lookup */

      in_hdl = out_hdl;

      /* compute next name */
      obj_name.len = 0;
      dest_ptr = obj_name.name;
      while(ptr_str[0] != '\0' && ptr_str[0] != '/')
        {
          dest_ptr[0] = ptr_str[0];
          dest_ptr++;
          ptr_str++;
          obj_name.len++;
        }
      /* final null char */
      dest_ptr[0] = '\0';

      /* skip multiple slashes */
      while(ptr_str[0] == '/')
        ptr_str++;

      /* is the next name empty ? */
      if(ptr_str[0] == '\0')
        b_is_last = TRUE;

      /*call to PROXYFSAL_lookup */
      status = PROXYFSAL_lookup((fsal_handle_t *) &in_hdl,        /* parent directory handle */
                                &obj_name,      /* object name */
                                p_context,      /* user's credentials */
                                (fsal_handle_t *) &out_hdl,       /* output root handle */
                                /* retrieves attributes if this is the last lookup : */
                                (b_is_last ? object_attributes : NULL));

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookupPath);

      /* if the target object is a junction, an we allow cross junction lookups,
       * we cross it.
       */
      if(global_fs_info.auth_exportpath_xdev
         && (out_hdl.data.object_type_reminder == FSAL_TYPE_JUNCTION))
        {
          proxyfsal_handle_t tmp_hdl;

          tmp_hdl = out_hdl;

          /*call to PROXYFSAL_lookup */
          status = PROXYFSAL_lookupJunction((fsal_handle_t *) &tmp_hdl,   /* object handle */
                                            p_context,  /* user's credentials */
                                            (fsal_handle_t *) &out_hdl,   /* output root handle */
                                            /* retrieves attributes if this is the last lookup : */
                                            (b_is_last ? object_attributes : NULL));

        }

      /* ptr_str is ok, we are ready for next loop */
    }

  (*(proxyfsal_handle_t *)object_handle) = out_hdl;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);

}
