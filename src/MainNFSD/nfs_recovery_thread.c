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



int check_for_me( int inum, struct dirent **namelist, ushort id)
{
char *cp, *cp2;
int icnt = 0;
int i, ientry;
time_t t_this_entry;
char workpath[PATH_MAX];

        ientry = inum - 1;
        while(ientry > 1) {              
                if ((namelist[ientry]->d_name[0] != 't'))
                        break;
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
                        ientry--;
                        continue;
                }
                /* id is the 3rd entry */
                cp2++;
                cp = cp2;
                while(*cp2 != DELIMIT)
                        cp2++;
                *cp2 = '\0';
                i = atoi(cp);
                if ( ((ushort) i == id ))
                        icnt++;
                ientry--;
        }
        LogDebug(COMPONENT_THREAD, "ipcount %d for node %d after %ld", icnt, id, t_after);
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
ushort myid;
size_t size;
nfs_grace_start_array_t *nfs_grace_start_array;

        SetNameFunction("recovery_thr");
        myid = gpfs_ganesha(OPENHANDLE_GET_NODEID, NULL) + 1;
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
                        if (( n <= 2 ) || (namelist[n-1]->d_name[0] != 't')) {
                                /* Common case. Nothing to do. */
                                break;
                        }
                        /* If we reach here then we have "takeip" records 
                         */

                        if ((ipcount = check_for_me( n, namelist, myid))) { 

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
                                if (( match_to_releaseip( n, namelist, myid, nfs_grace_start_array))) {
                                /* Couldn't match them all need to read all nodes */
                                        nfs_grace_start_array->num_elements = 1;
                                        nfs_grace_start_array->nfs_grace_start->nodeid = ALL_NODES;
                                        nfs_grace_start_array->nfs_grace_start->ipaddr[0]='\0';
                                        nfs_grace_start_array->nfs_grace_start->event=NOTSPECIFIED;
                                        LogEvent(COMPONENT_THREAD, "Grace entered for ALL_NODES on node %d ", myid);
                                } else {
                                       LogEvent(COMPONENT_THREAD, "Grace entered for specific nodes on node %d ", myid);
                                }
                                nfs4_start_grace( nfs_grace_start_array );
                                if ( nfs_grace_start_array ) {
                                        gsh_free( nfs_grace_start_array->nfs_grace_start );
                                        gsh_free( nfs_grace_start_array );
                                }
                                break;
                        } else {
                                nfs4_start_grace(NULL);
                                LogEvent(COMPONENT_THREAD, "Grace entered with no nodes on node %d", myid);
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
