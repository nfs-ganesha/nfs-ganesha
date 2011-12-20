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

#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"

#define NFS_V4_RECOV_ROOT "/var/lib/nfs/ganesha"
#define NFS_V4_RECOV_DIR "/var/lib/nfs/ganesha/v4recov"

/*
 * construct to enable grace period, this could be expanded to implement
 * grace instances, where a new grace period is started for every
 * failover.  for now keep it simple, just a global used by all clients.
 */
typedef struct grace
{
        time_t g_start;
        time_t g_duration;
        struct glist_head g_clid_list;
} grace_t;

static grace_t grace;

int grace_period = 90;  /* configurable, should use lease period */

typedef struct clid_entry
{
        struct glist_head cl_list;
        char cl_name[256];
} clid_entry_t;

void
nfs4_init_grace()
{
        init_glist(&grace.g_clid_list);
}

void
nfs4_start_grace()
{
        int duration = grace_period;

        /* if no clients able to recover, skip grace period */
        if (glist_empty(&grace.g_clid_list))
                duration = 0;

        LogDebug(COMPONENT_NFS_V4, "grace period started, duration(%d)",
            grace_period);
        /* lock */
        grace.g_start = time(NULL);
        grace.g_duration = duration;
        /* unlock */
}

int
nfs4_in_grace()
{
        int gp;

        /* lock */
        gp = ((grace.g_start + grace.g_duration) > time(NULL));
        /* unlock */

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

        nfs_clientid->recov_dir = malloc(256);
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

        snprintf(path, PATH_MAX, "%s/%s", NFS_V4_RECOV_DIR,
            nfs_clientid->recov_dir);

        err = mkdir(path, 0700);
        if (err == -1 && errno != EEXIST) {
                LogDebug(COMPONENT_NFS_V4,
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

        snprintf(path, PATH_MAX, "%s/%s", NFS_V4_RECOV_DIR,
            recov_dir);

        err = rmdir(path);
        if (err == -1) {
                LogDebug(COMPONENT_NFS_V4,
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
                LogDebug(COMPONENT_NFS_V4, "compare %s to %s",
                    clid_ent->cl_name, nfs_clientid->recov_dir);
                if (!strncmp(clid_ent->cl_name ,nfs_clientid->recov_dir,
                    256)) {
                        LogDebug(COMPONENT_NFS_V4,
                            "[%s] %llx is allowed to reclaim ",
                            nfs_clientid->client_name,
                            (long long)nfs_clientid->clientid);
                        nfs_clientid->allow_reclaim = 1;
                        return;
                }
        }
}

/*
 * create the client reclaim list.  open the recovery directory and loop
 * through all of its entries and add them to the list.
 */
void
nfs4_load_recov_clids()
{
        DIR *dp;
        struct dirent *dentp;
        struct glist_head *node;
        clid_entry_t *clid_entry;
        clid_entry_t *new_ent;

        /* start with an empty list */
        if (!glist_empty(&grace.g_clid_list)) {
                glist_for_each(node, &grace.g_clid_list) {
                        glist_del(node);
                        clid_entry = glist_entry(node, clid_entry_t, cl_list);
                        free(clid_entry);
                }
        }

        dp = opendir(NFS_V4_RECOV_DIR);
        if (dp == NULL) {
                LogDebug(COMPONENT_NFS_V4,
                    "Failed to open v4 recovery dir (%s), errno=%d",
                    NFS_V4_RECOV_DIR, errno);
                return;
        }

        dentp = readdir(dp);
        while (dentp != NULL) {
                /* don't add '.' and '..', or any '.*' entry */
                if (dentp->d_name[0] != '.') {
                        new_ent = malloc(sizeof(clid_entry_t));
                        strncpy(new_ent->cl_name, dentp->d_name, 256);
                        glist_add(&grace.g_clid_list, &new_ent->cl_list);
                        LogDebug(COMPONENT_NFS_V4, "added %s to clid list",
                            new_ent->cl_name);
                }
                dentp = readdir(dp);
        }
}

void
nfs4_clean_recov_dir()
{
        struct glist_head *node, *noden;
        clid_entry_t *clid_entry;

        if (!glist_empty(&grace.g_clid_list)) {
                glist_for_each_safe(node, noden, &grace.g_clid_list) {
                        glist_del(node);
                        clid_entry = glist_entry(node, clid_entry_t, cl_list);
                        nfs4_rm_clid(clid_entry->cl_name);
                        free(clid_entry);
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
        char *path;

        path = NFS_V4_RECOV_ROOT;
        err = mkdir(path, 0755);
        if (err == -1 && errno != EEXIST) {
                LogDebug(COMPONENT_NFS_V4,
                    "Failed to create v4 recovery dir (%s), errno=%d",
                    path, errno);
        }

        path = NFS_V4_RECOV_DIR;
        err = mkdir(path, 0755);
        if (err == -1 && errno != EEXIST) {
                LogDebug(COMPONENT_NFS_V4,
                    "Failed to create v4 recovery dir(%s), errno=%d",
                    path, errno);
        }
}
