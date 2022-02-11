/* SPDX-License-Identifier: unknown... */
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
