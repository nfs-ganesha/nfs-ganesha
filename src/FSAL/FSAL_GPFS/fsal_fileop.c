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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/**
 * \file    fsal_fileop.c
 * \date    $Date: 2006/01/17 14:20:07 $
 * \brief   Files operations.
 *
 */
#include "config.h"

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"
#include "FSAL/access_check.h"

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
fsal_status_t GPFSFSAL_open(struct fsal_obj_handle *obj_hdl,	/* IN */
			    const struct req_op_context *p_context,	/* IN */
			    fsal_openflags_t openflags,	/* IN */
			    int *file_desc,	/* IN/OUT */
			    struct attrlist *p_file_attributes, /* IN/OUT */
			    bool reopen) /* IN */
{

	int rc;
	fsal_status_t status;
	int posix_flags = 0;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_filesystem *gpfs_fs;

	/* sanity checks.
	 * note : file_attributes is optional.
	 */
	if (!obj_hdl || !file_desc)
		return fsalstat(ERR_FSAL_FAULT, 0);

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = obj_hdl->fs->private;

	/* convert fsal open flags to posix open flags */
	rc = fsal2posix_openflags(openflags, &posix_flags);
	/* flags conflicts. */
	if (rc) {
		LogWarn(COMPONENT_FSAL, "Invalid/conflicting flags : %#X",
			openflags);
		return fsalstat(rc, 0);
	}

	status = fsal_internal_handle2fd(gpfs_fs->root_fd, myself->handle,
					 file_desc, posix_flags, reopen);

	if (FSAL_IS_ERROR(status)) {
		*file_desc = 0;
		return status;
	}

	/* output attributes */
	if (p_file_attributes) {

		p_file_attributes->mask = GPFS_SUPPORTED_ATTRIBUTES;
		status = GPFSFSAL_getattrs(p_context->fsal_export,
					   gpfs_fs,
					   p_context, myself->handle,
					   p_file_attributes);
		if (FSAL_IS_ERROR(status)) {
			*file_desc = 0;
			close(*file_desc);
			return status;
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
fsal_status_t GPFSFSAL_read(int fd,	/* IN */
			    uint64_t offset,	/* IN */
			    size_t buffer_size,	/* IN */
			    caddr_t buffer,	/* OUT */
			    size_t *p_read_amount,	/* OUT */
			    bool *p_end_of_file)   /* OUT */
{
	struct read_arg rarg;
	ssize_t nb_read;
	int errsv = 0;

	/* sanity checks. */

	if (!buffer || !p_read_amount || !p_end_of_file)
		return fsalstat(ERR_FSAL_FAULT, 0);

	rarg.mountdirfd = fd;
	rarg.fd = fd;
	rarg.bufP = buffer;
	rarg.offset = offset;
	rarg.length = buffer_size;
	rarg.options = 0;

	/* read operation */

	nb_read = gpfs_ganesha(OPENHANDLE_READ_BY_FD, &rarg);
	errsv = errno;

	if (nb_read == -1) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}
	else if (nb_read == 0 || nb_read < buffer_size)
		*p_end_of_file = TRUE;

	*p_read_amount = nb_read;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_write:
 * Perform a write operation on an opened file.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param buffer_size (input):
 *        Amount (in bytes) of data to be written.
 * \param buffer (output):
 *        Address where the data is in memory.
 * \param write_amount (output):
 *        Pointer to the amount of data (in bytes) that have been written
 *        during this call.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - Another error code if an error occured during this call.
 */
fsal_status_t GPFSFSAL_write(int fd,	/* IN */
			     uint64_t offset,	/* IN */
			     size_t buffer_size,	/* IN */
			     caddr_t buffer,	/* IN */
			     size_t *p_write_amount,	/* OUT */
			     bool *fsal_stable,	/* IN/OUT */
			     const struct req_op_context *p_context)
{
	struct write_arg warg;
	ssize_t nb_write;
	int errsv = 0;
	uint32_t stability_got;

	/* sanity checks. */

	if (!buffer || !p_write_amount)
		return fsalstat(ERR_FSAL_FAULT, 0);

	warg.mountdirfd = fd;
	warg.fd = fd;
	warg.bufP = buffer;
	warg.offset = offset;
	warg.length = buffer_size;
	warg.stability_wanted = *fsal_stable;
	warg.stability_got = &stability_got;
	warg.options = 0;
	/* read operation */

	fsal_set_credentials(p_context->creds);

	nb_write = gpfs_ganesha(OPENHANDLE_WRITE_BY_FD, &warg);
	errsv = errno;

	fsal_restore_ganesha_credentials();

	if (nb_write == -1) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	*p_write_amount = nb_write;
	*fsal_stable = (stability_got) ? true : false;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_clear:
 * Perform a write operation on an opened file.
 *
 * \param file_descriptor (input):
 *        The file descriptor returned by FSAL_open.
 * \param buffer_size (input):
 *        Amount (in bytes) of data to be written.
 * \param buffer (output):
 *        Address where the data is in memory.
 * \param write_amount (output):
 *        Pointer to the amount of data (in bytes) that have been written
 *        during this call.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - Another error code if an error occured during this call.
 */
fsal_status_t GPFSFSAL_clear(int fd,	/* IN */
			     uint64_t offset,	/* IN */
			     size_t buffer_size,	/* IN */
			     caddr_t buffer,	/* IN */
			     size_t *p_write_amount,	/* OUT */
			     bool *fsal_stable,	/* IN/OUT */
			     const struct req_op_context *p_context,
			     bool allocate)
{
	struct write_arg warg;
	ssize_t nb_write;
	int errsv = 0;

	warg.mountdirfd = fd;
	warg.fd = fd;
	warg.bufP = buffer;
	warg.offset = offset;
	warg.length = buffer_size;
	warg.stability_wanted = *fsal_stable;
	warg.stability_got = (uint32_t *) fsal_stable;
	if (allocate)
		warg.options = IO_ALLOCATE;
	else
		warg.options = 0;

	/* read operation */

	fsal_set_credentials(p_context->creds);

	nb_write = gpfs_ganesha(OPENHANDLE_CLEAR_BY_FD, &warg);
	errsv = errno;

	fsal_restore_ganesha_credentials();

	if (nb_write == -1) {
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	*p_write_amount = buffer_size;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
