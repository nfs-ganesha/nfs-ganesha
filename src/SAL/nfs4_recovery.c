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
#include "nfs_core.h"
#include "nfs4.h"
#include "nfs4rec.h"
#include "sal_functions.h"
#include <sys/stat.h>
#include <sys/types.h>

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
nfs4_start_grace(nfs_grace_start_array_t *gsap)
{
        int duration;
        int i, iend;
        nfs_grace_start_t *gsp;

        /* limit the grace period to a maximum of 60 seconds */
        duration = MIN(60, nfs_param.nfsv4_param.lease_lifetime);

        P(grace.g_mutex);

        /*
         * if called from failover code and given a nodeid, then this node
         * is doing a take over.  read in the client ids from the failing node
         */
        if (gsap) {
                iend = gsap->num_elements;
                assert(iend);
                gsp = gsap->nfs_grace_start;
                for (i=0; i < iend; i++) {
                        assert(gsp->nodeid);  /* for initial debug */
                        nfs4_load_recov_clids_nolock(gsp->nodeid);
                        gsp++;
                }
        }

        LogEvent(COMPONENT_STATE,
                 "NFS Server Now IN GRACE, duration %d",
                 duration);

        grace.g_start = time(NULL);
        grace.g_duration = duration;

        V(grace.g_mutex);
}

int last_grace = -1;

int
nfs_in_grace()
{
        int in_grace;

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

/*
 * generate a name that identifies this client.  this name will be used to
 * know that a client was talking to the server before a restart so that it
 * will be allowed to do reclaims during grace period.
 */
void
nfs4_create_clid_name(nfs_client_record_t *cl_recp, nfs_client_id_t *pclientid,
    struct svc_req *svcp)
{
        int i;
        sockaddr_t sa;
        char buf[SOCK_NAME_MAX];
        longlong_t cl_val = 0;

        pclientid->cid_recov_dir = gsh_malloc(256);
        if (pclientid->cid_recov_dir == NULL) {
                LogEvent(COMPONENT_CLIENTID, "Mem_Alloc FAILED");
                return;
        }
        /* get the caller's IP addr */
        if (copy_xprt_addr(&sa, svcp->rq_xprt))
                sprint_sockip(&sa, buf, SOCK_NAME_MAX);
        else
                strncpy(buf, "Unknown", SOCK_NAME_MAX);

        for (i = 0; i < cl_recp->cr_client_val_len; i++)
                cl_val += cl_recp->cr_client_val[i];

        (void) snprintf(pclientid->cid_recov_dir, 256, "%s-%llx", buf, cl_val);

        LogDebug(COMPONENT_CLIENTID, "Created client name [%s]",
            pclientid->cid_recov_dir);
}

/*
 * create an entry in the recovery directory for this client so that it
 * will be able to reclaim state after a server reboot/restart.
 */
void
nfs4_add_clid(nfs_client_id_t *pclientid)
{
        int err;
        char path[PATH_MAX];

        /*
         * A little trickery here.  This client is now talking to the server
         * so it is allowed to do reclaim (during grace).  If the server
         * reboots, this is pointless.  However, if grace is started due to
         * a failover/takeover, then the stable storage won't be read in
         * and the client wouldn't be marked as able to reclaim.  Now, since
         * the server didn't reboot, the client doesn't need to reclaim, but
         * it may get ERR_GRACE during this failover grace period and decide
         * it needs to run through recovery.
         */
        pclientid->cid_allow_reclaim = 1;

        if (pclientid->cid_recov_dir == NULL) {
                LogDebug(COMPONENT_CLIENTID,
                    "Failed to create client in recovery dir, no name");
                return;
        }

        snprintf(path, PATH_MAX, "%s/%s", v4_recov_dir,
            pclientid->cid_recov_dir);

        err = mkdir(path, 0700);
        if (err == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to create client in recovery dir (%s), errno=%d",
                    path, errno);
        } else {
                LogDebug(COMPONENT_CLIENTID, "Created client dir [%s]", path);
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
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to remove client in recovery dir (%s), errno=%d",
                    path, errno);
        }
}

/*
 * determine whether or not this client is allowed to do reclaim operations.
 * if the server is not in grace period, then no reclaim can happen.
 */
void
nfs4_chk_clid(nfs_client_id_t *pclientid)
{
        struct glist_head *node;
        clid_entry_t *clid_ent;

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
                    clid_ent->cl_name, pclientid->cid_recov_dir);
                if (!strncmp(clid_ent->cl_name, pclientid->cid_recov_dir,
                    256)) {
                        if (isDebug(COMPONENT_CLIENTID)) {
                            char str[HASHTABLE_DISPLAY_STRLEN];

                            display_client_id_rec(pclientid, str);

                            LogFullDebug(COMPONENT_CLIENTID,
                                         "Allowed to reclaim ClientId %s",
                                         str);
                        }
                        pclientid->cid_allow_reclaim = 1;
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
                        new_ent = gsh_malloc(sizeof(clid_entry_t));
                        if (new_ent == NULL) {
                                LogEvent(COMPONENT_CLIENTID,
                                         "Unable to allocate memory.");
                                return -1;
                        }
                        strncpy(new_ent->cl_name, dentp->d_name, 256);
                        glist_add(&grace.g_clid_list, &new_ent->cl_list);
                        LogDebug(COMPONENT_CLIENTID, "added %s to clid list",
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
void nfs4_load_recov_clids_cluster_nolock(ushort nodeid)
{
        DIR *dp;
        int rc;
        char path[PATH_MAX];

        snprintf(path, PATH_MAX, "%s/%s/node%d",
        NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR, nodeid);

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
        return;
 }


static void
nfs4_load_recov_clids_nolock(ushort nodeid)
{
        DIR *dp;
        struct dirent **namelist;
        struct glist_head *node;
        clid_entry_t *clid_entry;
        int rc, n;
        ushort u_nodeid;
        char path[PATH_MAX];
        char *cp;

        if (nodeid == 0) {
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
                return;
        } 
        /* nodeid != 0 */
        assert(nfs_param.core_param.clustered);
        if ( nodeid != ALL_NODES) {
                nfs4_load_recov_clids_cluster_nolock(nodeid);
                return;
        }
        /* ALL_NODES */ 
        snprintf(path, PATH_MAX, "%s/%s",
        NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR);

        n = scandir(path, &namelist, 0, alphasort);
        if (n < 0) {
                LogEvent(COMPONENT_CLIENTID,
                "Failed to scan v4 recovery dir (%s), errno=%d",
                path, errno);
                return;
        }
        if (n <= 2) {
                LogEvent(COMPONENT_CLIENTID,
                "Empty v4 recovery dir (%s)",
                path);
                return;
        }
        while ( n > 2 ) {
                cp = namelist[n-1]->d_name;
                cp += 4; /* skip over the "node" */
                u_nodeid = (ushort) atoi(cp);
                nfs4_load_recov_clids_cluster_nolock(u_nodeid);
                free(namelist[n-1]);
                n--;
        }
        free(namelist[1]);
        free(namelist[0]);
        free(namelist);
        return;
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
                LogEvent(COMPONENT_CLIENTID,
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
                        LogEvent(COMPONENT_CLIENTID,
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
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to create v4 recovery dir (%s), errno=%d",
                    NFS_V4_RECOV_ROOT, errno);
        }

        snprintf(v4_recov_dir, PATH_MAX, "%s/%s",
            NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR);
        err = mkdir(v4_recov_dir, 0755);
        if (err == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to create v4 recovery dir(%s), errno=%d",
                    v4_recov_dir, errno);
        }

        snprintf(v4_old_dir, PATH_MAX, "%s/%s",
            NFS_V4_RECOV_ROOT, NFS_V4_OLD_DIR);
        err = mkdir(v4_old_dir, 0755);
        if (err == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to create v4 recovery dir(%s), errno=%d",
                    v4_old_dir, errno);
        }
        if (nfs_param.core_param.clustered) {
#ifdef SONAS
                short nodeid;

                /* HACK - get the node ID */
                nodeid = gpfs_ganesha(OPENHANDLE_GET_NODEID, NULL);
                /* GPFS starts with 0, we want node ids to be > 0 */
                g_nodeid = nodeid + 1;
                LogDebug(COMPONENT_NFS_V4, "nodeid = (%d)", g_nodeid);
#endif

                snprintf(v4_recov_dir, PATH_MAX, "%s/%s/node%d",
                    NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR, g_nodeid);

                err = mkdir(v4_recov_dir, 0755);
                if (err == -1 && errno != EEXIST) {
                        LogEvent(COMPONENT_CLIENTID,
                            "Failed to create v4 recovery dir(%s), errno=%d",
                            v4_recov_dir, errno);
                }

                snprintf(v4_old_dir, PATH_MAX, "%s/%s/node%d",
                    NFS_V4_RECOV_ROOT, NFS_V4_OLD_DIR, g_nodeid);

                err = mkdir(v4_old_dir, 0755);
                if (err == -1 && errno != EEXIST) {
                        LogEvent(COMPONENT_CLIENTID,
                            "Failed to create v4 recovery dir(%s), errno=%d",
                            v4_old_dir, errno);
                }
        }
}
