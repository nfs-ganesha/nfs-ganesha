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
#include "bsd-base64.h"
#include "client_mgr.h"
#include "fsal.h"
#include "recovery_fs.h"

char v4_recov_dir[PATH_MAX];
unsigned int v4_recov_dir_len;
char v4_old_dir[PATH_MAX];
unsigned int v4_old_dir_len;

/* this file denotes that the clientid has reclaimed completely */
static char *reclaim_complete_marker = "reclaim_complete";

/**
 * @brief create the name for the recovery directory in IP Based recovery
 *
 * This name will be used to know that a client was talking to the
 * server before a restart so that it will be allowed to do reclaims
 * during grace period.
 *
 * @param[in]  saddr IP  Address
 * @param[in]  is_old    Whether the name should be for the old state dir
 * @param[in]  path_size Maximum size of path
 * @param[out] path      The recovery dir name
 *
 * @return length of path or -ve on error
 */
static int fs_make_ip_recov_dir_name(sockaddr_t *saddr, bool is_old,
				     int path_size, char *path)
{
	int rc;
	char ipstring[SOCK_NAME_MAX];
	struct display_buffer dspbuf = { sizeof(ipstring), ipstring, ipstring };

	display_sockip(&dspbuf, saddr);

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
		    "ip_based=%d addr_int=%s is_old=%d",
		    nfs_param.nfsv4_param.recovery_backend_ipbased, ipstring,
		    is_old);
	if (is_old) {
		rc = snprintf(path, path_size, "%s/%s/ip_%s",
			      nfs_param.nfsv4_param.recov_root,
			      nfs_param.nfsv4_param.recov_old_dir, ipstring);
	} else {
		rc = snprintf(path, path_size, "%s/%s/ip_%s",
			      nfs_param.nfsv4_param.recov_root,
			      nfs_param.nfsv4_param.recov_dir, ipstring);
	}

	if (unlikely(rc >= path_size)) {
		LogCrit(COMPONENT_RECOVERY, "Path %s too long", path);
		return -1;
	} else if (unlikely(rc < 0)) {
		LogCrit(COMPONENT_RECOVERY,
			"Unexpected return from snprintf %d error %s (%d)", rc,
			strerror(errno), errno);
		return rc;
	}

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, "dir = %s", path);

	return rc;
}

int fs_create_recov_dir(void)
{
	int err, root_len, dir_len, old_len, node_size = 0;
	char node[15] = { 0 };

	/* Note below - all the size tests below are >= which assures space for
	 *              the terminating NUL.
	 */

	if (nfs_param.nfsv4_param.recovery_backend_ipbased) {
		node[0] = '\0';
	} else if (nfs_param.core_param.clustered && (g_nodeid >= 0)) {
		node_size = snprintf(node, sizeof(node), "/node%d", g_nodeid);

		if (unlikely(node_size >= sizeof(node) || node_size < 0)) {
			LogCrit(COMPONENT_RECOVERY,
				"snprintf returned unexpected %d", node_size);
			return -ENAMETOOLONG;
		}
	}

	err = mkdir(nfs_param.nfsv4_param.recov_root, 0755);
	if (err == -1 && errno != EEXIST) {
		err = errno;
		LogCrit(COMPONENT_RECOVERY,
			"Failed to create v4 recovery dir (%s), errno: %s (%d)",
			nfs_param.nfsv4_param.recov_root, strerror(errno),
			errno);
		return -err;
	}

	root_len = strlen(nfs_param.nfsv4_param.recov_root);
	dir_len = strlen(nfs_param.nfsv4_param.recov_dir);

	/* If not clustered: root + '/' + dir (nodesize = 0)
	 * If clustered: root + '/' + dir + "/node%d" (nodesize != 0)
	 */
	v4_recov_dir_len = root_len + 1 + dir_len + node_size;

	if (v4_recov_dir_len >= sizeof(v4_recov_dir)) {
		LogCrit(COMPONENT_RECOVERY,
			"v4 recovery dir path (%s/%s) is too long",
			nfs_param.nfsv4_param.recov_root,
			nfs_param.nfsv4_param.recov_dir);
		return -ENAMETOOLONG;
	}

	memcpy(v4_recov_dir, nfs_param.nfsv4_param.recov_root, root_len);
	v4_recov_dir[root_len] = '/';
	memcpy(v4_recov_dir + 1 + root_len, nfs_param.nfsv4_param.recov_dir,
	       dir_len + 1);
	dir_len = 1 + root_len + dir_len;

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, "v4_recov_dir=%s",
		    v4_recov_dir);

	err = mkdir(v4_recov_dir, 0755);
	if (err == -1 && errno != EEXIST) {
		err = errno;
		LogCrit(COMPONENT_RECOVERY,
			"Failed to create v4 recovery dir(%s), errno: %s (%d)",
			v4_recov_dir, strerror(errno), errno);
		return -err;
	}

	root_len = strlen(nfs_param.nfsv4_param.recov_root);
	old_len = strlen(nfs_param.nfsv4_param.recov_old_dir);

	/* If not clustered: root + '/' + old (nodesize = 0)
	 * If clustered: root + '/' + old + "/node%d" (nodesize != 0)
	 */
	v4_old_dir_len = root_len + 1 + old_len + node_size;

	if (v4_old_dir_len >= sizeof(v4_old_dir)) {
		LogCrit(COMPONENT_RECOVERY,
			"v4 recovery dir path (%s/%s) is too long",
			nfs_param.nfsv4_param.recov_root,
			nfs_param.nfsv4_param.recov_old_dir);
		return -ENAMETOOLONG;
	}

	memcpy(v4_old_dir, nfs_param.nfsv4_param.recov_root, root_len);
	v4_old_dir[root_len] = '/';
	memcpy(v4_old_dir + 1 + root_len, nfs_param.nfsv4_param.recov_old_dir,
	       old_len + 1);
	old_len = 1 + root_len + old_len;

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, "v4_old_dir=%s",
		    v4_old_dir);

	err = mkdir(v4_old_dir, 0755);

	if (err == -1 && errno != EEXIST) {
		err = errno;
		LogCrit(COMPONENT_RECOVERY,
			"Failed to create v4 recovery dir(%s), errno: %s (%d)",
			v4_old_dir, strerror(errno), errno);
		return -err;
	}
	if (nfs_param.core_param.clustered) {
		/* Now make the node specific directory.
		 * Note that node already includes the '/' path separator.
		 */

		/* Copy an extra byte to NUL terminate */
		memcpy(v4_recov_dir + dir_len, node, node_size + 1);
		memcpy(v4_old_dir + old_len, node, node_size + 1);

		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "v4_recov_dir=%s", v4_recov_dir);
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "v4_old_dir=%s", v4_old_dir);

		err = mkdir(v4_recov_dir, 0755);

		if (err == -1 && errno != EEXIST) {
			err = errno;
			LogCrit(COMPONENT_RECOVERY,
				"Failed to create v4 recovery dir(%s), errno: %s (%d)",
				v4_recov_dir, strerror(errno), errno);
			return -err;
		}

		err = mkdir(v4_old_dir, 0755);

		if (err == -1 && errno != EEXIST) {
			err = errno;
			LogCrit(COMPONENT_RECOVERY,
				"Failed to create v4 recovery dir(%s), errno: %s (%d)",
				v4_old_dir, strerror(errno), errno);
			return -err;
		}
	}

	LogInfo(COMPONENT_RECOVERY, "NFSv4 Recovery Directory %s",
		v4_recov_dir);
	LogInfo(COMPONENT_RECOVERY, "NFSv4 Recovery Directory (old) %s",
		v4_old_dir);

	LogEvent(COMPONENT_RECOVERY,
		 "fs recovery backend initialization complete");

	return 0;
}

/**
 * @brief Make a path for a given clid, optionally create the directories
 *
 * @param[in]      name      Name of the entry (for example clid name)
 * @param[in]      serv      Server address (for use w/recovery_backend_ipbased)
 * @param[in/out]  path      Buffer to build path in
 * @param[in]      pathsize  Size of path buffer
 * @param[in]      make      True if the directories should be created
 *
 * @return Length of path or -errno if failure
 */
int fs_make_clid_path(const char *name, sockaddr_t *serv, char *path,
		      int pathsize, bool make)
{
	int err = 0;
	int length, srcpos, dstpos;

	if (nfs_param.nfsv4_param.recovery_backend_ipbased) {
		dstpos = fs_make_ip_recov_dir_name(serv, false, pathsize, path);

		if (make) {
			err = mkdir(path, 0700);
			if (err == -1 && errno != EEXIST) {
				err = errno;
				LogEvent(
					COMPONENT_RECOVERY,
					"Failed to create recov_dir (%s), errno: %s (%d)",
					path, strerror(err), err);
				return -err;
			}
		}
	} else {
		memcpy(path, v4_recov_dir, v4_recov_dir_len);
		dstpos = v4_recov_dir_len;
	}

	if (isDebug(COMPONENT_CLIENTID) || isDebug(COMPONENT_RECOVERY)) {
		char ipstring[SOCK_NAME_MAX];
		struct display_buffer dspbuf = { sizeof(ipstring), ipstring,
						 ipstring };
		display_sockip(&dspbuf, serv);
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "server_addr=%s recov_dir=%s", ipstring, path);
	}

	/* break clientid down if it is greater than max dir name
	 * and create a directory hierarchy to represent the clientid.
	 */
	length = strlen(name);

	for (srcpos = 0; srcpos < length; srcpos += NAME_MAX) {
		/* if the (remaining) clientid is shorter than 255 create the
		 * last level of dir and break out
		 */
		int len = length - srcpos;

		/* No matter what, we need a '/' */
		path[dstpos++] = '/';

		/* Make sure there's still room in path */
		if ((dstpos + len) >= pathsize) {
			LogEvent(COMPONENT_RECOVERY, "CID Name too Long %s",
				 name);
			return -ENOMEM;
		}

		if (len <= NAME_MAX) {
			memcpy(path + dstpos, name + srcpos, len + 1);
			if (make) {
				err = mkdir(path, 0700);
				/* Handle error outside loop */
			}
			break;
		}

		/* if (remaining) clientid is longer than 255, get the next 255
		 * bytes and create a subdir
		 */
		memcpy(path + dstpos, name + srcpos, NAME_MAX);
		dstpos += NAME_MAX;
		path[dstpos] = '\0';

		if (make) {
			err = mkdir(path, 0700);
			if (err == -1 && errno != EEXIST) {
				/* Handle error outside loop */
				break;
			}
		}
	}

	if (err == -1 && errno != EEXIST) {
		err = errno;
		LogEvent(
			COMPONENT_RECOVERY,
			"Failed to create client in recovery dir (%s), errno: %s (%d)",
			path, strerror(err), err);
		return -err;
	}

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
		    "%srecovery entry dir [%s]", make ? "Created " : "", path);

	return dstpos;
}

void fs_add_clid(nfs_client_id_t *clientid)
{
	int err;
	char path[PATH_MAX];

	clientid->cid_recov_tag = nfs4_create_clid_name(clientid, NULL);

	err = fs_make_clid_path(clientid->cid_recov_tag,
				&clientid->cid_client_record->cr_server_addr,
				path, sizeof(path), true);

	assert(err > 0);
}

/**
 * @brief The function is called on OP_RECLAIM_COMPLETE. It adds
 * the reclaim_complete marker under the direcotory of the clientid,
 * which is done by creating a file with the special name.
 *
 * The marker will be used in "recovery_read_clids()" before entering
 * grace period, to reject states reclaim from questionable clients.
 */
void fs_reclaim_complete(nfs_client_id_t *clientid)
{
	char path[PATH_MAX];
	int fd, dstpos, marker_len;

	if (isDebug(COMPONENT_CLIENTID) || isDebug(COMPONENT_RECOVERY)) {
		char client_str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = { sizeof(client_str), client_str,
						 client_str };
		display_client_id_rec(&dspbuf, clientid);
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, "%s",
			    client_str);
	}

	marker_len = strlen(reclaim_complete_marker);

	/* Fill in path from the cid_recov_tag, don't create directories */
	dstpos = fs_make_clid_path(clientid->cid_recov_tag,
				   &clientid->cid_client_record->cr_server_addr,
				   path, sizeof(path), false);

	assert(dstpos > 0);

	if (dstpos + marker_len + 1 >= sizeof(path)) {
		LogCrit(COMPONENT_RECOVERY,
			"Could not reclaim_complete path %s/%s too long", path,
			reclaim_complete_marker);
		return;
	}

	/* Append the reclaim complete marker */
	path[dstpos] = '/';
	memcpy(path + dstpos + 1, reclaim_complete_marker, marker_len);

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, "Create marker %s",
		    path);

	fd = creat(path, 0700);

	if (fd < 0) {
		LogEvent(COMPONENT_RECOVERY,
			 "Failed to record revoke errno: %s (%d)",
			 strerror(errno), errno);
	} else {
		close(fd);
	}

	return;
}

/**
 * @brief Remove the records created under a specific client-id
 * path on the stable storage, including the revoked file handles
 * and reclaim_complete marker.
 *
 * @param[in] path The path of the client-id on the stable storage.
 */
static void fs_rm_client_records(char *path)
{
	DIR *dp;
	struct dirent *dentp;
	char del_path[PATH_MAX];
	int err = 0;

	dp = opendir(path);

	if (dp == NULL) {
		int err = errno;
		LogEvent(COMPONENT_RECOVERY, "opendir %s failed errno: %s (%d)",
			 path, strerror(err), err);
		return;
	}

	for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
		int rc;

		/* only delete revoke file handles or reclaim_complete marker */
		if (dentp->d_name[0] != '\x1' &&
		    strcmp(dentp->d_name, reclaim_complete_marker)) {
			continue;
		}

		rc = snprintf(del_path, sizeof(del_path), "%s/%s", path,
			      dentp->d_name);

		if (unlikely(rc >= sizeof(del_path))) {
			LogCrit(COMPONENT_RECOVERY, "Path %s/%s too long", path,
				dentp->d_name);
		} else if (unlikely(rc < 0)) {
			err = errno;
			LogCrit(COMPONENT_RECOVERY,
				"Unexpected return from snprintf %d error %s (%d)",
				rc, strerror(err), err);
		} else if (unlink(del_path) < 0) {
			err = errno;
			LogEvent(COMPONENT_RECOVERY,
				 "unlink of %s failed errno: %s (%d)", del_path,
				 strerror(err), err);
		}
	}

	(void)closedir(dp);
}

static void fs_rm_clid_impl(int position, char *recov_dir, int len,
			    char *parent_path, int parent_len)
{
	int err;
	char *path;
	int segment_len;
	int total_len;

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
		    "position=%d len=%d parent_path=%s recov_dir=%s", position,
		    len, parent_path, recov_dir);

	if (position == len) {
		/* We are at the tail directory of the clid,
		* remove revoked handles and reclaim complete marker, if any.
		*/
		fs_rm_client_records(parent_path);
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
	 * clientid. Note on the final iteration, position + segment_len will
	 * be the same as len, and this recursion will not process an additional
	 * path component, rather it will clean the final directory.
	 */
	fs_rm_clid_impl(position + segment_len, recov_dir, len, path,
			total_len - 1);

	/* Remove this sub-directory on tail recursion */

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, "Will remove %s",
		    path);

	err = rmdir(path);

	if (err == -1) {
		err = errno;
		LogEvent(
			COMPONENT_RECOVERY,
			"Failed to remove client recovery dir (%s), errno: %s (%d)",
			path, strerror(err), err);
	} else {
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "Removed client dir (%s)", path);
	}

	gsh_free(path);
}

void fs_rm_clid(nfs_client_id_t *clientid)
{
	if (clientid->cid_recov_tag == NULL)
		return;

	if (isDebug(COMPONENT_CLIENTID) || isDebug(COMPONENT_RECOVERY)) {
		char client_str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = { sizeof(client_str), client_str,
						 client_str };

		display_client_id_rec(&dspbuf, clientid);
		LogDebugAlt(
			COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			"v4_recov_dir=%s recov_dir=%s client=%s saved=%d confirmed=%d",
			v4_recov_dir, clientid->cid_recov_tag, client_str,
			clientid->cid_confirmed_saved, clientid->cid_confirmed);
	}

	if (nfs_param.nfsv4_param.recovery_backend_ipbased) {
		char ip_recov_dir[PATH_MAX];
		int ip_recov_dir_len;

		ip_recov_dir_len = fs_make_ip_recov_dir_name(
			&clientid->cid_client_record->cr_server_addr, false,
			sizeof(ip_recov_dir), ip_recov_dir);

		/*
		   In the case of v4.1, confirmed client ids are removed as
		   a result of OP_DESTROY_CLIENTID sent when a client umounts.
		   For V4.0, the CONFIRMED_CLIENT_ID status is trashed in
		   nfs3_clientid.c:nfs_client_id_expire().  We need that to
		   differentiate from "normal" expiry after a release IP.
		   So we save it the status in clientid->cid_confirmed_saved
		   in that function so that we can check it here.
		*/
		if ((clientid->cid_confirmed == CONFIRMED_CLIENT_ID) ||
		    (clientid->cid_minorversion == 0 &&
		     clientid->cid_confirmed_saved == CONFIRMED_CLIENT_ID)) {
			if (isDebug(COMPONENT_CLIENTID) ||
			    isDebug(COMPONENT_RECOVERY)) {
				char client_str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dspbuf = {
					sizeof(client_str), client_str,
					client_str
				};

				display_client_id_rec(&dspbuf, clientid);

				LogDebugAlt(COMPONENT_CLIENTID,
					    COMPONENT_RECOVERY,
					    "Will cleanup %s %s: client=%s",
					    ip_recov_dir,
					    clientid->cid_recov_tag,
					    client_str);
			}

			fs_rm_clid_impl(0, clientid->cid_recov_tag,
					strlen(clientid->cid_recov_tag),
					ip_recov_dir, ip_recov_dir_len);
			gsh_free(clientid->cid_recov_tag);
			clientid->cid_recov_tag = NULL;
		} else if (clientid->cid_minorversion > 0 &&
			   clientid->cid_confirmed == EXPIRED_CLIENT_ID &&
			   (isDebug(COMPONENT_CLIENTID) ||
			    isDebug(COMPONENT_RECOVERY))) {
			int pathpos = ip_recov_dir_len;

			ip_recov_dir[pathpos++] = '/';
			memcpy(ip_recov_dir + pathpos, clientid->cid_recov_tag,
			       strlen(clientid->cid_recov_tag) + 1);
			LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
				    "should expire client records %s",
				    ip_recov_dir);
		}
	} else {
		fs_rm_clid_impl(0, clientid->cid_recov_tag,
				strlen(clientid->cid_recov_tag), v4_recov_dir,
				v4_recov_dir_len);
		gsh_free(clientid->cid_recov_tag);
		clientid->cid_recov_tag = NULL;
	}
}

static bool fs_check_reclaim_complete(const char *clid_path)
{
	char path[PATH_MAX] = { 0 };
	int rc = snprintf(path, sizeof(path), "%s/%s", clid_path,
			  reclaim_complete_marker);

	if (unlikely(rc >= sizeof(path))) {
		LogCrit(COMPONENT_RECOVERY, "Path %s/%s too long", clid_path,
			reclaim_complete_marker);
	} else if (unlikely(rc < 0)) {
		LogCrit(COMPONENT_RECOVERY,
			"Unexpected return from snprintf %d error %s (%d)", rc,
			strerror(errno), errno);
	} else {
		struct stat buffer;

		if (stat(path, &buffer) == 0) {
			if (S_ISREG(buffer.st_mode))
				return true;
			else {
				LogDebugAlt(
					COMPONENT_CLIENTID, COMPONENT_RECOVERY,
					"unexpected non-regular type of path %s",
					path);
			}
		}
	}
	return false;
}

/**
 * @brief Copy and Populate revoked delegations for this client.
 *
 * Even after delegation revoke, it is possible for the client to
 * continue its lease and other operations. Sever saves revoked delegations
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
static void fs_cp_pop_revoked_delegs(clid_entry_t *clid_ent, char *path,
				     char *tgtdir, bool del)
{
	struct dirent *dentp;
	DIR *dp;
	rdel_fh_t *new_ent;

	/* Read the contents from recov dir of this clientid. */
	dp = opendir(path);
	if (dp == NULL) {
		LogEvent(COMPONENT_RECOVERY, "opendir %s failed errno: %s (%d)",
			 path, strerror(errno), errno);
		return;
	}

	for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
		if (!strcmp(dentp->d_name, ".") || !strcmp(dentp->d_name, ".."))
			continue;
		/* All the revoked filehandles stored with \x1 prefix */
		if (dentp->d_name[0] != '\x1') {
			continue;
		}

		if (tgtdir) {
			char lopath[PATH_MAX];
			int fd, rc;

			rc = snprintf(lopath, sizeof(lopath), "%s/%s", tgtdir,
				      dentp->d_name);

			if (unlikely(rc >= sizeof(lopath))) {
				LogCrit(COMPONENT_RECOVERY,
					"Path %s/%s too long", tgtdir,
					dentp->d_name);
			} else if (unlikely(rc < 0)) {
				LogCrit(COMPONENT_RECOVERY,
					"Unexpected return from snprintf %d error %s (%d)",
					rc, strerror(errno), errno);
			} else {
				fd = creat(lopath, 0700);
				if (fd < 0) {
					LogEvent(
						COMPONENT_RECOVERY,
						"Failed to copy revoked handle file %s to %s errno: %s(%d)",
						dentp->d_name, tgtdir,
						strerror(errno), errno);
				} else {
					close(fd);
				}
			}
		}

		/* Ignore the beginning \x1 and copy the rest (file handle) */
		new_ent = nfs4_add_rfh_entry(clid_ent, dentp->d_name + 1);

		LogFullDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
				"revoked handle: %s", new_ent->rdfh_handle_str);

		/* Since the handle is loaded into memory, go ahead and
		 * delete it from the stable storage.
		 */
		if (del) {
			char del_path[PATH_MAX];
			int rc;

			rc = snprintf(del_path, sizeof(del_path), "%s/%s", path,
				      dentp->d_name);

			if (unlikely(rc >= sizeof(del_path))) {
				LogCrit(COMPONENT_RECOVERY,
					"Path %s/%s too long", path,
					dentp->d_name);
			} else if (unlikely(rc < 0)) {
				LogCrit(COMPONENT_RECOVERY,
					"Unexpected return from snprintf %d error %s (%d)",
					rc, strerror(errno), errno);
			} else if (unlink(del_path) < 0) {
				LogEvent(COMPONENT_RECOVERY,
					 "unlink of %s failed errno: %s (%d)",
					 del_path, strerror(errno), errno);
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
static int fs_read_recov_clids_impl(const char *parent_path, char *clid_str,
				    char *tgtdir, int takeover)
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
	bool reclaim_complete;

	dp = opendir(parent_path);
	if (dp == NULL) {
		LogEvent(COMPONENT_RECOVERY,
			 "Failed to open v4 recovery dir (%s), errno: %s (%d)",
			 parent_path, strerror(errno), errno);
		return -1;
	}

	for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
		/* don't add '.' and '..' entry */
		if (!strcmp(dentp->d_name, ".") || !strcmp(dentp->d_name, ".."))
			continue;

		/* skip non-directory dentries */
		if (dentp->d_type != DT_DIR)
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
				LogEvent(COMPONENT_RECOVERY,
					 "mkdir %s failed errno: %s (%d)",
					 new_path, strerror(errno), errno);
			}
		}

		/* keep building the clientid str by recursively */
		/* reading the directory structure */
		total_clid_len = segment_len + 1 + clid_str_len;

		build_clid = gsh_malloc(total_clid_len);

		if (clid_str)
			memcpy(build_clid, clid_str, clid_str_len);

		memcpy(build_clid + clid_str_len, dentp->d_name,
		       segment_len + 1);

		rc = fs_read_recov_clids_impl(sub_path, build_clid, new_path,
					      takeover);
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
				LogEvent(COMPONENT_RECOVERY,
					 "invalid clid format: %s, too long",
					 build_clid);
				gsh_free(sub_path);
				gsh_free(build_clid);
				continue;
			}
			ptr = strchr(build_clid, '(');
			if (ptr == NULL) {
				LogEvent(COMPONENT_RECOVERY,
					 "invalid clid format: %s", build_clid);
				gsh_free(sub_path);
				gsh_free(build_clid);
				continue;
			}
			ptr2 = strchr(ptr, ':');
			if (ptr2 == NULL) {
				LogEvent(COMPONENT_RECOVERY,
					 "invalid clid format: %s", build_clid);
				gsh_free(sub_path);
				gsh_free(build_clid);
				continue;
			}
			len = ptr2 - ptr - 1;
			if (len >= 9) {
				LogEvent(COMPONENT_RECOVERY,
					 "invalid clid format: %s", build_clid);
				gsh_free(sub_path);
				gsh_free(build_clid);
				continue;
			}
			memcpy(temp, ptr + 1, len + 1);
			cid_len = atoi(temp);
			len = strlen(ptr2);
			if ((len == (cid_len + 2)) && (ptr2[len - 1] == ')')) {
				reclaim_complete =
					fs_check_reclaim_complete(sub_path);
				new_ent = nfs4_add_clid_entry(build_clid,
							      reclaim_complete);
				fs_cp_pop_revoked_delegs(new_ent, sub_path,
							 tgtdir, !takeover);
				LogDebugAlt(
					COMPONENT_CLIENTID, COMPONENT_RECOVERY,
					"added %s to clid list, reclaim_complete %d",
					new_ent->cl_name, reclaim_complete);
			}
		}
		gsh_free(build_clid);
		/* If this is not for takeover, remove the directory
		 * hierarchy  that represent the current clientid
		 */
		if (!takeover) {
			if (nfs_param.nfsv4_param.recovery_backend_ipbased) {
				LogDebugAlt(COMPONENT_CLIENTID,
					    COMPONENT_RECOVERY,
					    "Would remove %s", sub_path);
			} else {
				fs_rm_client_records(sub_path);
				rc = rmdir(sub_path);
				if (rc == -1) {
					LogEvent(
						COMPONENT_RECOVERY,
						"Failed to rmdir (%s), errno: %s (%d)",
						sub_path, strerror(errno),
						errno);
				}
			}
		}
		gsh_free(sub_path);
	}

	(void)closedir(dp);

	return num;
}

static void fs_read_recov_clids_recover(char *recov_dir, char *old_dir)
{
	int rc;

	rc = fs_read_recov_clids_impl(old_dir, NULL, NULL, 0);
	if (rc == -1) {
		LogEvent(COMPONENT_RECOVERY,
			 "Failed to read v4 recovery dir (%s)", old_dir);
		return;
	}

	rc = fs_read_recov_clids_impl(recov_dir, NULL, old_dir, 0);

	if (rc == -1) {
		LogEvent(COMPONENT_RECOVERY,
			 "Failed to read v4 recovery dir (%s)", recov_dir);
		return;
	}
}

static void create_dir(char *path)
{
	int err;

	err = mkdir(path, 0755);
	if (err == -1 && errno != EEXIST) {
		err = errno;
		LogEvent(COMPONENT_RECOVERY,
			 "Failed to create v4 recovery dir (%s), errno: %s (%d)",
			 path, strerror(errno), errno);
	}
}

/**
 * @brief Load clients for recovery, with no lock
 *
 * @param[in] nodeid Node, on takeover
 */
void fs_read_recov_clids_takeover(nfs_grace_start_t *gsp)
{
	int rc;
	sockaddr_t saddr;
	char path[PATH_MAX];
	char recov_dir[PATH_MAX];
	char old_dir[PATH_MAX];

	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
		    "ip_based=%d, gsp=%p",
		    nfs_param.nfsv4_param.recovery_backend_ipbased, gsp);
	if (nfs_param.nfsv4_param.recovery_backend_ipbased) {
		if (gsp) {
			/* should always be set */
			if (gsp->ipaddr) {
				LogDebugAlt(COMPONENT_CLIENTID,
					    COMPONENT_RECOVERY, "gsp_ipaddr=%s",
					    gsp->ipaddr);
				rc = ip_str_to_sockaddr(gsp->ipaddr, &saddr);
				if (rc != 0) {
					LogWarn(COMPONENT_RECOVERY,
						"Unable to convert IP string %s",
						gsp->ipaddr);
					return;
				}

				fs_make_ip_recov_dir_name(&saddr, false,
							  sizeof(recov_dir),
							  recov_dir);
				create_dir(recov_dir);
				fs_make_ip_recov_dir_name(&saddr, true,
							  sizeof(old_dir),
							  old_dir);
				create_dir(old_dir);
			}
		} else {
			LogDebugAlt(
				COMPONENT_CLIENTID, COMPONENT_RECOVERY,
				"IP BASED v4_recov_dir_len=%d strlen(v4_recov_dir)=%ld",
				v4_recov_dir_len, strlen(v4_recov_dir));
			memcpy(recov_dir, v4_recov_dir, v4_recov_dir_len + 1);
			memcpy(old_dir, v4_old_dir, v4_old_dir_len + 1);
		}
		LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
			    "IP BASED END %s %s", recov_dir, old_dir);
	} else {
		memcpy(recov_dir, v4_recov_dir, v4_recov_dir_len + 1);
		memcpy(old_dir, v4_old_dir, v4_old_dir_len + 1);
	}

	if (!gsp) {
		fs_read_recov_clids_recover(recov_dir, old_dir);
		return;
	}

	switch (gsp->event) {
	case EVENT_UPDATE_CLIENTS:
		rc = snprintf(path, sizeof(path), "%s", v4_recov_dir);

		if (unlikely(rc >= sizeof(path))) {
			LogCrit(COMPONENT_RECOVERY, "Path %s too long",
				v4_recov_dir);
			return;
		} else if (unlikely(rc < 0)) {
			LogCrit(COMPONENT_RECOVERY,
				"Unexpected return from snprintf %d error %s (%d)",
				rc, strerror(errno), errno);
		}
		break;
	case EVENT_TAKE_IP:
		if (nfs_param.nfsv4_param.recovery_backend_ipbased) {
			rc = snprintf(path, sizeof(path), "%s", recov_dir);

			if (unlikely(rc >= sizeof(path))) {
				LogCrit(COMPONENT_RECOVERY, "Path %s too long",
					path);
				return;
			} else if (unlikely(rc < 0)) {
				LogCrit(COMPONENT_RECOVERY,
					"Unexpected return from snprintf %d error %s (%d)",
					rc, strerror(errno), errno);
			}
		} else {
			rc = snprintf(path, sizeof(path), "%s/%s/%s",
				      nfs_param.nfsv4_param.recov_root,
				      gsp->ipaddr,
				      nfs_param.nfsv4_param.recov_dir);

			if (unlikely(rc >= sizeof(path))) {
				LogCrit(COMPONENT_RECOVERY,
					"Path %s/%s/%s too long",
					nfs_param.nfsv4_param.recov_root,
					gsp->ipaddr,
					nfs_param.nfsv4_param.recov_dir);
				return;
			} else if (unlikely(rc < 0)) {
				LogCrit(COMPONENT_RECOVERY,
					"Unexpected return from snprintf %d error %s (%d)",
					rc, strerror(errno), errno);
			}
		}
		break;
	case EVENT_TAKE_NODEID:
		rc = snprintf(path, sizeof(path), "%s/%s/node%d",
			      nfs_param.nfsv4_param.recov_root,
			      nfs_param.nfsv4_param.recov_dir, gsp->nodeid);

		if (unlikely(rc >= sizeof(path))) {
			LogCrit(COMPONENT_RECOVERY,
				"Path %s/%s/node%d too long",
				nfs_param.nfsv4_param.recov_root,
				nfs_param.nfsv4_param.recov_dir, gsp->nodeid);
			return;
		} else if (unlikely(rc < 0)) {
			LogCrit(COMPONENT_RECOVERY,
				"Unexpected return from snprintf %d error %s (%d)",
				rc, strerror(errno), errno);
		}
		break;
	default:
		LogWarn(COMPONENT_RECOVERY, "Recovery unknown event");
		return;
	}

	LogEvent(COMPONENT_RECOVERY, "Recovery for nodeid %d dir (%s)",
		 gsp->nodeid, path);

	rc = fs_read_recov_clids_impl(path, NULL, old_dir, 1);
	if (rc == -1) {
		LogEvent(COMPONENT_RECOVERY,
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
		LogEvent(
			COMPONENT_RECOVERY,
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

		if (dentp->d_type == DT_REG) {
			/* Remove regular files: revoked file handles */
			/* and reclaim_complete marker now */
			LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
				    "Will remove %s", path);

			if (unlink(path) < 0) {
				LogEvent(COMPONENT_RECOVERY,
					 "unlink of %s failed errno: %s (%d)",
					 path, strerror(errno), errno);
			}
		} else if (dentp->d_type == DT_DIR) {
			/* This is a directory, we need process files in it! */
			fs_clean_old_recov_dir_impl(path);

			LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY,
				    "Will remove %s", path);
			rc = rmdir(path);

			if (rc == -1) {
				LogEvent(COMPONENT_RECOVERY,
					 "Failed to remove %s, errno: %s (%d)",
					 path, strerror(errno), errno);
			}
		} else {
			LogEvent(COMPONENT_RECOVERY,
				 "unknown dentry type %c:%s", dentp->d_type,
				 path);
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
	char path[PATH_MAX] = { 0 };
	int length, position = 0, pathpos, rhdlstr_len;
	int fd;
	int __attribute__((unused)) retval;
	char client_str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = { sizeof(client_str), client_str,
					 client_str };

	display_client_id_rec(&dspbuf, delr_clid);
	LogDebugAlt(COMPONENT_CLIENTID, COMPONENT_RECOVERY, " %s", client_str);

	/* Convert nfs_fh4_val into base64 encoded string */
	retval = base64url_encode(delr_handle->nfs_fh4_val,
				  delr_handle->nfs_fh4_len, rhdlstr,
				  sizeof(rhdlstr));
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
				LogCrit(COMPONENT_RECOVERY,
					"Could not revoke path %s/%s/%s too long",
					path,
					delr_clid->cid_recov_tag + position,
					rhdlstr);
			}
			path[pathpos++] = '/';
			memcpy(path + pathpos,
			       delr_clid->cid_recov_tag + position, len);
			/* Prefix 1 to converted fh */
			memcpy(path + pathpos + len, "/\x1", 2);
			memcpy(path + pathpos + len + 2, rhdlstr,
			       rhdlstr_len + 1);
			fd = creat(path, 0700);
			if (fd < 0) {
				LogEvent(
					COMPONENT_RECOVERY,
					"Failed to record revoke errno: %s (%d)",
					strerror(errno), errno);
			} else {
				close(fd);
			}
			return;
		}
		path[pathpos++] = '/';
		memcpy(path + pathpos, delr_clid->cid_recov_tag + position,
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
	.reclaim_complete = fs_reclaim_complete,
	.add_clid = fs_add_clid,
	.rm_clid = fs_rm_clid,
	.add_revoke_fh = fs_add_revoke_fh,
};

void fs_backend_init(struct nfs4_recovery_backend **backend)
{
	*backend = &fs_backend;
}
