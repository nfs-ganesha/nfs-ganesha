/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat  Inc., 2013
 * Author: Anand Subramanian anands@redhat.com
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
 * @file main.c
 * @author Anand Subramanian <anands@redhat.com>
 *
 * @author Shyamsundar R     <srangana@redhat.com>
 *
 * @brief Module core functions for FSAL_GLUSTER functionality, init etc.
 * 
 */

#include "fsal.h"
#include "FSAL/fsal_init.h"
#include "gluster_internal.h"

/* GLUSTERFS FSAL module private storage
 */

const char glfsal_name[] = "GLUSTER";

/* filesystem info for GLUSTERFS */
static struct fsal_staticfsinfo_t default_gluster_info = {
	.maxfilesize = UINT64_MAX,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = true,
	.case_insensitive = false,
	.case_preserving = true,
	.link_support = true,
	.symlink_support = true,
	.lock_support = true,
	.lock_support_owner = false,
	.lock_support_async_block = false,
	.named_attr = true,
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = FSAL_ACLSUPPORT_ALLOW | FSAL_ACLSUPPORT_DENY,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = GLUSTERFS_SUPPORTED_ATTRIBUTES,
	.maxread = 0,
	.maxwrite = 0,
	.umask = 0,
	.auth_exportpath_xdev = false,
	.xattr_access_rights = 0400,	/* root=RW, owner=R */
};

static struct glusterfs_fsal_module *glfsal_module = NULL;

/* Module methods
 */

MODULE_INIT void glusterfs_init(void)
{
	/* register_fsal seems to expect zeroed memory. */
	glfsal_module = gsh_calloc(1, sizeof(struct glusterfs_fsal_module));
	if (glfsal_module == NULL) {
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate memory for Gluster FSAL module.");
		return;
	}

	if (register_fsal(&glfsal_module->fsal, glfsal_name, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_GLUSTER) != 0) {
		gsh_free(glfsal_module);
		LogCrit(COMPONENT_FSAL,
			"Gluster FSAL module failed to register.");
	}

	/* set up module operations */
	glfsal_module->fsal.ops->create_export = glusterfs_create_export;

	/* setup global handle internals */
	glfsal_module->fs_info = default_gluster_info;

	LogDebug(COMPONENT_FSAL, "FSAL Gluster initialized");
}

MODULE_FINI void glusterfs_unload(void)
{
	if (unregister_fsal(&glfsal_module->fsal) != 0) {
		LogCrit(COMPONENT_FSAL,
			"FSAL Gluster unable to unload.  Dying ...");
		abort();
	}

	gsh_free(glfsal_module);
	glfsal_module = NULL;

	LogDebug(COMPONENT_FSAL, "FSAL Gluster unloaded");
}
