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
#include "gpfs_methods.h"
#include <sys/fsuid.h>

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

#if 0 //??? not needed for now
fsal_status_t GPFSFSAL_open_by_name(struct gpfs_file_handle * dirhandle,      /* IN */
                                fsal_name_t * filename, /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                fsal_openflags_t openflags,     /* IN */
                                fsal_file_t * file_descriptor,  /* OUT */
                                fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t fsal_status;
  struct gpfs_file_handle filehandle;

  if(!dirhandle || !filename || !p_context || !file_descriptor)
    return fsalstat(ERR_FSAL_FAULT, 0);

  fsal_status = FSAL_lookup(dirhandle, filename, p_context, &filehandle, file_attributes);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  return FSAL_open(&filehandle, p_context, openflags, file_descriptor, file_attributes);
}
#endif

/**
 * FSAL_open:
 * Open a regular file for reading/writing its data content.
 *
 * \param noj_hdl (input):
 *        Handle of the file to be read/modified.
 * \param p_context (input):
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
fsal_status_t GPFSFSAL_open(struct fsal_obj_handle *obj_hdl,        /* IN */
                            const struct req_op_context *p_context, /* IN */
                            fsal_openflags_t openflags,            /* IN */
                            int * file_desc,                      /* OUT */
                            struct attrlist *p_file_attributes  /* IN/OUT */
    )
{

  int rc;
  fsal_status_t status;
  int posix_flags = 0;
  struct gpfs_fsal_obj_handle *myself;
  int mntfd;

  /* sanity checks.
   * note : file_attributes is optional.
   */
  if(!obj_hdl || !file_desc)
    return fsalstat(ERR_FSAL_FAULT, 0);

  myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
  mntfd = gpfs_get_root_fd(obj_hdl->export);


  /* convert fsal open flags to posix open flags */
  rc = fsal2posix_openflags(openflags, &posix_flags);
  /* flags conflicts. */
  if(rc)
    {
      LogWarn(COMPONENT_FSAL, "Invalid/conflicting flags : %#X", openflags);
      return fsalstat(rc, 0);
    }

  status = fsal_internal_handle2fd(mntfd, myself->handle, file_desc,
                                   posix_flags);

  if(FSAL_IS_ERROR(status))
    {
      *file_desc = 0;
      return(status);
    }

  /* output attributes */
  if(p_file_attributes)
    {

      p_file_attributes->mask = GPFS_SUPPORTED_ATTRIBUTES;
      status = GPFSFSAL_getattrs(obj_hdl->export, p_context, myself->handle,
                                 p_file_attributes);
      if(FSAL_IS_ERROR(status))
        {
          *file_desc = 0;
          close(*file_desc);
          return(status);
        }
    }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

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
fsal_status_t GPFSFSAL_read(int fd,        /* IN */
                        uint64_t offset,        /* [IN] */
                        size_t buffer_size,        /* IN */
                        caddr_t buffer, /* OUT */
                        size_t * p_read_amount,    /* OUT */
                        bool * p_end_of_file  /* OUT */
    )
{

  size_t i_size;
  ssize_t nb_read;
  int errsv = 0;
  int pcall = true;

  /* sanity checks. */

  if(!buffer || !p_read_amount || !p_end_of_file)
    return fsalstat(ERR_FSAL_FAULT, 0);

  /** @todo: manage fsal_size_t to size_t convertion */
  i_size = (size_t) buffer_size;

#if 0 //???
  /* positioning */

  if(p_seek_descriptor)
    {

      switch (p_seek_descriptor->whence)
        {
        case FSAL_SEEK_CUR:
          /* set position plus offset */
          pcall = false;
          rc = lseek(p_file_descriptor->fd, p_seek_descriptor->offset, SEEK_CUR);
          errsv = errno;
          break;

        case FSAL_SEEK_SET:
          /* use pread/pwrite call */
          pcall = true;
          rc = 0;
          break;

        case FSAL_SEEK_END:
          /* set end of file plus offset */
          pcall = false;
          rc = lseek(p_file_descriptor->fd, p_seek_descriptor->offset, SEEK_END);
          errsv = errno;

          break;
        }

      if(rc)
        {
          LogFullDebug(COMPONENT_FSAL,
                       "Error in posix fseek operation (whence=%s, offset=%lld)",
		       format_seek_whence(p_seek_descriptor->whence),
                       (long long) p_seek_descriptor->offset);

          return fsalstat(posix2fsal_error(errsv), errsv);
        }

    }
#endif
  /* read operation */

  if(pcall)
    nb_read = pread(fd, buffer, i_size, offset);
  else
    nb_read = read(fd, buffer, i_size);
  errsv = errno;

  /** @todo: manage ssize_t to fsal_size_t convertion */

  if(nb_read == -1)
    return fsalstat(posix2fsal_error(errsv), errsv);
  else if(nb_read == 0)
    *p_end_of_file = 1;

  *p_read_amount = nb_read;

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
#if 0 //???  done in file.c

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
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - Another error code if an error occured during this call.
 */
fsal_status_t GPFSFSAL_write(fsal_file_t * file_desc,       /* IN */
                         fsal_op_context_t * p_context,     /* IN */
                         fsal_seek_t * p_seek_descriptor,       /* IN */
                         fsal_size_t buffer_size,       /* IN */
                         caddr_t buffer,        /* IN */
                         fsal_size_t * p_write_amount   /* OUT */
    )
{

  ssize_t nb_written;
  size_t i_size;
  int rc = 0, errsv = 0;
  int pcall = false, fsuid, fsgid;
  gpfsfsal_file_t * p_file_descriptor = (gpfsfsal_file_t *)file_desc;

  /* sanity checks. */
  if(!p_file_descriptor || !buffer || !p_write_amount)
    return fsalstat(ERR_FSAL_FAULT, 0);

  if(p_file_descriptor->ro)
    return fsalstat(ERR_FSAL_PERM, 0);

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
          pcall = false;

          rc = lseek(p_file_descriptor->fd, p_seek_descriptor->offset, SEEK_CUR);
          errsv = errno;
          break;

        case FSAL_SEEK_SET:
          /* set absolute position to offset */
          pcall = true;
          rc = 0;
          break;

        case FSAL_SEEK_END:
          /* set end of file plus offset */
          pcall = false;

          rc = lseek(p_file_descriptor->fd, p_seek_descriptor->offset, SEEK_END);
          errsv = errno;

          break;
        }

      if(rc)
        {
          LogFullDebug(COMPONENT_FSAL,
                       "Error in posix fseek operation (whence=%s, offset=%lld)",
		       format_seek_whence(p_seek_descriptor->whence),
                       (long long) p_seek_descriptor->offset);

          return fsalstat(posix2fsal_error(errsv), errsv);

        }

      LogFullDebug(COMPONENT_FSAL,
                   "Write operation (whence=%s, offset=%lld, size=%zd)",
                   format_seek_whence(p_seek_descriptor->whence),
                   (long long) p_seek_descriptor->offset, buffer_size);

    }

  /* write operation */

  fsuid = setfsuid(p_context->credential.user);
  fsgid = setfsgid(p_context->credential.group);
  if(pcall)
    nb_written = pwrite(p_file_descriptor->fd, buffer, i_size, p_seek_descriptor->offset);
  else
    nb_written = write(p_file_descriptor->fd, buffer, i_size);
  errsv = errno;

  setfsuid(fsuid);
  setfsgid(fsgid);

  /** @todo: manage ssize_t to fsal_size_t convertion */
  if(nb_written <= 0)
    {
      if (p_seek_descriptor)
        LogDebug(COMPONENT_FSAL,
                 "Write operation of size %llu at offset %lld failed. fd=%d, errno=%d.",
                 (unsigned long long) i_size,
                 (long long) p_seek_descriptor->offset,
                 p_file_descriptor->fd,
                 errsv);
      else
        LogDebug(COMPONENT_FSAL,
                 "Write operation of size %llu at offset 0. fd=%d, errno=%d.",
                 (unsigned long long) i_size,
                 p_file_descriptor->fd,
                 errsv);

      return fsalstat(posix2fsal_error(errsv), errsv);
    }

  /* set output vars */

  *p_write_amount = (fsal_size_t) nb_written;

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

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

fsal_status_t GPFSFSAL_close(fsal_file_t * p_file_descriptor        /* IN */
    )
{

  int rc, errsv;

  /* sanity checks. */
  if(!p_file_descriptor)
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* call to close */

  rc = close(((gpfsfsal_file_t *)p_file_descriptor)->fd);
  errsv = errno;

  if(rc)
    return fsalstat(posix2fsal_error(errsv), errsv);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

unsigned int GPFSFSAL_GetFileno(fsal_file_t * pfile)
{
	return ((gpfsfsal_file_t *)pfile)->fd;
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
fsal_status_t GPFSFSAL_commit( fsal_file_t * p_file_descriptor, 
                             fsal_off_t    offset, 
                             fsal_size_t   length )
{
  int rc, errsv;

  /* sanity checks. */
  if(!p_file_descriptor)
    return fsalstat(ERR_FSAL_FAULT, 0);

  /* Flush data. */
  rc = fsync(((gpfsfsal_file_t *)p_file_descriptor)->fd);
  errsv = errno;

  if(rc)
    return fsalstat(posix2fsal_error(errsv), errsv);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
#endif
