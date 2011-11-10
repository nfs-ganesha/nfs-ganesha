/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * ------------- 
 */

/**
 * \file    fsal_fileop.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 14:20:07 $
 * \version $Revision: 1.9 $
 * \brief   Files operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

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

fsal_status_t XFSFSAL_open_by_name(fsal_handle_t * dirhandle,        /* IN */
                                   fsal_name_t * filename,      /* IN */
                                   fsal_op_context_t * p_context,    /* IN */
                                   fsal_openflags_t openflags,  /* IN */
                                   fsal_file_t * file_descriptor,    /* OUT */
                                   fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t fsal_status;
  xfsfsal_handle_t filehandle;

  if(!dirhandle || !filename || !p_context || !file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open_by_name);

  fsal_status =
      XFSFSAL_lookup(dirhandle, filename, p_context, &filehandle, file_attributes);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  return XFSFSAL_open(&filehandle, p_context, openflags, file_descriptor,
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
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - Another error code if an error occured during this call.
 */
fsal_status_t XFSFSAL_open(fsal_handle_t * p_filehandle,     /* IN */
                           fsal_op_context_t * p_context,    /* IN */
                           fsal_openflags_t openflags,  /* IN */
                           fsal_file_t * p_file_descriptor,  /* OUT */
                           fsal_attrib_list_t * p_file_attributes       /* [ IN/OUT ] */
    )
{

  int rc, errsv;
  fsal_status_t status;

  int fd = 0 ;
  struct stat buffstat;
  int posix_flags = 0;

  /* sanity checks.
   * note : file_attributes is optional.
   */
  if(!p_filehandle || !p_context || !p_file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_open);

  /* convert fsal open flags to posix open flags */
  rc = fsal2posix_openflags(openflags, &posix_flags);

  /* flags conflicts. */
  if(rc)
    {
      LogEvent(COMPONENT_FSAL, "Invalid/conflicting flags : %#X",
                        openflags);
      Return(rc, 0, INDEX_FSAL_open);
    }

  TakeTokenFSCall();
  status = fsal_internal_handle2fd(p_context, p_filehandle, &fd, posix_flags);
  ReleaseTokenFSCall();

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_open);

  /* retrieve file attributes for checking access rights */

  TakeTokenFSCall();
  rc = fstat(fd, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    {
      close(fd);

      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_open);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_open);
    }

#if 0
  /* No required, the open would have failed if not permitted */
  status =
      fsal_internal_testAccess(p_context,
                               openflags & FSAL_O_RDONLY ? FSAL_R_OK : FSAL_W_OK,
                               &buffstat, NULL);
  if(FSAL_IS_ERROR(status))
    {
      close(fd);
      ReturnStatus(status, INDEX_FSAL_open);
    }
#endif

  TakeTokenFSCall();
  ((xfsfsal_file_t *)p_file_descriptor)->fd = fd;
  errsv = errno;
  ReleaseTokenFSCall();

  /* set the read-only flag of the file descriptor */
  ((xfsfsal_file_t *)p_file_descriptor)->ro = openflags & FSAL_O_RDONLY;

  /* output attributes */
  if(p_file_attributes)
    {

      status = posix2fsal_attributes(&buffstat, p_file_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_file_attributes->asked_attributes);
          FSAL_SET_MASK(p_file_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
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
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - Another error code if an error occured during this call.
 */
fsal_status_t XFSFSAL_read(fsal_file_t * p_file_descriptor,  /* IN */
                           fsal_seek_t * p_seek_descriptor,     /* [IN] */
                           fsal_size_t buffer_size,     /* IN */
                           caddr_t buffer,      /* OUT */
                           fsal_size_t * p_read_amount, /* OUT */
                           fsal_boolean_t * p_end_of_file       /* OUT */
    )
{

  size_t i_size;
  ssize_t nb_read;
  int rc, errsv;
  int pcall = FALSE;

  /* sanity checks. */

  if(!p_file_descriptor || !buffer || !p_read_amount || !p_end_of_file)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_read);

  /** @todo: manage fsal_size_t to size_t convertion */
  i_size = (size_t) buffer_size;

  /* positioning */

  if(p_seek_descriptor)
    {

      switch (p_seek_descriptor->whence)
        {
        case FSAL_SEEK_CUR:
          /* set position plus offset */
          pcall = FALSE;
          TakeTokenFSCall();
          rc = lseek(((xfsfsal_file_t *)p_file_descriptor)->fd,
		     p_seek_descriptor->offset, SEEK_CUR);
          errsv = errno;
          ReleaseTokenFSCall();
          break;

        case FSAL_SEEK_SET:
          /* use pread/pwrite call */
          pcall = TRUE;
          rc = 0;
          break;

        case FSAL_SEEK_END:
          /* set end of file plus offset */
          pcall = FALSE;

          TakeTokenFSCall();
          rc = lseek(((xfsfsal_file_t *)p_file_descriptor)->fd,
		     p_seek_descriptor->offset, SEEK_END);
          errsv = errno;
          ReleaseTokenFSCall();

          break;
        }

      if(rc)
        {
          LogEvent(COMPONENT_FSAL,
                            "Error in posix fseek operation (whence=%s, offset=%"PRId64")",
                            (p_seek_descriptor->whence == FSAL_SEEK_CUR ? "SEEK_CUR" :
                             (p_seek_descriptor->whence == FSAL_SEEK_SET ? "SEEK_SET" :
                              (p_seek_descriptor->whence ==
                               FSAL_SEEK_END ? "SEEK_END" : "ERROR"))),
                            p_seek_descriptor->offset);

          Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_read);
        }

    }

  /* read operation */

  TakeTokenFSCall();

  if(pcall)
    nb_read = pread(((xfsfsal_file_t *)p_file_descriptor)->fd, buffer,
		    i_size, p_seek_descriptor->offset);
  else
    nb_read = read(((xfsfsal_file_t *)p_file_descriptor)->fd, buffer, i_size);
  errsv = errno;
  ReleaseTokenFSCall();

  /** @todo: manage ssize_t to fsal_size_t convertion */

  if(nb_read == -1)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_read);
  else if(nb_read == 0)
    *p_end_of_file = 1;

  *p_read_amount = nb_read;

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
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - Another error code if an error occured during this call.
 */
fsal_status_t XFSFSAL_write(fsal_file_t * p_file_descriptor, /* IN */
                            fsal_seek_t * p_seek_descriptor,    /* IN */
                            fsal_size_t buffer_size,    /* IN */
                            caddr_t buffer,     /* IN */
                            fsal_size_t * p_write_amount        /* OUT */
    )
{

  ssize_t nb_written;
  size_t i_size;
  int rc, errsv;
  int pcall = FALSE;

  /* sanity checks. */
  if(!p_file_descriptor || !buffer || !p_write_amount)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_write);

  if(((xfsfsal_file_t *)p_file_descriptor)->ro)
    Return(ERR_FSAL_PERM, 0, INDEX_FSAL_write);

  /** @todo: manage fsal_size_t to size_t convertion */
  i_size = (size_t) buffer_size;

  *p_write_amount = 0;

  /* positioning */

  if(p_seek_descriptor)
    {

      switch (p_seek_descriptor->whence)
        {
        case FSAL_SEEK_CUR:
          /* set position plus offset */
          pcall = FALSE;

          TakeTokenFSCall();
          rc = lseek(((xfsfsal_file_t *)p_file_descriptor)->fd, p_seek_descriptor->offset, SEEK_CUR);
          errsv = errno;
          ReleaseTokenFSCall();
          break;

        case FSAL_SEEK_SET:
          /* set absolute position to offset */
          pcall = TRUE;
          rc = 0;
          break;

        case FSAL_SEEK_END:
          /* set end of file plus offset */
          pcall = FALSE;

          TakeTokenFSCall();
          rc = lseek(((xfsfsal_file_t *)p_file_descriptor)->fd, p_seek_descriptor->offset, SEEK_END);
          errsv = errno;
          ReleaseTokenFSCall();

          break;
        }

      if(rc)
        {
	  LogEvent(COMPONENT_FSAL,
                            "Error in posix fseek operation (whence=%s, offset=%"PRId64")",
                            (p_seek_descriptor->whence == FSAL_SEEK_CUR ? "SEEK_CUR" :
                             (p_seek_descriptor->whence == FSAL_SEEK_SET ? "SEEK_SET" :
                              (p_seek_descriptor->whence ==
                               FSAL_SEEK_END ? "SEEK_END" : "ERROR"))),
                            p_seek_descriptor->offset);

          Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_write);

        }

      LogFullDebug(COMPONENT_FSAL,
                        "Write operation (whence=%s, offset=%"PRId64", size=%llu)",
                        (p_seek_descriptor->whence ==
                         FSAL_SEEK_CUR ? "SEEK_CUR" : (p_seek_descriptor->whence ==
                                                       FSAL_SEEK_SET ? "SEEK_SET"
                                                       : (p_seek_descriptor->whence ==
                                                          FSAL_SEEK_END ? "SEEK_END" :
                                                          "ERROR"))),
                        p_seek_descriptor->offset, buffer_size);

    }

  /* write operation */

  TakeTokenFSCall();

  if(pcall)
    nb_written = pwrite(((xfsfsal_file_t *)p_file_descriptor)->fd,
			buffer, i_size, p_seek_descriptor->offset);
  else
    nb_written = write(((xfsfsal_file_t *)p_file_descriptor)->fd, buffer, i_size);
  errsv = errno;

  ReleaseTokenFSCall();

  /** @todo: manage ssize_t to fsal_size_t convertion */
  if(nb_written <= 0)
    {
      LogDebug(COMPONENT_FSAL,
	       "Write operation of size %zu at offset %"PRId64" failed. fd=%d, errno=%d.",
	       i_size, p_seek_descriptor->offset,
	       ((xfsfsal_file_t *)p_file_descriptor)->fd, errsv);
      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_write);
    }

  /* set output vars */

  *p_write_amount = (fsal_size_t) nb_written;

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
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - Another error code if an error occured during this call.
 */

fsal_status_t XFSFSAL_close(fsal_file_t * p_file_descriptor  /* IN */
    )
{

  int rc, errsv;

  /* sanity checks. */
  if(!p_file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_close);

  if(((xfsfsal_file_t *)p_file_descriptor)->fd == 0 )
       Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_close);

  /* call to close */

  TakeTokenFSCall();

  rc = close(((xfsfsal_file_t *)p_file_descriptor)->fd);
  errsv = errno;

  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_close);

  ((xfsfsal_file_t *)p_file_descriptor)->fd = 0 ;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_close);

}

unsigned int XFSFSAL_GetFileno(fsal_file_t * pfile)
{
  return ((xfsfsal_file_t *) pfile)->fd;
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
fsal_status_t XFSFSAL_sync(fsal_file_t * p_file_descriptor       /* IN */)
{
  int rc, errsv;

  /* sanity checks. */
  if(!p_file_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_sync);

  if(((xfsfsal_file_t *)p_file_descriptor)->fd == 0 )
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_sync); /* Nothing to sync, the file is not opened */

  /* Flush data. */
  TakeTokenFSCall();
  rc = fsync(((xfsfsal_file_t *)p_file_descriptor)->fd);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    {
      LogEvent(COMPONENT_FSAL, "Error in fsync operation");
      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_sync);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_sync);
}

