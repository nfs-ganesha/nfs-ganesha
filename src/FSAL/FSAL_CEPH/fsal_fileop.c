/*
 * Copyright (C) 2010 The Linx Box Corporation
 * Contributor : Adam C. Emerson
 *
 * Some Portions Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    fsal_fileop.c
 * \brief   Files operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <stdio.h>

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
fsal_status_t CEPHFSAL_open(fsal_handle_t * exthandle,
                            fsal_op_context_t * extcontext,
                            fsal_openflags_t openflags,
                            fsal_file_t * extdescriptor,
                            fsal_attrib_list_t * file_attributes)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  cephfsal_file_t* descriptor = (cephfsal_file_t *) extdescriptor;
  struct ceph_mount_info *cmount = context->export_context->cmount;
  int rc = 0;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);
  int posix_flags = 0;
  Fh *desc = NULL;

  /* sanity checks.
   * note : file_attributes is optional.
   */
  if(!handle || !context || !descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open);

  rc = fsal2posix_openflags(openflags, &posix_flags);

  /* flags conflicts. */
  if(rc)
    Return(rc, 0, INDEX_FSAL_open);

  TakeTokenFSCall();

  rc = ceph_ll_open(cmount, VINODE(handle), posix_flags, &desc, uid, gid);

  descriptor->fh = desc;
  descriptor->vi = VINODE(handle);
  descriptor->ctx = *context;

  ReleaseTokenFSCall();

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_open);

  if(file_attributes)
    {
      fsal_status_t status
        = CEPHFSAL_getattrs(exthandle, extcontext, file_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(file_attributes->asked_attributes);
          FSAL_SET_MASK(file_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_open);
}

/**
 * FSAL_open_byname:
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

fsal_status_t CEPHFSAL_open_by_name(fsal_handle_t * exthandle,
                                    fsal_name_t * filename,
                                    fsal_op_context_t * extcontext,
                                    fsal_openflags_t openflags,
                                    fsal_file_t * extdescriptor,
                                    fsal_attrib_list_t * file_attributes)
{
  fsal_status_t fsal_status;
  fsal_handle_t found;

  if(!exthandle || !filename || !extcontext || !extdescriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_name);

  fsal_status =
    CEPHFSAL_lookup(exthandle, filename, extcontext, &found, file_attributes);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  return CEPHFSAL_open(&found, extcontext, openflags, extdescriptor,
                       file_attributes);
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
 *      - ERR_FSAL_NOT_OPENED   (tried to read in a non-opened fsal_file_t)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t CEPHFSAL_read(fsal_file_t * extdescriptor,
                            fsal_seek_t * seek_descriptor,
                            fsal_size_t buffer_size,
                            caddr_t buffer,
                            fsal_size_t * read_amount,
                            fsal_boolean_t * end_of_file)
{
  cephfsal_file_t* descriptor = (cephfsal_file_t*) extdescriptor;
  struct ceph_mount_info *cmount = descriptor->ctx.export_context->cmount;

  int nb_read = 0;
  fsal_off_t offset;

  /* sanity checks. */

  if(!descriptor || !buffer || !read_amount || !end_of_file)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_read);

  if (seek_descriptor)
    {
      if (seek_descriptor->whence != FSAL_SEEK_SET)
        {
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_write);
        }
      else
        {
          offset = seek_descriptor->offset;
        }
    }
  else
    {
      offset = 0;
    }

  TakeTokenFSCall();

  nb_read = ceph_ll_read(cmount, FH(descriptor), offset, buffer_size, buffer);

  ReleaseTokenFSCall();

  if (nb_read < 0)
    Return(posix2fsal_error(nb_read), 0, INDEX_FSAL_read);

  if (nb_read < buffer_size)
    *end_of_file = TRUE;
  else
    *end_of_file = FALSE;

  *read_amount = nb_read;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_read);
}

/**
 * FSAL_write:
 * Perform a write operation on an opened file.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
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
 *      - ERR_FSAL_NOT_OPENED   (tried to write in a non-opened fsal_file_t)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ERR_FSAL_NOSPC, ERR_FSAL_DQUOT...
 */
fsal_status_t CEPHFSAL_write(fsal_file_t * extdescriptor,
                             fsal_op_context_t * p_context,
                             fsal_seek_t * seek_descriptor,
                             fsal_size_t buffer_size,
                             caddr_t buffer,
                             fsal_size_t * write_amount)
{
  cephfsal_file_t* descriptor = (cephfsal_file_t*) extdescriptor;
  struct ceph_mount_info *cmount = descriptor->ctx.export_context->cmount;
  int nb_written=0;
  fsal_off_t offset;

  /* sanity checks. */
  if(!descriptor || !buffer || !write_amount)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_write);

  if (seek_descriptor)
    {
      if (seek_descriptor->whence != FSAL_SEEK_SET)
        {
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_write);
        }
      else
        {
          offset = seek_descriptor->offset;
        }
    }
  else
    {
      offset = 0;
    }

  TakeTokenFSCall();

  nb_written = ceph_ll_write(cmount, FH(descriptor), offset, buffer_size,
                             buffer);

  ReleaseTokenFSCall();

  if (nb_written < 0)
    Return(posix2fsal_error(nb_written), 0, INDEX_FSAL_write);

  *write_amount = nb_written;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_write);

}

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

fsal_status_t CEPHFSAL_close(fsal_file_t * extdescriptor)
{
  cephfsal_file_t* descriptor = (cephfsal_file_t*) extdescriptor;
  struct ceph_mount_info *cmount = descriptor->ctx.export_context->cmount;
  int rc = 0;

  /* sanity checks. */
  if(!descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_close);

  /* This is because of a bug in the cache layer which must be fixed
     later.  Right now, we work around it. */

  if(!FH(descriptor))
    Return(ERR_FSAL_NOT_OPENED, 0, INDEX_FSAL_close);

  TakeTokenFSCall();

  rc = ceph_ll_close(cmount, FH(descriptor));

  ReleaseTokenFSCall();

  FH(descriptor) = NULL;

  if (rc != 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_read);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_close);

}

unsigned int CEPHFSAL_GetFileno(fsal_file_t * pfile)
{
  unsigned int mask=0xFFFFFFFF;
  return (mask & ((uintptr_t) FH((cephfsal_file_t*) pfile)));
}

/**
 * FSAL_commit:
 * This function is used for processing stable writes and COMMIT requests.
 * Calling this function makes sure the changes to a specific file are
 * written to disk rather than kept in memory.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param offset:
 *        The starting offset for the portion of file to be synced       
 * \param length:
 *        The length for the portion of file to be synced.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - Another error code if an error occured during this call.
 */
fsal_status_t CEPHFSAL_commit( fsal_file_t * extdescriptor, 
                             fsal_off_t    offset, 
                             fsal_size_t   length )
{
  cephfsal_file_t* descriptor = (cephfsal_file_t*) extdescriptor;
  struct ceph_mount_info *cmount = descriptor->ctx.export_context->cmount;
  int rc = 0;

  /* sanity checks. */
  if(!extdescriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_commit);

  TakeTokenFSCall();
  if ((rc = ceph_ll_fsync(cmount, FH(descriptor), 0)) < 0)
    {
      Return(posix2fsal_error(rc), 0, INDEX_FSAL_commit);
    }
  ReleaseTokenFSCall();

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_commit);
}
