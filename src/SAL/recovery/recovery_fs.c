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

char v4_recov_dir[PATH_MAX];
unsigned int v4_recov_dir_len;
char v4_old_dir[PATH_MAX];
unsigned int v4_old_dir_len;

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
	char cidstr_lenx[5];
	int total_size, cidstr_lenx_len, cidstr_len, str_client_addr_len;

	/* get the caller's IP addr */
	if (clientid->gsh_client != NULL)
		str_client_addr = clientid->gsh_client->hostaddr_str;

	if (fs_convert_opaque_value_max_for_dir(&dspbuf,
						cl_rec->cr_client_val,
						cl_rec->cr_client_val_len,
						PATH_MAX) > 0) {
		cidstr_len = strlen(cidstr);
		str_client_addr_len = strlen(str_client_addr);

		/* fs_convert_opaque_value_max_for_dir does not prefix
		 * the "(<length>:". So we need to do it here */
		cidstr_lenx_len = snprintf(cidstr_lenx, sizeof(cidstr_lenx),
					   "%d", cidstr_len);

		if (unlikely(cidstr_lenx_len >= sizeof(cidstr_lenx) ||
			     cidstr_lenx_len < 0)) {
			/* cidrstr can at most be PATH_MAX or 1024, so at most
			 * 4 characters plus NUL are necessary, so we won't
			 * overrun, nor can we get a -1 with EOVERFLOW or EINVAL
			 */
			LogFatal(COMPONENT_CLIENTID,
				 "snprintf returned unexpected %d",
				 cidstr_lenx_len);
		}

		total_size = cidstr_len + str_client_addr_len + 5 +
			     cidstr_lenx_len;

		/* hold both long form clientid and IP */
		clientid->cid_recov_tag = gsh_malloc(total_size);

		/* Can't overrun and shouldn't return EOVERFLOW or EINVAL */
		(void) snprintf(clientid->cid_recov_tag, total_size,
				"%s-(%s:%s)",
				str_client_addr, cidstr_lenx, cidstr);
	}

	LogDebug(COMPONENT_CLIENTID, "Created client name [%s]",
		 clientid->cid_recov_tag);
}

int fs_create_recov_dir(void)
{
	int err, root_len, dir_len, old_len, node_size = 0;
	char node[14];

	if (nfs_param.core_param.clustered) {
		node_size = snprintf(node, sizeof(node), "node%d", g_nodeid);

		if (unlikely(node_size >= sizeof(node) || node_size < 0)) {
			LogFatal(COMPONENT_CLIENTID,
				 "snprintf returned unexpected %d", node_size);
		}
		/* Now include the '/' */
		node_size++;
	}

	err = mkdir(nfs_param.nfsv4_param.recov_root, 0755);
	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir (%s), errno: %s (%d)",
			 nfs_param.nfsv4_param.recov_root,
			 strerror(errno), errno);
	}

	root_len = strlen(nfs_param.nfsv4_param.recov_root);
	dir_len = strlen(nfs_param.nfsv4_param.recov_dir);
	v4_recov_dir_len = root_len + 1 + dir_len + node_size;

	if (v4_recov_dir_len >= sizeof(v4_recov_dir))
		LogFatal(COMPONENT_CLIENTID,
			 "v4 recovery dir path (%s/%s) is to long",
			 nfs_param.nfsv4_param.recov_root,
			 nfs_param.nfsv4_param.recov_dir);

	memcpy(v4_recov_dir, nfs_param.nfsv4_param.recov_root, root_len);
	v4_recov_dir[root_len] = '/';
	memcpy(v4_recov_dir + 1 + root_len,
	       nfs_param.nfsv4_param.recov_dir, dir_len + 1);
	dir_len = 1 + root_len + dir_len;

	LogDebug(COMPONENT_CLIENTID, "v4_recov_dir=%s", v4_recov_dir);

	err = mkdir(v4_recov_dir, 0755);
	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir(%s), errno: %s (%d)",
			 v4_recov_dir, strerror(errno), errno);
	}

	root_len = strlen(nfs_param.nfsv4_param.recov_root);
	old_len = strlen(nfs_param.nfsv4_param.recov_old_dir);
	v4_old_dir_len = root_len + 1 + dir_len + node_size;

	if (v4_old_dir_len >= sizeof(v4_old_dir))
		LogFatal(COMPONENT_CLIENTID,
			 "v4 recovery dir path (%s/%s) is to long",
			 nfs_param.nfsv4_param.recov_root,
			 nfs_param.nfsv4_param.recov_old_dir);

	memcpy(v4_old_dir, nfs_param.nfsv4_param.recov_root, root_len);
	v4_old_dir[root_len] = '/';
	memcpy(v4_old_dir + 1 + root_len,
	       nfs_param.nfsv4_param.recov_old_dir, old_len + 1);
	old_len = 1 + root_len + old_len;

	LogDebug(COMPONENT_CLIENTID, "v4_old_dir=%s", v4_old_dir);

	err = mkdir(v4_old_dir, 0755);

	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create v4 recovery dir(%s), errno: %s (%d)",
			 v4_old_dir, strerror(errno), errno);
	}
	if (nfs_param.core_param.clustered) {
		/* Now make the node specific directories */
		v4_recov_dir[dir_len] = '/';
		v4_old_dir[old_len] = '/';

		/* Copy an extra byte to NUL terminate */
		memcpy(v4_recov_dir + 1 + dir_len, node, node_size + 1);
		memcpy(v4_old_dir + 1 + old_len, node, node_size + 1);

		LogDebug(COMPONENT_CLIENTID, "v4_recov_dir=%s", v4_recov_dir);
		LogDebug(COMPONENT_CLIENTID, "v4_old_dir=%s", v4_old_dir);

		err = mkdir(v4_recov_dir, 0755);

		if (err == -1 && errno != EEXIST) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to create v4 recovery dir(%s), errno: %s (%d)",
				 v4_recov_dir, strerror(errno), errno);
		}

		err = mkdir(v4_old_dir, 0755);

		if (err == -1 && errno != EEXIST) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to create v4 recovery dir(%s), errno: %s (%d)",
				 v4_old_dir, strerror(errno), errno);
		}
	}


	LogInfo(COMPONENT_CLIENTID,
		"NFSv4 Recovery Directory %s", v4_recov_dir);
	LogInfo(COMPONENT_CLIENTID,
		"NFSv4 Recovery Directory (old) %s", v4_old_dir);

	return 0;
}

void fs_add_clid(nfs_client_id_t *clientid)
{
	int err = 0;
	char path[PATH_MAX] = {0};
	int length, position;
	int pathpos = strlen(v4_recov_dir);

	fs_create_clid_name(clientid);

	/* break clientid down if it is greater than max dir name */
	/* and create a directory hierarchy to represent the clientid. */
	memcpy(path, v4_recov_dir, pathpos + 1);

	length = strlen(clientid->cid_recov_tag);

	for (position = 0; position < length; position += NAME_MAX) {
		/* if the (remaining) clientid is shorter than 255 */
		/* create the last level of dir and break out */
		int len = length - position;

		/* No matter what, we need a '/' */
		path[pathpos++] = '/';

		/* Make sure there's still room in path */
		if ((pathpos + len) >= sizeof(path)) {
			errno = ENOMEM;
			err = -1;
			break;
		}

		if (len <= NAME_MAX) {
			memcpy(path + pathpos,
			       clientid->cid_recov_tag + position,
			       len + 1);
			err = mkdir(path, 0700);
			break;
		}

		/* if (remaining) clientid is longer than 255, */
		/* get the next 255 bytes and create a subdir */
		memcpy(path + pathpos,
		       clientid->cid_recov_tag + position,
		       NAME_MAX);
		pathpos += NAME_MAX;
		path[pathpos] = '\0';

		err = mkdir(path, 0700);
		if (err == -1 && errno != EEXIST)
			break;
	}

	if (err == -1 && errno != EEXIST) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create client in recovery dir (%s), errno: %s (%d)",
			 path, strerror(errno), errno);
	} else {
		LogDebug(COMPONENT_CLIENTID, "Created client dir [%s]", path);
	}
}

/**
 * @brief Remove the revoked file handles created under a specific
 * client-id path on the stable storage.
 *
 * @param[in] path The path of the client-id on the stable storage.
 */
static void fs_rm_revoked_handles(char *path)
{
	DIR *dp;
	struct dirent *dentp;
	char del_path[PATH_MAX];

	dp = opendir(path);
	if (dp == NULL) {
		LogEvent(COMPONENT_CLIENTID, "opendir %s failed errno: %s (%d)",
			path, strerror(errno), errno);
		return;
	}
	for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
		int rc;

		if (!strcmp(dentp->d_name, ".") ||
				!strcmp(dentp->d_name, "..") ||
				dentp->d_name[0] != '\x1') {
			continue;
		}

		rc = snprintf(del_path, sizeof(del_path), "%s/%s",
			      path, dentp->d_name);

		if (unlikely(rc >= sizeof(del_path))) {
			LogCrit(COMPONENT_CLIENTID,
				"Path %s/%s too long",
				path, dentp->d_name);
		} else if (unlikely(rc < 0)) {
			LogCrit(COMPONENT_CLIENTID,
				"Unexpected return from snprintf %d error %s (%d)",
				rc, strerror(errno), errno);
		} else if (unlink(del_path) < 0) {
			LogEvent(COMPONENT_CLIENTID,
				 "unlink of %s failed errno: %s (%d)",
				 del_path,
				 strerror(errno), errno);
		}
	}
	(void)closedir(dp);
}

static void fs_rm_clid_impl(int position,
			    char *recov_dir, int len,
			    char *parent_path, int parent_len)
{
	int err;
	char *path;
	int segment_len;
	int total_len;

	if (position == len) {
		/* We are at the tail directory of the clid,
		* remove revoked handles, if any.
		*/
		fs_rm_revoked_handles(parent_path);
		return;
	}

	if ((len - position) > NAME_MAX)
		segment_len = NAME_MAX;
	else
		segment_len = len - position;

	/* allocate enough memory for the new part of the string
	 * which is parent path + '/' + new segment
	 */
	total_len = parent_len + segment_len + 2;
	path = gsh_malloc(total_len);

	memcpy(path, parent_path, parent_len);
	path[parent_len] = '/';
	memcpy(path + parent_len + 1, recov_dir + position, segment_len);
	path[total_len - 1] = '\0';

	/* recursively remove the directory hirerchy which represent the
	 *clientid
	 */
	fs_rm_clid_impl(position + segment_len,
			recov_dir, len,
			path, total_len - 1);

	err = rmdir(path);
	if (err == -1) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to remove client recovery dir (%s), errno: %s (%d)",
			 path, strerror(errno), errno);
	} else {
		LogDebug(COMPONENT_CLIENTID, "Removed client dir (%s)", path);
	}
	gsh_free(path);
}

void fs_rm_clid(nfs_client_id_t *clientid)
{
	char *recov_dir = clientid->cid_recov_tag;

	if (recov_dir == NULL)
		return;

	clientid->cid_recov_tag = NULL;
	fs_rm_clid_impl(0,
			recov_dir, strlen(recov_dir),
			v4_recov_dir, v4_recov_dir_len);
	gsh_free(recov_dir);
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
 * @param[in] clientid The clientid that is being created.
 * @param[in] path The path of the directory structure.
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
		LogEvent(COMPONENT_CLIENTID, "opendir %s failed errno: %s (%d)",
			path, strerror(errno), errno);
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
			int fd, rc;

			rc = snprintf(lopath, sizeof(lopath), "%s/%s",
				      tgtdir, dentp->d_name);

			if (unlikely(rc >= sizeof(lopath))) {
				LogCrit(COMPONENT_CLIENTID,
					"Path %s/%s too long",
					 tgtdir, dentp->d_name);
			} else if (unlikely(rc < 0)) {
				LogCrit(COMPONENT_CLIENTID,
					"Unexpected return from snprintf %d error %s (%d)",
					rc, strerror(errno), errno);
			} else {
				fd = creat(lopath, 0700);
				if (fd < 0) {
					LogEvent(COMPONENT_CLIENTID,
						"Failed to copy revoked handle file %s to %s errno: %s(%d)",
						dentp->d_name, tgtdir,
						strerror(errno), errno);
				} else {
					close(fd);
				}
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
			int rc;

			rc = snprintf(del_path, sizeof(del_path), "%s/%s",
				      path, dentp->d_name);

			if (unlikely(rc >= sizeof(del_path))) {
				LogCrit(COMPONENT_CLIENTID,
					"Path %s/%s too long",
					 path, dentp->d_name);
			} else if (unlikely(rc < 0)) {
				LogCrit(COMPONENT_CLIENTID,
					"Unexpected return from snprintf %d error %s (%d)",
					rc, strerror(errno), errno);
			} else if (unlink(del_path) < 0) {
				LogEvent(COMPONENT_CLIENTID,
					 "unlink of %s failed errno: %s (%d)",
					 del_path,
					 strerror(errno), errno);
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
	int total_clid_len;
	int clid_str_len = (clid_str == NULL) ? 0 : strlen(clid_str);

	dp = opendir(parent_path);
	if (dp == NULL) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to open v4 recovery dir (%s), errno: %s (%d)",
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
		new_path = NULL;

		/* construct the path by appending the subdir for the
		 * next readdir. This recursion keeps reading the
		 * subdirectory until reaching the end.
		 */
		segment_len = strlen(dentp->d_name);
		sub_path = gsh_concat_sep(parent_path, '/', dentp->d_name);

		/* if tgtdir is not NULL, we need to build
		 * nfs4old/currentnode
		 */
		if (tgtdir) {
			new_path = gsh_concat_sep(tgtdir, '/', dentp->d_name);

			rc = mkdir(new_path, 0700);

			if ((rc == -1) && (errno != EEXIST)) {
				LogEvent(COMPONENT_CLIENTID,
					 "mkdir %s faied errno: %s (%d)",
					 new_path, strerror(errno), errno);
			}
		}

		/* keep building the clientid str by recursively */
		/* reading the directory structure */
		total_clid_len = segment_len + 1 + clid_str_len;

		build_clid = gsh_malloc(total_clid_len);

		if (clid_str)
			memcpy(build_clid, clid_str, clid_str_len);

		memcpy(build_clid + clid_str_len,
		       dentp->d_name,
		       segment_len + 1);

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
			if (total_clid_len >= PATH_MAX) {
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
					 "Failed to rmdir (%s), errno: %s (%d)",
					 sub_path, strerror(errno), errno);
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
		rc = snprintf(path, sizeof(path), "%s", v4_recov_dir);

		if (unlikely(rc >= sizeof(path))) {
			LogCrit(COMPONENT_CLIENTID,
				"Path %s too long",
				v4_recov_dir);
			return;
		} else if (unlikely(rc < 0)) {
			LogCrit(COMPONENT_CLIENTID,
				"Unexpected return from snprintf %d error %s (%d)",
				rc, strerror(errno), errno);
		}
		break;
	case EVENT_TAKE_IP:
		rc = snprintf(path, sizeof(path), "%s/%s/%s",
			      nfs_param.nfsv4_param.recov_root, gsp->ipaddr,
			      nfs_param.nfsv4_param.recov_dir);

		if (unlikely(rc >= sizeof(path))) {
			LogCrit(COMPONENT_CLIENTID,
				"Path %s/%s/%s too long",
				nfs_param.nfsv4_param.recov_root, gsp->ipaddr,
				nfs_param.nfsv4_param.recov_dir);
			return;
		} else if (unlikely(rc < 0)) {
			LogCrit(COMPONENT_CLIENTID,
				"Unexpected return from snprintf %d error %s (%d)",
				rc, strerror(errno), errno);
		}
		break;
	case EVENT_TAKE_NODEID:
		rc = snprintf(path, sizeof(path), "%s/%s/node%d",
			      nfs_param.nfsv4_param.recov_root,
			      nfs_param.nfsv4_param.recov_dir,
			      gsp->nodeid);

		if (unlikely(rc >= sizeof(path))) {
			LogCrit(COMPONENT_CLIENTID,
				"Path %s/%s/node%d too long",
				nfs_param.nfsv4_param.recov_root,
				nfs_param.nfsv4_param.recov_dir,
				gsp->nodeid);
			return;
		} else if (unlikely(rc < 0)) {
			LogCrit(COMPONENT_CLIENTID,
				"Unexpected return from snprintf %d error %s (%d)",
				rc, strerror(errno), errno);
		}
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

	dp = opendir(parent_path);
	if (dp == NULL) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to open old v4 recovery dir (%s), errno: %s (%d)",
			 v4_old_dir, strerror(errno), errno);
		return;
	}

	for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
		/* don't remove '.' and '..' entry */
		if (!strcmp(dentp->d_name, ".") || !strcmp(dentp->d_name, ".."))
			continue;

		/* Assemble the path */
		path = gsh_concat_sep(parent_path, '/', dentp->d_name);

		/* If there is a filename starting with '\x1', then it is
		 * a revoked handle, go ahead and remove it.
		 */
		if (dentp->d_name[0] == '\x1') {
			if (unlink(path) < 0) {
				LogEvent(COMPONENT_CLIENTID,
						"unlink of %s failed errno: %s (%d)",
						path, strerror(errno), errno);
			}
		} else {
			/* This is a directory, we need process files in it! */
			fs_clean_old_recov_dir_impl(path);

			rc = rmdir(path);

			if (rc == -1) {
				LogEvent(COMPONENT_CLIENTID,
					 "Failed to remove %s, errno: %s (%d)",
					 path, strerror(errno), errno);
			}
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
	char path[PATH_MAX] = {0};
	int length, position = 0, pathpos, rhdlstr_len;
	int fd;
	int retval;

	/* Convert nfs_fh4_val into base64 encoded string */
	retval = base64url_encode(delr_handle->nfs_fh4_val,
				  delr_handle->nfs_fh4_len,
				  rhdlstr, sizeof(rhdlstr));
	assert(retval != -1);
	rhdlstr_len = strlen(rhdlstr);

	/* Parse through the clientid directory structure */
	assert(delr_clid->cid_recov_tag != NULL);

	assert(v4_recov_dir_len < sizeof(path));

	memcpy(path, v4_recov_dir, v4_recov_dir_len);
	pathpos = v4_recov_dir_len;

	length = strlen(delr_clid->cid_recov_tag);

	while (position < length) {
		int len = length - position;

		if (len <= NAME_MAX) {
			int new_pathpos = pathpos + 1 + len + 3 + rhdlstr_len;

			if (new_pathpos >= sizeof(path)) {
				LogCrit(COMPONENT_CLIENTID,
					"Could not revoke path %s/%s/%s too long",
					path,
					delr_clid->cid_recov_tag + position,
					rhdlstr);
			}
			path[pathpos++] = '/';
			memcpy(path + pathpos,
			       delr_clid->cid_recov_tag + position,
			       len);
			/* Prefix 1 to converted fh */
			memcpy(path + pathpos + len, "/\x1", 2);
			memcpy(path + pathpos + len + 2,
			       rhdlstr,
			       rhdlstr_len + 1);
			fd = creat(path, 0700);
			if (fd < 0) {
				LogEvent(COMPONENT_CLIENTID,
					"Failed to record revoke errno: %s (%d)",
					strerror(errno), errno);
			} else {
				close(fd);
			}
			return;
		}
		path[pathpos++] = '/';
		memcpy(path + pathpos,
		       delr_clid->cid_recov_tag + position,
		       NAME_MAX);
		pathpos += NAME_MAX;
		path[pathpos] = '\0';
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
