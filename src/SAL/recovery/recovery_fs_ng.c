#define _GNU_SOURCE
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

static char v4_recov_link[sizeof(NFS_V4_RECOV_ROOT) +
			  sizeof(NFS_V4_RECOV_DIR) +
			  NI_MAXHOST + 1];

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
		snprintf(pathbuf, sizeof(pathbuf), "%s.XXXXXX", v4_recov_link);

		dname = mkdtemp(pathbuf);
		if (!dname) {
			LogEvent(COMPONENT_CLIENTID,
				"Failed to create temp file (%s): %s",
				pathbuf, strerror(errno));
			return;
		}

		ret = rename(v4_recov_link, dname);
		if (ret != 0) {
			LogEvent(COMPONENT_CLIENTID,
				"Failed to rename v4 recovery dir (%s) to (%s): %s",
				v4_recov_link, dname, strerror(errno));
			return;
		}

		ret = symlink(basename(dname), v4_recov_link);
		if (ret != 0) {
			LogEvent(COMPONENT_CLIENTID,
				"Failed to set recoverydir symlink at %s: %s",
				dname, strerror(errno));
			return;
		}
	}
}

static int fs_ng_create_recov_dir(void)
{
	int err;
	char *newdir;
	char host[NI_MAXHOST];

	err = mkdir(NFS_V4_RECOV_ROOT, 0700);
	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir (%s): %s",
			 NFS_V4_RECOV_ROOT, strerror(errno));
	}

	snprintf(v4_recov_dir, sizeof(v4_recov_dir), "%s/%s", NFS_V4_RECOV_ROOT,
		 NFS_V4_RECOV_DIR);
	err = mkdir(v4_recov_dir, 0700);
	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir(%s): %s",
			 v4_recov_dir, strerror(errno));
	}

	/* Populate link path string */
	if (nfs_param.core_param.clustered) {
		snprintf(host, sizeof(host), "node%d", g_nodeid);
	} else {
		err = gethostname(host, sizeof(host));
		if (err) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to gethostname: %s",
				 strerror(errno));
			return -errno;
		}
	}

	snprintf(v4_recov_link, sizeof(v4_recov_link), "%s/%s/%s",
		 NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR, host);

	snprintf(v4_recov_dir, sizeof(v4_recov_dir), "%s.XXXXXX",
		 v4_recov_link);

	newdir = mkdtemp(v4_recov_dir);
	if (newdir != v4_recov_dir) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir(%s): %s",
			 v4_recov_dir, strerror(errno));
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
	int total_len;
	int total_clid_len;

	dp = opendir(parent_path);
	if (dp == NULL) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to open v4 recovery dir (%s): %s",
			 parent_path, strerror(errno));
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
		segment_len = strlen(dentp->d_name);
		total_len = segment_len + 2 + strlen(parent_path);
		sub_path = gsh_malloc(total_len);

		memset(sub_path, 0, total_len);

		strcpy(sub_path, parent_path);
		strcat(sub_path, "/");
		strncat(sub_path, dentp->d_name, segment_len);
		/* keep building the clientid str by recursively */
		/* reading the directory structure */
		total_clid_len = segment_len + 1;
		if (clid_str)
			total_clid_len += strlen(clid_str);
		build_clid = gsh_calloc(1, total_clid_len);
		if (clid_str)
			strcpy(build_clid, clid_str);
		strncat(build_clid, dentp->d_name, segment_len);

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
			strncpy(temp, ptr+1, len);
			temp[len] = 0;
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
		snprintf(path, sizeof(path), "%s/%s/node%d",
			 NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR,
			 gsp->nodeid);
		break;
	default:
		LogWarn(COMPONENT_STATE, "Recovery unknown event: %d",
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
	snprintf(tmp_link, PATH_MAX, "%s.tmp", v4_recov_link);

	/* unlink old symlink, if any */
	ret = unlink(tmp_link);
	if (ret != 0 && errno != ENOENT) {
		LogEvent(COMPONENT_CLIENTID,
			 "Unable to remove recoverydir symlink: %s",
			 strerror(errno));
		return;
	}

	/* make a new symlink in a temporary spot */
	ret = symlink(basename(v4_recov_dir), tmp_link);
	if (ret != 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Unable to create recoverydir symlink: %s",
			 strerror(errno));
		return;
	}

	/* rename tmp link into place */
	ret = rename(tmp_link, v4_recov_link);
	if (ret != 0) {
		LogEvent(COMPONENT_CLIENTID,
			 "Unable to rename recoverydir symlink: %s",
			 strerror(errno));
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
