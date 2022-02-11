// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @addtogroup FSAL
 * @{
 */

/**
 * @file fsal_config.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Initialize configuration parameters
 */

#include "config.h"

#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "fsal.h"
#include "FSAL/fsal_init.h"

/* filesystem info handlers
 * common functions for fsal info methods
 */

bool fsal_supports(struct fsal_staticfsinfo_t *info,
		   fsal_fsinfo_options_t option)
{
	switch (option) {
	case fso_no_trunc:
		return !!info->no_trunc;
	case fso_chown_restricted:
		return !!info->chown_restricted;
	case fso_case_insensitive:
		return !!info->case_insensitive;
	case fso_case_preserving:
		return !!info->case_preserving;
	case fso_link_support:
		return !!info->link_support;
	case fso_symlink_support:
		return !!info->symlink_support;
	case fso_lock_support:
		return !!info->lock_support;
	case fso_lock_support_async_block:
		return !!info->lock_support_async_block;
	case fso_named_attr:
		return !!info->named_attr;
	case fso_unique_handles:
		return !!info->unique_handles;
	case fso_cansettime:
		return !!info->cansettime;
	case fso_homogenous:
		return !!info->homogenous;
	case fso_auth_exportpath_xdev:
		return !!info->auth_exportpath_xdev;
	case fso_delegations_r:
		return !!(info->delegations & FSAL_OPTION_FILE_READ_DELEG);
	case fso_delegations_w:
		return !!(info->delegations & FSAL_OPTION_FILE_WRITE_DELEG);
	case fso_pnfs_mds_supported:
		return !!info->pnfs_mds;
	case fso_pnfs_ds_supported:
		return !!info->pnfs_ds;
	case fso_grace_method:
		return !!info->fsal_grace;
	case fso_link_supports_permission_checks:
		return !!info->link_supports_permission_checks;
	case fso_rename_changes_key:
		return !!info->rename_changes_key;
	case fso_compute_readdir_cookie:
		return !!info->compute_readdir_cookie;
	case fso_whence_is_name:
		return !!info->whence_is_name;
	case fso_readdir_plus:
		return !!info->readdir_plus;
	case fso_compliant_eof_behavior:
		return !!info->compliant_eof_behavior;
	case fso_xattr_support:
		return !!info->xattr_support;
	default:
		return false;	/* whatever I don't know about,
				 * you can't do
				 */
	}
}

uint64_t fsal_maxfilesize(struct fsal_staticfsinfo_t *info)
{
	return info->maxfilesize;
}

uint32_t fsal_maxlink(struct fsal_staticfsinfo_t *info)
{
	return info->maxlink;
}

uint32_t fsal_maxnamelen(struct fsal_staticfsinfo_t *info)
{
	return info->maxnamelen;
}

uint32_t fsal_maxpathlen(struct fsal_staticfsinfo_t *info)
{
	return info->maxpathlen;
}

fsal_aclsupp_t fsal_acl_support(struct fsal_staticfsinfo_t *info)
{
	return info->acl_support;
}

attrmask_t fsal_supported_attrs(struct fsal_staticfsinfo_t *info)
{
	return info->supported_attrs;
}

uint32_t fsal_maxread(struct fsal_staticfsinfo_t *info)
{
	return info->maxread;
}

uint32_t fsal_maxwrite(struct fsal_staticfsinfo_t *info)
{
	return info->maxwrite;
}

uint32_t fsal_umask(struct fsal_staticfsinfo_t *info)
{
	return info->umask;
}
int32_t fsal_expiretimeparent(struct fsal_staticfsinfo_t *info)
{
	return info->expire_time_parent;
}

/** @} */
