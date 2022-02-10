// SPDX-License-Identifier: LGPL-3.0-or-later
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

#include "config.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <netdb.h>
#include "bsd-base64.h"
#include "client_mgr.h"
#include "fsal.h"
#include "recovery_fs.h"
#include <libgen.h>

static char v4_recov_link[PATH_MAX];

/*
 * If we have a "legacy" fs driver database, we can allow clients to recover
 * using that. In order to handle it though, we must rename the thing and
 * set symlink for it.
 *
 * Unfortunately this is not atomic, but it should be a one-time thing.
 */
static void legacy_fs_db_migrate(void)
{
	int ret;
	struct stat st;

	ret = lstat(v4_recov_link, &st);
	if (!ret && S_ISDIR(st.st_mode)) {
		char pathbuf[PATH_MAX];
		char *dname;

		/* create empty tmpdir in same parent */
		ret = snprintf(pathbuf, sizeof(pathbuf), "%s.XXXXXX",
			       v4_recov_link);

		if (unlikely(ret >= sizeof(pathbuf))) {
			LogCrit(COMPONENT_CLIENTID,
				"Path too long %s.XXXXXX", v4_recov_link);
			return;
		} else if (unlikely(ret < 0)) {
			LogCrit(COMPONENT_CLIENTID,
				"Unexpected return from snprintf %d error %s (%d)",
				ret, strerror(errno), errno);
			return;
		}

		dname = mkdtemp(pathbuf);
		if (!dname) {
			LogEvent(COMPONENT_CLIENTID,
				"Failed to create temp file (%s): %s (%d)",
				pathbuf, strerror(errno), errno);
			return;
		}

		ret = rename(v4_recov_link, dname);
		if (ret != 0) {
			LogEvent(COMPONENT_CLIENTID,
				"Failed to rename v4 recovery dir (%s) to (%s): %s (%d)",
				v4_recov_link, dname, strerror(errno), errno);
			return;
		}

		ret = symlink(basename(dname), v4_recov_link);
		if (ret != 0) {
			LogEvent(COMPONENT_CLIENTID,
				"Failed to set recoverydir symlink at %s: %s (%d)",
				dname, strerror(errno), errno);
			return;
		}
	}
}

static int fs_ng_create_recov_dir(void)
{
	int err;
	char *newdir;
	char host[NI_MAXHOST];

	err = mkdir(nfs_param.nfsv4_param.recov_root, 0700);
	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir (%s): %s (%d)",
			 nfs_param.nfsv4_param.recov_root,
			 strerror(errno), errno);
	}

	err = snprintf(v4_recov_dir, sizeof(v4_recov_dir), "%s/%s",
		       nfs_param.nfsv4_param.recov_root,
		       nfs_param.nfsv4_param.recov_dir);

	if (unlikely(err >= sizeof(v4_recov_dir))) {
		LogCrit(COMPONENT_CLIENTID,
			"Path too long %s/%s",
			nfs_param.nfsv4_param.recov_root,
			nfs_param.nfsv4_param.recov_dir);
		return -EINVAL;
	} else if (unlikely(err < 0)) {
		int error = errno;

		LogCrit(COMPONENT_CLIENTID,
			"Unexpected return from snprintf %d error %s (%d)",
			err, strerror(error), error);
		return -error;
	}

	err = mkdir(v4_recov_dir, 0700);
	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir(%s): %s (%d)",
			 v4_recov_dir, strerror(errno), errno);
	}

	/* Populate link path string */
	if (nfs_param.core_param.clustered) {
		err = snprintf(host, sizeof(host), "node%d", g_nodeid);

		if (unlikely(err >= sizeof(host))) {
			LogCrit(COMPONENT_CLIENTID,
				"node%d too long", g_nodeid);
			return -EINVAL;
		} else if (unlikely(err < 0)) {
			int error = errno;

			LogCrit(COMPONENT_CLIENTID,
				"Unexpected return from snprintf %d error %s (%d)",
				err, strerror(error), error);
			return -error;
		}
	} else {
		err = gethostname(host, sizeof(host));
		if (err) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to gethostname: %s (%d)",
				 strerror(errno), errno);
			return -errno;
		}
	}

	err = snprintf(v4_recov_link, sizeof(v4_recov_link), "%s/%s/%s",
		       nfs_param.nfsv4_param.recov_root,
		       nfs_param.nfsv4_param.recov_dir, host);

	if (unlikely(err >= sizeof(v4_recov_link))) {
		LogCrit(COMPONENT_CLIENTID,
			"Path too long %s/%s/%s",
			nfs_param.nfsv4_param.recov_root,
			nfs_param.nfsv4_param.recov_dir, host);
		return -EINVAL;
	} else if (unlikely(err < 0)) {
		int error = errno;

		LogCrit(COMPONENT_CLIENTID,
			"Unexpected return from snprintf %d error %s (%d)",
			err, strerror(error), error);
		return -error;
	}

	err = snprintf(v4_recov_dir, sizeof(v4_recov_dir), "%s.XXXXXX",
		       v4_recov_link);

	if (unlikely(err >= sizeof(v4_recov_dir))) {
		LogCrit(COMPONENT_CLIENTID,
			"Path too long %s.XXXXXX",
			v4_recov_link);
		return -EINVAL;
	} else if (unlikely(err < 0)) {
		int error = errno;

		LogCrit(COMPONENT_CLIENTID,
			"Unexpected return from snprintf %d error %s (%d)",
			err, strerror(error), error);
		return -error;
	}

	newdir = mkdtemp(v4_recov_dir);
	if (newdir != v4_recov_dir) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir(%s): %s (%d)",
			 v4_recov_dir, strerror(errno), errno);
	}

	legacy_fs_db_migrate();
	return 0;
}

/**
 * @brief Create the client reclaim list from previous database
 *
 * @param[in] dp       Recovery directory
 * @param[in] srcdir   Path to the source directory on failover
 *
 * @return POSIX error codes.
 */
static int fs_ng_read_recov_clids_impl(const char *parent_path,
				    char *clid_str,
				    add_clid_entry_hook add_clid_entry,
				    add_rfh_entry_hook add_rfh_entry)
{
	struct dirent *dentp;
	DIR *dp;
	clid_entry_t *new_ent;
	char *sub_path = NULL;
	char *build_clid = NULL;
	int rc = 0;
	int num = 0;
	char *ptr, *ptr2;
	char temp[10];
	int cid_len, len;
	int segment_len;
	int clid_str_len = clid_str ? strlen(clid_str) : 0;

	dp = opendir(parent_path);
	if (dp == NULL) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to open v4 recovery dir (%s): %s (%d)",
			 parent_path, strerror(errno), errno);
		return -1;
	}

	for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
		/* don't add '.' and '..' entry */
		if (!strcmp(dentp->d_name, ".") || !strcmp(dentp->d_name, ".."))
			continue;

		/* Skip names that start with '\x1' as they are files
		 * representing revoked file handles
		 */
		if (dentp->d_name[0] == '\x1')
			continue;

		num++;

		/* construct the path by appending the subdir for the
		 * next readdir. This recursion keeps reading the
		 * subdirectory until reaching the end.
		 */
		sub_path = gsh_concat_sep(parent_path, '/', dentp->d_name);
		segment_len = strlen(dentp->d_name);

		/* keep building the clientid str by recursively */
		/* reading the directory structure */
		build_clid = gsh_malloc(clid_str_len + segment_len + 1);

		if (clid_str)
			memcpy(build_clid, clid_str, clid_str_len);

		memcpy(build_clid + clid_str_len,
		       dentp->d_name,
		       segment_len + 1);

		rc = fs_ng_read_recov_clids_impl(sub_path,
					      build_clid,
					      add_clid_entry,
					      add_rfh_entry);

		/* after recursion, if the subdir has no non-hidden
		 * directory this is the end of this clientid str. Add
		 * the clientstr to the list.
		 */
		if (rc == 0) {
			/* the clid format is
			 * <IP>-(clid-len:long-form-clid-in-string-form)
			 * make sure this reconstructed string is valid
			 * by comparing clid-len and the actual
			 * long-form-clid length in the string. This is
			 * to prevent getting incompleted strings that
			 * might exist due to program crash.
			 */
			if (strlen(build_clid) >= PATH_MAX) {
				LogEvent(COMPONENT_CLIENTID,
					"invalid clid format: %s, too long",
					build_clid);
				gsh_free(sub_path);
				gsh_free(build_clid);
				continue;
			}
			ptr = strchr(build_clid, '(');
			if (ptr == NULL) {
				LogEvent(COMPONENT_CLIENTID,
					 "invalid clid format: %s",
					 build_clid);
				gsh_free(sub_path);
				gsh_free(build_clid);
				continue;
			}
			ptr2 = strchr(ptr, ':');
			if (ptr2 == NULL) {
				LogEvent(COMPONENT_CLIENTID,
					 "invalid clid format: %s",
					 build_clid);
				gsh_free(sub_path);
				gsh_free(build_clid);
				continue;
			}
			len = ptr2-ptr-1;
			if (len >= 9) {
				LogEvent(COMPONENT_CLIENTID,
					 "invalid clid format: %s",
					 build_clid);
				gsh_free(sub_path);
				gsh_free(build_clid);
				continue;
			}
			memcpy(temp, ptr+1, len+1);
			cid_len = atoi(temp);
			len = strlen(ptr2);
			if ((len == (cid_len+2)) && (ptr2[len-1] == ')')) {
				new_ent = add_clid_entry(build_clid);
				LogDebug(COMPONENT_CLIENTID,
					 "added %s to clid list",
					 new_ent->cl_name);
			}
		}
		gsh_free(build_clid);
		gsh_free(sub_path);
	}

	(void)closedir(dp);

	return num;
}

static void fs_ng_read_recov_clids_recover(add_clid_entry_hook add_clid_entry,
					add_rfh_entry_hook add_rfh_entry)
{
	int rc;

	rc = fs_ng_read_recov_clids_impl(v4_recov_link, NULL,
				      add_clid_entry,
				      add_rfh_entry);
	if (rc == -1) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to read v4 recovery dir (%s)",
			 v4_recov_link);
		return;
	}
}

/**
 * @brief Load clients for recovery, with no lock
 *
 * @param[in] nodeid Node, on takeover
 */
static void fs_ng_read_recov_clids(nfs_grace_start_t *gsp,
				  add_clid_entry_hook add_clid_entry,
				  add_rfh_entry_hook add_rfh_entry)
{
	int rc;
	char path[PATH_MAX];

	if (!gsp) {
		fs_ng_read_recov_clids_recover(add_clid_entry, add_rfh_entry);
		return;
	}

	/*
	 * FIXME: make the rest of this work
	 */
	return;

	switch (gsp->event) {
	case EVENT_TAKE_NODEID:
		rc = snprintf(path, sizeof(path), "%s/%s/node%d",
			      nfs_param.nfsv4_param.recov_root,
			      nfs_param.nfsv4_param.recov_dir, gsp->nodeid);

		if (unlikely(rc >= sizeof(path))) {
			LogCrit(COMPONENT_CLIENTID,
				"Path too long %s/%s/node%d",
				nfs_param.nfsv4_param.recov_root,
				nfs_param.nfsv4_param.recov_dir,
				gsp->nodeid);
		} else if (unlikely(rc < 0)) {
			LogCrit(COMPONENT_CLIENTID,
				"Unexpected return from snprintf %d error %s (%d)",
				rc, strerror(errno), errno);
			return;
		}
		break;
	default:
		LogWarn(COMPONENT_CLIENTID, "Recovery unknown event: %d",
				gsp->event);
		return;
	}

	LogEvent(COMPONENT_CLIENTID, "Recovery for nodeid %d dir (%s)",
		 gsp->nodeid, path);

	rc = fs_ng_read_recov_clids_impl(path, NULL,
				      add_clid_entry,
				      add_rfh_entry);
	if (rc == -1) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to read v4 recovery dir (%s)", path);
		return;
	}
}

static void fs_ng_swap_recov_dir(void)
{
	int ret;
	char old_pathbuf[PATH_MAX];
	char tmp_link[PATH_MAX];
	char *old_path;

	/* save off the old link path so we can do some cleanup afterward */
	old_path = realpath(v4_recov_link, old_pathbuf);

	/* Make a new symlink at a temporary location, pointing to new dir */
	ret = snprintf(tmp_link, sizeof(tmp_link), "%s.tmp", v4_recov_link);

	if (unlikely(ret >= sizeof(tmp_link))) {
		LogCrit(COMPONENT_CLIENTID,
			"Path too long %s.tmp", v4_recov_link);
		return;
	} else if (unlikely(ret < 0)) {
		LogCrit(COMPONENT_CLIENTID,
			"Unexpected return from snprintf %d error %s (%d)",
			ret, strerror(errno), errno);
		return;
	}

	/* unlink old symlink, if any */
	ret = unlink(tmp_link);
	if (ret != 0 && errno != ENOENT) {
		LogEvent(COMPONENT_CLIENTID,
			 "Unable to remove recoverydir symlink: %s (%d)",
			 strerror(errno), errno);
		return;
	}

	/* make a new symlink in a temporary spot */
	ret = symlink(basename(v4_recov_dir), tmp_link);
	if (ret != 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Unable to create recoverydir symlink: %s (%d)",
			 strerror(errno), errno);
		return;
	}

	/* rename tmp link into place */
	ret = rename(tmp_link, v4_recov_link);
	if (ret != 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Unable to rename recoverydir symlink: %s (%d)",
			 strerror(errno), errno);
		return;
	}

	/* now clean up old path, if any */
	if (old_path) {
		fs_clean_old_recov_dir_impl(old_path);
		rmdir(old_path);
	}
}

static struct nfs4_recovery_backend fs_ng_backend = {
	.recovery_init = fs_ng_create_recov_dir,
	.end_grace = fs_ng_swap_recov_dir,
	.recovery_read_clids = fs_ng_read_recov_clids,
	.add_clid = fs_add_clid,
	.rm_clid = fs_rm_clid,
	.add_revoke_fh = fs_add_revoke_fh,
};

void fs_ng_backend_init(struct nfs4_recovery_backend **backend)
{
	*backend = &fs_ng_backend;
}
