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

#ifndef _RECOVERY_FS_H
#define _RECOVERY_FS_H
/*
 * Select bits from recovery_fs.c that we can reuse elsewhere
 */

extern char v4_recov_dir[PATH_MAX];

void fs_add_clid(nfs_client_id_t *clientid);
void fs_rm_clid(nfs_client_id_t *clientid);
void fs_add_revoke_fh(nfs_client_id_t *delr_clid, nfs_fh4 *delr_handle);
void fs_clean_old_recov_dir_impl(char *parent_path);
#endif	/* _RECOVERY_FS_H */
