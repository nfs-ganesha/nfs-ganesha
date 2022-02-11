// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2016 Red Hat, Inc. and/or its affiliates.
 * Author: Daniel Gryniewicz <dang@redhat.com>
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/* xattrs.c
 * NULL object (file|dir) handle object extended attributes
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include "os/xattr.h"
#include "gsh_list.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "mdcache_int.h"

/**
 * @brief List extended attributes on a file
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to walk
 * @param[in] argcookie	Cookie for caller
 * @param[out] xattrs_tab	Output buffer for xattrs
 * @param[in] xattrs_tabsize	Size of @a xattrs_tab
 * @param[out] p_nb_returned	Number of xattrs returned
 * @param[out] end_of_list	True if reached end of list
 * @return FSAL status
 */
fsal_status_t mdcache_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				     unsigned int argcookie,
				     fsal_xattrent_t *xattrs_tab,
				     unsigned int xattrs_tabsize,
				     unsigned int *p_nb_returned,
				     int *end_of_list)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
		     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->list_ext_attrs(
			handle->sub_handle, argcookie, xattrs_tab,
			xattrs_tabsize, p_nb_returned, end_of_list)
	       );

	return status;
}

/**
 * @brief Get ID of xattr by name
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] name	Name of xattr to look up
 * @param[out] p_id	ID of xattr, if found
 * @return FSAL status
 */
fsal_status_t mdcache_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *name,
					    unsigned int *p_id)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->getextattr_id_by_name(
				handle->sub_handle, name, p_id)
	       );

	return status;
}

/**
 * @brief Get contents of xattr by ID
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] id	ID of xattr to read
 * @param[out] buf	Output buffer
 * @param[in] buf_size	Size of @a buf
 * @param[out] p_output_size	Amount written to buf
 * @return FSAL status
 */
fsal_status_t mdcache_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					     unsigned int id,
					     void *buf,
					     size_t buf_size,
					     size_t *p_output_size)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->getextattr_value_by_id(
				handle->sub_handle, id, buf,
				buf_size, p_output_size)
	       );

	return status;
}

/**
 * @brief Get contents of xattr by name
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] name	Name of xattr to look up
 * @param[out] buf	Output buffer
 * @param[in] buf_size	Size of @a buf
 * @param[out] p_output_size	Amount written to buf
 * @return FSAL status
 */
fsal_status_t mdcache_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					       const char *name,
					       void *buf,
					       size_t buf_size,
					       size_t *p_output_size)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->getextattr_value_by_name(
				handle->sub_handle, name, buf,
				buf_size, p_output_size)
	       );

	return status;
}

/**
 * @brief Set contents of xattr by name
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] name	Name of xattr to set
 * @param[out] buf	Output buffer
 * @param[in] buf_size	Size of @a buf
 * @param[in] create	If true, create xattr
 * @return FSAL status
 */
fsal_status_t mdcache_setextattr_value(struct fsal_obj_handle *obj_hdl,
				       const char *name,
				       void *buf,
				       size_t buf_size,
				       int create)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->setextattr_value(
			handle->sub_handle, name, buf,
			buf_size, create)
	       );

	return status;
}

/**
 * @brief Set contents of xattr by ID
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] id	ID of xattr to set
 * @param[out] buf	Output buffer
 * @param[in] buf_size	Size of @a buf
 * @return FSAL status
 */
fsal_status_t mdcache_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					     unsigned int id,
					     void *buf,
					     size_t buf_size)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->setextattr_value_by_id(
				handle->sub_handle, id, buf,
				buf_size)
	       );

	return status;
}

/**
 * @brief Remove an xattr by ID
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] id	ID of xattr to remove
 * @return FSAL status
 */
fsal_status_t mdcache_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					   unsigned int id)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->remove_extattr_by_id(
			handle->sub_handle, id)
	       );

	return status;
}

/**
 * @brief Remove an xattr by name
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] name	Name of xattr to remove
 * @return FSAL status
 */
fsal_status_t mdcache_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					     const char *name)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->remove_extattr_by_name(
			handle->sub_handle, name)
	       );

	return status;
}

/**
 * @brief Get an Extended Attribute
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] name	Name of attribute
 * @param[out] value	Value of attribute
 * @return FSAL status
 */
fsal_status_t mdcache_getxattrs(struct fsal_obj_handle *obj_hdl,
				xattrkey4 *name, xattrvalue4 *value)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->getxattrs(
			handle->sub_handle, name, value)
	       );

	return status;
}

/**
 * @brief Set an Extended Attribute
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] type	Type of attribute
 * @param[in] name	Name of attribute
 * @param[in] value	Value of attribute
 * @return FSAL status
 */
fsal_status_t mdcache_setxattrs(struct fsal_obj_handle *obj_hdl,
				setxattr_option4 option, xattrkey4 *name,
				xattrvalue4 *value)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->setxattrs(
			handle->sub_handle, option, name, value)
	       );

	return status;
}

/**
 * @brief Remove an Extended Attribute
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] name	Type of attribute
 * @return FSAL status
 */
fsal_status_t mdcache_removexattrs(struct fsal_obj_handle *obj_hdl,
				   xattrkey4 *name)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->removexattrs(
			handle->sub_handle, name)
	       );

	return status;
}

/**
 * @brief List Extended Attributes
 *
 * Pass through to sub-FSAL
 *
 * @param[in] obj_hdl	File to search
 * @param[in] len	Length of names buffer
 * @param[in,out] cookie The cookie for list
 * @param[in,out] verf	cookie verifier
 * @param[out] eof	set if no more extended attributes
 * @param[out] names	list of extended attribute names
 * @return FSAL status
 */
fsal_status_t mdcache_listxattrs(struct fsal_obj_handle *obj_hdl,
				 count4 len, nfs_cookie4 *cookie,
				 bool_t *eof, xattrlist4 *names)
{
	struct mdcache_fsal_obj_handle *handle =
		container_of(obj_hdl, struct mdcache_fsal_obj_handle,
			     obj_handle);
	fsal_status_t status;

	subcall(
		status = handle->sub_handle->obj_ops->listxattrs(
			handle->sub_handle, len, cookie, eof, names)
	       );

	return status;
}
