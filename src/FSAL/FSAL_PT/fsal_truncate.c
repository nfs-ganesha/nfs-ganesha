/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_truncate.c
 * Description: FSAL truncate operations implementation
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

#include <unistd.h>
#include <sys/types.h>

#include "pt_ganesha.h"
#include "pt_methods.h"

/**
 * FSAL_truncate:
 * Modify the data length of a regular file.
 *
 * \param export (input):
 *        For use of mount fd
 * \param p_filehandle (input):
 *        Handle of the file is to be truncated.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param length (input):
 *        The new data length for the file.
 * \param p_object_attributes (optionnal input/output):
 *        The post operation attributes of the file.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occurred.
 */
fsal_status_t PTFSAL_truncate(struct fsal_export *export,	/* IN */
			      struct pt_fsal_obj_handle *p_filehandle,	/* IN */
			      const struct req_op_context *p_context,	/* IN */
			      size_t length,	/* IN */
			      struct attrlist *p_object_attributes)
{				/* IN/OUT */

	int rc = 0, errsv;
	int fd = -1;
	fsal_status_t st;

	FSI_TRACE(FSI_DEBUG, "Truncate called, length=%ld", length);
	/* sanity checks.
	 * note : object_attributes is optional.
	 */
	if (!p_filehandle || !p_context || !export)
		return fsalstat(ERR_FSAL_FAULT, 0);

	ptfsal_print_handle(p_filehandle->handle->data.handle.f_handle);

	/* Check to see if we already have fd */
	if (p_filehandle && p_filehandle->u.file.fd > 0) {
		fd = p_filehandle->u.file.fd;
		FSI_TRACE(FSI_DEBUG,
			  "Truncating with fd=%d, truncate length=%ld", fd,
			  length);
		rc = ptfsal_ftruncate(p_context, export, fd, length);
		errsv = errno;
	}

	/* either the fd passed in was 0, or invalid */
	if (rc || fd == -1) {
		/* Get an fd since we dont have one */
		st = fsal_internal_handle2fd(p_context, p_filehandle, &fd,
					     O_RDWR);

		if (FSAL_IS_ERROR(st))
			return st;

		/* Executes the PT truncate operation */
		FSI_TRACE(FSI_DEBUG,
			  "Truncating with POSIX truncate fd=%d, truncate length=%ld",
			  fd, length);
		rc = ptfsal_ftruncate(p_context, export, fd, length);
		errsv = errno;

		/* Before checking for truncate error, close the fd we opened */
		PTFSAL_close(fd);

		/* Now check ftruncate and convert return code */
		if (rc) {
			if (errsv == ENOENT)
				return fsalstat(ERR_FSAL_STALE, errsv);
			else
				return fsalstat(posix2fsal_error(errsv), errsv);
		}
	}

	/* Optionally retrieve attributes */
	if (p_object_attributes) {

		fsal_status_t st;

		st = PTFSAL_getattrs(export, p_context, p_filehandle->handle,
				     p_object_attributes);

		if (FSAL_IS_ERROR(st)) {
			FSAL_CLEAR_MASK(p_object_attributes->mask);
			FSAL_SET_MASK(p_object_attributes->mask,
				      ATTR_RDATTR_ERR);
		}

	}

	/* No error occurred */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
