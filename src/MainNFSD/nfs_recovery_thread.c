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
time_t t_after; /* Careful here. */

extern hash_table_t *ht_nsm_client;

static void
nfs_release_nlm_state()
{
        hash_table_t *ht = ht_nsm_client;
        state_nsm_client_t *nsm_cp;
        struct rbt_head *head_rbt;
        struct rbt_node *pn;
        hash_data_t *pdata;
        state_status_t err, status;
        int i;

        /* walk the client list and call state_nlm_notify */
        for(i = 0; i < ht->parameter.index_size; i++) {
                pthread_rwlock_wrlock(&ht->partitions[i].lock);
                head_rbt = &ht->partitions[i].rbt;
                /* go through all entries in the red-black-tree*/
                RBT_LOOP(head_rbt, pn) {
                        pdata = RBT_OPAQ(pn);

                        nsm_cp = (state_nsm_client_t *)pdata->buffval.pdata;
                        inc_nsm_client_ref(nsm_cp);
                        err = state_nlm_notify(nsm_cp, NULL, &status);
                        if (err != STATE_SUCCESS)
                                LogDebug(COMPONENT_THREAD,
                                    "state_nlm_notify failed with %d",
                                    status);
                        dec_nsm_client_ref(nsm_cp);
                        RBT_INCREMENT(pn);
                }
                pthread_rwlock_unlock(&ht->partitions[i].lock);
        }
        return;
}

static int
ip_match(char *ip, char *recov_dir)
{
        size_t rdlen = 0;
        char *cp;

        /* ip is of the form a.b.c.d_N_interface */
        cp = ip;
        while(*cp != DELIMIT)
                cp++;
        *cp = '\0';

        /* recov_dir is of the form a.b.c.d-NN */
        while (recov_dir[rdlen] != '-')
                rdlen++;

        if (rdlen != strlen(ip))
                return 0;

        if (strncmp(ip, recov_dir, rdlen))
                return 0;

        return 1; /* they match */
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

                pthread_rwlock_wrlock(&ht->partitions[i].lock);
                head_rbt = &ht->partitions[i].rbt;

                /* go through all entries in the red-black-tree*/
                RBT_LOOP(head_rbt, pn) {
                        pdata = RBT_OPAQ(pn);

                        cp = (nfs_client_id_t *)pdata->buffval.pdata;
                        P(cp->cid_mutex);
                        if (ip_match(ip, cp->cid_recov_dir)) {
                                inc_client_id_ref(cp);

                                /* Take a reference to the client record */
                                recp = cp->cid_client_record;
                                inc_client_record_ref(recp);

                                V(cp->cid_mutex);

                                pthread_rwlock_unlock(&ht->partitions[i].lock);

                                P(recp->cr_mutex);

                                (void) nfs_client_id_expire(cp);

                                V(recp->cr_mutex);

                                dec_client_id_ref(cp);
                                dec_client_record_ref(recp);
                                return;
                        } else {
                                V(cp->cid_mutex);
                        }

                        RBT_INCREMENT(pn);
                }

                pthread_rwlock_unlock(&ht->partitions[i].lock);
        }
}

void release_ip(char *ip, int notdone)
{
        if (notdone)
                nfs_release_nlm_state();
        nfs_release_v4_client(ip);
}

/* try to find this node's id in an event */
int check_for_id( int inum, struct dirent **namelist, ushort id)
{
char *cp, *cp2;
int icnt = 0;
int i, ientry, notdone;
int take, rel;
time_t t_this_entry;
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
        for (ientry = (inum - 1); ientry > 2; ientry--) {
                take = rel = 0;
                switch (namelist[ientry]->d_name[0]) {
                        case 'r':
                                rel = 1; /* releaseip event */
                                break;
                        case 't':
                                take = 1; /* takeip event */
                                break;
                }
                /*
                 * if not a takeip event nor a releaseip event skip.
                 * if id is 0 we only care about takeip events.
                 */
                if ((!take && !rel) || (id == 0 && rel))
                        continue;

                strcpy(workpath, namelist[ientry]->d_name);
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
                t_this_entry = (time_t) atol(cp);
                if (t_this_entry < t_after) {
                        continue;
                }
                if ( id > 0 ) {
                        /* id is the 3rd entry */
                        cp2++;
                        cp = cp2;
                        while(*cp2 != DELIMIT)
                                cp2++;
                        *cp2 = '\0';
                        i = atoi(cp);
                        if ( ((ushort) i == id )) {
                                icnt++;
                                if (rel) {
                                        /* IP is the 4th entry */
                                        cp2++;
                                        release_ip(cp2, notdone);
                                        /* we are done with v3 */
                                        notdone = 0;
                                }
                        }
                } else {
                        t_after = t_this_entry + 1;
                        icnt = 1;
                        break;
                }
        }
        LogDebug(COMPONENT_THREAD, "ipcount %d for node %d after %ld",
            icnt, id, t_after);

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
void rec_gc( int inum, struct dirent **namelist)
{
int ientry;
time_t t_this_entry, t_dead, sticky_time;
char workpath[PATH_MAX];

        sticky_time = ( NFS_RECOV_CYCLE * NFS_RECOV_GC );
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
int match_to_releaseip( int num, struct dirent **namelist, ushort id, nfs_grace_start_array_t *array)
{

char *cp, *cp2;
int ifound, iend;
int i, ientry, ientry_r, ientry_rstart;
char workpath[PATH_MAX];
char workaddr[IPADDR_STRSZ];
nfs_grace_start_t *working;
time_t t_time, r_time, t_done;

        working = array->nfs_grace_start;
        ientry = num - 1;
        ifound = ientry_rstart = 0;
        t_time = t_done = 0;
        iend = array->num_elements;
        while (1) {
                /* locate the eligible takeip records. There should be exactly
                 * num_elements of them.
                 */
                if ((namelist[ientry]->d_name[0] != 't')) {
                        break;
                }
                t_time = parse_time(namelist[ientry]->d_name);
                if (t_time < t_after) {
                        ientry--;
                        continue;
                }
                strcpy(workpath, namelist[ientry]->d_name);
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
                if ( ((ushort) i != id )) {
                        ientry--;
                        continue;
                }
                /* We start from the most recent record what ever
                 * action we take here clears them all
                 */
                if ( ifound == 0 )
                        t_done = (time_t) (t_time + 1);
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
                strcpy(workaddr, cp);
                /* Don't match it again */
                ientry--;
                if ( !ientry_rstart )
                        ientry_r = ientry;
                else
                        ientry_r = ientry_rstart;
                while (1) {
                        if (ientry_r <= 1) {
                                if ( t_done )
                                        t_after = t_done;
                                return(-1);
                        }
                        if (namelist[ientry_r]->d_name[0] != 'r') {
                                ientry_r--;
                                continue;
                        }
                        if ( !ientry_rstart )
                                ientry_rstart = ientry_r;
                        strcpy(workpath, namelist[ientry_r]->d_name);
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
                                /* We require the matched release to be earlier than the takeip */
                                r_time = parse_time(namelist[ientry_r]->d_name);
                                if (t_time < r_time) {
                                        ientry_r--;
                                        continue;
                                } else
                                        break;
                        }
                }
                /* Found one */
                /* backup and get the node id */
                cp--;
                *cp = '\0';
                cp--;
                while(*cp != DELIMIT)
                        cp--;
                cp++;
                working->nodeid = (ushort) atoi(cp);
                working->event = TAKEIP;
                strcpy(working->ipaddr,workaddr);
                ifound++;
                LogDebug(COMPONENT_THREAD, "found %d address %s at release entry %d from node %d", ifound, working->ipaddr, ientry_r, working->nodeid );
                if ( ifound < iend )
                        working++;
                else
                        break;
        }
        if ( t_done )
                t_after = t_done;
        if ( ifound == array->num_elements ) {
                return(0);
        } else {
                /* Shouldn't happen */
                LogEvent(COMPONENT_THREAD, "code should not be reached found = %d searching for %d addresses", ifound, array->num_elements);
                return(-1);
        }
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

#endif /* SONAS */

void *recovery_thread(void *UnusedArg)
{
#ifdef SONAS
struct dirent **namelist;
int n, ipcount;
uint32_t uerr=0;
uint32_t ucnt=0;
size_t size;
nfs_grace_start_array_t *nfs_grace_start_array;

        SetNameFunction("recovery_thr");
        t_after = 0;

        while(1) {
                if ( ucnt == 0 ) { /* We are just coming up. Should be in grace for us. */
                        sleep(NFS_RECOV_GC); /* let things clear out */
                        ucnt++;
                }
                nfs_grace_start_array = NULL;
                n = scandir(NFS_RECOV_EVENTS, &namelist, 0, alphasort);
                if (n < 0) {
                        /* Don't flood the log */
                        if ( uerr < 3 ) {
                                LogEvent(COMPONENT_THREAD, "scandir of %s failed errno = %d", NFS_RECOV_EVENTS, errno);
                                sleep(NFS_RECOV_CYCLE);
                                uerr++;
                                continue;
                        }
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

                        if ((ipcount = check_for_id( n, namelist, g_nodeid))) { 

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
                                size = sizeof(nfs_grace_start_t) * ipcount;
                                nfs_grace_start_array->nfs_grace_start = gsh_malloc(size);
                                if (nfs_grace_start_array->nfs_grace_start == NULL) {
                                        LogCrit(COMPONENT_THREAD, "ENOMEM");
                                        gsh_free( nfs_grace_start_array );
                                        break;
                                }
                                nfs_grace_start_array->num_elements = ipcount;
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
                                if ((check_for_id( n, namelist, 0))) {
                                        nfs4_start_grace(NULL);
                                        LogEvent(COMPONENT_THREAD, "Grace started with NULL node id %d", g_nodeid);
                                }
                                break;
                        }
                }
                rec_gc( n, namelist);
                free_dirent(n, namelist);
                sleep(10);
        } /* while ( 1 ) */
#endif /* SONAS */

        return NULL;
}
