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

/*
 *
 * nfs4_recovery.c : Some functions to manage NFSv4 recovery
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "log.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"

#define NFS_V4_RECOV_ROOT "/var/lib/nfs/ganesha"
#define NFS_V4_RECOV_DIR "v4recov"
#define NFS_V4_OLD_DIR "v4old"

char v4_recov_dir[PATH_MAX];
char v4_old_dir[PATH_MAX];

/*
 * construct to enable grace period, this could be expanded to implement
 * grace instances, where a new grace period is started for every
 * failover.  for now keep it simple, just a global used by all clients.
 */
typedef struct grace
{
        pthread_mutex_t g_mutex;
        time_t g_start;
        time_t g_duration;
        struct glist_head g_clid_list;
} grace_t;

static grace_t grace;

int grace_period = 45;

typedef struct clid_entry
{
        struct glist_head cl_list;
        char cl_name[256];
} clid_entry_t;

static void nfs4_load_recov_clids_nolock(ushort);

void
nfs4_init_grace()
{
        init_glist(&grace.g_clid_list);
        (void)pthread_mutex_init(&grace.g_mutex, NULL);
}

/*
 * Routine to start grace period.  Can be called due to server start/restart
 * or from failover code.  If this node is taking over for a node, that nodeid
 * will be passed to this routine inside of the grace start structure.
 */

void
nfs4_start_grace(nfs_grace_start_t *gsp)
{
        int duration = grace_period;

        P(grace.g_mutex);

        /* if not failover and no clients able to recover, skip grace period */
        if ((gsp == NULL) && glist_empty(&grace.g_clid_list))
                duration = 0;

        /*
         * if called from failover code and given a nodeid, then this node
         * is doing a take over.  read in the client ids from the failing node
         */
        if (gsp && gsp->nodeid != 0)
                nfs4_load_recov_clids_nolock(gsp->nodeid);

        LogDebug(COMPONENT_NFS_V4, "grace period started, duration(%d)",
            duration);

        grace.g_start = time(NULL);
        grace.g_duration = duration;

        V(grace.g_mutex);
}

int
nfs4_in_grace()
{
        int gp;

        P(grace.g_mutex);

        gp = ((grace.g_start + grace.g_duration) > time(NULL));

        V(grace.g_mutex);

        LogDebug(COMPONENT_NFS_V4, "in grace period  == %d", gp);

        return gp;
}

/*
 * generate a name that identifies this client.  this name will be used to
 * know that a client was talking to the server before a restart so that it
 * will be allowed to do reclaims during grace period.
 */
void
nfs4_create_clid_name(nfs_client_id_t *nfs_clientid, struct svc_req *svcp)
{
        sockaddr_t sa;
        char buf[SOCK_NAME_MAX];

        /* get the caller's IP addr */
        if (copy_xprt_addr(&sa, svcp->rq_xprt))
                sprint_sockip(&sa, buf, SOCK_NAME_MAX);
        else
                strncpy(buf, "Unknown", SOCK_NAME_MAX);

        nfs_clientid->recov_dir = Mem_Alloc(256);
        if (nfs_clientid->recov_dir == NULL) {
                LogEvent(COMPONENT_NFS_V4, "Mem_Alloc FAILED");
                return;
        }
        (void) snprintf(nfs_clientid->recov_dir, 256, "%s-%llx", buf,
            (longlong_t)nfs_clientid->clientid);

        LogDebug(COMPONENT_NFS_V4, "Created client name [%s]",
            nfs_clientid->recov_dir);
}

/*
 * create an entry in the recovery directory for this client so that it
 * will be able to reclaim state after a server reboot/restart.
 */
void
nfs4_add_clid(nfs_client_id_t *nfs_clientid)
{
        int err;
        char path[PATH_MAX];

        if (nfs_clientid->recov_dir == NULL) {
                LogDebug(COMPONENT_NFS_V4,
                    "Failed to create client in recovery dir, no name");
                return;
        }

        snprintf(path, PATH_MAX, "%s/%s", v4_recov_dir,
            nfs_clientid->recov_dir);

        err = mkdir(path, 0700);
        if (err == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_NFS_V4,
                    "Failed to create client in recovery dir (%s), errno=%d",
                    path, errno);
        } else {
                LogDebug(COMPONENT_NFS_V4, "Created client dir [%s]", path);
        }
}

/*
 * remove a client entry from the recovery directory.  this would be called
 * when a client expires.
 */
void
nfs4_rm_clid(char *recov_dir)
{
        int err;
        char path[PATH_MAX];

        if (recov_dir == NULL)
                return;

        snprintf(path, PATH_MAX, "%s/%s", v4_recov_dir, recov_dir);

        err = rmdir(path);
        if (err == -1) {
                LogEvent(COMPONENT_NFS_V4,
                    "Failed to remove client in recovery dir (%s), errno=%d",
                    path, errno);
        }
}

/*
 * determine whether or not this client is allowed to do reclaim operations.
 * if the server is not in grace period, then no reclaim can happen.
 */
void
nfs4_chk_clid(nfs_client_id_t *nfs_clientid)
{
        struct glist_head *node;
        clid_entry_t *clid_ent;

        /* If we aren't in grace period, then reclaim is not possible */
        if (!nfs4_in_grace())
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
                LogDebug(COMPONENT_NFS_V4, "compare %s to %s",
                    clid_ent->cl_name, nfs_clientid->recov_dir);
                if (!strncmp(clid_ent->cl_name ,nfs_clientid->recov_dir,
                    256)) {
                        LogDebug(COMPONENT_NFS_V4,
                            "[%s] %llx is allowed to reclaim ",
                            nfs_clientid->client_name,
                            (long long)nfs_clientid->clientid);
                        nfs_clientid->allow_reclaim = 1;
                        V(grace.g_mutex);
                        return;
                }
        }
        V(grace.g_mutex);
}

/*
 * create the client reclaim list.
 * when not doing a take over, first open the old state dir and read in
 * those entries.  the reason for the two directories is in case of a 
 * reboot/restart during grace period.  next, read in entries from the
 * recovery directory and then move them into the old state directory.
 * if called due to a take over, nodeid will be nonzero.  in this case,
 * add that node's clientids to the existing list.  then move those
 * entries into the old state directory.
 */
static int
nfs4_read_recov_clids(DIR *dp, char *srcdir, int takeover)
{
        struct dirent *dentp;
        clid_entry_t *new_ent;
        char src[PATH_MAX], dest[PATH_MAX];
        int rc;

        dentp = readdir(dp);
        while (dentp != NULL) {
                /* don't add '.' and '..', or any '.*' entry */
                if (dentp->d_name[0] != '.') {
                        new_ent =
                            (clid_entry_t *) Mem_Alloc(sizeof(clid_entry_t));
                        if (new_ent == NULL) {
                                LogEvent(COMPONENT_NFS_V4, "Mem_Alloc FAILED");
                                return -1;
                        }
                        strncpy(new_ent->cl_name, dentp->d_name, 256);
                        glist_add(&grace.g_clid_list, &new_ent->cl_list);
                        LogDebug(COMPONENT_NFS_V4, "added %s to clid list",
                            new_ent->cl_name);
                        if (srcdir != NULL) {
                                (void) snprintf(src, PATH_MAX, "%s/%s",
                                    srcdir, dentp->d_name);
                                (void) snprintf(dest, PATH_MAX, "%s/%s",
                                    v4_old_dir, dentp->d_name);
                                if (takeover)
                                        rc = mkdir(dest, 0700);
                                else
                                        rc = rename(src, dest);
                                if (rc == -1) {
                                        LogEvent(COMPONENT_NFS_V4,
                                          "Failed to make dir (%s), errno=%d",
                                          dest, errno);
                                }
                        }
                }
                dentp = readdir(dp);
        }

        return 0;
}

static void
nfs4_load_recov_clids_nolock(ushort nodeid)
{
        DIR *dp;
        struct glist_head *node;
        clid_entry_t *clid_entry;
        int rc;
        char path[PATH_MAX];

        if (nodeid == 0) {
                /* when not doing a takeover, start with an empty list */
                if (!glist_empty(&grace.g_clid_list)) {
                        glist_for_each(node, &grace.g_clid_list) {
                                glist_del(node);
                                clid_entry = glist_entry(node,
                                    clid_entry_t, cl_list);
                                Mem_Free(clid_entry);
                        }
                }

                dp = opendir(v4_old_dir);
                if (dp == NULL) {
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to open v4 recovery dir (%s), errno=%d",
                            v4_old_dir, errno);
                        return;
                }
                rc = nfs4_read_recov_clids(dp, NULL, 0);
                if (rc == -1) {
                        (void) closedir(dp);
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to read v4 recovery dir (%s)", v4_old_dir);
                        return;
                }
                (void) closedir(dp);

                dp = opendir(v4_recov_dir);
                if (dp == NULL) {
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to open v4 recovery dir (%s), errno=%d",
                            v4_recov_dir, errno);
                        return;
                }

                rc = nfs4_read_recov_clids(dp, v4_recov_dir, 0);
                if (rc == -1) {
                        (void) closedir(dp);
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to read v4 recovery dir (%s)",
                            v4_recov_dir);
                        return;
                }
                rc = closedir(dp);
                if (rc == -1) {
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to close v4 recovery dir (%s), errno=%d",
                            v4_recov_dir, errno);
                }

        } else {
                snprintf(path, PATH_MAX, "%s/%s/node%d",
                    NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR, nodeid);

                dp = opendir(path);
                if (dp == NULL) {
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to open v4 recovery dir (%s), errno=%d",
                            path, errno);
                        return;
                }

                rc = nfs4_read_recov_clids(dp, path, 1);
                if (rc == -1) {
                        (void) closedir(dp);
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to read v4 recovery dir (%s)",
                            path);
                        return;
                }
                rc = closedir(dp);
                if (rc == -1) {
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to close v4 recovery dir (%s), errno=%d",
                            path, errno);
                }
        }

}

void
nfs4_load_recov_clids(ushort nodeid)
{
        P(grace.g_mutex);

        nfs4_load_recov_clids_nolock(nodeid);

        V(grace.g_mutex);
}

void
nfs4_clean_old_recov_dir()
{
        DIR *dp;
        struct dirent *dentp;
        char path[PATH_MAX];
        int rc;

        dp = opendir(v4_old_dir);
        if (dp == NULL) {
                LogEvent(COMPONENT_NFS_V4,
                    "Failed to open old v4 recovery dir (%s), errno=%d",
                    v4_old_dir, errno);
                return;
        }

        for (dentp = readdir(dp); dentp != NULL; dentp = readdir(dp)) {
                /* don't remove '.' and '..', or any '.*' entry */
                if (dentp->d_name[0] == '.')
                        continue;

                (void) snprintf(path, PATH_MAX, "%s/%s",
                    v4_old_dir, dentp->d_name);

                rc = rmdir(path);
                if (rc == -1) {
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to remove %s, errno=%d",
                            path, errno);
                }
        }
}

/*
 * the recovery directory may not exist yet, so create it.  this should
 * only need to be done once (if at all).  also, the location of the
 * directory could be configurable.
 */
void
nfs4_create_recov_dir()
{
        int err;

        err = mkdir(NFS_V4_RECOV_ROOT, 0755);
        if (err == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_NFS_V4,
                    "Failed to create v4 recovery dir (%s), errno=%d",
                    NFS_V4_RECOV_ROOT, errno);
        }

        snprintf(v4_recov_dir, PATH_MAX, "%s/%s",
            NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR);
        err = mkdir(v4_recov_dir, 0755);
        if (err == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_NFS_V4,
                    "Failed to create v4 recovery dir(%s), errno=%d",
                    v4_recov_dir, errno);
        }

        snprintf(v4_old_dir, PATH_MAX, "%s/%s",
            NFS_V4_RECOV_ROOT, NFS_V4_OLD_DIR);
        err = mkdir(v4_old_dir, 0755);
        if (err == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_NFS_V4,
                    "Failed to create v4 recovery dir(%s), errno=%d",
                    v4_old_dir, errno);
        }
        if (nfs_param.core_param.clustered) {
                snprintf(v4_recov_dir, PATH_MAX, "%s/%s/node%d",
                    NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR, g_nodeid);

                err = mkdir(v4_recov_dir, 0755);
                if (err == -1 && errno != EEXIST) {
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to create v4 recovery dir(%s), errno=%d",
                            v4_recov_dir, errno);
                }

                snprintf(v4_old_dir, PATH_MAX, "%s/%s/node%d",
                    NFS_V4_RECOV_ROOT, NFS_V4_OLD_DIR, g_nodeid);

                err = mkdir(v4_old_dir, 0755);
                if (err == -1 && errno != EEXIST) {
                        LogEvent(COMPONENT_NFS_V4,
                            "Failed to create v4 recovery dir(%s), errno=%d",
                            v4_old_dir, errno);
                }
        }
}
