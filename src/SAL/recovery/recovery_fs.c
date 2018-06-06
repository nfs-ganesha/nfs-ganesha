#include "config.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>
#include "bsd-base64.h"
#include "client_mgr.h"
#include "fsal.h"
#include "recovery_fs.h"

#define NFS_V4_OLD_DIR "v4old"

char v4_recov_dir[PATH_MAX];
char v4_old_dir[PATH_MAX];

/**
 * @brief convert clientid opaque bytes as a hex string for mkdir purpose.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     value  The bytes to display
 * @param[in]     len    The number of bytes to display
 *
 * @return the bytes remaining in the buffer.
 *
 */
static int fs_convert_opaque_value_max_for_dir(struct display_buffer *dspbuf,
					       void *value,
					       int len,
					       int max)
{
	unsigned int i = 0;
	int          b_left = display_start(dspbuf);
	int          cpy = len;

	if (b_left <= 0)
		return 0;

	/* Check that the length is ok
	 * If the value is empty, display EMPTY value. */
	if (len <= 0 || len > max)
		return 0;

	/* If the value is NULL, display NULL value. */
	if (value == NULL)
		return 0;

	/* Determine if the value is entirely printable characters, */
	/* and it contains no slash character (reserved for filename) */
	for (i = 0; i < len; i++)
		if ((!isprint(((char *)value)[i])) ||
		    (((char *)value)[i] == '/'))
			break;

	if (i == len) {
		/* Entirely printable character, so we will just copy the
		 * characters into the buffer (to the extent there is room
		 * for them).
		 */
		b_left = display_len_cat(dspbuf, value, cpy);
	} else {
		b_left = display_opaque_bytes(dspbuf, value, cpy);
	}

	if (b_left <= 0)
		return 0;

	return b_left;
}

/**
 * @brief generate a name that identifies this client
 *
 * This name will be used to know that a client was talking to the
 * server before a restart so that it will be allowed to do reclaims
 * during grace period.
 *
 * @param[in] clientid Client record
 */
static void fs_create_clid_name(nfs_client_id_t *clientid)
{
	nfs_client_record_t *cl_rec = clientid->cid_client_record;
	const char *str_client_addr = "(unknown)";
	char cidstr[PATH_MAX] = { 0, };
	struct display_buffer dspbuf = {sizeof(cidstr), cidstr, cidstr};
	char cidstr_len[20];
	int total_len;

	/* get the caller's IP addr */
	if (clientid->gsh_client != NULL)
		str_client_addr = clientid->gsh_client->hostaddr_str;

	if (fs_convert_opaque_value_max_for_dir(&dspbuf,
					     cl_rec->cr_client_val,
					     cl_rec->cr_client_val_len,
					     PATH_MAX) > 0) {
		/* fs_convert_opaque_value_max_for_dir does not prefix
		 * the "(<length>:". So we need to do it here */
		snprintf(cidstr_len, sizeof(cidstr_len), "%zd", strlen(cidstr));
		total_len = strlen(cidstr) + strlen(str_client_addr) + 5 +
			    strlen(cidstr_len);
		/* hold both long form clientid and IP */
		clientid->cid_recov_tag = gsh_malloc(total_len);

		(void) snprintf(clientid->cid_recov_tag, total_len,
				"%s-(%s:%s)",
				str_client_addr, cidstr_len, cidstr);
	}

	LogDebug(COMPONENT_CLIENTID, "Created client name [%s]",
		 clientid->cid_recov_tag);
}

int fs_create_recov_dir(void)
{
	int err;

	err = mkdir(NFS_V4_RECOV_ROOT, 0755);
	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir (%s), errno=%d",
			 NFS_V4_RECOV_ROOT, errno);
	}

	snprintf(v4_recov_dir, sizeof(v4_recov_dir), "%s/%s", NFS_V4_RECOV_ROOT,
		 NFS_V4_RECOV_DIR);
	err = mkdir(v4_recov_dir, 0755);
	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir(%s), errno=%d",
			 v4_recov_dir, errno);
	}

	snprintf(v4_old_dir, sizeof(v4_old_dir), "%s/%s", NFS_V4_RECOV_ROOT,
		 NFS_V4_OLD_DIR);
	err = mkdir(v4_old_dir, 0755);
	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir(%s), errno=%d",
			 v4_old_dir, errno);
	}
	if (nfs_param.core_param.clustered) {
		snprintf(v4_recov_dir, sizeof(v4_recov_dir), "%s/%s/node%d",
			 NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR, g_nodeid);

		err = mkdir(v4_recov_dir, 0755);
		if (err == -1 && errno != EEXIST) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to create v4 recovery dir(%s), errno=%d",
				 v4_recov_dir, errno);
		}

		snprintf(v4_old_dir, sizeof(v4_old_dir), "%s/%s/node%d",
			 NFS_V4_RECOV_ROOT, NFS_V4_OLD_DIR, g_nodeid);

		err = mkdir(v4_old_dir, 0755);
		if (err == -1 && errno != EEXIST) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to create v4 recovery dir(%s), errno=%d",
				 v4_old_dir, errno);
		}
	}
	return 0;
}

void fs_add_clid(nfs_client_id_t *clientid)
{
	int err = 0;
	char path[PATH_MAX] = {0}, segment[NAME_MAX + 1] = {0};
	int length, position = 0;

	fs_create_clid_name(clientid);

	/* break clientid down if it is greater than max dir name */
	/* and create a directory hierarchy to represent the clientid. */
	snprintf(path, sizeof(path), "%s", v4_recov_dir);

	length = strlen(clientid->cid_recov_tag);
	while (position < length) {
		/* if the (remaining) clientid is shorter than 255 */
		/* create the last level of dir and break out */
		int len = strlen(&clientid->cid_recov_tag[position]);

		if (len <= NAME_MAX) {
			strcat(path, "/");
			strncat(path, &clientid->cid_recov_tag[position], len);
			err = mkdir(path, 0700);
			break;
		}
		/* if (remaining) clientid is longer than 255, */
		/* get the next 255 bytes and create a subdir */
		strncpy(segment, &clientid->cid_recov_tag[position], NAME_MAX);
		strcat(path, "/");
		strncat(path, segment, NAME_MAX);
		err = mkdir(path, 0700);
		if (err == -1 && errno != EEXIST)
			break;
		position += NAME_MAX;
	}

	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create client in recovery dir (%s), errno=%d",
			 path, errno);
	} else {
		LogDebug(COMPONENT_CLIENTID, "Created client dir [%s]", path);
	}
}

/**
 * @brief Remove the revoked file handles created under a specific
 * client-id path on the stable storage.
 *
 * @param[in] path Path of the client-id on the stable storage.
 */
static void fs_rm_revoked_handles(char *path)
{
	DIR *dp;
	struct dirent *dentp;
	char del_path[PATH_MAX];

	dp = opendir(path);
	if (dp == NULL) {
		LogEvent(COMPONENT_CLIENTID, "opendir %s failed errno=%d",
			path, errno);
		return;
	}
	for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
		if (!strcmp(dentp->d_name, ".") ||
				!strcmp(dentp->d_name, "..") ||
				dentp->d_name[0] != '\x1') {
			continue;
		}

		snprintf(del_path, sizeof(del_path), "%s/%s",
			 path, dentp->d_name);

		if (unlink(del_path) < 0) {
			LogEvent(COMPONENT_CLIENTID,
					"unlink of %s failed errno: %d",
					del_path,
					errno);
		}
	}
	(void)closedir(dp);
}

static void fs_rm_clid_impl(char *recov_dir, char *parent_path, int position)
{
	int err;
	char *path;
	char *segment;
	int len, segment_len;
	int total_len;

	if (recov_dir == NULL)
		return;

	len = strlen(recov_dir);
	if (position == len) {
		/* We are at the tail directory of the clid,
		* remove revoked handles, if any.
		*/
		fs_rm_revoked_handles(parent_path);
		return;
	}
	segment = gsh_malloc(NAME_MAX+1);

	memset(segment, 0, NAME_MAX+1);
	strncpy(segment, &recov_dir[position], NAME_MAX);
	segment_len = strlen(segment);

	/* allocate enough memory for the new part of the string */
	/* which is parent path + '/' + new segment */
	total_len = strlen(parent_path) + segment_len + 2;
	path = gsh_malloc(total_len);

	memset(path, 0, total_len);
	(void) snprintf(path, total_len, "%s/%s",
			parent_path, segment);
	/* free setment as it has no use now */
	gsh_free(segment);

	/* recursively remove the directory hirerchy which represent the
	 *clientid
	 */
	fs_rm_clid_impl(recov_dir, path, position + segment_len);

	err = rmdir(path);
	if (err == -1) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to remove client recovery dir (%s), errno=%d",
			 path, errno);
	} else {
		LogDebug(COMPONENT_CLIENTID, "Removed client dir [%s]", path);
	}
	gsh_free(path);
}

void fs_rm_clid(nfs_client_id_t *clientid)
{
	char *recov_tag = clientid->cid_recov_tag;

	clientid->cid_recov_tag = NULL;
	fs_rm_clid_impl(recov_tag, v4_recov_dir, 0);
	gsh_free(recov_tag);
}

/**
 * @brief Copy and Populate revoked delegations for this client.
 *
 * Even after delegation revoke, it is possible for the client to
 * contiue its leas and other operatoins. Sever saves revoked delegations
 * in the memory so client will not be granted same delegation with
 * DELEG_CUR ; but it is possible that the server might reboot and has
 * no record of the delegatin. This list helps to reject delegations
 * client is obtaining through DELEG_PREV.
 *
 * @param[in] clientid Clientid that is being created.
 * @param[in] path Path of the directory structure.
 * @param[in] Target dir to copy.
 * @param[in] del Delete after populating
 */
static void fs_cp_pop_revoked_delegs(clid_entry_t *clid_ent,
				     char *path,
				     char *tgtdir,
				     bool del,
				     add_rfh_entry_hook add_rfh_entry)
{
	struct dirent *dentp;
	DIR *dp;
	rdel_fh_t *new_ent;

	/* Read the contents from recov dir of this clientid. */
	dp = opendir(path);
	if (dp == NULL) {
		LogEvent(COMPONENT_CLIENTID, "opendir %s failed errno=%d",
			path, errno);
		return;
	}

	for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
		if (!strcmp(dentp->d_name, ".") || !strcmp(dentp->d_name, ".."))
			continue;
		/* All the revoked filehandles stored with \x1 prefix */
		if (dentp->d_name[0] != '\x1') {
			/* Something wrong; it should not happen */
			LogMidDebug(COMPONENT_CLIENTID,
				"%s showed up along with revoked FHs. Skipping",
				dentp->d_name);
			continue;
		}

		if (tgtdir) {
			char lopath[PATH_MAX];
			int fd;

			snprintf(lopath, sizeof(lopath), "%s/", tgtdir);
			strncat(lopath, dentp->d_name, strlen(dentp->d_name));
			fd = creat(lopath, 0700);
			if (fd < 0) {
				LogEvent(COMPONENT_CLIENTID,
					"Failed to copy revoked handle file %s to %s errno:%d\n",
				dentp->d_name, tgtdir, errno);
			} else {
				close(fd);
			}
		}

		/* Ignore the beginning \x1 and copy the rest (file handle) */
		new_ent = add_rfh_entry(clid_ent, dentp->d_name + 1);

		LogFullDebug(COMPONENT_CLIENTID,
			"revoked handle: %s",
			new_ent->rdfh_handle_str);

		/* Since the handle is loaded into memory, go ahead and
		 * delete it from the stable storage.
		 */
		if (del) {
			char del_path[PATH_MAX];

			snprintf(del_path, sizeof(del_path), "%s/%s",
				 path, dentp->d_name);

			if (unlink(del_path) < 0) {
				LogEvent(COMPONENT_CLIENTID,
						"unlink of %s failed errno: %d",
						del_path,
						errno);
			}
		}
	}

	(void)closedir(dp);
}

/**
 * @brief Create the client reclaim list
 *
 * When not doing a take over, first open the old state dir and read
 * in those entries.  The reason for the two directories is in case of
 * a reboot/restart during grace period.  Next, read in entries from
 * the recovery directory and then move them into the old state
 * directory.  if called due to a take over, nodeid will be nonzero.
 * in this case, add that node's clientids to the existing list.  Then
 * move those entries into the old state directory.
 *
 * @param[in] dp       Recovery directory
 * @param[in] srcdir   Path to the source directory on failover
 * @param[in] takeover Whether this is a takeover.
 *
 * @return POSIX error codes.
 */
static int fs_read_recov_clids_impl(const char *parent_path,
				    char *clid_str,
				    char *tgtdir,
				    int takeover,
				    add_clid_entry_hook add_clid_entry,
				    add_rfh_entry_hook add_rfh_entry)
{
	struct dirent *dentp;
	DIR *dp;
	clid_entry_t *new_ent;
	char *sub_path = NULL;
	char *new_path = NULL;
	char *build_clid = NULL;
	int rc = 0;
	int num = 0;
	char *ptr, *ptr2;
	char temp[10];
	int cid_len, len;
	int segment_len;
	int total_len;
	int total_tgt_len;
	int total_clid_len;

	dp = opendir(parent_path);
	if (dp == NULL) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to open v4 recovery dir (%s), errno=%d",
			 parent_path, errno);
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
		new_path = NULL;

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
		/* if tgtdir is not NULL, we need to build
		 * nfs4old/currentnode
		 */
		if (tgtdir) {
			total_tgt_len = segment_len + 2 +
					strlen(tgtdir);
			new_path = gsh_malloc(total_tgt_len);

			memset(new_path, 0, total_tgt_len);
			strcpy(new_path, tgtdir);
			strcat(new_path, "/");
			strncat(new_path, dentp->d_name, segment_len);
			rc = mkdir(new_path, 0700);
			if ((rc == -1) && (errno != EEXIST)) {
				LogEvent(COMPONENT_CLIENTID,
					 "mkdir %s faied errno=%d",
					 new_path, errno);
			}
		}
		/* keep building the clientid str by recursively */
		/* reading the directory structure */
		total_clid_len = segment_len + 1;
		if (clid_str)
			total_clid_len += strlen(clid_str);
		build_clid = gsh_calloc(1, total_clid_len);
		if (clid_str)
			strcpy(build_clid, clid_str);
		strncat(build_clid, dentp->d_name, segment_len);

		rc = fs_read_recov_clids_impl(sub_path,
					      build_clid,
					      new_path,
					      takeover,
					      add_clid_entry,
					      add_rfh_entry);
		gsh_free(new_path);

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
				fs_cp_pop_revoked_delegs(new_ent,
							 sub_path,
							 tgtdir,
							 !takeover,
							 add_rfh_entry);
				LogDebug(COMPONENT_CLIENTID,
					 "added %s to clid list",
					 new_ent->cl_name);
			}
		}
		gsh_free(build_clid);
		/* If this is not for takeover, remove the directory
		 * hierarchy  that represent the current clientid
		 */
		if (!takeover) {
			rc = rmdir(sub_path);
			if (rc == -1) {
				LogEvent(COMPONENT_CLIENTID,
					 "Failed to rmdir (%s), errno=%d",
					 sub_path, errno);
			}
		}
		gsh_free(sub_path);
	}

	(void)closedir(dp);

	return num;
}

static void fs_read_recov_clids_recover(add_clid_entry_hook add_clid_entry,
					add_rfh_entry_hook add_rfh_entry)
{
	int rc;

	rc = fs_read_recov_clids_impl(v4_old_dir, NULL, NULL, 0,
				      add_clid_entry,
				      add_rfh_entry);
	if (rc == -1) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to read v4 recovery dir (%s)",
			 v4_old_dir);
		return;
	}

	rc = fs_read_recov_clids_impl(v4_recov_dir, NULL, v4_old_dir, 0,
				      add_clid_entry,
				      add_rfh_entry);
	if (rc == -1) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to read v4 recovery dir (%s)",
			 v4_recov_dir);
		return;
	}
}

/**
 * @brief Load clients for recovery, with no lock
 *
 * @param[in] nodeid Node, on takeover
 */
void fs_read_recov_clids_takeover(nfs_grace_start_t *gsp,
				  add_clid_entry_hook add_clid_entry,
				  add_rfh_entry_hook add_rfh_entry)
{
	int rc;
	char path[PATH_MAX];

	if (!gsp) {
		fs_read_recov_clids_recover(add_clid_entry, add_rfh_entry);
		return;
	}

	switch (gsp->event) {
	case EVENT_UPDATE_CLIENTS:
		snprintf(path, sizeof(path), "%s", v4_recov_dir);
		break;
	case EVENT_TAKE_IP:
		snprintf(path, sizeof(path), "%s/%s/%s",
			 NFS_V4_RECOV_ROOT, gsp->ipaddr,
			 NFS_V4_RECOV_DIR);
		break;
	case EVENT_TAKE_NODEID:
		snprintf(path, sizeof(path), "%s/%s/node%d",
			 NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR,
			 gsp->nodeid);
		break;
	default:
		LogWarn(COMPONENT_STATE, "Recovery unknown event");
		return;
	}

	LogEvent(COMPONENT_CLIENTID, "Recovery for nodeid %d dir (%s)",
		 gsp->nodeid, path);

	rc = fs_read_recov_clids_impl(path, NULL, v4_old_dir, 1,
				      add_clid_entry,
				      add_rfh_entry);
	if (rc == -1) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to read v4 recovery dir (%s)", path);
		return;
	}
}

void fs_clean_old_recov_dir_impl(char *parent_path)
{
	DIR *dp;
	struct dirent *dentp;
	char *path = NULL;
	int rc;
	int total_len;

	dp = opendir(parent_path);
	if (dp == NULL) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to open old v4 recovery dir (%s), errno=%d",
			 v4_old_dir, errno);
		return;
	}

	for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
		/* don't remove '.' and '..' entry */
		if (!strcmp(dentp->d_name, ".") || !strcmp(dentp->d_name, ".."))
			continue;

		/* If there is a filename starting with '\x1', then it is
		 * a revoked handle, go ahead and remove it.
		 */
		if (dentp->d_name[0] == '\x1') {
			char del_path[PATH_MAX];

			snprintf(del_path, sizeof(del_path), "%s/%s",
				 parent_path, dentp->d_name);

			if (unlink(del_path) < 0) {
				LogEvent(COMPONENT_CLIENTID,
						"unlink of %s failed errno: %d",
						del_path,
						errno);
			}

			continue;
		}

		/* This is a directory, we need process files in it! */
		total_len = strlen(parent_path) + strlen(dentp->d_name) + 2;
		path = gsh_malloc(total_len);

		snprintf(path, total_len, "%s/%s", parent_path, dentp->d_name);

		fs_clean_old_recov_dir_impl(path);
		rc = rmdir(path);
		if (rc == -1) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to remove %s, errno=%d", path, errno);
		}
		gsh_free(path);
	}
	(void)closedir(dp);
}

void fs_clean_old_recov_dir(void)
{
	fs_clean_old_recov_dir_impl(v4_old_dir);
}

void fs_add_revoke_fh(nfs_client_id_t *delr_clid, nfs_fh4 *delr_handle)
{
	char rhdlstr[NAME_MAX];
	char path[PATH_MAX] = {0}, segment[NAME_MAX + 1] = {0};
	int length, position = 0;
	int fd;
	int retval;

	/* Convert nfs_fh4_val into base64 encoded string */
	retval = base64url_encode(delr_handle->nfs_fh4_val,
				  delr_handle->nfs_fh4_len,
				  rhdlstr, sizeof(rhdlstr));
	assert(retval != -1);

	/* Parse through the clientid directory structure */
	assert(delr_clid->cid_recov_tag != NULL);

	snprintf(path, sizeof(path), "%s", v4_recov_dir);
	length = strlen(delr_clid->cid_recov_tag);
	while (position < length) {
		int len = strlen(&delr_clid->cid_recov_tag[position]);

		if (len <= NAME_MAX) {
			strcat(path, "/");
			strncat(path, &delr_clid->cid_recov_tag[position], len);
			strcat(path, "/\x1"); /* Prefix 1 to converted fh */
			strncat(path, rhdlstr, strlen(rhdlstr));
			fd = creat(path, 0700);
			if (fd < 0) {
				LogEvent(COMPONENT_CLIENTID,
					"Failed to record revoke errno:%d\n",
					errno);
			} else {
				close(fd);
			}
			return;
		}
		strncpy(segment, &delr_clid->cid_recov_tag[position], NAME_MAX);
		strcat(path, "/");
		strncat(path, segment, NAME_MAX);
		position += NAME_MAX;
	}
}

struct nfs4_recovery_backend fs_backend = {
	.recovery_init = fs_create_recov_dir,
	.end_grace = fs_clean_old_recov_dir,
	.recovery_read_clids = fs_read_recov_clids_takeover,
	.add_clid = fs_add_clid,
	.rm_clid = fs_rm_clid,
	.add_revoke_fh = fs_add_revoke_fh,
};

void fs_backend_init(struct nfs4_recovery_backend **backend)
{
	*backend = &fs_backend;
}
