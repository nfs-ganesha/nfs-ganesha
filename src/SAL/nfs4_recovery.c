/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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

#define NFS_V4_RECOV_ROOT "/var/lib/nfs/ganesha"
#define NFS_V4_RECOV_DIR "v4recov"
#define NFS_V4_OLD_DIR "v4old"

char v4_recov_dir[PATH_MAX + 1];
char v4_old_dir[PATH_MAX + 1];

/**
 * @brief Grace period control structure
 *
 * This could be expanded to implement grace instances, where a new
 * grace period is started for every failover.  for now keep it
 * simple, just a global used by all clients.
 */
typedef struct grace
{
        pthread_mutex_t g_mutex; /*< Mutex */
        time_t g_start; /*< Start of grace period */
        time_t g_duration; /*< Duration of grace period */
        struct glist_head g_clid_list; /*< Clients*/
} grace_t;

/**
 * @brief Grace period control data
 */
static grace_t grace;

/**
 * @brief A client entry
 */
typedef struct clid_entry
{
        struct glist_head cl_list; /*< Link in the list */
        char cl_name[256]; /*< Client name */
} clid_entry_t;

static void nfs4_load_recov_clids_nolock(nfs_grace_start_t *gsp);
extern hash_table_t *ht_nsm_client;
static void nfs_release_nlm_state();
static void nfs_release_v4_client(char *ip);

/**
 * @brief Initialize grace/recovery
 */
void
nfs4_init_grace()
{
        glist_init(&grace.g_clid_list);
        pthread_mutex_init(&grace.g_mutex, NULL);
}

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
        P(grace.g_mutex);

        /* grace should always be greater than or equal to lease time,
         * some clients are known to have problems with grace greater than 60 seconds
         * Lease_Lifetime should be set to a smaller value for those setups. 
         */
        grace.g_start = time(NULL);
        grace.g_duration = nfs_param.nfsv4_param.lease_lifetime;

        LogEvent(COMPONENT_STATE,
                 "NFS Server Now IN GRACE, duration %d",
                 (int) grace.g_duration);
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
			nfs_release_nlm_state();
			if (gsp->event == EVENT_RELEASE_IP)
				nfs_release_v4_client(gsp->ipaddr);
			else
				nfs4_load_recov_clids_nolock(gsp);
		}
        }
        V(grace.g_mutex);
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

        P(grace.g_mutex);

        in_grace = ((grace.g_start + grace.g_duration) > time(NULL));

        if(in_grace != last_grace)
          {
            LogEvent(COMPONENT_STATE,
                     "NFS Server Now %s",
                     in_grace ? "IN GRACE" : "NOT IN GRACE");
            last_grace = in_grace;
          }
        else if(in_grace)
          {
            LogDebug(COMPONENT_STATE,
                    "NFS Server IN GRACE");
          }

        V(grace.g_mutex);

        return in_grace;
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
void
nfs4_create_clid_name(nfs_client_record_t *cl_rec, nfs_client_id_t *clientid,
		      struct svc_req *svc)
{
        int i;
        sockaddr_t sa;
        char buf[SOCK_NAME_MAX + 1];
        longlong_t cl_val = 0;

        clientid->cid_recov_dir = gsh_malloc(256);
        if (clientid->cid_recov_dir == NULL) {
                LogEvent(COMPONENT_CLIENTID, "Mem_Alloc FAILED");
                return;
        }
        /* get the caller's IP addr */
        if (copy_xprt_addr(&sa, svc->rq_xprt))
                sprint_sockip(&sa, buf, SOCK_NAME_MAX);
        else
                strmaxcpy(buf, "Unknown", SOCK_NAME_MAX);

        for (i = 0; i < cl_rec->cr_client_val_len; i++)
                cl_val += cl_rec->cr_client_val[i];

        snprintf(clientid->cid_recov_dir, 256, "%s-%llx", buf, cl_val);

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
void
nfs4_create_clid_name41(nfs_client_record_t *cl_rec, nfs_client_id_t *clientid)
{
        int i;
        char buf[SOCK_NAME_MAX + 1];
        longlong_t cl_val = 0;

        clientid->cid_recov_dir = gsh_malloc(256);
        if (clientid->cid_recov_dir == NULL) {
                LogEvent(COMPONENT_CLIENTID, "Mem_Alloc FAILED");
                return;
        }
        /* get the caller's IP addr */
	sprint_sockip(&clientid->cid_client_addr, buf, sizeof(buf));

        for (i = 0; i < cl_rec->cr_client_val_len; i++)
                cl_val += cl_rec->cr_client_val[i];

        snprintf(clientid->cid_recov_dir, 256, "%s-%llx", buf, cl_val);

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
void
nfs4_add_clid(nfs_client_id_t *clientid)
{
        int err;
        char path[PATH_MAX + 1];

        if (clientid->cid_minorversion > 0)
		nfs4_create_clid_name41(clientid->cid_client_record, clientid);

        if (clientid->cid_recov_dir == NULL) {
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to create client in recovery dir, no name");
                return;
        }

	snprintf(path, sizeof(path), "%s/%s", v4_recov_dir,
		 clientid->cid_recov_dir);

        err = mkdir(path, 0700);
        if (err == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to create client in recovery dir (%s), errno=%d",
                    path, errno);
        } else {
                LogDebug(COMPONENT_CLIENTID, "Created client dir [%s]", path);
        }
}

/**
 * @brief Remove a client entry from the recovery directory
 *
 * This function would be called when a client expires.
 *
 * @param[in] recov_dir Recovery directory
 */
void
nfs4_rm_clid(char *recov_dir)
{
        int err;
        char path[PATH_MAX + 1];

        if (recov_dir == NULL)
                return;

	snprintf(path, sizeof(path), "%s/%s", v4_recov_dir, recov_dir);

        err = rmdir(path);
        if (err == -1) {
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to remove client in recovery dir (%s), errno=%d",
                    path, errno);
        }
}

/**
 * @brief Determine whether or not this client may reclaim state
 *
 * If the server is not in grace period, then no reclaim can happen.
 *
 * @param[in] clientid Client record
 */
void
nfs4_chk_clid(nfs_client_id_t *clientid)
{
        struct glist_head *node;
        clid_entry_t *clid_ent;

        LogDebug(COMPONENT_CLIENTID, "chk for %s", clientid->cid_recov_dir);
        if (clientid->cid_recov_dir == NULL)
                return;

        /* If we aren't in grace period, then reclaim is not possible */
        if (!nfs_in_grace())
                return;

        P(grace.g_mutex);

        /* If there were no clients at time of restart, we're done */
        if (glist_empty(&grace.g_clid_list)) {
                V(grace.g_mutex);
                return;
        }

        /*
         * loop through the list and try to find this client.  if we
         * find it, mark it to allow reclaims.  perhaps the client should
         * be removed from the list at this point to make the list shorter?
         */
        glist_for_each(node, &grace.g_clid_list) {
                clid_ent = glist_entry(node, clid_entry_t, cl_list);
                LogDebug(COMPONENT_CLIENTID, "compare %s to %s",
                    clid_ent->cl_name, clientid->cid_recov_dir);
                if (!strncmp(clid_ent->cl_name, clientid->cid_recov_dir,
                    256)) {
                        if (isDebug(COMPONENT_CLIENTID)) {
                            char str[HASHTABLE_DISPLAY_STRLEN];

                            display_client_id_rec(clientid, str);

                            LogFullDebug(COMPONENT_CLIENTID,
                                         "Allowed to reclaim ClientId %s",
                                         str);
                        }
                        clientid->cid_allow_reclaim = 1;
                        V(grace.g_mutex);
                        return;
                }
        }
        V(grace.g_mutex);
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
static int
nfs4_read_recov_clids(DIR *dp, char *srcdir, bool takeover)
{
        struct dirent *dentp;
        clid_entry_t *new_ent;
        char src[PATH_MAX + 1], dest[PATH_MAX + 1];
        int rc;

        dentp = readdir(dp);
        while (dentp != NULL) {
                /* don't add '.' and '..', or any '.*' entry */
                if (dentp->d_name[0] != '.') {
                        new_ent = gsh_malloc(sizeof(clid_entry_t));
                        if (new_ent == NULL) {
                                LogEvent(COMPONENT_CLIENTID,
                                         "Unable to allocate memory.");
                                return -1;
                        }
                        strmaxcpy(new_ent->cl_name, dentp->d_name, 256);
                        glist_add(&grace.g_clid_list, &new_ent->cl_list);
                        LogDebug(COMPONENT_CLIENTID, "added %s to clid list",
                            new_ent->cl_name);
                        if (srcdir != NULL) {
                                snprintf(src, sizeof(src), "%s/%s",
					 srcdir, dentp->d_name);
                                snprintf(dest, sizeof(dest), "%s/%s",
					 v4_old_dir, dentp->d_name);
                                if (takeover)
                                        rc = mkdir(dest, 0700);
                                else
                                        rc = rename(src, dest);
                                if (rc == -1) {
                                        LogEvent(COMPONENT_CLIENTID,
                                          "Failed to make dir (%s), errno=%d",
                                          dest, errno);
                                }
                        }
                }
                dentp = readdir(dp);
        }

        return 0;
}

/**
 * @brief Load clients for recovery, with no lock
 *
 * @param[in] nodeid Node, on takeover
 */
static void
nfs4_load_recov_clids_nolock(nfs_grace_start_t *gsp)
{
        DIR *dp;
        struct glist_head *node;
        clid_entry_t *clid_entry;
        int rc;
        char path[PATH_MAX + 1];

	LogDebug(COMPONENT_STATE,
		"Load recovery cli %p", gsp);

        if (gsp == NULL) {
                /* when not doing a takeover, start with an empty list */
                if (!glist_empty(&grace.g_clid_list)) {
                        glist_for_each(node, &grace.g_clid_list) {
                                glist_del(node);
                                clid_entry = glist_entry(node,
                                    clid_entry_t, cl_list);
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
                rc = nfs4_read_recov_clids(dp, NULL, 0);
                if (rc == -1) {
                        (void) closedir(dp);
                        LogEvent(COMPONENT_CLIENTID,
                            "Failed to read v4 recovery dir (%s)", v4_old_dir);
                        return;
                }
                (void) closedir(dp);

                dp = opendir(v4_recov_dir);
                if (dp == NULL) {
                        LogEvent(COMPONENT_CLIENTID,
                            "Failed to open v4 recovery dir (%s), errno=%d",
                            v4_recov_dir, errno);
                        return;
                }

                rc = nfs4_read_recov_clids(dp, v4_recov_dir, 0);
                if (rc == -1) {
                        (void) closedir(dp);
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
	  		   NFS_V4_RECOV_ROOT, gsp->ipaddr, NFS_V4_RECOV_DIR);
	  		
                else if (gsp->event == EVENT_TAKE_NODEID)
                  snprintf(path, sizeof(path), "%s/%s/node%d",
			   NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR, gsp->nodeid);
			
                else
                        return;

                LogEvent(COMPONENT_CLIENTID,
                         "Recovery for nodeid %d dir (%s)",
                          gsp->nodeid, path);

                dp = opendir(path);
                if (dp == NULL) {
                        LogEvent(COMPONENT_CLIENTID,
                            "Failed to open v4 recovery dir (%s), errno=%d",
                            path, errno);
                        return;
                }

                rc = nfs4_read_recov_clids(dp, path, 1);
                if (rc == -1) {
                        (void) closedir(dp);
                        LogEvent(COMPONENT_CLIENTID,
                            "Failed to read v4 recovery dir (%s)",
                            path);
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
void
nfs4_load_recov_clids(nfs_grace_start_t *gsp)
{
        P(grace.g_mutex);

        nfs4_load_recov_clids_nolock(gsp);

        V(grace.g_mutex);
}

/**
 * @brief Clean up recovery directory
 */
void
nfs4_clean_old_recov_dir(void)
{
        DIR *dp;
        struct dirent *dentp;
        char path[PATH_MAX + 1];
        int rc;

        dp = opendir(v4_old_dir);
        if (dp == NULL) {
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to open old v4 recovery dir (%s), errno=%d",
                    v4_old_dir, errno);
                return;
        }

        for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
                /* don't remove '.' and '..', or any '.*' entry */
                if (dentp->d_name[0] == '.')
                        continue;

                snprintf(path, sizeof(path), "%s/%s",
			 v4_old_dir, dentp->d_name);

                rc = rmdir(path);
                if (rc == -1) {
                        LogEvent(COMPONENT_CLIENTID,
                            "Failed to remove %s, errno=%d",
                            path, errno);
                }
        }
        closedir(dp);
}

/**
 * @brief Create the recovery directory
 *
 * The recovery directory may not exist yet, so create it.  This
 * should only need to be done once (if at all).  Also, the location
 * of the directory could be configurable.
 */
void
nfs4_create_recov_dir(void)
{
        int err;

        err = mkdir(NFS_V4_RECOV_ROOT, 0755);
        if (err == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to create v4 recovery dir (%s), errno=%d",
                    NFS_V4_RECOV_ROOT, errno);
        }

        snprintf(v4_recov_dir, sizeof(v4_recov_dir), "%s/%s",
		 NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR);
        err = mkdir(v4_recov_dir, 0755);
        if (err == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to create v4 recovery dir(%s), errno=%d",
                    v4_recov_dir, errno);
        }

        snprintf(v4_old_dir, sizeof(v4_old_dir), "%s/%s",
		 NFS_V4_RECOV_ROOT, NFS_V4_OLD_DIR);
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
 * @brief Release all NLM state
 */
static void
nfs_release_nlm_state()
{
        hash_table_t *ht = ht_nsm_client;
        state_nsm_client_t *nsm_cp;
        struct rbt_head *head_rbt;
        struct rbt_node *pn;
        struct hash_data *pdata;
        state_status_t err;
        int i;

        LogDebug(COMPONENT_STATE, "Release all NLM locks");

        cancel_all_nlm_blocked();

        /* walk the client list and call state_nlm_notify */
        for(i = 0; i < ht->parameter.index_size; i++) {
                PTHREAD_RWLOCK_wrlock(&ht->partitions[i].lock);
                head_rbt = &ht->partitions[i].rbt;
                /* go through all entries in the red-black-tree*/
                RBT_LOOP(head_rbt, pn) {
                        pdata = RBT_OPAQ(pn);

                        nsm_cp = (state_nsm_client_t *)pdata->val.addr;
                        inc_nsm_client_ref(nsm_cp);
                        PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);
                        err = state_nlm_notify(nsm_cp, NULL, NULL);
                        if (err != STATE_SUCCESS)
                                LogDebug(COMPONENT_STATE,
                                         "state_nlm_notify failed with %d",
                                         err);
                        dec_nsm_client_ref(nsm_cp);
                        PTHREAD_RWLOCK_wrlock(&ht->partitions[i].lock);
                        RBT_INCREMENT(pn);
                }
                PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);
        }
        return;
}

static int
ip_match(char *ip, nfs_client_id_t *cid)
{
	LogDebug(COMPONENT_STATE,
		"NFS Server V4 match ip %s with (%s) or (%s)",
		 ip, cid->cid_server_owner, cid->cid_client_record->cr_client_val);

        if (strlen(ip) == 0)  /* No IP all are matching */
                return 1;

        if ((strlen(cid->cid_server_owner) > 0) &&  /* Set only for v4.1 */
                  (strncmp(ip, cid->cid_server_owner,
                           strlen(cid->cid_server_owner)) == 0))
                return 1;

        if (strstr(cid->cid_client_record->cr_client_val, ip) != NULL)
                return 1;

        return 0; /* no match */
}

/*
 * try to find a V4 client that matches the IP we are releasing.
 * only search the confirmed clients, unconfirmed clients won't
 * have any state to release.
 */
static void
nfs_release_v4_client(char *ip)
{
        hash_table_t *ht = ht_confirmed_client_id;
        struct rbt_head *head_rbt;
        struct rbt_node *pn;
        struct hash_data *pdata;
        nfs_client_id_t *cp;
        nfs_client_record_t *recp;
        int i;

	LogEvent(COMPONENT_STATE,
		"NFS Server V4 recovery release ip %s", ip);

        /* go through the confirmed clients looking for a match */
        for(i = 0; i < ht->parameter.index_size; i++) {

                PTHREAD_RWLOCK_wrlock(&ht->partitions[i].lock);
                head_rbt = &ht->partitions[i].rbt;

                /* go through all entries in the red-black-tree*/
                RBT_LOOP(head_rbt, pn) {
                    pdata = RBT_OPAQ(pn);

                    cp = (nfs_client_id_t *)pdata->val.addr;
                    P(cp->cid_mutex);
                    if (ip_match(ip, cp)) {
                        inc_client_id_ref(cp);

                        /* Take a reference to the client record */
                        recp = cp->cid_client_record;
                        inc_client_record_ref(recp);

                        V(cp->cid_mutex);

                        PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);

                        P(recp->cr_mutex);

                        (void) nfs_client_id_expire(cp, NULL);

                        V(recp->cr_mutex);

                        dec_client_id_ref(cp);
                        dec_client_record_ref(recp);
                        return;

                    } else {
                        V(cp->cid_mutex);
                    }
                    RBT_INCREMENT(pn);
                }
                PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);
        }
}


/** @} */
