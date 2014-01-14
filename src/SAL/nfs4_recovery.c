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

#define MAXNAMEL 255

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
        char cl_name[PATH_MAX];
} clid_entry_t;

static void nfs4_load_recov_clids_nolock(int);

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
        fsal_op_context_t  context;
        int                fsg;
        extern int         get_first_context(fsal_op_context_t *);

        /*
         * grace should always be greater than or equal to lease time,
         * some clients are known to have problems with grace greater
         * than 60 seconds. Lease_Lifetime should be set to a smaller
         * value for those setups.
         */

        duration = nfs_param.nfsv4_param.lease_lifetime;
        fsg = get_first_context(&context);

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

        if (fsg) {
                /*
                 * Put the FS in grace. 
                 * Let the FSAL decide for how much time it goes in grace, 
                 * based on the ganesha grace duration.
                 */
                FSAL_start_grace(&context, duration);
        }
        LogEvent(COMPONENT_STATE,
                "NFS Server Now IN GRACE: duration = %d seconds",
                duration);

        grace.g_start = time(NULL);
        grace.g_duration = duration;

        V(grace.g_mutex);
}

int last_grace = -1;

int
nfs_in_grace()
{
        int             in_grace;
        uint64_t        redo_locks = 0;
        extern void     resub_all_nlm_blocked();

        P(grace.g_mutex);

        in_grace = ((grace.g_start + grace.g_duration) > time(NULL));

        if(in_grace != last_grace)
          {
            LogEvent(COMPONENT_STATE,
                     "NFS Server Now %s",
                     in_grace ? "IN GRACE" : "NOT IN GRACE");
            last_grace = in_grace;
            atomic_inc_uint64_t(&redo_locks);
          }
        else if(in_grace)
          {
            LogDebug(COMPONENT_STATE,
                    "NFS Server IN GRACE");
          }

        V(grace.g_mutex);

        if (!in_grace && redo_locks) {
                resub_all_nlm_blocked();
                atomic_dec_uint64_t(&redo_locks);
        }

        return in_grace;
}

/*
 * generate a name that identifies this client.  this name will be used to
 * know that a client was talking to the server before a restart so that it
 * will be allowed to do reclaims during grace period.
 */
void
nfs4_create_clid_name(nfs_client_record_t * cl_recp,
                      nfs_client_id_t     * pclientid,
                      compound_data_t     * data)
{
        char localaddr_str[SOCK_NAME_MAX];
        char cidstr[PATH_MAX];
        struct sockaddr             *local_addr_ptr;
        sockaddr_t                   local_addr;
        socklen_t                    addr_len;
        struct display_buffer       dspbuf = {sizeof(cidstr), cidstr, cidstr};

        /* hold both long form clientid and IP */
        pclientid->cid_recov_dir = gsh_malloc(PATH_MAX);
        if (pclientid->cid_recov_dir == NULL) {
                LogEvent(COMPONENT_CLIENTID, "Mem_Alloc FAILED");
                return;
        }
        if (convert_opaque_value_max_for_dir(&dspbuf, cl_recp->cr_client_val, cl_recp->cr_client_val_len, PATH_MAX) > 0)
                (void) snprintf(pclientid->cid_recov_dir, PATH_MAX, "%s-%s",
                        data->pworker->hostaddr_str, cidstr);

        local_addr_ptr = (struct sockaddr *)&local_addr;
        addr_len = sizeof(struct sockaddr);
        if (getsockname(data->reqp->rq_xprt->xp_fd, local_addr_ptr, &addr_len) == -1) {
                LogEvent(COMPONENT_CLIENTID, "Failed to get local addr.");
        }
        else
        {
                sprint_sockip(&local_addr, localaddr_str, sizeof(localaddr_str));
                pclientid->cid_server_ip = gsh_strdup(localaddr_str);
                if (pclientid->cid_server_ip == NULL) {
                        LogEvent(COMPONENT_CLIENTID, "Mem_Alloc FAILED");
                        return;
                }
        }

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
        int err=0;
        char path[PATH_MAX]={0}, segment[257]={0};
        int length,position=0;

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
                LogEvent(COMPONENT_CLIENTID,
                    "Failed to create client in recovery dir, no name");
                return;
        }

        /* break clientid down if it is greater than max dir name */
        /* and create a directory hierachy to represent the clientid. */
        snprintf(path, sizeof(path), "%s", v4_recov_dir);

        length = strlen(pclientid->cid_recov_dir);
        while (position < length)
        {
                /* if the (remaining) clientid is shorter than 255 */
                /* create the last level of dir and break out */
                int len = strlen(&pclientid->cid_recov_dir[position]);
                if (len <= MAXNAMEL) {
                        strcat(path, "/");
                        strncat(path, &pclientid->cid_recov_dir[position], len);
                        err = mkdir(path, 0700);
                        break;
                }
                /* if (remaining) clientid is longer than 255, */
                /* get the next 255 bytes and create a subdir */
                strncpy(segment, &pclientid->cid_recov_dir[position], MAXNAMEL);
                strcat(path, "/");
                strncat(path, segment, MAXNAMEL);
                err = mkdir(path, 0700);
                if (err == -1 && errno != EEXIST)
                        break;
                position += MAXNAMEL;
        }

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
nfs4_rm_clid(char *recov_dir, char *parent_path, int position)
{
        int err;
        char path[PATH_MAX]={0}, segment[256]={0};
        int len, segment_len;

        if (recov_dir == NULL)
                return;

        len = strlen(recov_dir); 
        if (position == len)
                return;

        strncpy(segment, &recov_dir[position], MAXNAMEL); 
        segment_len = strlen(segment);

        (void) snprintf(path, sizeof(path), "%s/%s",
                    parent_path, segment);


        /* recursively remove the directory hirerchy which represent the clientid */
        nfs4_rm_clid(recov_dir, path, position+segment_len);

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
                            char                  str[LOG_BUFF_LEN];
                            struct display_buffer dspbuf = {sizeof(str), str, str};

                            (void) display_client_id_rec(&dspbuf, pclientid);

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
nfs4_read_recov_clids(DIR *dp, char *parent_path, char *clid_str, char *tgtdir, int takeover)
{
        struct dirent *dentp;
        DIR *subdp;
        clid_entry_t *new_ent;
        char path[PATH_MAX]={0};
        char new_path[PATH_MAX]={0};
        char build_clid[PATH_MAX]={0};
        int rc=0;
        int num=0;
        char *ptr, *ptr2;
        char temp[10]; 
        int cid_len,len; 

        dentp = readdir(dp);
        while (dentp != NULL) {
                /* don't add '.' and '..', or any '.*' entry */
                if (dentp->d_name[0] != '.') {
                        num++;
                        if (clid_str)
                                strcpy(build_clid, clid_str);
                        else
                                build_clid[0] = 0;

                        /* construct the path by appending the subdir for the next */
                        /* readdir. This recursion keeps reading the subdirectory */
                        /* until reaching the end. */
                        strcpy(path, parent_path);
                        strcat(path, "/");
                        strncat(path, dentp->d_name, strlen(dentp->d_name));
                        /* if tgtdir is not NULL, we need to build nfs4old/currentnode */
                        if (tgtdir)
                        {
                                strcpy(new_path, tgtdir);
                                strcat(new_path, "/");
                                strncat(new_path, dentp->d_name, strlen(dentp->d_name));
                                rc = mkdir(new_path, 0700);
                                if ((rc == -1) && (errno != EEXIST))
                                {
                                        LogEvent(COMPONENT_CLIENTID, "mkdir %s faied errno=%d", new_path, errno);
                                        return rc;
                                }
                        }
                        /* keep building the clientid str by cursively */
                        /* reading the directory structure */
                        strncat(build_clid, dentp->d_name, strlen(dentp->d_name));
                        subdp = opendir(path);
                        if (subdp == NULL)
                        {
                                LogEvent(COMPONENT_CLIENTID, "opendir %s failed errno=%d", dentp->d_name, errno);
                        }
                        if (tgtdir)
                                rc = nfs4_read_recov_clids(subdp, path, build_clid, new_path, takeover);
                        else
                                rc = nfs4_read_recov_clids(subdp, path, build_clid, NULL, takeover);

                        /* after recursion, if the subdir has no non-hidden directory */
                        /* this is the end of this clientid str. Add the clientstr to the list. */
                        if (rc == 0)
                        {
                                ptr = strchr(build_clid, '(');               
                                if (ptr == NULL)
                                        return -1; 
                                ptr2 = strchr(ptr, ':');
                                if (ptr2 == NULL)
                                        return -1; 
                                len = ptr2-ptr-1;
                                if (len >= 9)
                                        return -1;
                                strncpy(temp, ptr+1, len);
                                temp[len] = 0;
                                cid_len = atoi(temp);
                                len = strlen(ptr2);
                                if ((len == (cid_len+2)) && (ptr2[len-1] == ')'))
                                {
                                	new_ent = gsh_malloc(sizeof(clid_entry_t));
                                	if (new_ent == NULL) {
                                        	LogEvent(COMPONENT_CLIENTID,
                                                	"Unable to allocate memory.");
                                                	return -1;
                                	}
                                	strncpy(new_ent->cl_name, build_clid, PATH_MAX);
                                	glist_add(&grace.g_clid_list, &new_ent->cl_list);
                                	LogDebug(COMPONENT_CLIENTID, "added %s to clid list",
                                        	new_ent->cl_name);
				}
                        }
                        /* If this is not for takeover, remove the directory hierarchy */
                        /* that represent the current clientid */
                        if (!takeover) {
                                rc = rmdir(path);
                                if (rc == -1) {
                                        LogEvent(COMPONENT_CLIENTID,
                                                 "Failed to rmdir (%s), errno=%d",
                                                 path, errno);
                                }
                        }
                }
                dentp = readdir(dp);
        }

        return num;
}
void nfs4_load_recov_clids_cluster_nolock(int nodeid)
{
        DIR *dp;
        int rc;
        char path[PATH_MAX];

        snprintf(path, sizeof(path), "%s/%s/node%d",
        NFS_V4_RECOV_ROOT, NFS_V4_RECOV_DIR, nodeid);

        dp = opendir(path);
        if (dp == NULL) {
                LogEvent(COMPONENT_CLIENTID,
                "Failed to open v4 recovery dir (%s), errno=%d",
                 path, errno);
                 return;
        }

        rc = nfs4_read_recov_clids(dp, path, NULL, v4_old_dir, 1);
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
nfs4_load_recov_clids_nolock(int nodeid)
{
        DIR *dp;
        struct dirent **namelist;
        struct glist_head *node;
        clid_entry_t *clid_entry;
        int rc, n;
        int  u_nodeid;
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
                rc = nfs4_read_recov_clids(dp, v4_old_dir, NULL, NULL, 0);
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

                rc = nfs4_read_recov_clids(dp, v4_recov_dir, NULL, v4_old_dir, 0);
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
        snprintf(path, sizeof(path), "%s/%s",
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
                u_nodeid = atoi(cp);
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
nfs4_load_recov_clids(int nodeid)
{
        P(grace.g_mutex);

        nfs4_load_recov_clids_nolock(nodeid);

        V(grace.g_mutex);
}

void
nfs4_clean_old_recov_dir(char *parent_path)
{
        DIR *dp;
        struct dirent *dentp;
        char path[PATH_MAX];
        int rc;

        dp = opendir(parent_path);
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

                (void) snprintf(path, sizeof(path), "%s/%s",
                    parent_path, dentp->d_name);

		nfs4_clean_old_recov_dir(path);	
                rc = rmdir(path);
                if (rc == -1) {
                        LogEvent(COMPONENT_CLIENTID,
                            "Failed to remove %s, errno=%d",
                            path, errno);
                }
        }
        rc = closedir(dp);
        if (rc == -1) {
          LogEvent(COMPONENT_CLIENTID,
                   "Failed to close v4 recovery dir (%s), errno=%d",
                   v4_old_dir, errno);
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
