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

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "namespace.h"
#include <string.h>

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
fsal_status_t FUSEFSAL_open(fsal_handle_t * file_hdl,     /* IN */
                            fsal_op_context_t * p_context,  /* IN */
                            fsal_openflags_t openflags, /* IN */
                            fsal_file_t * file_desc,  /* OUT */
                            fsal_attrib_list_t * file_attributes        /* [ IN/OUT ] */
    )
{

  int rc = 0;
  char object_path[FSAL_MAX_PATH_LEN];
  int file_info_provided = FALSE;
  fusefsal_handle_t * filehandle = (fusefsal_handle_t *)file_hdl;
  fusefsal_file_t * file_descriptor = (fusefsal_file_t *)file_desc;

  /* sanity checks.
   * note : file_attributes is optional.
   */
  if(!filehandle || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open);

  /* get the full path for this file */
  rc = NamespacePath(filehandle->data.inode, filehandle->data.device, filehandle->data.validator,
                     object_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_open);

  memset(file_descriptor, 0, sizeof(fusefsal_file_t));

  /* set access mode flags */

  file_descriptor->file_info.flags = 0;

  if(openflags & FSAL_O_RDONLY)
    file_descriptor->file_info.flags |= O_RDONLY;
  if(openflags & FSAL_O_WRONLY)
    file_descriptor->file_info.flags |= O_WRONLY;
  if(openflags & FSAL_O_RDWR)
    file_descriptor->file_info.flags |= O_RDWR;

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  /* check open call */

  if(p_fs_ops->open)
    {
      LogFullDebug(COMPONENT_FSAL, "Call to open( %s, %#X )", object_path, file_descriptor->file_info.flags);

      TakeTokenFSCall();
      rc = p_fs_ops->open(object_path, &(file_descriptor->file_info));
      ReleaseTokenFSCall();

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_open);

      file_info_provided = TRUE;

    }
  else
    {
      LogFullDebug(COMPONENT_FSAL, "no open command provided");

      /* ignoring open */
      memset(&(file_descriptor->file_info), 0, sizeof(struct ganefuse_file_info));
    }

  /* check open flags (only FSAL_O_TRUNC and FSAL_O_APPEND are used for FUSE filesystems) */

  if(openflags & FSAL_O_TRUNC)
    {
      if(file_info_provided && p_fs_ops->ftruncate)
        {
          LogFullDebug(COMPONENT_FSAL, "call to ftruncate on file since FSAL_O_TRUNC was set");

          /* ftruncate the file */
          TakeTokenFSCall();
          rc = p_fs_ops->ftruncate(object_path, 0, &(file_descriptor->file_info));
          ReleaseTokenFSCall();

          if(rc)
            Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_open);
        }
      else if(p_fs_ops->truncate)
        {
          LogFullDebug(COMPONENT_FSAL, "call to truncate on file since FSAL_O_TRUNC was set");

          /* truncate the file */
          TakeTokenFSCall();
          rc = p_fs_ops->truncate(object_path, 0);
          ReleaseTokenFSCall();

          if(rc)
            Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_open);
        }
      /* else: ignoring flag */
    }

  if(openflags & FSAL_O_APPEND)
    {
      struct stat stbuf;

      /* In this case, this only solution is to get file attributes */

      if(file_info_provided && p_fs_ops->fgetattr)
        {
          rc = p_fs_ops->fgetattr(object_path, &stbuf, &(file_descriptor->file_info));
        }
      else
        {
          rc = p_fs_ops->getattr(object_path, &stbuf);
        }

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_open);

      file_descriptor->current_offset = stbuf.st_size;
    }
  else
    {
      file_descriptor->current_offset = 0;
    }

  /* fill the file descriptor structure */
  file_descriptor->file_handle = *filehandle;

  /* backup context */
  file_descriptor->context = *(fusefsal_op_context_t *)p_context;

  if(file_info_provided)
    LogFullDebug(COMPONENT_FSAL, "FSAL_open: FH=%"PRId64, file_descriptor->file_info.fh);

  if(file_attributes)
    {
      fsal_status_t status;

      status = FUSEFSAL_getattrs((fsal_handle_t *)filehandle, p_context, file_attributes);

      /* on error, we set a special bit in the mask. */
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

fsal_status_t FUSEFSAL_open_by_name(fsal_handle_t * dirhandle,      /* IN */
                                    fsal_name_t * filename,     /* IN */
                                    fsal_op_context_t * p_context,  /* IN */
                                    fsal_openflags_t openflags, /* IN */
                                    fsal_file_t * file_descriptor,  /* OUT */
                                    fsal_attrib_list_t *
                                    file_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t fsal_status;
  fsal_handle_t filehandle;

  if(!dirhandle || !filename || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_name);

  fsal_status =
      FUSEFSAL_lookup(dirhandle, filename, p_context, &filehandle, file_attributes);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  return FUSEFSAL_open(&filehandle, p_context, openflags, file_descriptor,
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
 *      - ERR_FSAL_NOT_OPENED   (tried to read in a non-opened fusefsal_file_t)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t FUSEFSAL_read(fsal_file_t * file_desc,  /* IN */
                            fsal_seek_t * seek_descriptor,      /* [IN] */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* OUT */
                            fsal_size_t * read_amount,  /* OUT */
                            fsal_boolean_t * end_of_file        /* OUT */
    )
{
  size_t req_size;
  size_t nb_read;
  int rc;
  off_t seekoffset = 0;
  struct stat stbuf;
  char object_path[FSAL_MAX_PATH_LEN];
  fusefsal_file_t * file_descriptor = (fusefsal_file_t *)file_desc;

  /* sanity checks. */

  if(!file_descriptor || !buffer || !read_amount || !end_of_file)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_read);

  if(!p_fs_ops->read)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_read);

  /* initialize returned values */

  *read_amount = 0;
  *end_of_file = 0;

  req_size = (size_t) buffer_size;

  /* get file's full path */
  rc = NamespacePath(file_descriptor->file_handle.data.inode,
                     file_descriptor->file_handle.data.device,
                     file_descriptor->file_handle.data.validator, object_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_read);

  /* set context so it can be retrieved by FS */
  fsal_set_thread_context((fsal_op_context_t *) &file_descriptor->context);

  LogFullDebug(COMPONENT_FSAL, "FSAL_read: FH=%"PRId64, file_descriptor->file_info.fh);

  if(seek_descriptor)
    {

      switch (seek_descriptor->whence)
        {
        case FSAL_SEEK_SET:
          /* set absolute position to offset */
          seekoffset = seek_descriptor->offset;
          break;

        case FSAL_SEEK_CUR:
          /* current position + offset */
          seekoffset = file_descriptor->current_offset + seek_descriptor->offset;
          break;

        case FSAL_SEEK_END:
          /* set end of file + offset */
          /* in this case, the only solution is to get entry attributes */

          if(p_fs_ops->fgetattr)
            {
              rc = p_fs_ops->fgetattr(object_path, &stbuf, &(file_descriptor->file_info));
            }
          else
            {
              rc = p_fs_ops->getattr(object_path, &stbuf);
            }

          if(rc)
            Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_read);

          seekoffset = (off_t) stbuf.st_size + seek_descriptor->offset;

          break;

        default:
          LogCrit(COMPONENT_FSAL, "FSAL_read: Invalid seek parameter: whence=%d",
                  seek_descriptor->whence);
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_read);
        }
    }
  else
    {
      seekoffset = file_descriptor->current_offset;
    }

  /* If the call does not fill all the buffer, the rest of the data must be
   * substituted with zeroes. */
  memset(buffer, 0, req_size);

  TakeTokenFSCall();
  rc = p_fs_ops->read(object_path, buffer, req_size, seekoffset,
                      &(file_descriptor->file_info));
  ReleaseTokenFSCall();

  if(rc < 0)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_read);

  /* rc >= 0 */

  *read_amount = (fsal_size_t) rc;
  *end_of_file = (rc < (off_t) req_size);
  file_descriptor->current_offset = seekoffset + (off_t) rc;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_read);

}

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
 *      - ERR_FSAL_NOT_OPENED   (tried to write in a non-opened fusefsal_file_t)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ERR_FSAL_NOSPC, ERR_FSAL_DQUOT...
 */
fsal_status_t FUSEFSAL_write(fsal_file_t * file_desc, /* IN */
                             fsal_seek_t * seek_descriptor,     /* IN */
                             fsal_size_t buffer_size,   /* IN */
                             caddr_t buffer,    /* IN */
                             fsal_size_t * write_amount /* OUT */
    )
{
  size_t req_size;
  int rc;
  off_t seekoffset = 0;
  struct stat stbuf;
  char object_path[FSAL_MAX_PATH_LEN];
  fusefsal_file_t * file_descriptor = (fusefsal_file_t *)file_desc;

  /* sanity checks. */
  if(!file_descriptor || !buffer || !write_amount)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_write);

  if(!p_fs_ops->write)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_write);

  /* initialize returned values */

  *write_amount = 0;

  req_size = (size_t) buffer_size;

  /* set context so it can be retrieved by FS */
  fsal_set_thread_context((fsal_op_context_t *) &file_descriptor->context);

  LogFullDebug(COMPONENT_FSAL, "FSAL_write: FH=%"PRId64, file_descriptor->file_info.fh);

  /* get file's full path */
  rc = NamespacePath(file_descriptor->file_handle.data.inode,
                     file_descriptor->file_handle.data.device,
                     file_descriptor->file_handle.data.validator, object_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_write);

  if(seek_descriptor)
    {

      switch (seek_descriptor->whence)
        {
        case FSAL_SEEK_SET:
          /* set absolute position to offset */
          seekoffset = seek_descriptor->offset;
          break;

        case FSAL_SEEK_CUR:
          /* current position + offset */
          seekoffset = file_descriptor->current_offset + seek_descriptor->offset;
          break;

        case FSAL_SEEK_END:
          /* set end of file + offset */
          /* in this case, the only solution is to get entry attributes */

          if(p_fs_ops->fgetattr)
            {
              rc = p_fs_ops->fgetattr(object_path, &stbuf, &(file_descriptor->file_info));
            }
          else
            {
              rc = p_fs_ops->getattr(object_path, &stbuf);
            }

          if(rc)
            Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_write);

          seekoffset = (off_t) stbuf.st_size + seek_descriptor->offset;

          break;

        default:
          LogCrit(COMPONENT_FSAL, "FSAL_write: Invalid seek parameter: whence=%d",
                  seek_descriptor->whence);
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_write);
        }
    }
  else
    {
      seekoffset = file_descriptor->current_offset;
    }

  TakeTokenFSCall();
  rc = p_fs_ops->write(object_path, buffer, req_size, seekoffset,
                       &(file_descriptor->file_info));
  ReleaseTokenFSCall();

  if(rc < 0)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_write);

  file_descriptor->current_offset = seekoffset + (off_t) rc;

  *write_amount = (fsal_size_t) rc;

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

fsal_status_t FUSEFSAL_close(fsal_file_t * file_desc  /* IN */
    )
{

  int rc;
  char file_path[FSAL_MAX_PATH_LEN];
  fusefsal_file_t * file_descriptor = (fusefsal_file_t *)file_desc;

  /* sanity checks. */
  if(!file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_close);

  /* get the full path for file inode */
  rc = NamespacePath(file_descriptor->file_handle.data.inode,
                     file_descriptor->file_handle.data.device,
                     file_descriptor->file_handle.data.validator, file_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_close);

  if(!p_fs_ops->release)
    /* ignore this call */
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_close);

  /* set context so it can be retrieved by FS */
  fsal_set_thread_context((fsal_op_context_t *) &file_descriptor->context);

  LogFullDebug(COMPONENT_FSAL, "FSAL_close: FH=%"PRId64, file_descriptor->file_info.fh);

  TakeTokenFSCall();

  rc = p_fs_ops->release(file_path, &file_descriptor->file_info);

  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_close);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_close);

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
fsal_status_t FUSEFSAL_sync(fsal_file_t * p_file_descriptor       /* IN */)
{
  int rc, errsv;

  /* sanity checks. */
  if(!p_file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_sync);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_sync);
}

unsigned int FUSEFSAL_GetFileno(fsal_file_t * pfile)
{
	return ((fusefsal_file_t *)pfile)->file_info.fh;
}
