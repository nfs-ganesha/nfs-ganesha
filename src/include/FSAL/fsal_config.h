/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
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
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/*
 * configuration structure management functions
 */

bool fsal_supports(struct fsal_staticfsinfo_t *info,
		   fsal_fsinfo_options_t option);

uint64_t fsal_maxfilesize(struct fsal_staticfsinfo_t *info);

uint32_t fsal_maxlink(struct fsal_staticfsinfo_t *info);

uint32_t fsal_maxnamelen(struct fsal_staticfsinfo_t *info);

uint32_t fsal_maxpathlen(struct fsal_staticfsinfo_t *info);

fsal_aclsupp_t fsal_acl_support(struct fsal_staticfsinfo_t *info);

attrmask_t fsal_supported_attrs(struct fsal_staticfsinfo_t *info);

uint32_t fsal_maxread(struct fsal_staticfsinfo_t *info);

uint32_t fsal_maxwrite(struct fsal_staticfsinfo_t *info);

uint32_t fsal_umask(struct fsal_staticfsinfo_t *info);

int32_t fsal_expiretimeparent(struct fsal_staticfsinfo_t *info);
