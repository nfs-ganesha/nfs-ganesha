/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2012, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsal_symlinks.c
 * Description: FSAL symlink operations implementation
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
#include "pt_methods.h"
#include <string.h>
#include <unistd.h>

/**
 * FSAL_readlink:
 * Read the content of a symbolic link.
 *
 * \param dir_hdl (input):
 *        Handle of the link to be read.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param p_link_content (output):
 *        Pointer to an fsal path structure where
 *        the link content is to be stored..
 * \param link_len (input/output):
 *        In pointer to len of content buff.
 .        Out actual len of content.
 * \param link_attributes (optionnal input/output):
 *        The post operation attributes of the symlink link.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t PTFSAL_readlink(struct fsal_obj_handle *dir_hdl,	/* IN */
			      const struct req_op_context *p_context,	/* IN */
			      char *p_link_content,	/* OUT */
			      size_t *link_len,	/* IN/OUT */
			      struct attrlist *p_link_attributes)
{				/* IN/OUT */

	fsal_status_t status;
	struct pt_fsal_obj_handle *pt_hdl;
	char link_content_out[PATH_MAX];

	/* sanity checks.
	 * note : link_attributes is optional.
	 */
	if (!dir_hdl || !p_context || !p_link_content)
		return fsalstat(ERR_FSAL_FAULT, 0);

	pt_hdl = container_of(dir_hdl, struct pt_fsal_obj_handle, obj_handle);

	memset(link_content_out, 0, sizeof(link_content_out));

	/* Read the link on the filesystem */
	status = fsal_readlink_by_handle(p_context, p_context->fsal_export,
				pt_hdl->handle, p_link_content, *link_len);

	if (FSAL_IS_ERROR(status))
		return status;

	/* retrieves object attributes, if asked */

	if (p_link_attributes) {

		status = PTFSAL_getattrs(p_context->fsal_export, p_context,
					 pt_hdl->handle, p_link_attributes);

		/* On error, we set a flag in the returned attributes */

		if (FSAL_IS_ERROR(status)) {
			FSAL_CLEAR_MASK(p_link_attributes->mask);
			FSAL_SET_MASK(p_link_attributes->mask, ATTR_RDATTR_ERR);
		}

	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
