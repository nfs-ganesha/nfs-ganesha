/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_fileop.c
 * Description: FSAL file operation implementation
 * Author:      FSI IPC dev team
 * ----------------------------------------------------------------------------
 */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 *
 * -------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "pt_methods.h"

/* PTFSAL */
#include "pt_ganesha.h"

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
fsal_status_t PTFSAL_open(struct fsal_obj_handle *obj_hdl,	/* IN */
			  const struct req_op_context *p_context,	/* IN */
			  fsal_openflags_t openflags,	/* IN */
			  int *file_desc,	/* OUT */
			  struct attrlist *p_file_attributes	/* IN/OUT */
			)
{

	int rc;
	fsal_status_t status;

	int posix_flags = 0;
	struct pt_fsal_obj_handle *myself;

	FSI_TRACE(FSI_DEBUG,
		  "FSI - PTFSAL Open********************************\n");

	/* sanity checks.
	 * note : file_attributes is optional.
	 */
	if (!obj_hdl || !file_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);

	myself = container_of(obj_hdl, struct pt_fsal_obj_handle, obj_handle);

	/* convert fsal open flags to posix open flags */
	rc = fsal2posix_openflags(openflags, &posix_flags);

	/* flags conflicts. */
	if (rc) {
		LogWarn(COMPONENT_FSAL, "Invalid/conflicting flags : %#X",
			openflags);
		return fsalstat(rc, 0);
	}

	status =
	    fsal_internal_handle2fd(p_context, myself, file_desc, posix_flags);

	FSI_TRACE(FSI_DEBUG, "FSI - PTFSAL fd = %d\n", *file_desc);

	if (FSAL_IS_ERROR(status)) {
		*file_desc = 0;
		return status;
	}

	/* output attributes */
	if (p_file_attributes) {
		p_file_attributes->mask = PT_SUPPORTED_ATTRIBUTES;
		status =
		    PTFSAL_getattrs(p_context->fsal_export, NULL,
				    myself->handle, p_file_attributes);
		if (FSAL_IS_ERROR(status)) {
			*file_desc = 0;
			PTFSAL_close(*file_desc);
			return status;
		}
	}

	FSI_TRACE(FSI_DEBUG, "FSI - End PTFSAL open********************\n");
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
fsal_status_t PTFSAL_read(struct pt_fsal_obj_handle *myself,	/* IN */
			  const struct req_op_context *opctx,
			  uint64_t offset,	/* [IN] */
			  size_t buffer_size,	/* IN */
			  caddr_t buffer,	/* OUT */
			  size_t *p_read_amount,	/* OUT */
			  bool *p_end_of_file	/* OUT */
			)
{

	ssize_t nb_read;
	int errsv = 0;
	int handle_index;	/* FSI handle index */

	FSI_TRACE(FSI_DEBUG, "Read Begin================================\n");

	/* sanity checks. */
	if (!buffer || !p_read_amount || !p_end_of_file)
		return fsalstat(ERR_FSAL_FAULT, 0);

	*p_end_of_file = 0;

	/* get FSI location */
	handle_index = myself->u.file.fd;
	if (fsi_check_handle_index(handle_index) < 0)
		return fsalstat(ERR_FSAL_FAULT, 0);
	FSI_TRACE(FSI_DEBUG, "FSI - read from handle %d\n", handle_index);

	/* read operation */
	nb_read =
	    ptfsal_read(myself, opctx, buffer, buffer_size, offset,
			handle_index);
	errsv = errno;
	if (nb_read == -1)
		return fsalstat(posix2fsal_error(errsv), errsv);
	else if (nb_read == 0)
		*p_end_of_file = 1;

	*p_read_amount = nb_read;

	FSI_TRACE(FSI_DEBUG, "Read end=================================");
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

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
fsal_status_t PTFSAL_write(struct pt_fsal_obj_handle *p_file_descriptor,
			   const struct req_op_context *opctx,
			   uint64_t offset,	/* IN */
			   size_t buffer_size,	/* IN */
			   caddr_t buffer,	/* IN */
			   size_t *p_write_amount,	/* OUT */
			   bool *fsal_stable)
{				/* IN/OUT */

	size_t nb_written;
	size_t i_size;
	int errsv = 0;
	int handle_index;	/* FSI handle index */

	FSI_TRACE(FSI_DEBUG, "FSI - PTFSAL write-----------------\n");

	/* sanity checks. */
	if (!buffer || !p_write_amount)
		return fsalstat(ERR_FSAL_FAULT, 0);
	/* get FSI location */
	handle_index = p_file_descriptor->u.file.fd;
	if (fsi_check_handle_index(handle_index) < 0)
		return fsalstat(ERR_FSAL_FAULT, 0);
	FSI_TRACE(FSI_DEBUG, "FSI - write to handle %d\n", handle_index);

  /** @todo: manage size_t to size_t convertion */
	i_size = (size_t) buffer_size;

	*p_write_amount = 0;

	/* write operation */
	nb_written =
	    ptfsal_write(p_file_descriptor, opctx, buffer, buffer_size, offset,
			 handle_index);

	errsv = errno;

	FSI_TRACE(FSI_INFO, "Number of bytes written %ld", nb_written);
	FSI_TRACE(FSI_DEBUG, "The errno %d", errsv);

	if (nb_written > 0) {
		errsv = 0;
	} else {
		FSI_TRACE(FSI_ERR,
			  "Failed to write data, nb_written %ld errno %d",
			  nb_written, errsv);
		LogDebug(COMPONENT_FSAL,
			 "Write operation of size %llu at offset 0. fd=%d, errno=%d.",
			 (unsigned long long)i_size,
			 p_file_descriptor->u.file.fd, errsv);
		errno = errsv;
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	/* set output vars */

	*p_write_amount = (size_t) nb_written;

	FSI_TRACE(FSI_DEBUG,
		  "FSI - END PTFSAL write--------------------------\n");
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

fsal_status_t PTFSAL_close(int p_file_descriptor)
{				/* IN */

	FSI_TRACE(FSI_DEBUG, "FSI - Begin PTFSAL close---------------\n");

	/*
	 * call to close
	 * Change to NFS_CLOSE only if it is NFS_OPEN. The calling
	 * function will ignore other nfs state.
	 */
	int state_rc =
	    CCL_SAFE_UPDATE_HANDLE_NFS_STATE(p_file_descriptor, NFS_CLOSE,
					     NFS_OPEN);

	if (state_rc) {
		FSI_TRACE(FSI_WARNING,
			  "Unexpected state, not updating nfs state");
	}

	/* call ptfsal */
	ptfsal_close(p_file_descriptor);

	FSI_TRACE(FSI_DEBUG, "FSI - End PTFSAL close-----------------\n");
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

unsigned int PTFSAL_GetFileno(int pfile)
{
	return pfile;
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
fsal_status_t PTFSAL_commit(struct pt_fsal_obj_handle *p_file_descriptor,
			    const struct req_op_context *opctx,
			    uint64_t offset, size_t length)
{
	int rc, errsv;

	FSI_TRACE(FSI_DEBUG, "FSI - Begin PTFSAL commit-----------------\n");

	/* sanity checks. */
	if (!p_file_descriptor)
		return fsalstat(ERR_FSAL_FAULT, 0);

	rc = ptfsal_fsync(p_file_descriptor, opctx);

	if (rc) {
		errsv = errno;
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	FSI_TRACE(FSI_DEBUG, "FSI - End PTFSAL commit-----------------\n");
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
