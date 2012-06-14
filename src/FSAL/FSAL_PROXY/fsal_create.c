/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_create.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:36 $
 * \version $Revision: 1.18 $
 * \brief   Filesystem objects creation functions.
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
#include "nfs_proto_tools.h"
#include "fsal_nfsv4_macros.h"

#ifdef _APPLE
#define strnlen( s, l ) strlen( s )
#endif

/**
 * FSAL_create:
 * Create a regular file.
 *
 * \param parent_directory_handle (input):
 *        Handle of the parent directory where the file is to be created.
 * \param p_filename (input):
 *        Pointer to the name of the file to be created.
 * \param cred (input):
 *        Authentication context for the operation (user, export...).
 * \param accessmode (input):
 *        Mode for the file to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param object_handle (output):
 *        Pointer to the handle of the created file.
 * \param object_attributes (optionnal input/output): 
 *        The postop attributes of the created file.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        Can be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_EXIST, ERR_FSAL_IO, ...
 *            
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */
fsal_status_t PROXYFSAL_create(fsal_handle_t * parent_directory_handle,    /* IN */
                               fsal_name_t * p_filename,        /* IN */
                               fsal_op_context_t *context,      /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               fsal_handle_t * object_handle,      /* OUT */
                               fsal_attrib_list_t * object_attributes   /* [ IN/OUT ] */
    )
{
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  bitmap4 bitmap;
  uint32_t bitmap_res[2];
  uint32_t bitmap_conv_val[2];
  uint32_t bitmap_create[2];
  uint32_t bitmap_getattr_res[2];
  fattr4 input_attr;
  bitmap4 convert_bitmap;
  component4 name;
  char nameval[MAXNAMLEN];
  char padfilehandle[FSAL_PROXY_FILEHANDLE_MAX_LEN];
  fsal_status_t fsal_status;
  proxyfsal_file_t fd;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

#define FSAL_CREATE_NB_OP_ALLOC 4
#define FSAL_CREATE_VAL_BUFFER  1024

  fsal_proxy_internal_fattr_t fattr_internal;
  fsal_attrib_list_t create_mode_attr;
  fsal_attrib_list_t attributes;
  nfs_argop4 argoparray[FSAL_CREATE_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_CREATE_NB_OP_ALLOC];
  struct timeval timeout = TIMEOUTRPC;
  char owner_val[FSAL_PROXY_OWNER_LEN];
  unsigned int owner_len = 0;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!parent_directory_handle || !p_context || !object_handle || !p_filename)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_create);

  PRINT_HANDLE("FSAL_create", parent_directory_handle);

  /* Create the owner */
  snprintf(owner_val, FSAL_PROXY_OWNER_LEN, "GANESHA/PROXY: pid=%u ctx=%p file=%llu",
           getpid(), p_context, (unsigned long long int)p_context->file_counter);
  owner_len = strnlen(owner_val, FSAL_PROXY_OWNER_LEN);
  p_context->file_counter += 1;

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Mkdir" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  fsal_internal_proxy_setup_fattr(&fattr_internal);

  convert_bitmap.bitmap4_val = bitmap_conv_val;
  convert_bitmap.bitmap4_len = 2;

  memset((char *)&name, 0, sizeof(component4));
  name.utf8string_val = nameval;
  name.utf8string_len = sizeof(nameval);
  if(fsal_internal_proxy_fsal_name_2_utf8(p_filename, &name) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_create);

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, parent_directory_handle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_create);

  if(isFullDebug(COMPONENT_FSAL))
    {
      char outstr[1024];

      nfs4_sprint_fhandle(&nfs4fh, outstr);
      LogFullDebug(COMPONENT_FSAL, "FSAL_CREATE: extracted server (as client) parent handle=%s\n",
                   outstr);
    }

  bitmap.bitmap4_val = bitmap_create;
  bitmap.bitmap4_len = 2;
  fsal_internal_proxy_create_fattr_bitmap(&bitmap);

  create_mode_attr.asked_attributes = FSAL_ATTR_MODE;
  create_mode_attr.mode = accessmode;
  fsal_interval_proxy_fsalattr2bitmap4(&create_mode_attr, &convert_bitmap);

  if(nfs4_FSALattr_To_Fattr(NULL,       /* no exportlist required here */
                            &create_mode_attr, &input_attr, NULL,       /* no compound data required here */
                            NULL,       /* No fh here, filehandle is not a settable attribute */
                            &convert_bitmap) == -1)
    Return(ERR_FSAL_INVAL, -1, INDEX_FSAL_create);

#define FSAL_CREATE_IDX_OP_PUTFH       0
#define FSAL_CREATE_IDX_OP_OPEN_CREATE 1
#define FSAL_CREATE_IDX_OP_GETFH       2
#define FSAL_CREATE_IDX_OP_GETATTR     3
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_OPEN_CREATE(argnfs4, name, input_attr, p_context->clientid,
                                    owner_val, owner_len);
  COMPOUNDV4_ARG_ADD_OP_GETFH(argnfs4);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);

  resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_OPEN_CREATE].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.attrset.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_OPEN_CREATE].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.attrset.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
      GETFH4res_u.resok4.object.nfs_fh4_val = (char *)padfilehandle;
  resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
      GETFH4res_u.resok4.object.nfs_fh4_len = FSAL_PROXY_FILEHANDLE_MAX_LEN;

  resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_getattr_res;
  resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  nfs4_Fattr_Free(&input_attr);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_create);
    }

  ReleaseTokenFSCall();

  /* >> convert error code, and return on error << */
  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_create);

  /* Use NFSv4 service function to build the FSAL_attr */
  if(nfs4_Fattr_To_FSAL_attr(&attributes,
                             &resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_GETATTR].
                             nfs_resop4_u.opgetattr.GETATTR4res_u.resok4.
                             obj_attributes) != NFS4_OK)
    {
      FSAL_CLEAR_MASK(attributes.asked_attributes);
      FSAL_SET_MASK(attributes.asked_attributes, FSAL_ATTR_RDATTR_ERR);

      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_create);

    }

  /* Return attributes if asked */
  if(object_attributes)
    {
      memcpy(object_attributes, &attributes, sizeof(attributes));
    }

  if(isFullDebug(COMPONENT_FSAL))
    {
      char outstr[1024];

      nfs4_sprint_fhandle(&nfs4fh, outstr);
      LogFullDebug(COMPONENT_FSAL, "FSAL_CREATE: extracted server (as client) created file handle=%s\n",
           outstr);
    }

  if(fsal_internal_proxy_create_fh
     (&
      (resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
       GETFH4res_u.resok4.object), FSAL_TYPE_FILE, attributes.fileid,
      object_handle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_create);

  /* Keep the information into the file descriptor as well */
  memcpy((char *)&fd.fhandle, (char *)object_handle, sizeof(fd.fhandle));

  fd.openflags = FSAL_O_RDWR;
  fd.current_offset = 0;
  fd.pcontext = p_context;

  /* Keep the returned stateid for later use */
  fd.stateid.seqid =
      resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_OPEN_CREATE].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.stateid.seqid;
  memcpy((char *)fd.stateid.other,
         resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_OPEN_CREATE].nfs_resop4_u.
         opopen.OPEN4res_u.resok4.stateid.other, 12);

  /* See if a OPEN_CONFIRM is required */
  if(resnfs4.resarray.resarray_val[FSAL_CREATE_IDX_OP_OPEN_CREATE].nfs_resop4_u.opopen.
     OPEN4res_u.resok4.rflags & OPEN4_RESULT_CONFIRM)
    {
      fsal_status = FSAL_proxy_open_confirm(&fd);
      if(FSAL_IS_ERROR(fsal_status))
        Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_create);
    }

  /* The created file is still opened, to preserve the correct seqid for later use, we close it */
  fsal_status = FSAL_close((fsal_file_t *) &fd);
  if(FSAL_IS_ERROR(fsal_status))
    Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_create);

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_create);
}                               /* FSAL_create */

/**
 * FSAL_mknode:
 * Create a special object in the filesystem.
 * Not supported in upper layers in this GANESHA's version.
 *
 * \return ERR_FSAL_NOTSUPP.
 */
fsal_status_t PROXYFSAL_mknode(fsal_handle_t * parentdir_handle,   /* IN */
                               fsal_name_t * p_node_name,       /* IN */
                               fsal_op_context_t * p_context,      /* IN */
                               fsal_accessmode_t accessmode,    /* IN */
                               fsal_nodetype_t nodetype,        /* IN */
                               fsal_dev_t * dev,        /* IN */
                               fsal_handle_t * p_object_handle,    /* OUT (handle to the created node) */
                               fsal_attrib_list_t * node_attributes     /* [ IN/OUT ] */
    )
{

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!parentdir_handle || !p_context || !nodetype || !dev || !p_node_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mknode);

  /* Not implemented */
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_mknode);

}
