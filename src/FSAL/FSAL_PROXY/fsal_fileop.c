/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_fileop.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/15 14:26:10 $
 * \version $Revision: 1.11 $
 * \brief   Files operations.
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

#ifdef _APPLE
#define strnlen( s, l ) strlen( s )
#endif

/**
 * FSAL_open_by_name:
 * Open a regular file for reading/writing its data content.
 *
 * \param dirhandle (input):
 *        Handle of the directory that contain the file to be read/modified.
 * \param filename (input):
 *        Name of the file to be read/modified
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param openflags (input):
 *        Flags that indicates behavior for file opening and access.
 *        This is an inclusive OR of the following values
 *        ( such of them are not compatible) : 
 *        - FSAL_O_RDONLY: opening file for reading only.
 *        - FSAL_O_RDWR: opening file for reading and writing.
 *        - FSAL_O_WRONLY: opening file for writting only.
 *        - FSAL_O_APPEND: always write at the end of the file.
 *        - FSAL_O_TRUNC: truncate the file to 0 on opening.
 * \param file_descriptor (output):
 *        The file descriptor to be used for FSAL_read/write operations.
 * \param file_attributes (optionnal input/output):
 *        Post operation attributes.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_ACCESS       (user doesn't have the permissions for opening the file)
 *      - ERR_FSAL_STALE        (filehandle does not address an existing object) 
 *      - ERR_FSAL_INVAL        (filehandle does not address a regular file,
 *                               or open flags are conflicting)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t PROXYFSAL_open_by_name(fsal_handle_t * dirhandle,    /* IN */
                                     fsal_name_t * filename,    /* IN */
                                     fsal_op_context_t *context,        /* IN */
                                     fsal_openflags_t openflags,        /* IN */
                                     fsal_file_t * file_desc,        /* OUT */
                                     fsal_attrib_list_t * file_attributes       /* [ IN/OUT ] */
    )
{
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  bitmap4 bitmap;
  uint32_t bitmap_val[2];
  uint32_t bitmap_res[2];
  uint32_t bitmap_conv_val[2];
  uint32_t bitmap_open[2];
  uint32_t bitmap_getattr_res[2];
  uint32_t share_access;
  component4 name;
  char nameval[MAXNAMLEN];
  fattr4 input_attr;
  bitmap4 convert_bitmap;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;
  proxyfsal_file_t * file_descriptor = (proxyfsal_file_t *)file_desc;

#define FSAL_OPEN_NB_OP_ALLOC 4
#define FSAL_OPEN_VAL_BUFFER  1024

  fsal_proxy_internal_fattr_t fattr_internal;
  fsal_attrib_list_t attributes;
  nfs_argop4 argoparray[FSAL_OPEN_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_OPEN_NB_OP_ALLOC];
  char fattr_val[FSAL_OPEN_VAL_BUFFER];
  char padfilehandle[FSAL_PROXY_FILEHANDLE_MAX_LEN];
  fsal_status_t fsal_status;
  struct timeval timeout = TIMEOUTRPC;
  char owner_val[FSAL_PROXY_OWNER_LEN];
  unsigned int owner_len = 0;

  /* sanity checks.
   * note : file_attributes is optional.
   */
  if(!dirhandle || !filename || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_name);

  PRINT_HANDLE("FSAL_open", dirhandle);

  if(((proxyfsal_handle_t *)dirhandle)->data.object_type_reminder != FSAL_TYPE_DIR)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open_by_name);
    }

  /* Create the owner */
  snprintf(owner_val, FSAL_PROXY_OWNER_LEN, "GANESHA/PROXY: pid=%u ctx=%p file=%llu",
           getpid(), p_context, (unsigned long long int)p_context->file_counter);
  owner_len = strnlen(owner_val, FSAL_PROXY_OWNER_LEN);
  p_context->file_counter += 1;

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Open By Name" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  input_attr.attrmask.bitmap4_val = bitmap_val;
  input_attr.attrmask.bitmap4_len = 2;

  input_attr.attr_vals.attrlist4_val = fattr_val;
  input_attr.attr_vals.attrlist4_len = FSAL_OPEN_VAL_BUFFER;

  fsal_internal_proxy_setup_fattr(&fattr_internal);

  memset((char *)&name, 0, sizeof(component4));
  name.utf8string_val = nameval;
  if(fsal_internal_proxy_fsal_name_2_utf8(filename, &name) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_name);

  convert_bitmap.bitmap4_val = bitmap_conv_val;
  convert_bitmap.bitmap4_len = 2;

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, dirhandle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_name);

  bitmap.bitmap4_val = bitmap_open;
  bitmap.bitmap4_len = 2;

  fsal_internal_proxy_create_fattr_bitmap(&bitmap);

  share_access = 0;
  if((openflags & FSAL_O_RDWR) == FSAL_O_RDWR)
    share_access |= OPEN4_SHARE_ACCESS_BOTH;

  if((openflags & FSAL_O_RDONLY) == FSAL_O_RDONLY)
    share_access |= OPEN4_SHARE_ACCESS_READ;

  if(((openflags & FSAL_O_WRONLY) == FSAL_O_WRONLY) ||
     ((openflags & FSAL_O_APPEND) == FSAL_O_APPEND))
    share_access |= OPEN4_SHARE_ACCESS_WRITE;

  /* >> you can check if this is a file if the information
   * is stored into the handle << */
#define FSAL_OPEN_IDX_OP_PUTFH         0
#define FSAL_OPEN_IDX_OP_OPEN_NOCREATE 1
#define FSAL_OPEN_IDX_OP_GETFH         2
#define FSAL_OPEN_IDX_OP_GETATTR       3
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_OPEN_NOCREATE(argnfs4, file_descriptor->stateid.seqid,
                                      p_context->clientid, share_access, name, owner_val,
                                      owner_len);
  COMPOUNDV4_ARG_ADD_OP_GETFH(argnfs4);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);

  resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_OPEN_NOCREATE].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.attrset.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_OPEN_NOCREATE].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.attrset.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_getattr_res;
  resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_GETFH].nfs_resop4_u.opgetfh.GETFH4res_u.
      resok4.object.nfs_fh4_val = (char *)padfilehandle;
  resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_GETFH].nfs_resop4_u.opgetfh.GETFH4res_u.
      resok4.object.nfs_fh4_len = FSAL_PROXY_FILEHANDLE_MAX_LEN;

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();
      Return(ERR_FSAL_IO, rc, INDEX_FSAL_open_by_name);
    }

  ReleaseTokenFSCall();

  /* >> convert error code, and return on error << */
  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_open_by_name);

  /* Use NFSv4 service function to build the FSAL_attr */
  if(nfs4_Fattr_To_FSAL_attr(&attributes,
                             &resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_GETATTR].
                             nfs_resop4_u.opgetattr.GETATTR4res_u.resok4.
                             obj_attributes) != NFS4_OK)
    {
      FSAL_CLEAR_MASK(file_attributes->asked_attributes);
      FSAL_SET_MASK(file_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open_by_name);
    }

  if(file_attributes)
    {
      memcpy(file_attributes, &attributes, sizeof(attributes));
    }

  /* >> fill output struct << */
  /* Build the handle */
  if(fsal_internal_proxy_create_fh
     (&resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
      GETFH4res_u.resok4.object, FSAL_TYPE_FILE, attributes.fileid,
      (fsal_handle_t *) &file_descriptor->fhandle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_name);

  file_descriptor->openflags = openflags;
  file_descriptor->current_offset = 0;
  file_descriptor->pcontext = p_context;

  /* Keep the returned stateid for later use */
  file_descriptor->stateid.seqid =
      resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_OPEN_NOCREATE].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.stateid.seqid;
  memcpy((char *)file_descriptor->stateid.other,
         resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_OPEN_NOCREATE].nfs_resop4_u.
         opopen.OPEN4res_u.resok4.stateid.other, 12);

  /* See if a OPEN_CONFIRM is required */
  if(resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_OPEN_NOCREATE].nfs_resop4_u.opopen.
     OPEN4res_u.resok4.rflags & OPEN4_RESULT_CONFIRM)
    {
      fsal_status = FSAL_proxy_open_confirm(file_descriptor);
      if(FSAL_IS_ERROR(fsal_status))
        Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_open_by_name);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_open_by_name);
}                               /* FSAL_open_by_name */

/**
 * FSAL_open_stateless:
 * Open a regular file for reading/writing its data content, in a stateless way.
 *
 * \param filehandle (input):
 *        Handle of the directory that contain the file to be read/modified.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param file_descriptor (output):
 *        The file descriptor to be used for FSAL_read/write operations.
 * \param file_attributes (optionnal input/output):
 *        Post operation attributes.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_ACCESS       (user doesn't have the permissions for opening the file)
 *      - ERR_FSAL_STALE        (filehandle does not address an existing object) 
 *      - ERR_FSAL_INVAL        (filehandle does not address a regular file,
 *                               or open flags are conflicting)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */

static fsal_status_t PROXYFSAL_open_stateless(fsal_handle_t * filehandle,  /* IN */
                                              fsal_op_context_t *context,       /* IN */
                                              fsal_openflags_t openflags,       /* IN */
                                              fsal_file_t * file_desc,       /* OUT */
                                              fsal_attrib_list_t * file_attributes      /* [ IN/OUT ] */
    )
{
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  bitmap4 bitmap;
  uint32_t bitmap_val[2];
  uint32_t bitmap_open[2];
  uint32_t bitmap_getattr_res[2];
  uint32_t share_access;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;
  proxyfsal_file_t * file_descriptor = (proxyfsal_file_t *)file_desc;

#define FSAL_OPEN_STATELESS_NB_OP_ALLOC 2
#define FSAL_OPEN_STATELESS_VAL_BUFFER  1024

  fsal_proxy_internal_fattr_t fattr_internal;
  fsal_attrib_list_t attributes;
  nfs_argop4 argoparray[FSAL_OPEN_STATELESS_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_OPEN_STATELESS_NB_OP_ALLOC];
  char fattr_val[FSAL_OPEN_STATELESS_VAL_BUFFER];
  struct timeval timeout = TIMEOUTRPC;
  u_int32_t owner = time(NULL);

  /* sanity checks.
   * note : file_attributes is optional.
   */
  if(!filehandle || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open);

  PRINT_HANDLE("FSAL_open_stateless", filehandle);

  if(((proxyfsal_handle_t *)filehandle)->data.object_type_reminder != FSAL_TYPE_FILE)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open);
    }

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Open By Name" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  fsal_internal_proxy_setup_fattr(&fattr_internal);

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, filehandle) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open);

  bitmap.bitmap4_val = bitmap_open;
  bitmap.bitmap4_len = 2;
  fsal_internal_proxy_create_fattr_bitmap(&bitmap);

  share_access = 0;
  if((openflags & FSAL_O_RDWR) == FSAL_O_RDWR)
    share_access |= OPEN4_SHARE_ACCESS_BOTH;

  if((openflags & FSAL_O_RDONLY) == FSAL_O_RDONLY)
    share_access |= OPEN4_SHARE_ACCESS_READ;

  if(((openflags & FSAL_O_WRONLY) == FSAL_O_WRONLY) ||
     ((openflags & FSAL_O_APPEND) == FSAL_O_APPEND))
    share_access |= OPEN4_SHARE_ACCESS_WRITE;

  /* >> you can check if this is a file if the information
   * is stored into the handle << */
#define FSAL_OPEN_STATELESS_IDX_OP_PUTFH         0
#define FSAL_OPEN_STATELESS_IDX_OP_GETATTR       1
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);

  resnfs4.resarray.resarray_val[FSAL_OPEN_STATELESS_IDX_OP_GETATTR].nfs_resop4_u.
      opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val =
      bitmap_getattr_res;
  resnfs4.resarray.resarray_val[FSAL_OPEN_STATELESS_IDX_OP_GETATTR].nfs_resop4_u.
      opgetattr.GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_OPEN_STATELESS_IDX_OP_GETATTR].nfs_resop4_u.
      opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_OPEN_STATELESS_IDX_OP_GETATTR].nfs_resop4_u.
      opgetattr.GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();
      Return(ERR_FSAL_IO, rc, INDEX_FSAL_open);
    }

  ReleaseTokenFSCall();

  /* >> convert error code, and return on error << */
  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_open);

  /* Use NFSv4 service function to build the FSAL_attr */
  if(file_attributes)
    {
      if(nfs4_Fattr_To_FSAL_attr(&attributes,
                                 &resnfs4.resarray.resarray_val
                                 [FSAL_OPEN_STATELESS_IDX_OP_GETATTR].
                                 nfs_resop4_u.opgetattr.GETATTR4res_u.resok4.
                                 obj_attributes) != NFS4_OK)
        {
          FSAL_CLEAR_MASK(file_attributes->asked_attributes);
          FSAL_SET_MASK(file_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open);
        }

      memcpy(file_attributes, &attributes, sizeof(attributes));
    }

  /* >> fill output struct << */
  memcpy((char *)&file_descriptor->fhandle, filehandle, sizeof(proxyfsal_handle_t));
  file_descriptor->openflags = openflags;
  file_descriptor->current_offset = 0;
  file_descriptor->pcontext = p_context;

  /* Keep the returned stateid for later use */
  file_descriptor->stateid.seqid = 0;
  memset((char *)file_descriptor->stateid.other, 0, 12);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_open);
}                               /* FSAL_open_stateless */

/**
 * FSAL_open:
 * Open a regular file for reading/writing its data content.
 *
 * \param filehandle (input):
 *        Handle of the file to be read/modified.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param openflags (input):
 *        Flags that indicates behavior for file opening and access.
 *        This is an inclusive OR of the following values
 *        ( such of them are not compatible) :
 *        - FSAL_O_RDONLY: opening file for reading only.
 *        - FSAL_O_RDWR: opening file for reading and writing.
 *        - FSAL_O_WRONLY: opening file for writting only.
 *        - FSAL_O_APPEND: always write at the end of the file.
 *        - FSAL_O_TRUNC: truncate the file to 0 on opening.
 * \param file_descriptor (output):
 *        The file descriptor to be used for FSAL_read/write operations.
 * \param file_attributes (optionnal input/output):
 *        Post operation attributes.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_ACCESS       (user doesn't have the permissions for opening the file)
 *      - ERR_FSAL_STALE        (filehandle does not address an existing object)
 *      - ERR_FSAL_INVAL        (filehandle does not address a regular file,
 *                               or open flags are conflicting)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */

fsal_status_t PROXYFSAL_open(fsal_handle_t * filehandle,   /* IN */
                             fsal_op_context_t * p_context,        /* IN */
                             fsal_openflags_t openflags,        /* IN */
                             fsal_file_t * file_descriptor,        /* OUT */
                             fsal_attrib_list_t * file_attributes       /* [ IN/OUT ] */
    )
{
  fsal_status_t fsal_status;

  /* sanity checks.
   * note : file_attributes is optional.
   */
  if(!filehandle || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open);

  /* >> you can check if this is a file if the information
   * is stored into the handle << */

  if(((proxyfsal_handle_t *)filehandle)->data.object_type_reminder != FSAL_TYPE_FILE)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open);
    }

  fsal_status =
      PROXYFSAL_open_stateless(filehandle, p_context, openflags, file_descriptor,
                               file_attributes);
  Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_open);
}

/**
 * FSAL_read:
 * Perform a read operation on an opened file.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param seek_descriptor (optional input):
 *        Specifies the position where data is to be read.
 *        If not specified, data will be read at the current position.
 * \param buffer_size (input):
 *        Amount (in bytes) of data to be read.
 * \param buffer (output):
 *        Address where the read data is to be stored in memory.
 * \param read_amount (output):
 *        Pointer to the amount of data (in bytes) that have been read
 *        during this call.
 * \param end_of_file (output):
 *        Pointer to a boolean that indicates whether the end of file
 *        has been reached during this call.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_INVAL        (invalid parameter)
 *      - ERR_FSAL_NOT_OPENED   (tried to read in a non-opened proxyfsal_file_t)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t PROXYFSAL_read(fsal_file_t * file_desc,        /* IN */
                             fsal_seek_t * seek_descriptor,     /* IN */
                             fsal_size_t buffer_size,   /* IN */
                             caddr_t buffer,    /* OUT */
                             fsal_size_t * read_amount, /* OUT */
                             fsal_boolean_t * end_of_file       /* OUT */
    )
{
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  fsal_off_t offset;
  struct timeval timeout = TIMEOUTRPC;
  proxyfsal_file_t * file_descriptor = (proxyfsal_file_t *)file_desc;

#define FSAL_READ_NB_OP_ALLOC 2

  nfs_argop4 argoparray[FSAL_READ_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_READ_NB_OP_ALLOC];

  /* sanity checks. */

  if(!file_descriptor || !buffer || !read_amount || !end_of_file)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_read);

  if(seek_descriptor == NULL)
    offset = file_descriptor->current_offset;
  else
    {
      switch (seek_descriptor->whence)
        {
        case FSAL_SEEK_SET:
          offset = seek_descriptor->offset;
          break;

        case FSAL_SEEK_CUR:
          offset = seek_descriptor->offset + file_descriptor->current_offset;
          break;

        case FSAL_SEEK_END:
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_read);
          break;
        }
    }

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Read" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, (fsal_handle_t *) &(file_descriptor->fhandle)) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_read);

#define FSAL_READ_IDX_OP_PUTFH   0
#define FSAL_READ_IDX_OP_READ    1

  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_READ(argnfs4, &(file_descriptor->stateid), offset, buffer_size);

  resnfs4.resarray.resarray_val[FSAL_READ_IDX_OP_READ].nfs_resop4_u.opread.READ4res_u.
      resok4.data.data_val = buffer;

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(file_descriptor->pcontext, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_read);
    }

  ReleaseTokenFSCall();

  /* >> convert error code, and return on error << */
  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_read);

  /* >> dont forget setting output vars : read_amount, end_of_file << */
  *end_of_file =
      resnfs4.resarray.resarray_val[FSAL_READ_IDX_OP_READ].nfs_resop4_u.opread.READ4res_u.
      resok4.eof;
  *read_amount =
      resnfs4.resarray.resarray_val[FSAL_READ_IDX_OP_READ].nfs_resop4_u.opread.READ4res_u.
      resok4.data.data_len;

  /* update the offset within the fsal_fd_t */
  file_descriptor->current_offset +=
      resnfs4.resarray.resarray_val[FSAL_READ_IDX_OP_READ].nfs_resop4_u.opread.READ4res_u.
      resok4.data.data_len;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_read);

}                               /* FSAL_read */

/**
 * FSAL_write:
 * Perform a write operation on an opened file.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param seek_descriptor (optional input):
 *        Specifies the position where data is to be written.
 *        If not specified, data will be written at the current position.
 * \param buffer_size (input):
 *        Amount (in bytes) of data to be written.
 * \param buffer (input):
 *        Address in memory of the data to write to file.
 * \param write_amount (output):
 *        Pointer to the amount of data (in bytes) that have been written
 *        during this call.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_INVAL        (invalid parameter)
 *      - ERR_FSAL_NOT_OPENED   (tried to write in a non-opened proxyfsal_file_t)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ERR_FSAL_NOSPC, ERR_FSAL_DQUOT...
 */
fsal_status_t PROXYFSAL_write(fsal_file_t * file_desc,       /* IN */
                              fsal_seek_t * seek_descriptor,    /* IN */
                              fsal_size_t buffer_size,  /* IN */
                              caddr_t buffer,   /* IN */
                              fsal_size_t * write_amount        /* OUT */
    )
{
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  proxyfsal_file_t * file_descriptor = (proxyfsal_file_t *)file_desc;

  fsal_off_t offset;

  struct timeval timeout = TIMEOUTRPC;

#define FSAL_WRITE_NB_OP_ALLOC 2

  nfs_argop4 argoparray[FSAL_WRITE_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_WRITE_NB_OP_ALLOC];

  /* sanity checks. */
  if(!file_descriptor || !buffer || !write_amount)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_write);

  if(seek_descriptor == NULL)
    offset = file_descriptor->current_offset;
  else
    {
      switch (seek_descriptor->whence)
        {
        case FSAL_SEEK_SET:
          offset = seek_descriptor->offset;
          break;

        case FSAL_SEEK_CUR:
          offset = seek_descriptor->offset + file_descriptor->current_offset;
          break;

        case FSAL_SEEK_END:
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_write);
          break;
        }
    }

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Write" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, (fsal_handle_t *) &(file_descriptor->fhandle)) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_write);

#define FSAL_READ_IDX_OP_PUTFH    0
#define FSAL_READ_IDX_OP_WRITE    1

  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_WRITE(argnfs4, &(file_descriptor->stateid), offset, buffer,
                              buffer_size);

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(file_descriptor->pcontext, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_write);
    }

  ReleaseTokenFSCall();

  /* >> convert error code, and return on error << */
  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_write);

  /* Set write_amount */
  *write_amount =
      (fsal_size_t) resnfs4.resarray.resarray_val[FSAL_READ_IDX_OP_WRITE].nfs_resop4_u.
      opwrite.WRITE4res_u.resok4.count;

  /* update the offset within the fsal_fd_t */
  file_descriptor->current_offset +=
      resnfs4.resarray.resarray_val[FSAL_READ_IDX_OP_WRITE].nfs_resop4_u.opwrite.
      WRITE4res_u.resok4.count;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_write);
}                               /* FSAL_write */

/**
 * FSAL_close:
 * Free the resources allocated by the FSAL_open call.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 *
 * \return Major error codes:
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */

fsal_status_t PROXYFSAL_close(fsal_file_t * file_desc        /* IN */
    )
{
#define FSAL_CLOSE_NB_OP_ALLOC 2
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_argop4 argoparray[FSAL_CLOSE_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_CLOSE_NB_OP_ALLOC];
  struct timeval timeout = TIMEOUTRPC;
  char All_Zero[] = "\0\0\0\0\0\0\0\0\0\0\0\0"; /* 12 times \0 */
  nfs_fh4 nfs4fh;
  proxyfsal_file_t * file_descriptor = (proxyfsal_file_t *)file_desc;

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Open By Name" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  /* sanity checks. */
  if(!file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_close);

  /* Check if this was a "stateless" open, then nothing is to be done at close */
  if(!memcmp(file_descriptor->stateid.other, All_Zero, 12))
   {
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_close);
   }

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, (fsal_handle_t *)&(file_descriptor->fhandle)) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_close);

#define FSAL_CLOSE_IDX_OP_PUTFH 0
#define FSAL_CLOSE_IDX_OP_CLOSE 1
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_CLOSE(argnfs4, file_descriptor->stateid);

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(file_descriptor->pcontext, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_close);
    }

  ReleaseTokenFSCall();

  /* >> convert error code, and return on error << */
  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_close);

  /* Update fd structure */
  file_descriptor->stateid.seqid +=  1;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_close);
}                               /* FSAL_close */

/**
 * FSAL_close_by_fileid:
 * Free the resources allocated by the FSAL_open_by_fileid call.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 *
 * \return Major error codes:
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */

fsal_status_t PROXYFSAL_close_by_fileid(fsal_file_t * file_desc /* IN */ ,
                                        fsal_u64_t fileid)
#ifndef _USE_PROXY
{
  return ERR_FSAL_NOTSUPP;
}
#else
{
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh_hldir;
  component4 name;
  char nameval[MAXNAMLEN];
  char filename[MAXNAMLEN];
  fsal_status_t fsal_status;
  struct timeval timeout = TIMEOUTRPC;
  proxyfsal_file_t * file_descriptor = (proxyfsal_file_t *)file_desc;

#define FSAL_CLOSE_BY_FILEID_NB_OP 4

  nfs_argop4 argoparray[FSAL_CLOSE_BY_FILEID_NB_OP];
  nfs_resop4 resoparray[FSAL_CLOSE_BY_FILEID_NB_OP];

  /* sanity checks. */
  if(!file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_close_by_fileid);

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Write" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  /* Get NFSv4 File handle */

  if(fsal_internal_proxy_extract_fh
     (&nfs4fh_hldir, (fsal_handle_t *) &(file_descriptor->pcontext->openfh_wd_handle)) == FALSE)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_close_by_fileid);

  snprintf(filename, MAXNAMLEN, ".ganesha.open_by_fid.%llu", fileid);
  name.utf8string_val = nameval;

  if(str2utf8(filename, &name) == -1)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_close_by_fileid);

#define FSAL_CLOSE_BY_FILEID_IDX_OP_PUTFH    0
#define FSAL_CLOSE_BY_FILEID_IDX_OP_REMOVE   1

  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh_hldir);
  COMPOUNDV4_ARG_ADD_OP_REMOVE(argnfs4, name);

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(file_descriptor->pcontext, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_close_by_fileid);
    }

  ReleaseTokenFSCall();

  fsal_status = FSAL_close((fsal_file_t *)file_descriptor);

  Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_close_by_fileid);
}
#endif

/**
 * FSAL_open_by_fileid:
 * Open a regular file for reading/writing its data content.
 *
 * \param dirhandle (input):
 *        Handle of the file  to be opened 
 * \param fileid (input):
 *        file id for the file to be opened
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param openflags (input):
 *        Flags that indicates behavior for file opening and access.
 *        This is an inclusive OR of the following values
 *        ( such of them are not compatible) : 
 *        - FSAL_O_RDONLY: opening file for reading only.
 *        - FSAL_O_RDWR: opening file for reading and writing.
 *        - FSAL_O_WRONLY: opening file for writting only.
 *        - FSAL_O_APPEND: always write at the end of the file.
 *        - FSAL_O_TRUNC: truncate the file to 0 on opening.
 * \param file_descriptor (output):
 *        The file descriptor to be used for FSAL_read/write operations.
 * \param file_attributes (optionnal input/output):
 *        Post operation attributes.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_ACCESS       (user doesn't have the permissions for opening the file)
 *      - ERR_FSAL_STALE        (filehandle does not address an existing object) 
 *      - ERR_FSAL_INVAL        (filehandle does not address a regular file,
 *                               or open flags are conflicting)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t PROXYFSAL_open_by_fileid(fsal_handle_t * filehandle, /* IN */
                                       fsal_u64_t fileid,       /* IN */
                                       fsal_op_context_t *context,      /* IN */
                                       fsal_openflags_t openflags,      /* IN */
                                       fsal_file_t * file_desc,      /* OUT */
                                       fsal_attrib_list_t *
                                       file_attributes /* [ IN/OUT ] */ )
#ifndef _USE_PROXY
{
  return ERR_FSAL_NOTSUPP;
}
#else
{
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  nfs_fh4 nfs4fh;
  nfs_fh4 nfs4fh_hldir;
  bitmap4 bitmap;
  uint32_t bitmap_val[2];
  uint32_t bitmap_res[2];
  uint32_t bitmap_conv_val[2];
  uint32_t bitmap_open[2];
  uint32_t bitmap_getattr_res[2];
  uint32_t share_access;
  component4 name;
  char nameval[MAXNAMLEN];
  char filename[MAXNAMLEN];
  fattr4 input_attr;
  bitmap4 convert_bitmap;
  fsal_status_t fsal_status;
  proxyfsal_file_t * file_descriptor = (proxyfsal_file_t *)file_desc;
  proxyfsal_op_context_t * p_context = (proxyfsal_op_context_t *)context;

#define FSAL_OPEN_BY_FILEID_NB_OP_ALLOC 7
#define FSAL_OPEN_VAL_BUFFER  1024

  fsal_proxy_internal_fattr_t fattr_internal;
  fsal_attrib_list_t attributes;
  nfs_argop4 argoparray[FSAL_OPEN_BY_FILEID_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_OPEN_BY_FILEID_NB_OP_ALLOC];
  char fattr_val[FSAL_OPEN_VAL_BUFFER];
  char padfilehandle[FSAL_PROXY_FILEHANDLE_MAX_LEN];
  struct timeval timeout = TIMEOUTRPC;
  char owner_val[FSAL_PROXY_OWNER_LEN];
  unsigned int owner_len = 0;

  /* sanity checks.
   * note : file_attributes is optional.
   */
  if(!filehandle || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_fileid);

  PRINT_HANDLE("FSAL_open_by_fileid", filehandle);

  if(((proxyfsal_handle_t *)filehandle)->data.object_type_reminder != FSAL_TYPE_FILE)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open_by_fileid);
    }

  /* Create the owner */
  snprintf(owner_val, FSAL_PROXY_OWNER_LEN, "GANESHA/PROXY: pid=%u ctx=%p file=%llu",
           getpid(), p_context, (unsigned long long int)((proxyfsal_op_context_t *)p_context)->file_counter);
  owner_len = strnlen(owner_val, FSAL_PROXY_OWNER_LEN);
  ((proxyfsal_op_context_t *)p_context)->file_counter += 1;

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Open By Name" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  input_attr.attrmask.bitmap4_val = bitmap_val;
  input_attr.attrmask.bitmap4_len = 2;

  input_attr.attr_vals.attrlist4_val = fattr_val;
  input_attr.attr_vals.attrlist4_len = FSAL_OPEN_VAL_BUFFER;

  fsal_internal_proxy_setup_fattr(&fattr_internal);

  snprintf(filename, MAXNAMLEN, ".ganesha.open_by_fid.%llu", fileid);
  name.utf8string_val = nameval;

  if(str2utf8(filename, &name) == -1)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_fileid);

  convert_bitmap.bitmap4_val = bitmap_conv_val;
  convert_bitmap.bitmap4_len = 2;

  /* Get NFSv4 File handle */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, filehandle) == FALSE)
    {
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_fileid);
    }

  if(fsal_internal_proxy_extract_fh(&nfs4fh_hldir, (fsal_handle_t *) &(p_context->openfh_wd_handle)) ==
     FALSE)
    {
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_fileid);
    }
  bitmap.bitmap4_val = bitmap_open;
  bitmap.bitmap4_len = 2;
  fsal_internal_proxy_create_fattr_bitmap(&bitmap);

  share_access = 0;
  if((openflags & FSAL_O_RDWR) == FSAL_O_RDWR)
    share_access |= OPEN4_SHARE_ACCESS_BOTH;

  if((openflags & FSAL_O_RDONLY) == FSAL_O_RDONLY)
    share_access |= OPEN4_SHARE_ACCESS_READ;

  if(((openflags & FSAL_O_WRONLY) == FSAL_O_WRONLY) ||
     ((openflags & FSAL_O_APPEND) == FSAL_O_APPEND))
    share_access |= OPEN4_SHARE_ACCESS_WRITE;

  /* >> you can check if this is a file if the information
   * is stored into the handle << */
#define FSAL_OPEN_BYFID_IDX_OP_PUTFH         0
#define FSAL_OPEN_BYFID_IDX_OP_SAVEFH        1
#define FSAL_OPEN_BYFID_IDX_OP_PUTFH_HLDIR   2
#define FSAL_OPEN_BYFID_IDX_OP_LINK          3
#define FSAL_OPEN_BYFID_IDX_OP_OPEN_NOCREATE 4
#define FSAL_OPEN_BYFID_IDX_OP_GETFH         5
#define FSAL_OPEN_BYFID_IDX_OP_GETATTR       6
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_SAVEFH(argnfs4);
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh_hldir);
  COMPOUNDV4_ARG_ADD_OP_LINK(argnfs4, name);
  COMPOUNDV4_ARG_ADD_OP_OPEN_NOCREATE(argnfs4, file_descriptor->stateid.seqid,
                                      p_context->clientid, share_access, name, owner_val,
                                      owner_len);
  COMPOUNDV4_ARG_ADD_OP_GETFH(argnfs4);
  COMPOUNDV4_ARG_ADD_OP_GETATTR(argnfs4, bitmap);

  resnfs4.resarray.resarray_val[FSAL_OPEN_BYFID_IDX_OP_OPEN_NOCREATE].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.attrset.bitmap4_val = bitmap_res;
  resnfs4.resarray.resarray_val[FSAL_OPEN_BYFID_IDX_OP_OPEN_NOCREATE].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.attrset.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_OPEN_BYFID_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val = bitmap_getattr_res;
  resnfs4.resarray.resarray_val[FSAL_OPEN_BYFID_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_len = 2;

  resnfs4.resarray.resarray_val[FSAL_OPEN_BYFID_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val =
      (char *)&fattr_internal;
  resnfs4.resarray.resarray_val[FSAL_OPEN_BYFID_IDX_OP_GETATTR].nfs_resop4_u.opgetattr.
      GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_len =
      sizeof(fattr_internal);

  resnfs4.resarray.resarray_val[FSAL_OPEN_BYFID_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
      GETFH4res_u.resok4.object.nfs_fh4_val = (char *)padfilehandle;
  resnfs4.resarray.resarray_val[FSAL_OPEN_BYFID_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
      GETFH4res_u.resok4.object.nfs_fh4_len = FSAL_PROXY_FILEHANDLE_MAX_LEN;

  TakeTokenFSCall();

  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_open_by_fileid);
    }

  ReleaseTokenFSCall();

  /* >> convert error code, and return on error << */
  if(resnfs4.status != NFS4_OK)
    {
      return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_open_by_fileid);
    }

  /* Use NFSv4 service function to build the FSAL_attr */
  if(nfs4_Fattr_To_FSAL_attr(&attributes,
                             &resnfs4.resarray.
                             resarray_val[FSAL_OPEN_BYFID_IDX_OP_GETATTR].nfs_resop4_u.
                             opgetattr.GETATTR4res_u.resok4.obj_attributes) != NFS4_OK)
    {
      FSAL_CLEAR_MASK(file_attributes->asked_attributes);
      FSAL_SET_MASK(file_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);

      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open_by_fileid);
    }

  if(file_attributes)
    {
      memcpy(file_attributes, &attributes, sizeof(attributes));
    }

  /* Keep the returned stateid for later use */
  file_descriptor->stateid.seqid =
      resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_OPEN_NOCREATE].nfs_resop4_u.opopen.
      OPEN4res_u.resok4.stateid.seqid;
  memcpy((char *)file_descriptor->stateid.other,
         resnfs4.resarray.resarray_val[FSAL_OPEN_IDX_OP_OPEN_NOCREATE].nfs_resop4_u.
         opopen.OPEN4res_u.resok4.stateid.other, 12);

  /* >> fill output struct << */
  /* Build the handle */
  if(fsal_internal_proxy_create_fh
     (&resnfs4.resarray.resarray_val[FSAL_OPEN_BYFID_IDX_OP_GETFH].nfs_resop4_u.opgetfh.
      GETFH4res_u.resok4.object, FSAL_TYPE_FILE, attributes.fileid,
      (fsal_handle_t *) &file_descriptor->fhandle) == FALSE)
    {
      Mem_Free((char *)name.utf8string_val);
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_fileid);
    }

  file_descriptor->openflags = openflags;
  file_descriptor->current_offset = 0;
  file_descriptor->pcontext = p_context;

  /* See if a OPEN_CONFIRM is required */
  if(resnfs4.resarray.resarray_val[FSAL_OPEN_BYFID_IDX_OP_OPEN_NOCREATE].nfs_resop4_u.
     opopen.OPEN4res_u.resok4.rflags & OPEN4_RESULT_CONFIRM)
    {
      fsal_status = FSAL_proxy_open_confirm(file_descriptor);
      if(FSAL_IS_ERROR(fsal_status))
        Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_open);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_open_by_fileid);

}                               /* FSAL_open_by_fileid */
#endif

unsigned int PROXYFSAL_GetFileno(fsal_file_t * pfile)
{
  unsigned int intpfile;

  memcpy((char *)&intpfile, pfile, sizeof(unsigned int));
  return intpfile;
}


/**
 * FSAL_sync:
 * This function is used for processing stable writes and COMMIT requests.
 * Calling this function makes sure the changes to a specific file are
 * written to disk rather than kept in memory.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - Another error code if an error occured during this call.
 */
fsal_status_t PROXYFSAL_sync(fsal_file_t * p_file_descriptor     /* IN */)
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_sync);
}
