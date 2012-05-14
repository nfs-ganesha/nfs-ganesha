/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_fileop.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/15 14:26:10 $
 * \version $Revision: 1.11 $
 * \brief   Files operations.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "HPSSclapiExt/hpssclapiext.h"

#include <hpss_errno.h>

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

fsal_status_t HPSSFSAL_open_by_name(hpssfsal_handle_t * dirhandle,      /* IN */
                                    fsal_name_t * filename,     /* IN */
                                    hpssfsal_op_context_t * p_context,  /* IN */
                                    fsal_openflags_t openflags, /* IN */
                                    hpssfsal_file_t * file_descriptor,  /* OUT */
                                    fsal_attrib_list_t *
                                    file_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t fsal_status;
  hpssfsal_handle_t filehandle;

  if(!dirhandle || !filename || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_name);

  fsal_status =
      HPSSFSAL_lookup(dirhandle, filename, p_context, &filehandle, file_attributes);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  return HPSSFSAL_open(&filehandle, p_context, openflags, file_descriptor,
                       file_attributes);
}

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
fsal_status_t HPSSFSAL_open(hpssfsal_handle_t * filehandle,     /* IN */
                            hpssfsal_op_context_t * p_context,  /* IN */
                            fsal_openflags_t openflags, /* IN */
                            hpssfsal_file_t * file_descriptor,  /* OUT */
                            fsal_attrib_list_t * file_attributes        /* [ IN/OUT ] */
    )
{

  int rc;
  int hpss_flags;
  hpss_Attrs_t hpss_attributes;
  TYPE_TOKEN_HPSS hpss_authz;

  /* sanity checks.
   * note : file_attributes is optional.
   */
  if(!filehandle || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open);

  /* check if it is a file */

  if(filehandle->data.obj_type != FSAL_TYPE_FILE)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_open);
    }

  /* convert fsal open flags to hpss open flags */

  rc = fsal2hpss_openflags(openflags, &hpss_flags);

  /* flags conflicts. */

  if(rc)
    {
      LogEvent(COMPONENT_FSAL,
                        "Invalid/conflicting flags : %#X", openflags);
      Return(rc, 0, INDEX_FSAL_open);
    }

  TakeTokenFSCall();

  rc = HPSSFSAL_OpenHandle(&filehandle->data.ns_handle,      /* IN - object handle */
                           NULL, hpss_flags,    /* IN - Type of file access */
                           (mode_t) 0644,       /* IN - Desired file perms if create */
                           &p_context->credential.hpss_usercred,        /* IN - User credentials */
                           NULL,        /* IN - Desired class of service */
                           NULL,        /* IN - Priorities of hint struct */
                           NULL,        /* OUT - Granted class of service */
                           (file_attributes ? &hpss_attributes : NULL), /* OUT - returned attributes */
                           NULL,        /* OUT - returned handle */
                           &hpss_authz  /* OUT - Client authorization */
      );

  ReleaseTokenFSCall();

  /* /!\ rc is the file descriptor number !!! */

  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT)
    Return(ERR_FSAL_STALE, -rc, INDEX_FSAL_open);
  else if(rc < 0)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_open);

  /* fills output struct */

  file_descriptor->filedes = rc;
#if HPSS_MAJOR_VERSION < 7
  file_descriptor->fileauthz = hpss_authz;
#endif

  /* set output attributes if asked */

  if(file_attributes)
    {

      fsal_status_t status;

      status = hpss2fsal_attributes(&(filehandle->data.ns_handle),
                                    &hpss_attributes, file_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(file_attributes->asked_attributes);
          FSAL_SET_MASK(file_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_open);

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
 *      - ERR_FSAL_NOT_OPENED   (tried to read in a non-opened hpssfsal_file_t)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ...
 */
fsal_status_t HPSSFSAL_read(hpssfsal_file_t * file_descriptor,  /* IN */
                            fsal_seek_t * seek_descriptor,      /* [IN] */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* OUT */
                            fsal_size_t * read_amount,  /* OUT */
                            fsal_boolean_t * end_of_file        /* OUT */
    )
{

  ssize_t nb_read;
  size_t i_size;
  int error = 0;
  off_t seekoffset = 0;
  /* sanity checks. */

  if(!file_descriptor || !buffer || !read_amount || !end_of_file)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_read);

  /** @todo: manage fsal_size_t to size_t convertion */
  i_size = (size_t) buffer_size;

  /* positioning */

  if(seek_descriptor)
    {

      switch (seek_descriptor->whence)
        {
        case FSAL_SEEK_CUR:
          /* set position plus offset */

          TakeTokenFSCall();

          seekoffset =
              hpss_Lseek(file_descriptor->filedes, seek_descriptor->offset, SEEK_CUR);

          ReleaseTokenFSCall();

          break;

        case FSAL_SEEK_SET:
          /* set absolute position to offset */

          TakeTokenFSCall();

          seekoffset =
              hpss_Lseek(file_descriptor->filedes, seek_descriptor->offset, SEEK_SET);

          ReleaseTokenFSCall();

          break;

        case FSAL_SEEK_END:
          /* set end of file plus offset */

          TakeTokenFSCall();

          seekoffset =
              hpss_Lseek(file_descriptor->filedes, seek_descriptor->offset, SEEK_END);

          ReleaseTokenFSCall();

          break;
        }

      if(seekoffset < 0)
        {
          error = (int)seekoffset;

          LogEvent(COMPONENT_FSAL,
                            "Error in hpss_Lseek operation (whence=%s, offset=%lld)",
                            (seek_descriptor->whence == FSAL_SEEK_CUR ? "SEEK_CUR" :
                             (seek_descriptor->whence == FSAL_SEEK_SET ? "SEEK_SET" :
                              (seek_descriptor->whence ==
                               FSAL_SEEK_END ? "SEEK_END" : "ERROR"))),
                            seek_descriptor->offset);

          Return(hpss2fsal_error(error), -error, INDEX_FSAL_read);
        }

    }

  /* read operation */

  TakeTokenFSCall();

  nb_read = hpss_Read(file_descriptor->filedes, /* IN - ID of object to be read  */
                      (void *)buffer,   /* IN - Buffer in which to receive data */
                      i_size);  /* IN - number of bytes to read         */

  ReleaseTokenFSCall();

  /** @todo: manage ssize_t to fsal_size_t convertion */

  if(nb_read < 0)
    {
      error = (int)nb_read;
      Return(hpss2fsal_error(error), -error, INDEX_FSAL_read);
    }

  /* set output vars */

  *read_amount = (fsal_size_t) nb_read;
  *end_of_file = (nb_read == 0);

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
 *      - ERR_FSAL_NOT_OPENED   (tried to write in a non-opened hpssfsal_file_t)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ERR_FSAL_NOSPC, ERR_FSAL_DQUOT...
 */
fsal_status_t HPSSFSAL_write(hpssfsal_file_t * file_descriptor, /* IN */
                             fsal_op_context_t * p_context,     /* IN */
                             fsal_seek_t * seek_descriptor,     /* IN */
                             fsal_size_t buffer_size,   /* IN */
                             caddr_t buffer,    /* IN */
                             fsal_size_t * write_amount /* OUT */
    )
{

  ssize_t nb_written;
  size_t i_size;
  int error = 0;
  off_t seekoffset = 0;

  /* sanity checks. */
  if(!file_descriptor || !buffer || !write_amount)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_write);

  /** @todo: manage fsal_size_t to size_t convertion */
  i_size = (size_t) buffer_size;

  /* positioning */

  if(seek_descriptor)
    {

      switch (seek_descriptor->whence)
        {
        case FSAL_SEEK_CUR:
          /* set position plus offset */

          TakeTokenFSCall();

          seekoffset =
              hpss_Lseek(file_descriptor->filedes, seek_descriptor->offset, SEEK_CUR);

          ReleaseTokenFSCall();

          break;

        case FSAL_SEEK_SET:
          /* set absolute position to offset */

          TakeTokenFSCall();

          seekoffset =
              hpss_Lseek(file_descriptor->filedes, seek_descriptor->offset, SEEK_SET);

          ReleaseTokenFSCall();

          break;

        case FSAL_SEEK_END:
          /* set end of file plus offset */

          TakeTokenFSCall();

          seekoffset =
              hpss_Lseek(file_descriptor->filedes, seek_descriptor->offset, SEEK_END);

          ReleaseTokenFSCall();

          break;
        }

      if(seekoffset < 0)
        {
          error = (int)seekoffset;

          LogEvent(COMPONENT_FSAL,
                            "FSAL_write: Error in hpss_Lseek operation (whence=%s, offset=%lld)",
                            (seek_descriptor->whence == FSAL_SEEK_CUR ? "SEEK_CUR" :
                             (seek_descriptor->whence == FSAL_SEEK_SET ? "SEEK_SET" :
                              (seek_descriptor->whence ==
                               FSAL_SEEK_END ? "SEEK_END" : "ERROR"))),
                            seek_descriptor->offset);

          Return(hpss2fsal_error(error), -error, INDEX_FSAL_write);
        }

    }

  /* write operation */

  TakeTokenFSCall();

  nb_written = hpss_Write(file_descriptor->filedes,     /* IN - ID of object to be read  */
                          (void *)buffer,       /* IN - Buffer in which to receive data */
                          i_size);      /* IN - number of bytes to read         */

  ReleaseTokenFSCall();

  /** @todo: manage ssize_t to fsal_size_t convertion */

  if(nb_written < 0)
    {
      error = (int)nb_written;
      Return(hpss2fsal_error(error), -error, INDEX_FSAL_write);
    }

  /* set output vars */

  *write_amount = (fsal_size_t) nb_written;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_write);

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
fsal_status_t HPSSFSAL_commit( hpssfsal_file_t * p_file_descriptor,
                             fsal_off_t        offset, 
                             fsal_size_t       length )
{
  int rc, errsv;

  /* sanity checks. */
  if(!p_file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_commit);

  /* Flush data. */
  TakeTokenFSCall();
  rc = hpss_Fsync(p_file_descriptor->filedes);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    {
      LogEvent(COMPONENT_FSAL, "Error in fsync operation");
      Return(hpss2fsal_error(errsv), errsv, INDEX_FSAL_commit);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_commit);
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

fsal_status_t HPSSFSAL_close(hpssfsal_file_t * file_descriptor  /* IN */
    )
{

  int rc;

  /* sanity checks. */
  if(!file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_close);

  /* call to close */

  TakeTokenFSCall();

  rc = hpss_Close(file_descriptor->filedes);

  ReleaseTokenFSCall();

  if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_close);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_close);

}

/* Some unsupported calls used in FSAL_PROXY, just for permit the ganeshell to compile */
fsal_status_t HPSSFSAL_open_by_fileid(hpssfsal_handle_t * filehandle,   /* IN */
                                      fsal_u64_t fileid,        /* IN */
                                      hpssfsal_op_context_t * p_context,        /* IN */
                                      fsal_openflags_t openflags,       /* IN */
                                      hpssfsal_file_t * file_descriptor,        /* OUT */
                                      fsal_attrib_list_t *
                                      file_attributes /* [ IN/OUT ] */ )
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_open_by_fileid);
}

fsal_status_t HPSSFSAL_close_by_fileid(hpssfsal_file_t * file_descriptor /* IN */ ,
                                       fsal_u64_t fileid)
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_open_by_fileid);
}

unsigned int HPSSFSAL_GetFileno(hpssfsal_file_t * pfile)
{
  return pfile->filedes;
}
