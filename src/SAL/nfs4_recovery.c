/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * ---------------------------------------
 */

/**
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file nfs4_recovery.c
 * @brief NFSv4 recovery
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

#define NFS_V4_RECOV_DIR "v4recov"
#define NFS_V4_OLD_DIR "v4old"

char v4_recov_dir[PATH_MAX];
char v4_old_dir[PATH_MAX];

/**
 * @brief Grace period control data
 */
static grace_t grace = {
	.g_clid_list = GLIST_HEAD_INIT(grace.g_clid_list),
	.g_mutex = PTHREAD_MUTEX_INITIALIZER
};

static void nfs4_load_recov_clids_nolock(nfs_grace_start_t *gsp);
static void nfs_release_nlm_state(char *release_ip);
static void nfs_release_v4_client(char *ip);

/**
 * @brief Start grace period
 *
 * This routine can be called due to server start/restart or from
 * failover code.  If this node is taking over for a node, that nodeid
 * will be passed to this routine inside of the grace start structure.
 *
 * @param[in] gsp Grace period start information
 */
void nfs4_start_grace(nfs_grace_start_t *gsp)
{
	pthread_mutex_lock(&grace.g_mutex);

	/* grace should always be greater than or equal to lease time,
	 * some clients are known to have problems with grace greater than 60
	 * seconds Lease_Lifetime should be set to a smaller value for those
	 * setups.
	 */
	grace.g_start = time(NULL);
	grace.g_duration = nfs_param.nfsv4_param.lease_lifetime;

	LogEvent(COMPONENT_STATE, "NFS Server Now IN GRACE, duration %d",
		 (int)grace.g_duration);
	/*
	 * if called from failover code and given a nodeid, then this node
	 * is doing a take over.  read in the client ids from the failing node
	 */
	if (gsp && gsp->event != EVENT_JUST_GRACE) {
		LogEvent(COMPONENT_STATE,
			 "NFS Server recovery event %d nodeid %d ip %s",
			 gsp->event, gsp->nodeid, gsp->ipaddr);

		if (gsp->event == EVENT_CLEAR_BLOCKED)
			cancel_all_nlm_blocked();
		else {
			nfs_release_nlm_state(gsp->ipaddr);
			if (gsp->event == EVENT_RELEASE_IP)
				nfs_release_v4_client(gsp->ipaddr);
			else
				nfs4_load_recov_clids_nolock(gsp);
		}
	}
	pthread_mutex_unlock(&grace.g_mutex);
}

int last_grace = -1;

/**
 * @brief Check if we are in the grace period
 *
 * @retval true if so.
 * @retval false if not.
 */
int nfs_in_grace(void)
{
	int in_grace;

	if (nfs_param.nfsv4_param.graceless)
		return 0;

	pthread_mutex_lock(&grace.g_mutex);

	in_grace = ((grace.g_start + grace.g_duration) > time(NULL));

	if (in_grace != last_grace) {
		LogEvent(COMPONENT_STATE, "NFS Server Now %s",
			 in_grace ? "IN GRACE" : "NOT IN GRACE");
		last_grace = in_grace;
	} else if (in_grace) {
		LogDebug(COMPONENT_STATE, "NFS Server IN GRACE");
	}

	pthread_mutex_unlock(&grace.g_mutex);

	return in_grace;
}

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
int convert_opaque_value_max_for_dir(struct display_buffer *dspbuf,
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
 * @param[in] cl_rec   Client name record
 * @param[in] clientid Client record
 * @param[in] svc      RPC transport
 */
void nfs4_create_clid_name(nfs_client_record_t *cl_rec,
			   nfs_client_id_t *clientid, struct svc_req *svc)
{
	sockaddr_t sa;
	char buf[SOCK_NAME_MAX + 1];
	char cidstr[PATH_MAX];
	struct display_buffer       dspbuf = {sizeof(cidstr), cidstr, cidstr};
	char                         cidstr_len[10];
	int total_len;

	/* get the caller's IP addr */
	if (copy_xprt_addr(&sa, svc->rq_xprt))
		sprint_sockip(&sa, buf, SOCK_NAME_MAX);
	else
		strmaxcpy(buf, "Unknown", SOCK_NAME_MAX);

	if (convert_opaque_value_max_for_dir(&dspbuf,
					     cl_rec->cr_client_val,
					     cl_rec->cr_client_val_len,
					     PATH_MAX) > 0) {
		/* convert_opaque_value_max_for_dir does not prefix
		 * the "(<length>:". So we need to do it here */
		sprintf(cidstr_len, "%ld", strlen(cidstr));
		total_len = strlen(cidstr) + strlen(buf) + 5 +
			    strlen(cidstr_len);
		/* hold both long form clientid and IP */
		clientid->cid_recov_dir = gsh_malloc(total_len);
		if (clientid->cid_recov_dir == NULL) {
			LogEvent(COMPONENT_CLIENTID, "Mem_Alloc FAILED");
			return;
		}
		memset(clientid->cid_recov_dir, 0, total_len);

		(void) snprintf(clientid->cid_recov_dir, total_len,
				"%s-(%s:%s)",
				buf, cidstr_len, cidstr);
	}

	LogDebug(COMPONENT_CLIENTID, "Created client name [%s]",
		 clientid->cid_recov_dir);
}

/**
 * @brief generate a name that identifies this 4.1 client
 *
 * This name will be used to know that a client was talking to the
 * server before a restart so that it will be allowed to do reclaims
 * during grace period.
 *
 * @param[in] cl_rec   Client name record
 * @param[in] clientid Client record
 */
void nfs4_create_clid_name41(nfs_client_record_t *cl_rec,
			     nfs_client_id_t *clientid)
{
	char buf[SOCK_NAME_MAX + 1];
	char cidstr[PATH_MAX];
	struct display_buffer       dspbuf = {sizeof(cidstr), cidstr, cidstr};
	char                         cidstr_len[10];
	int total_len;

	/* get the caller's IP addr */
	sprint_sockip(&clientid->cid_client_addr, buf, sizeof(buf));

	if (convert_opaque_value_max_for_dir(&dspbuf,
					     cl_rec->cr_client_val,
					     cl_rec->cr_client_val_len,
					     PATH_MAX) > 0) {
		/* convert_opaque_value_max_for_dir does not prefix
		 * the "(<length>:". So we need to do it here */
		sprintf(cidstr_len, "%ld", strlen(cidstr));
		total_len = strlen(cidstr) + strlen(buf) + 5 +
			    strlen(cidstr_len);
		/* hold both long form clientid and IP */
		clientid->cid_recov_dir = gsh_malloc(total_len);
		if (clientid->cid_recov_dir == NULL) {
			LogEvent(COMPONENT_CLIENTID, "Mem_Alloc FAILED");
			return;
		}
		memset(clientid->cid_recov_dir, 0, total_len);

		(void) snprintf(clientid->cid_recov_dir, total_len,
				"%s-(%s:%s)",
				buf, cidstr_len, cidstr);
	}

	LogDebug(COMPONENT_CLIENTID, "Created client name [%s]",
		 clientid->cid_recov_dir);
}

/**
 * @brief Create an entry in the recovery directory
 *
 * This entry alows the client to reclaim state after a server
 * reboot/restart.
 *
 * @param[in] clientid Client record
 */
void nfs4_add_clid(nfs_client_id_t *clientid)
{
	int err = 0;
	char path[PATH_MAX] = {0}, segment[NAME_MAX + 1] = {0};
	int length, position = 0;

	if (clientid->cid_minorversion > 0)
		nfs4_create_clid_name41(clientid->cid_client_record, clientid);

	if (clientid->cid_recov_dir == NULL) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to create client in recovery dir, no name");
		return;
	}

	/* break clientid down if it is greater than max dir name */
	/* and create a directory hierachy to represent the clientid. */
	snprintf(path, sizeof(path), "%s", v4_recov_dir);

	length = strlen(clientid->cid_recov_dir);
	while (position < length) {
		/* if the (remaining) clientid is shorter than 255 */
		/* create the last level of dir and break out */
		int len = strlen(&clientid->cid_recov_dir[position]);
		if (len <= NAME_MAX) {
			strcat(path, "/");
			strncat(path, &clientid->cid_recov_dir[position], len);
			err = mkdir(path, 0700);
			break;
		}
		/* if (remaining) clientid is longer than 255, */
		/* get the next 255 bytes and create a subdir */
		strncpy(segment, &clientid->cid_recov_dir[position], NAME_MAX);
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

void nfs4_rm_revoked_handles(char *path)
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
		sprintf(del_path, "%s/%s", path, dentp->d_name);
		if (unlink(del_path) < 0) {
			LogEvent(COMPONENT_CLIENTID,
					"unlink of %s failed errno: %d",
					del_path,
					errno);
		}
	}
	(void)closedir(dp);
}

/**
 * @brief Remove a client entry from the recovery directory
 *
 * This function would be called when a client expires.
 *
 * @param[in] recov_dir Recovery directory
 */
void nfs4_rm_clid(const char *recov_dir, char *parent_path, int position)
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
		nfs4_rm_revoked_handles(parent_path);
		return;
	}
	segment = gsh_malloc(NAME_MAX+1);
	if (segment == NULL) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to remove client in recovery dir (%s), ENOMEM",
			  recov_dir);
		return;
	}

	memset(segment, 0, NAME_MAX+1);
	strncpy(segment, &recov_dir[position], NAME_MAX);
	segment_len = strlen(segment);

	/* allocate enough memory for the new part of the string */
	/* which is parent path + '/' + new segment */
	total_len = strlen(parent_path) + segment_len + 2;
	path = gsh_malloc(total_len);
	if (path == NULL) {
		LogEvent(COMPONENT_CLIENTID,
			 "Failed to remove client in recovery dir (%s), ENOMEM",
			  recov_dir);
		gsh_free(segment);
		return;
	}
	memset(path, 0, total_len);
	(void) snprintf(path, total_len, "%s/%s",
			parent_path, segment);
	/* free setment as it has no use now */
	gsh_free(segment);

	/* recursively remove the directory hirerchy which represent the
	 *clientid
	 */
	nfs4_rm_clid(recov_dir, path, position+segment_len);

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

/**
 * @brief Determine whether or not this client may reclaim state
 *
 * If the server is not in grace period, then no reclaim can happen.
 *
 * @param[in] clientid Client record
 */
void  nfs4_chk_clid_impl(nfs_client_id_t *clientid, clid_entry_t **clid_ent_arg)
{
	struct glist_head *node;
	clid_entry_t *clid_ent;
	*clid_ent_arg = NULL;

	LogDebug(COMPONENT_CLIENTID, "chk for %s", clientid->cid_recov_dir);
	if (clientid->cid_recov_dir == NULL)
		return;

	/* If there were no clients at time of restart, we're done */
	if (glist_empty(&grace.g_clid_list))
		return;

	/*
	 * loop through the list and try to find this client.  if we
	 * find it, mark it to allow reclaims.  perhaps the client should
	 * be removed from the list at this point to make the list shorter?
	 */
	glist_for_each(node, &grace.g_clid_list) {
		clid_ent = glist_entry(node, clid_entry_t, cl_list);
		LogDebug(COMPONENT_CLIENTID, "compare %s to %s",
			 clid_ent->cl_name, clientid->cid_recov_dir);
		if (!strncmp(clid_ent->cl_name,
			     clientid->cid_recov_dir,
			     PATH_MAX)) {
			if (isDebug(COMPONENT_CLIENTID)) {
				char str[HASHTABLE_DISPLAY_STRLEN];

				display_client_id_rec(clientid, str);

				LogFullDebug(COMPONENT_CLIENTID,
					     "Allowed to reclaim ClientId %s",
					     str);
			}
			clientid->cid_allow_reclaim = 1;
			*clid_ent_arg = clid_ent;
			return;
		}
	}
}

void  nfs4_chk_clid(nfs_client_id_t *clientid)
{
	clid_entry_t *dummy_clid_ent;

	/* If we aren't in grace period, then reclaim is not possible */
	if (!nfs_in_grace())
		return;
	pthread_mutex_lock(&grace.g_mutex);
	nfs4_chk_clid_impl(clientid, &dummy_clid_ent);
	pthread_mutex_unlock(&grace.g_mutex);
	return;
}

static void free_heap(char *path, char *new_path, char *build_clid)
{
	if (path)
		gsh_free(path);
	if (new_path)
		gsh_free(new_path);
	if (build_clid)
		gsh_free(build_clid);
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

void nfs4_cp_pop_revoked_delegs(clid_entry_t *clid_ent,
				char *path,
				char *tgtdir,
				bool del)
{
	struct dirent *dentp;
	DIR *dp;
	rdel_fh_t *new_ent;

	glist_init(&clid_ent->cl_rfh_list);
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
			sprintf(lopath, "%s/", tgtdir);
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

		new_ent = gsh_malloc(sizeof(rdel_fh_t));
		if (new_ent == NULL) {
			LogEvent(COMPONENT_CLIENTID, "Alloc Failed: rdel_fh_t");
			continue;
		}

		/* Ignore the beginning \x1 and copy the rest (file handle) */
		new_ent->rdfh_handle_str = gsh_strdup(dentp->d_name+1);
		if (new_ent->rdfh_handle_str == NULL) {
			gsh_free(new_ent);
			LogEvent(COMPONENT_CLIENTID,
				"Alloc Failed: rdel_fh_t->rdfh_handle_str");
			continue;
		}
		glist_add(&clid_ent->cl_rfh_list, &new_ent->rdfh_list);
		LogFullDebug(COMPONENT_CLIENTID,
			"revoked handle: %s",
			new_ent->rdfh_handle_str);

		/* Since the handle is loaded into memory, go ahead and
		 * delete it from the stable storage.
		 */
		if (del) {
			char del_path[PATH_MAX];
			sprintf(del_path, "%s/%s", path, dentp->d_name);
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
static int nfs4_read_recov_clids(DIR *dp,
				 const char *parent_path,
				 char *clid_str,
				 char *tgtdir,
				 int takeover)
{
	struct dirent *dentp;
	DIR *subdp;
	clid_entry_t *new_ent;
	char *path = NULL;
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
		path = gsh_malloc(total_len);
		/* if failed on this subdirectory, move to next */
		/* we might be lucky */
		if (path == NULL) {
			LogEvent(COMPONENT_CLIENTID,
				 "malloc faied errno=%d", errno);
			continue;
		}
		memset(path, 0, total_len);

		strcpy(path, parent_path);
		strcat(path, "/");
		strncat(path, dentp->d_name, segment_len);
		/* if tgtdir is not NULL, we need to build
		 * nfs4old/currentnode
		 */
		if (tgtdir) {
			total_tgt_len = segment_len + 2 +
					strlen(tgtdir);
			new_path = gsh_malloc(total_tgt_len);
			if (new_path == NULL) {
				LogEvent(COMPONENT_CLIENTID,
					 "malloc faied errno=%d",
					 errno);
				gsh_free(path);
				continue;
			}
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
		/* keep building the clientid str by cursively */
		/* reading the directory structure */
		if (clid_str)
			total_clid_len = segment_len + 1 +
					 strlen(clid_str);
		else
			total_clid_len = segment_len + 1;
		build_clid = gsh_malloc(total_clid_len);
		if (build_clid == NULL) {
			LogEvent(COMPONENT_CLIENTID,
				 "malloc faied errno=%d", errno);
			free_heap(path, new_path, NULL);
			continue;
		}
		memset(build_clid, 0, total_clid_len);
		if (clid_str)
			strcpy(build_clid, clid_str);
		strncat(build_clid, dentp->d_name, segment_len);
		subdp = opendir(path);
		if (subdp == NULL) {
			LogEvent(COMPONENT_CLIENTID,
				 "opendir %s failed errno=%d",
				 dentp->d_name, errno);
			free_heap(path, new_path, build_clid);
			/* this shouldn't happen, but we should skip
			 * the entry to avoid infinite loops
			 */
			continue;
		}

		if (tgtdir)
			rc = nfs4_read_recov_clids(subdp,
						   path,
						   build_clid,
						   new_path,
						   takeover);
		else
			rc = nfs4_read_recov_clids(subdp,
						   path,
						   build_clid,
						   NULL,
						   takeover);

		/* close the sub directory */
		(void)closedir(subdp);

		if (new_path)
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
				free_heap(path, NULL, build_clid);
				continue;
			}
			ptr = strchr(build_clid, '(');
			if (ptr == NULL) {
				LogEvent(COMPONENT_CLIENTID,
					 "invalid clid format: %s",
					 build_clid);
				free_heap(path, NULL, build_clid);
				continue;
			}
			ptr2 = strchr(ptr, ':');
			if (ptr2 == NULL) {
				LogEvent(COMPONENT_CLIENTID,
					 "invalid clid format: %s",
					 build_clid);
				free_heap(path, NULL, build_clid);
				continue;
			}
			len = ptr2-ptr-1;
			if (len >= 9) {
				LogEvent(COMPONENT_CLIENTID,
					 "invalid clid format: %s",
					 build_clid);
				free_heap(path, NULL, build_clid);
				continue;
			}
			strncpy(temp, ptr+1, len);
			temp[len] = 0;
			cid_len = atoi(temp);
			len = strlen(ptr2);
			if ((len == (cid_len+2)) &&
			    (ptr2[len-1] == ')')) {
				new_ent =
				    gsh_malloc(sizeof(clid_entry_t));
				if (new_ent == NULL) {
					LogEvent(COMPONENT_CLIENTID,
						 "Unable to allocate memory.");
					free_heap(path,
						  NULL,
						  build_clid);
					continue;
				}
				nfs4_cp_pop_revoked_delegs(new_ent,
							path,
							tgtdir,
							!takeover);
				strcpy(new_ent->cl_name, build_clid);
				glist_add(&grace.g_clid_list,
					  &new_ent->cl_list);
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
			rc = rmdir(path);
			if (rc == -1) {
				LogEvent(COMPONENT_CLIENTID,
					 "Failed to rmdir (%s), errno=%d",
					 path, errno);
			}
		}
		gsh_free(path);
	}

	return num;
}

/**
 * @brief Load clients for recovery, with no lock
 *
 * @param[in] nodeid Node, on takeover
 */
static void nfs4_load_recov_clids_nolock(nfs_grace_start_t *gsp)
{
	DIR *dp;
	struct glist_head *node;
	clid_entry_t *clid_entry;
	int rc;
	char path[PATH_MAX];

	LogDebug(COMPONENT_STATE, "Load recovery cli %p", gsp);

	if (gsp == NULL) {
		/* when not doing a takeover, start with an empty list */
		if (!glist_empty(&grace.g_clid_list)) {
			glist_for_each(node, &grace.g_clid_list) {
				glist_del(node);
				clid_entry =
				    glist_entry(node, clid_entry_t, cl_list);
				gsh_free(clid_entry);
			}
		}

		dp = opendir(v4_old_dir);
		if (dp == NULL) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to open v4 recovery dir (%s), errno=%d",
				 v4_old_dir, errno);
			return;
		}
		rc = nfs4_read_recov_clids(dp, v4_old_dir, NULL, NULL, 0);
		if (rc == -1) {
			(void)closedir(dp);
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to read v4 recovery dir (%s)",
				 v4_old_dir);
			return;
		}
		(void)closedir(dp);

		dp = opendir(v4_recov_dir);
		if (dp == NULL) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to open v4 recovery dir (%s), errno=%d",
				 v4_recov_dir, errno);
			return;
		}

		rc = nfs4_read_recov_clids(dp, v4_recov_dir,
					   NULL, v4_old_dir, 0);
		if (rc == -1) {
			(void)closedir(dp);
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to read v4 recovery dir (%s)",
				 v4_recov_dir);
			return;
		}
		rc = closedir(dp);
		if (rc == -1) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to close v4 recovery dir (%s), errno=%d",
				 v4_recov_dir, errno);
		}

	} else {
		if (gsp->event == EVENT_UPDATE_CLIENTS)
			snprintf(path, sizeof(path), "%s", v4_recov_dir);

		else if (gsp->event == EVENT_TAKE_IP)
			snprintf(path, sizeof(path), "%s/%s/%s",
				 NFS_V4_RECOV_ROOT, gsp->ipaddr,
				 NFS_V4_RECOV_DIR);

		else if (gsp->event == EVENT_TAKE_NODEID)
			snprintf(path, sizeof(path), "%s/%s/node%d",
				 NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR,
				 gsp->nodeid);

		else
			return;

		LogEvent(COMPONENT_CLIENTID, "Recovery for nodeid %d dir (%s)",
			 gsp->nodeid, path);

		dp = opendir(path);
		if (dp == NULL) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to open v4 recovery dir (%s), errno=%d",
				 path, errno);
			return;
		}

		rc = nfs4_read_recov_clids(dp, path, NULL, v4_old_dir, 1);
		if (rc == -1) {
			(void)closedir(dp);
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to read v4 recovery dir (%s)", path);
			return;
		}
		rc = closedir(dp);
		if (rc == -1) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to close v4 recovery dir (%s), errno=%d",
				 path, errno);
		}
	}
}

/**
 * @brief Load clients for recovery
 *
 * @param[in] nodeid Node, on takeover
 */
void nfs4_load_recov_clids(nfs_grace_start_t *gsp)
{
	pthread_mutex_lock(&grace.g_mutex);

	nfs4_load_recov_clids_nolock(gsp);

	pthread_mutex_unlock(&grace.g_mutex);
}

/**
 * @brief Clean up recovery directory
 */
void nfs4_clean_old_recov_dir(char *parent_path)
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

			sprintf(del_path, "%s/%s", parent_path, dentp->d_name);
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
		if (path == NULL) {
			LogEvent(COMPONENT_CLIENTID,
				 "Unable to allocate memory.");
			continue;
		}

		snprintf(path, total_len, "%s/%s", parent_path, dentp->d_name);

		nfs4_clean_old_recov_dir(path);
		rc = rmdir(path);
		if (rc == -1) {
			LogEvent(COMPONENT_CLIENTID,
				 "Failed to remove %s, errno=%d", path, errno);
		}
		gsh_free(path);
	}
	(void)closedir(dp);
}

/**
 * @brief Create the recovery directory
 *
 * The recovery directory may not exist yet, so create it.  This
 * should only need to be done once (if at all).  Also, the location
 * of the directory could be configurable.
 */
void nfs4_create_recov_dir(void)
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
}

/**
 * @brief Record revoked filehandle under the client.
 *
 * @param[in] clientid Client record
 * @param[in] filehandle of the revoked file.
 */
void nfs4_record_revoke(nfs_client_id_t *delr_clid, nfs_fh4 *delr_handle)
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

	/* A client's lease is reserved while recalling or revoking a
	 * delegation which means the client will not expire until we
	 * complete this revoke operation. The only exception is when
	 * the reaper thread revokes delegations of an already expired
	 * client!
	 */
	pthread_mutex_lock(&delr_clid->cid_mutex);
	if (delr_clid->cid_confirmed == EXPIRED_CLIENT_ID) {
		/* Called from reaper thread, no need to record
		 * revoked file handles for an expired client.
		 */
		pthread_mutex_unlock(&delr_clid->cid_mutex);
		return;
	}
	pthread_mutex_unlock(&delr_clid->cid_mutex);

	/* Parse through the clientid directory structure */
	assert(delr_clid->cid_recov_dir != NULL);

	snprintf(path, sizeof(path), "%s", v4_recov_dir);
	length = strlen(delr_clid->cid_recov_dir);
	while (position < length) {
		int len = strlen(&delr_clid->cid_recov_dir[position]);
		if (len <= NAME_MAX) {
			strcat(path, "/");
			strncat(path, &delr_clid->cid_recov_dir[position], len);
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
		strncpy(segment, &delr_clid->cid_recov_dir[position], NAME_MAX);
		strcat(path, "/");
		strncat(path, segment, NAME_MAX);
		position += NAME_MAX;
	}
}

/**
 * @brief Decides if it is allowed to reclaim a given delegation
 *
 * @param[in] clientid Client record
 * @param[in] filehandle of the revoked file.
 * @retval true if allowed and false if not.
 *
 */
bool nfs4_check_deleg_reclaim(nfs_client_id_t *clid, nfs_fh4 *fhandle)
{
	char rhdlstr[NAME_MAX];
	struct glist_head *node;
	rdel_fh_t *rfh_entry;
	clid_entry_t *clid_ent;
	int retval;

	/* If we aren't in grace period, then reclaim is not possible */
	if (!nfs_in_grace())
		return false;

	/* Convert nfs_fh4_val into base64 encoded string */
	retval = base64url_encode(fhandle->nfs_fh4_val, fhandle->nfs_fh4_len,
				  rhdlstr, sizeof(rhdlstr));
	assert(retval != -1);

	pthread_mutex_lock(&grace.g_mutex);
	nfs4_chk_clid_impl(clid, &clid_ent);
	if (clid_ent == NULL || glist_empty(&clid_ent->cl_rfh_list)) {
		pthread_mutex_unlock(&grace.g_mutex);
		return true;
	}

	glist_for_each(node, &clid_ent->cl_rfh_list) {
		rfh_entry = glist_entry(node, rdel_fh_t, rdfh_list);
		assert(rfh_entry != NULL);
		if (!strcmp(rhdlstr, rfh_entry->rdfh_handle_str)) {
			pthread_mutex_unlock(&grace.g_mutex);
			LogFullDebug(COMPONENT_CLIENTID,
				"Can't reclaim revoked fh:%s",
				rfh_entry->rdfh_handle_str);
			return false;
		}
	}

	pthread_mutex_unlock(&grace.g_mutex);
	LogFullDebug(COMPONENT_CLIENTID, "Returning TRUE");
	return true;
}


/**
 * @brief Release NLM state
 */
static void nlm_releasecall(struct fridgethr_context *ctx)
{
	state_nsm_client_t *nsm_cp;
	state_status_t err;

	nsm_cp = ctx->arg;
	err = state_nlm_notify(nsm_cp, false, NULL);
	if (err != STATE_SUCCESS)
		LogDebug(COMPONENT_STATE,
			"state_nlm_notify failed with %d",
			err);
	dec_nsm_client_ref(nsm_cp);
}

void extractv4(char *ipv6, char *ipv4)
{
	char *token, *saveptr;
	char *delim = ":";
	token = strtok_r(ipv6, delim, &saveptr);
	while (token != NULL) {
		/* IPv4 delimiter is '.' */
		if (strchr(token, '.') != NULL) {
			(void)strcpy(ipv4, token);
			return;
		}
		token = strtok_r(NULL, delim, &saveptr);
	}
	/* failed, copy a null string */
	(void)strcpy(ipv4, "");
}

bool ip_str_match(char *release_ip, char *server_ip)
{
	bool ripv6, sipv6;
	char ipv4[SOCK_NAME_MAX + 1];

	/* IPv6 delimiter is ':' */
	ripv6 = (strchr(release_ip, ':') != NULL);
	sipv6 = (strchr(server_ip, ':') != NULL);

	if (ripv6) {
		if (sipv6)
			return !strcmp(release_ip, server_ip);
		else {
			/* extract v4 addr from release_ip*/
			extractv4(release_ip, ipv4);
			return !strcmp(ipv4, server_ip);
		}
	} else {
		if (sipv6) {
			/* extract v4 addr from server_ip*/
			extractv4(server_ip, ipv4);
			return !strcmp(ipv4, release_ip);
		}
	}
	/* Both are ipv4 addresses */
	return !strcmp(release_ip, server_ip);
}

/**
 * @brief Release all NLM state
 */
static void nfs_release_nlm_state(char *release_ip)
{
	hash_table_t *ht = ht_nlm_client;
	state_nlm_client_t *nlm_cp;
	state_nsm_client_t *nsm_cp;
	struct rbt_head *head_rbt;
	struct rbt_node *pn;
	struct hash_data *pdata;
	state_status_t state_status;
	char serverip[SOCK_NAME_MAX + 1];
	int i;

	LogDebug(COMPONENT_STATE, "Release all NLM locks");

	cancel_all_nlm_blocked();

	/* walk the client list and call state_nlm_notify */
	for (i = 0; i < ht->parameter.index_size; i++) {
		PTHREAD_RWLOCK_wrlock(&ht->partitions[i].lock);
		head_rbt = &ht->partitions[i].rbt;
		/* go through all entries in the red-black-tree */
		RBT_LOOP(head_rbt, pn) {
			pdata = RBT_OPAQ(pn);
			nlm_cp = (state_nlm_client_t *) pdata->val.addr;
			sprint_sockip(&(nlm_cp->slc_server_addr),
					serverip,
					SOCK_NAME_MAX + 1);
			if (ip_str_match(release_ip, serverip)) {
				nsm_cp = nlm_cp->slc_nsm_client;
				inc_nsm_client_ref(nsm_cp);
				state_status = fridgethr_submit(
						state_async_fridge,
						nlm_releasecall,
						nsm_cp);
				if (state_status != STATE_SUCCESS) {
					dec_nsm_client_ref(nsm_cp);
					LogCrit(COMPONENT_STATE,
						"failed to submit nlm release thread ");
				}
			}
			RBT_INCREMENT(pn);
		}
		PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);
	}
	return;
}

static int ip_match(char *ip, nfs_client_id_t *cid)
{
	LogDebug(COMPONENT_STATE, "NFS Server V4 match ip %s with (%s) or (%s)",
		 ip, cid->cid_server_owner,
		 cid->cid_client_record->cr_client_val);

	if (strlen(ip) == 0)	/* No IP all are matching */
		return 1;

	if ((strlen(cid->cid_server_owner) > 0) &&	/* Set only for v4.1 */
	    (strncmp(ip, cid->cid_server_owner, strlen(cid->cid_server_owner))
	     == 0))
		return 1;

	if (strstr(cid->cid_client_record->cr_client_val, ip) != NULL)
		return 1;

	return 0;		/* no match */
}

/*
 * try to find a V4 client that matches the IP we are releasing.
 * only search the confirmed clients, unconfirmed clients won't
 * have any state to release.
 */
static void nfs_release_v4_client(char *ip)
{
	hash_table_t *ht = ht_confirmed_client_id;
	struct rbt_head *head_rbt;
	struct rbt_node *pn;
	struct hash_data *pdata;
	nfs_client_id_t *cp;
	nfs_client_record_t *recp;
	int i;

	LogEvent(COMPONENT_STATE, "NFS Server V4 recovery release ip %s", ip);

	/* go through the confirmed clients looking for a match */
	for (i = 0; i < ht->parameter.index_size; i++) {

		PTHREAD_RWLOCK_wrlock(&ht->partitions[i].lock);
		head_rbt = &ht->partitions[i].rbt;

		/* go through all entries in the red-black-tree */
		RBT_LOOP(head_rbt, pn) {
			pdata = RBT_OPAQ(pn);

			cp = (nfs_client_id_t *) pdata->val.addr;
			pthread_mutex_lock(&cp->cid_mutex);
			if ((cp->cid_confirmed == CONFIRMED_CLIENT_ID)
			     && ip_match(ip, cp)) {
				inc_client_id_ref(cp);

				/* Take a reference to the client record */
				recp = cp->cid_client_record;
				inc_client_record_ref(recp);

				pthread_mutex_unlock(&cp->cid_mutex);

				PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);

				pthread_mutex_lock(&recp->cr_mutex);

				nfs_client_id_expire(cp, true);

				pthread_mutex_unlock(&recp->cr_mutex);

				dec_client_id_ref(cp);
				dec_client_record_ref(recp);
				return;

			} else {
				pthread_mutex_unlock(&cp->cid_mutex);
			}
			RBT_INCREMENT(pn);
		}
		PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);
	}
}

/** @} */
