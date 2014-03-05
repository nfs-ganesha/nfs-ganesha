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
 * nfs_recovery_thread.c : handle recovery events like IP relocation
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include "log.h"
#include "nfs4.h"
#include "nfs4rec.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_core.h"
#include "log.h"

#ifdef SONAS
#define DELIMIT '_'

extern hash_table_t *ht_nlm_client;
extern hash_table_t *ht_nsm_client;
extern uint64_t rpc_in;
extern uint64_t rpc_out;
uint64_t rpc_in_old;
uint64_t rpc_out_old;

static int
ip_match(char *ip, char *cid_server_ip)
{
        char *cp;

        /* ip is of the form a.b.c.d_N_interface */
        cp = ip;
        /* DELIMIT could be replaced by null with multiple call to ip_match */
        /* so need to check null as well */
        while((*cp != DELIMIT) && (*cp != 0x0))
                cp++;
        *cp = '\0';

        if (strncmp(ip, cid_server_ip, strlen(cid_server_ip)))
                return 0;

        return 1; /* they match */
}

static void
nfs_release_nlm_state(char *ip)
{
        hash_table_t *ht = ht_nlm_client;
        state_nlm_client_t *nlm_cp;
        state_nsm_client_t *nsm_cp;
        struct rbt_head *head_rbt;
        struct rbt_node *pn;
        hash_data_t *pdata;
        state_status_t err, status;
        char   serverip[MAXPATHLEN];
        struct display_buffer dspbuf = {sizeof(serverip), serverip, serverip};
        int i;

        LogDebug(COMPONENT_STATE, "Release all NLM locks");

        cancel_all_nlm_blocked();

        /* walk the client list and call state_nlm_notify */
        for(i = 0; i < ht->parameter.index_size; i++) {
                PTHREAD_RWLOCK_WRLOCK(&ht->partitions[i].lock);
                head_rbt = &ht->partitions[i].rbt;
                /* go through all entries in the red-black-tree*/
                RBT_LOOP(head_rbt, pn) {
                        pdata = RBT_OPAQ(pn);

                        nlm_cp = (state_nlm_client_t *)pdata->buffval.pdata;
                        inc_nlm_client_ref(nlm_cp);
                        PTHREAD_RWLOCK_UNLOCK(&ht->partitions[i].lock);
                        if(display_sockaddr(&dspbuf, &(nlm_cp->slc_server_addr), FALSE) > 0)
                        {
                                if (ip_match(ip, serverip)) {
                                        nsm_cp = nlm_cp->slc_nsm_client;
                                        inc_nsm_client_ref(nsm_cp);
                                        err = state_nlm_notify(nsm_cp, NULL, &status, TRUE);
                                        if (err != STATE_SUCCESS)
                                                LogDebug(COMPONENT_THREAD,
                                                         "state_nlm_notify failed with %d",
                                                         status);
                                        dec_nsm_client_ref(nsm_cp);
                                }
                        }
                        dec_nlm_client_ref(nlm_cp);
                        PTHREAD_RWLOCK_WRLOCK(&ht->partitions[i].lock);
                        RBT_INCREMENT(pn);
                }
                PTHREAD_RWLOCK_UNLOCK(&ht->partitions[i].lock);
        }
        return;
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
        hash_data_t *pdata;
        nfs_client_id_t *cp;
        nfs_client_record_t *recp;
        int i;

        /* go through the confirmed clients looking for a match */
        for(i = 0; i < ht->parameter.index_size; i++) {

                PTHREAD_RWLOCK_WRLOCK(&ht->partitions[i].lock);
                head_rbt = &ht->partitions[i].rbt;

                /* go through all entries in the red-black-tree*/
                RBT_LOOP(head_rbt, pn) {
                        pdata = RBT_OPAQ(pn);

                        cp = (nfs_client_id_t *)pdata->buffval.pdata;
                        P(cp->cid_mutex);
                        /* only checking clientid that are not stale */
                        if ((cp->cid_confirmed == CONFIRMED_CLIENT_ID) && ip_match(ip, cp->cid_server_ip)) {
                                inc_client_id_ref(cp);

                                /* Take a reference to the client record */
                                recp = cp->cid_client_record;
                                inc_client_record_ref(recp);

                                V(cp->cid_mutex);

                                PTHREAD_RWLOCK_UNLOCK(&ht->partitions[i].lock);

                                P(recp->cr_mutex);

                                (void) nfs_client_id_expire(cp, 1);

                                V(recp->cr_mutex);

                                dec_client_id_ref(cp);
                                dec_client_record_ref(recp);
                                return;
                        } else {
                                V(cp->cid_mutex);
                        }

                        RBT_INCREMENT(pn);
                }

                PTHREAD_RWLOCK_UNLOCK(&ht->partitions[i].lock);
        }
}

void release_ip(char *ip, int notdone)
{
        if (notdone)
                nfs_release_nlm_state(ip);
        nfs_release_v4_client(ip);
}

/* If node_id is zero, do nothing.
* For (node_id > 0):
*       Check if there are any myrelease events for node_id
*       If there are any myrelease_ip event, release nlm locks and V4 state.
*       Remove the myrelease_* file(s) for node_id 
*/
void check_for_id( int inum, struct dirent **namelist, int id)
{
char *cp, *cp2;
int i, ientry, notdone;
char workpath[PATH_MAX];

        /*
         * if there are releaseip events, we need to expire the v4
         * client for each IP being released.  however, we also need
         * to drop all v3 locks (state) for this server since the
         * v3 clients will be notified that the server rebooted.  we
         * only want to do this once, no matter how many releaseip
         * events there are in this list, hence the 'notdone' flag.
         */
        notdone = 1;

        /* stop at 2 to skip '.' and '..' */
        for (ientry = (inum - 1); ientry >= 2; ientry--) {
                // We are only interested into myrelease_* files
                if( namelist[ientry]->d_name[0] != 'm')
                        continue;

                if(strmaxcpy(workpath,
                             namelist[ientry]->d_name,
                             sizeof(workpath)) == -1){
                        assert(0);
                        break;
                }

                cp = workpath;

                LogDebug(COMPONENT_THREAD, "recovery event file: [%s]", workpath);
                i = 2; /* id is the 3rd entry */
                while(i--) {
                        while(*cp != DELIMIT)
                                cp++;
                        cp++;
                }
                cp2 = cp;
                while(*cp2 != DELIMIT)
                        cp2++;
                *cp2 = '\0';

                i = atoi(cp);
                if ( i == id ) {
                        /* IP is the 4th entry */
                        cp2++;
                        release_ip(cp2, notdone);

                        // done processing the entry. Delete the file.
                        sprintf(workpath,"%s/%s",NFS_RECOV_EVENTS,namelist[ientry]->d_name);
                        remove(workpath);
                        LogEvent(COMPONENT_THREAD, "Deleted myrelease file :%s ", workpath);

                        /* we are done with v3 */
                        notdone = 0;
                }
        }

        return;
}


int get_takeip_count( int inum, struct dirent **namelist, int id)
{
char *cp, *cp2;
int i, ientry, icnt=0;
char workpath[PATH_MAX];

        /* stop at 2 to skip '.' and '..' */
        for (ientry = (inum - 1); ientry >= 2; ientry--) {
                // We are only interested into take_* files
                if( namelist[ientry]->d_name[0] != 't')
                        continue;

                if(strmaxcpy(workpath,
                             namelist[ientry]->d_name,
                             sizeof(workpath)) == -1){
                        assert(0);
                        break;
                }

                cp = workpath;

                LogDebug(COMPONENT_THREAD, "recovery event file: [%s]", workpath);
                i = 2; /* id is the 3rd entry */
                while(i--) {
                        while(*cp != DELIMIT)
                                cp++;
                        cp++;
                }
                cp2 = cp;
                while(*cp2 != DELIMIT)
                        cp2++;
                *cp2 = '\0';

                i = atoi(cp);
                if ( i == id || id == 0) {
                        icnt++;        
                }
        }

        LogDebug(COMPONENT_THREAD, "take_ip count %d for node %d", icnt, id);

        return(icnt);
}



time_t parse_time( char *the_target)
{
char *cp, *cp2;
int i;
char workpath[PATH_MAX];

                strcpy(workpath, the_target);
                cp = workpath;
                i = 1; /* time is the second entry */
                while(i--) {
                        while(*cp != DELIMIT)
                                cp++;
                        cp++;
                }
                cp2 = cp;
                while(*cp2 != DELIMIT)
                        cp2++;
                *cp2 = '\0';
                return( (time_t) atol(cp));
}
void free_dirent( int inum, struct dirent **namelist)
{
int ientry;

        for( ientry = 0; ientry < inum; ientry++ ) {
                free(namelist[ientry]);
        }
        free(namelist);
        return;
}
void do_state()
{
int ientry, ientry2, n;
time_t new_entry;
char workpath[PATH_MAX];
const char * status;
struct dirent **state_namelist = NULL;

#define RECORD_HISTORY 13 /* We keep 12 state records including the one we are
                           * about to write.
                           */

        n = scandir(NFS_V4_RECOV_LOCAL, &state_namelist, 0, alphasort);
        ientry = n;
        if (ientry < 0) {
                LogEvent(COMPONENT_THREAD, "scandir of %s failed errno = %d", NFS_RECOV_EVENTS, errno);
        }
        if ( ientry > RECORD_HISTORY ) {
                ientry2 = 2;
                while(ientry >= RECORD_HISTORY) {              
                        sprintf( workpath,"%s/%s",NFS_V4_RECOV_LOCAL,state_namelist[ientry2]->d_name);
                        (void) remove(workpath);
                        ientry--;
                        ientry2++;
                }
        }
        /* Make the restart recommendation evaluation here */
        while(1) {
        /* Here are the current rules:
         * We keep 12 state records = 2 minutes worth.
         * two reds in that period then CTDB restarts us
         * No new state record in 2 minutes then CTDB restarts us
         */
                if ( ((atomic_fetch_uint64_t(&rpc_in)) > rpc_in_old ) &&  ((atomic_fetch_uint64_t(&rpc_out)) > rpc_out_old) ) {
                        status = "green";
                        break;
                }
                if ( (atomic_fetch_uint64_t(&rpc_in) > rpc_in_old ) &&  (atomic_fetch_uint64_t(&rpc_out) <= rpc_out_old) ) {
                        status = "red";
                        break;
                }
                status = "yellow";
                break;
        }
        new_entry = time(NULL);
        snprintf(workpath, sizeof(workpath), "%s/%lu_%s_%lu_%lu",
                NFS_V4_RECOV_LOCAL, (ulong) new_entry,status,rpc_in, rpc_out);

        ientry = creat(workpath, 0755);
        if (ientry == -1) {
                LogEvent(COMPONENT_THREAD,
                    "Failed to create state file (%s), errno=%d",
                    workpath, errno);
        } else
                close(ientry);
        rpc_in_old = atomic_fetch_uint64_t(&rpc_in);
        rpc_out_old = atomic_fetch_uint64_t(&rpc_out);
        if(state_namelist)
          free_dirent(n, state_namelist);
        return;
}
void rec_gc( int inum, struct dirent **namelist)
{
int ientry;
time_t t_this_entry, t_dead, sticky_time;
char workpath[PATH_MAX];

        sticky_time = ( NFS_RECOV_CYCLE * NFS_RECOV_GC * 10 );
        t_dead = time(NULL);
        t_dead -= sticky_time;
        ientry = inum - 1;
        while(ientry > 1) {              
                t_this_entry = parse_time(namelist[ientry]->d_name);
                if ( t_this_entry < t_dead ) {
                        sprintf( workpath,"%s/%s",NFS_RECOV_EVENTS,namelist[ientry]->d_name);
                        (void) remove(workpath);
                }
                ientry--;
        }
        return;
}
int match_to_releaseip( int num, struct dirent **namelist, int id, nfs_grace_start_array_t *array)
{

char *cp, *cp2;
int ifound, iend;
int i, ientry, ientry_r, ientry_rstart;
char workpath[PATH_MAX];
char workaddr[IPADDR_STRSZ];
char remove_file[PATH_MAX];
nfs_grace_start_t *working;

        working = array->nfs_grace_start;
        ientry = num - 1;
        ifound = ientry_rstart = 0;
        iend = array->num_elements;
        while (1) {
                /* locate the eligible takeip records. 
                 * Ideally there should be exactly num_elements of them.
                 */
                if ((namelist[ientry]->d_name[0] != 't')) {
                        break;
                }

                if(strmaxcpy(workpath,
                             namelist[ientry]->d_name,
                             sizeof(workpath)) == -1)
                {
                        assert(0);
                        break;
                }
                cp = workpath;
                i = 2; /* id is the third entry */
                while(i--) {
                        while(*cp != DELIMIT)
                                cp++;
                        cp++;
                }
                cp2 = cp;
                while(*cp2 != DELIMIT)
                        cp2++;
                *cp2 = '\0';
                i = atoi(cp);
                if ( i != id ) {
                        ientry--;
                        continue;
                }

                /* Save the ip address entry four and five */
                cp2++;
                cp = cp2;
                 /* get address and netmask together */
                while(*cp2 != DELIMIT)
                        cp2++;
                cp2++;
                while(*cp2 != DELIMIT)
                        cp2++;
                *cp2 = '\0';
                if((cp2 - cp) >= sizeof(workaddr))
                        break;
                strcpy(workaddr, cp); /* can't overflow */

                if ( !ientry_rstart )
                        ientry_r = ientry -1;
                else
                        ientry_r = ientry_rstart;
                while (1) {
                        if (ientry_r <= 1) {
                                // Skip . and .. entries
                                goto done;
                        }
                        if (namelist[ientry_r]->d_name[0] != 'r') {
                                ientry_r--;
                                continue;
                        }
                        if ( !ientry_rstart )
                                ientry_rstart = ientry_r;
                        if(strmaxcpy(workpath,
                                     namelist[ientry_r]->d_name,
                                     sizeof(workpath)) == -1)
                        {
                                assert(0);
                                break;
                        }
                        cp = workpath;
                        i = 3; /* address is entry  four and five */
                        while(i--) {
                                while(*cp != DELIMIT)
                                        cp++;
                                cp++;
                        }
                        cp2 = cp;
                        /* get address and netmask together */
                        while(*cp2 != DELIMIT)
                                cp2++;
                        cp2++;
                        while(*cp2 != DELIMIT)
                                cp2++;
                        *cp2 = '\0';
                        if ( strcmp(cp, workaddr ) ) 
                                ientry_r--;
                        else {
                                LogEvent(COMPONENT_THREAD, "matching release:%s and take: %s",
                                                           cp, workaddr);

                                // Delete the processed release_ip file.
                                sprintf(remove_file, "%s/%s",NFS_RECOV_EVENTS, namelist[ientry_r]->d_name);
                                remove(remove_file);

                                LogEvent(COMPONENT_THREAD, "Deleted release file :%s ", remove_file);

                                break;
                        }
                }

                // Delete the processed take_ip file.
                sprintf(remove_file, "%s/%s",NFS_RECOV_EVENTS, namelist[ientry]->d_name);
                remove(remove_file);

                LogEvent(COMPONENT_THREAD, "Deleted take file :%s ", remove_file);

                /* Found one */
                /* backup and get the node id */
                cp--;
                *cp = '\0';
                cp--;
                while(*cp != DELIMIT)
                        cp--;
                cp++;
                working->nodeid = atoi(cp);
                working->event = TAKEIP;
                if(strmaxcpy(working->ipaddr,
                             workaddr,
                             sizeof(working->ipaddr)) == -1)
                        break;
                ifound++;
                LogDebug(COMPONENT_THREAD, "found %d address %s at release entry %d from node %d",
                                           ifound, working->ipaddr, ientry_r, working->nodeid );
                if ( ifound < iend )
                        working++;
                else
                        break;

                /* Don't match it again */
                ientry--;
        }

done:
        assert(ifound <= array->num_elements);
        if ( ifound == array->num_elements ) {
                return(0);
        } else {
                /** Found fewer matching release_ip entries. 
                *  This can happen if the failing node has crashed,
                *  and has failed to create the release_ip event file
                *  OR - take_ip is happening much later,
                *       and we have garbage collected the release_ip event file.
                */
                LogEvent(COMPONENT_THREAD, "Found = %d searching for %d addresses", 
                         ifound, array->num_elements);

                return(-1);
        }
}

#endif /* SONAS */

void *recovery_thread(void *UnusedArg)
{
#ifdef SONAS
struct dirent **namelist = NULL;
int n, takeip_count;
uint32_t uerr=0;
uint32_t ucnt=0;
size_t size;
nfs_grace_start_array_t *nfs_grace_start_array;

        SetNameFunction("recov");

        uerr = mkdir(NFS_V4_RECOV_LOCAL, 0755);
        if (uerr == -1 && errno != EEXIST) {
                LogEvent(COMPONENT_THREAD,
                    "Failed to create v4 recovery dir (%s), errno=%d",
                    NFS_V4_RECOV_LOCAL, errno);
        }
        rpc_in =  rpc_out = 0;
        rpc_in_old =  rpc_out_old = 0;
        uerr = 0;

        while(1) {
                if ((ucnt % NFS_RECOV_STATE_CNT) == 0) {
                        do_state();
                }
          
                if ( ucnt == 0 ) { /* We are just coming up. Should be in grace for us. */
                        sleep(NFS_RECOV_GC); /* let things clear out */
                        ucnt += NFS_RECOV_STATE_CNT;
                } else    
                        ucnt++;

                nfs_grace_start_array = NULL;
                n = scandir(NFS_RECOV_EVENTS, &namelist, 0, alphasort);
                if (n < 0) {
                        /* Don't flood the log */
                        if ( uerr < 3 ) {
                                LogEvent(COMPONENT_THREAD, "scandir of %s failed errno = %d", NFS_RECOV_EVENTS, errno);
                                uerr++;
                        }
                        sleep(NFS_RECOV_CYCLE);
                        continue;
                } 
                uerr = 0;
                while(1) {
                        if ( n <= 2 ) {  /* '.' and '..' are counted */
                                /* Common case. Nothing to do. */
                                break;
                        }
                        /*
                         * If we reach here then we have either "takeip"
                         * or "releaseip" records 
                         */
                        nfs4_start_grace(NULL);

                        if ( !g_nodeid ) {
                                /* If node id is not set (No exports defined or the FSAL call did not set it) */
                                LogDebug(COMPONENT_THREAD, "Node ID is zero, will takeover state for all IPs");
                        }

                        /* If there is any releas_ip event for this node,
                         * release nlm locks and V4 state.
                         * The associated myrelease_ip_* file(s) will be deleted.
                         */ 
                        check_for_id( n, namelist, g_nodeid);

                         /* find number of take_ip events we have for g_nodeid.
                         */                       
                        if ((takeip_count = get_takeip_count( n, namelist, g_nodeid))) {

                                /* Clients are coming to this node for reclaims */
                                /* See if we can find from where, if so we will
                                 * read only the from nodes clids. If not we will read 
                                 * all nodes.
                                 */
                                nfs_grace_start_array = gsh_malloc(sizeof(nfs_grace_start_array_t));
                                if (nfs_grace_start_array == NULL) {
                                        LogCrit(COMPONENT_THREAD, "ENOMEM");
                                        break;
                                }
                                size = sizeof(nfs_grace_start_t) * takeip_count;
                                nfs_grace_start_array->nfs_grace_start = gsh_malloc(size);
                                if (nfs_grace_start_array->nfs_grace_start == NULL) {
                                        LogCrit(COMPONENT_THREAD, "ENOMEM");
                                        gsh_free( nfs_grace_start_array );
                                        break;
                                }
                                nfs_grace_start_array->num_elements = takeip_count;
                                if (( match_to_releaseip( n, namelist, g_nodeid, nfs_grace_start_array))) {
                                /* Couldn't match them all need to read all nodes */
                                        nfs_grace_start_array->num_elements = 1;
                                        nfs_grace_start_array->nfs_grace_start->nodeid = ALL_NODES;
                                        nfs_grace_start_array->nfs_grace_start->ipaddr[0]='\0';
                                        nfs_grace_start_array->nfs_grace_start->event=NOTSPECIFIED;
                                        LogEvent(COMPONENT_THREAD, "Grace entered for ALL_NODES on node %d ", g_nodeid);
                                } else {
                                       LogEvent(COMPONENT_THREAD, "Grace entered for specific nodes on node %d ", g_nodeid);
                                }
                                nfs4_start_grace( nfs_grace_start_array );
                                if ( nfs_grace_start_array ) {
                                        gsh_free( nfs_grace_start_array->nfs_grace_start );
                                        gsh_free( nfs_grace_start_array );
                                }
                                break;
                        } else {
                                /* No take_ip event for this node
                                 * No need to recover any state.
                                 * No clients are expected to reclaim from this node.
                                 * We just need to start Grace period which we already did.
                                 */
                                LogEvent(COMPONENT_THREAD, "Grace started with NULL node id %d", g_nodeid);

                                break;
                        }
                }
                rec_gc( n, namelist);
                free_dirent(n, namelist);
                sleep(NFS_RECOV_CYCLE);
        } /* while ( 1 ) */
#endif /* SONAS */

        return NULL;
}
