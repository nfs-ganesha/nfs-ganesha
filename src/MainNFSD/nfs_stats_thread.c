/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * \file    nfs_stats_thread.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/02/22 12:01:58 $
 * \version $Revision: 1.6 $
 * \brief   The file that contain the 'stats_thread' routine for the nfsd.
 *
 * nfs_stats_thread.c : The file that contain the 'stats_thread' routine for the nfsd.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <sys/stat.h>
#include <time.h>
#include "nfs_core.h"
#include "nfs_stat.h"
#include "nfs_exports.h"
#include "log.h"

extern hash_table_t *ht_ip_stats[NB_MAX_WORKER_THREAD];

static void accum_op_stats(nfs_op_stat_item_t * global,
                           const nfs_op_stat_item_t *mine,
                           int cnt)
{
    int j;
    for (j = 0; j < cnt; j++) {
        global[j].total += mine[j].total;
        global[j].success += mine[j].success;
        global[j].failed += mine[j].failed;
    }
}
static void accum_req_stats(nfs_request_stat_item_t * global,
                            const nfs_request_stat_item_t *mine,
                            int cnt)
{
    int j;
    for (j = 0; j < cnt; j++) {
        global[j].total += mine[j].total;
        global[j].success += mine[j].success;
        global[j].dropped += mine[j].dropped;
    }
}

/*
 * This function collects statistics from all Ganesha system modules so that they can then
 * be pushed into various users (e.g.: a statistics file, a network mgmt serviece, ...)
 * That is why collection of statistics is separated into its own function so that any user
 * can call for it.
 */
void stats_collect (ganesha_stats_t                 *ganesha_stats)
{
    hash_stat_t            *cache_inode_stat = &ganesha_stats->cache_inode_hstat;
    nfs_worker_stat_t      *global_worker_stat = &ganesha_stats->global_worker_stat;
    unsigned int           i, j;

    memset(ganesha_stats, 0, sizeof(ganesha_stats_t));

    HashTable_GetStats(fh_to_cache_entry_ht, cache_inode_stat);

    /* Merging the NFS protocols stats together */
    for (i = 0; i < nfs_param.core_param.nb_worker; i++) {
        nfs_worker_stat_t *wds = &workers_data[i].stats;
        global_worker_stat->nb_total_req += wds->nb_total_req;
        global_worker_stat->nb_udp_req += wds->nb_udp_req;
        global_worker_stat->nb_tcp_req += wds->nb_tcp_req;
        global_worker_stat->stat_req.nb_mnt1_req += wds->stat_req.nb_mnt1_req;
        global_worker_stat->stat_req.nb_mnt3_req += wds->stat_req.nb_mnt3_req;
        global_worker_stat->stat_req.nb_nfs2_req += wds->stat_req.nb_nfs2_req;
        global_worker_stat->stat_req.nb_nfs3_req += wds->stat_req.nb_nfs3_req;
        global_worker_stat->stat_req.nb_nfs4_req += wds->stat_req.nb_nfs4_req;
        global_worker_stat->stat_req.nb_nfs40_op += wds->stat_req.nb_nfs40_op;
        global_worker_stat->stat_req.nb_nfs41_op += wds->stat_req.nb_nfs41_op;
        global_worker_stat->stat_req.nb_nlm4_req += wds->stat_req.nb_nlm4_req;
        global_worker_stat->stat_req.nb_rquota1_req += wds->stat_req.nb_rquota1_req;
        global_worker_stat->stat_req.nb_rquota2_req += wds->stat_req.nb_rquota2_req;
#ifdef _USE_9P
        global_worker_stat->_9p_stat_req.nb_9p_req +=
             workers_data[i].stats._9p_stat_req.nb_9p_req ;
#endif

        accum_req_stats(global_worker_stat->stat_req.stat_req_mnt1,
                        wds->stat_req.stat_req_mnt1, MNT_V1_NB_COMMAND);

        accum_req_stats(global_worker_stat->stat_req.stat_req_mnt3,
                        wds->stat_req.stat_req_mnt3, MNT_V3_NB_COMMAND);

        accum_req_stats(global_worker_stat->stat_req.stat_req_nfs2, 
                        wds->stat_req.stat_req_nfs2, NFS_V2_NB_COMMAND);

        for (j = 0; j < NFS_V3_NB_COMMAND; j++) {
            nfs_request_stat_item_t * global = 
                global_worker_stat->stat_req.stat_req_nfs3 + j;
            const nfs_request_stat_item_t *mine = 
                wds->stat_req.stat_req_nfs3 + j;

            if(mine->total > 0) {
                if(global->total == 0) {
                    /* No requests recorded yet, so min/max starts here */
                    global->min_latency = mine->min_latency;
                    global->max_latency = mine->max_latency;
                } else {
                    /* Check if new min/max are lower/higher */
                    if (mine->min_latency < global->min_latency)
                        global->min_latency = mine->min_latency;
                    if(global->max_latency <  mine->max_latency)
                        global->max_latency = mine->max_latency;
                }
            }
            global->total += mine->total;
            global->success += mine->success;
            global->dropped += mine->dropped;
            global->tot_latency += mine->tot_latency;
        }

        accum_req_stats(global_worker_stat->stat_req.stat_req_nfs4,
                        wds->stat_req.stat_req_nfs4, NFS_V4_NB_COMMAND);
        accum_op_stats(global_worker_stat->stat_req.stat_op_nfs40,
                       wds->stat_req.stat_op_nfs40, NFS_V40_NB_OPERATION);
        accum_op_stats(global_worker_stat->stat_req.stat_op_nfs41,
                       wds->stat_req.stat_op_nfs41, NFS_V41_NB_OPERATION);
        accum_req_stats(global_worker_stat->stat_req.stat_req_nlm4,
                        wds->stat_req.stat_req_nlm4, NLM_V4_NB_OPERATION);
        accum_req_stats(global_worker_stat->stat_req.stat_req_rquota1,
                        wds->stat_req.stat_req_rquota1, RQUOTA_NB_COMMAND);
        accum_req_stats(global_worker_stat->stat_req.stat_req_rquota2,
                        wds->stat_req.stat_req_rquota2, RQUOTA_NB_COMMAND);
#ifdef _USE_9P
        for (j = 0; j < _9P_NB_COMMAND; j++) {
            if (i == 0) 
             {
                global_worker_stat->_9p_stat_req.stat_req_9p[j].total = 
                     workers_data[i].stats._9p_stat_req.stat_req_9p[j].total ;
                global_worker_stat->_9p_stat_req.stat_req_9p[j].success = 
                     workers_data[i].stats._9p_stat_req.stat_req_9p[j].success ;
                global_worker_stat->_9p_stat_req.stat_req_9p[j].failed = 
                     workers_data[i].stats._9p_stat_req.stat_req_9p[j].failed ;
             }
            else
             { 
                global_worker_stat->_9p_stat_req.stat_req_9p[j].total += 
                     workers_data[i].stats._9p_stat_req.stat_req_9p[j].total ;
                global_worker_stat->_9p_stat_req.stat_req_9p[j].success += 
                     workers_data[i].stats._9p_stat_req.stat_req_9p[j].success ;
                global_worker_stat->_9p_stat_req.stat_req_9p[j].failed += 
                     workers_data[i].stats._9p_stat_req.stat_req_9p[j].failed ;
             }
        }
#endif
    }                       /* for( i = 0 ; i < nfs_param.core_param.nb_worker ; i++ ) */

    for (j = 0; j < NFS_V3_NB_COMMAND; j++) {
        if (global_worker_stat->stat_req.stat_req_nfs3[j].total > 0) {
            ganesha_stats->avg_latency = (global_worker_stat->stat_req.stat_req_nfs3[j].tot_latency /
                                         global_worker_stat->stat_req.stat_req_nfs3[j].total);
        } else {
            ganesha_stats->avg_latency = 0;
        }
    }

    /* Printing the UIDMAP_TYPE hash table stats */
    idmap_get_stats(UIDMAP_TYPE, &ganesha_stats->uid_map, &ganesha_stats->uid_reverse);
    /* Printing the GIDMAP_TYPE hash table stats */
    idmap_get_stats(GIDMAP_TYPE, &ganesha_stats->gid_map, &ganesha_stats->gid_reverse);
    /* Stats for the IP/Name hashtable */
    nfs_ip_name_get_stats(&ganesha_stats->ip_name_map);
}

static void dump_op_stats(FILE * stats_file,
                           const char *tag,
                           const char *timestamp,
                           unsigned int nreqs,
                           const nfs_op_stat_item_t *stats,
                           int cnt)
{
    int j;
    fprintf(stats_file, "%s,%s;%u", tag, timestamp, nreqs);
    for(j = 0; j < cnt; j++) {
        fprintf(stats_file, "|%u,%u,%u",
                stats[j].total,
                stats[j].success,
                stats[j].failed);
    }
    fprintf(stats_file, "\n");
}

static void dump_req_stats(FILE * stats_file,
                           const char *tag,
                           const char *timestamp,
                           unsigned int nreqs,
                           const nfs_request_stat_item_t *stats,
                           int cnt)
{
    int j;
    fprintf(stats_file, "%s,%s;%u", tag, timestamp, nreqs);
    for(j = 0; j < cnt; j++) {
        fprintf(stats_file, "|%u,%u,%u",
                stats[j].total,
                stats[j].success,
                stats[j].dropped);
    }
    fprintf(stats_file, "\n");
}

void *stats_thread(void *UnusedArg)
{
  FILE *stats_file = NULL;
  struct stat statref;
  struct stat stattest;
  time_t current_time;
  struct tm *current_time_struct;
  struct tm *boot_time_struct;
  char strdate[1024];
  char strbootdate[1024];
  unsigned int j = 0;
  int reopen_stats = false;

  ganesha_stats_t        ganesha_stats;
  nfs_worker_stat_t      *global_worker_stat = &ganesha_stats.global_worker_stat;
  hash_stat_t            *cache_inode_stat = &ganesha_stats.cache_inode_hstat;
  hash_stat_t            *uid_map_hstat = &ganesha_stats.uid_map;
  hash_stat_t            *gid_map_hstat = &ganesha_stats.gid_map;
  hash_stat_t            *ip_name_hstat = &ganesha_stats.ip_name_map;
  hash_stat_t            *hstat_uid_reverse = &ganesha_stats.uid_reverse;
  hash_stat_t            *hstat_gid_reverse = &ganesha_stats.gid_reverse;
  hash_stat_t            *hstat_drc_udp = &ganesha_stats.drc_udp;
  hash_stat_t            *hstat_drc_tcp = &ganesha_stats.drc_tcp;


  SetNameFunction("stat_thr");

  /* Open the stats file, in append mode */
  if((stats_file = fopen(nfs_param.core_param.stats_file_path, "a")) == NULL)
    {
      LogCrit(COMPONENT_MAIN,
              "NFS STATS : Could not open stats file %s, no stats will be made...",
              nfs_param.core_param.stats_file_path);
      return NULL;
    }

  if(stat(nfs_param.core_param.stats_file_path, &statref) != 0)
    {
      LogCrit(COMPONENT_MAIN,
              "NFS STATS : Could not get inode for %s, no stats will be made...",
              nfs_param.core_param.stats_file_path);
      fclose(stats_file);
      return NULL;
    }

#ifdef _SNMP_ADM_ACTIVE
  /* start snmp library */
  if(stats_snmp() == 0)
    LogInfo(COMPONENT_MAIN,
            "NFS STATS: SNMP stats service was started successfully");
  else
    LogCrit(COMPONENT_MAIN,
            "NFS STATS: ERROR starting SNMP stats export thread");
#endif /*_SNMP_ADM_ACTIVE*/

  while(1)
    {
      /* Initial wait */
      sleep(nfs_param.core_param.stats_update_delay);

      /* Debug trace */
      LogInfo(COMPONENT_MAIN, "NFS STATS : now dumping stats");

      /* Stats main loop */
      if(stat(nfs_param.core_param.stats_file_path, &stattest) == 0)
        {
          if(stattest.st_ino != statref.st_ino)
            reopen_stats = true;
        }
      else
        {
          if(errno == ENOENT)
            reopen_stats = true;
        }

      /* Check is file has changed (the inode number will be different) */
      if(reopen_stats == true)
        {
          /* Stats file has changed */
          LogEvent(COMPONENT_MAIN,
                   "NFS STATS : stats file has changed or was removed, I close and reopen it");
          fflush(stats_file);
          fclose(stats_file);
          if((stats_file = fopen(nfs_param.core_param.stats_file_path, "a")) == NULL)
            {
              LogCrit(COMPONENT_MAIN,
                      "NFS STATS : Could not open stats file %s, no further stats will be made...",
                      nfs_param.core_param.stats_file_path);
              return NULL;
            }
          statref = stattest;
          reopen_stats = false;
        }

      /* Get the current epoch time */
      current_time = time(NULL);
      current_time_struct = localtime(&current_time);
      snprintf(strdate, 1024, "%u, %.2d/%.2d/%.4d %.2d:%.2d:%.2d ",
               (unsigned int)current_time,
               current_time_struct->tm_mday,
               current_time_struct->tm_mon + 1,
               1900 + current_time_struct->tm_year,
               current_time_struct->tm_hour,
               current_time_struct->tm_min,
               current_time_struct->tm_sec);

      /* Printing the general Stats */
      boot_time_struct = localtime(&ServerBootTime);
      snprintf(strbootdate, 1024, "%u, %.2d/%.2d/%.4d %.2d:%.2d:%.2d ",
               (unsigned int)ServerBootTime,
               boot_time_struct->tm_mday,
               boot_time_struct->tm_mon + 1,
               1900 + boot_time_struct->tm_year,
               boot_time_struct->tm_hour,
               boot_time_struct->tm_min,
               boot_time_struct->tm_sec);

      fprintf(stats_file, "NFS_SERVER_GENERAL,%s;%s\n", strdate, strbootdate);

      /* collect statistics */
      stats_collect(&ganesha_stats);

      /* Pinting the cache inode hash stat */
      /* This is done only on worker[0]: the hashtable is shared and worker 0 always exists */
      HashTable_GetStats(fh_to_cache_entry_ht, cache_inode_stat);

      fprintf(stats_file,
              "CACHE_INODE_HASH,%s;%zu,%zu,%zu,%zu\n",
              strdate, cache_inode_stat->entries,
              cache_inode_stat->min_rbt_num_node,
              cache_inode_stat->max_rbt_num_node,
              cache_inode_stat->average_rbt_num_node);

      fprintf(stats_file, "NFS/MOUNT STATISTICS,%s;%u,%u,%u|%u,%u,%u,%u,%u\n",
              strdate,
              global_worker_stat->nb_total_req,
              global_worker_stat->nb_udp_req,
              global_worker_stat->nb_tcp_req,
              global_worker_stat->stat_req.nb_mnt1_req,
              global_worker_stat->stat_req.nb_mnt3_req,
              global_worker_stat->stat_req.nb_nfs2_req,
              global_worker_stat->stat_req.nb_nfs3_req,
              global_worker_stat->stat_req.nb_nfs4_req);

      dump_req_stats(stats_file, "MNT V1 REQUEST", strdate,
                     global_worker_stat->stat_req.nb_mnt1_req,
                     global_worker_stat->stat_req.stat_req_mnt1,
                     MNT_V1_NB_COMMAND);

      dump_req_stats(stats_file, "MNT V3 REQUEST", strdate,
                     global_worker_stat->stat_req.nb_mnt3_req,
                     global_worker_stat->stat_req.stat_req_mnt3,
                     MNT_V3_NB_COMMAND);

      dump_req_stats(stats_file, "NFS V2 REQUEST", strdate,
                     global_worker_stat->stat_req.nb_nfs2_req,
                     global_worker_stat->stat_req.stat_req_nfs2,
                     NFS_V2_NB_COMMAND);

      fprintf(stats_file, "NFS V3 REQUEST,%s;%u", strdate,
              global_worker_stat->stat_req.nb_nfs3_req);
      for (j = 0; j < NFS_V3_NB_COMMAND; j++)
	{
          fprintf(stats_file, "|%u,%u,%u,%u,%u,%u,%u",
                  global_worker_stat->stat_req.stat_req_nfs3[j].total,
                  global_worker_stat->stat_req.stat_req_nfs3[j].success,
                  global_worker_stat->stat_req.stat_req_nfs3[j].dropped,
                  global_worker_stat->stat_req.stat_req_nfs3[j].tot_latency,
                  ganesha_stats.avg_latency,
                  global_worker_stat->stat_req.stat_req_nfs3[j].min_latency,
                  global_worker_stat->stat_req.stat_req_nfs3[j].max_latency);
        }
      fprintf(stats_file, "\n");

      dump_req_stats(stats_file, "NFS V4 REQUEST", strdate,
                     global_worker_stat->stat_req.nb_nfs4_req,
                     global_worker_stat->stat_req.stat_req_nfs4,
                     NFS_V4_NB_COMMAND);

      dump_op_stats(stats_file, "NFS V4.0 OPERATIONS", strdate,
                    global_worker_stat->stat_req.nb_nfs40_op,
                    global_worker_stat->stat_req.stat_op_nfs40,
                    NFS_V40_NB_OPERATION);

      dump_op_stats(stats_file, "NFS V4.1 OPERATIONS", strdate,
                    global_worker_stat->stat_req.nb_nfs41_op,
                    global_worker_stat->stat_req.stat_op_nfs41,
                    NFS_V41_NB_OPERATION);

      dump_req_stats(stats_file, "NLM V4 REQUEST,%s;%u", strdate,
                     global_worker_stat->stat_req.nb_nlm4_req,
                     global_worker_stat->stat_req.stat_req_nlm4,
                     NLM_V4_NB_OPERATION);

      dump_req_stats(stats_file, "RQUOTA V1 REQUEST", strdate,
                     global_worker_stat->stat_req.nb_rquota1_req,
                     global_worker_stat->stat_req.stat_req_rquota1,
                     RQUOTA_NB_COMMAND);

      dump_req_stats(stats_file, "RQUOTA V2 REQUEST", strdate,
                     global_worker_stat->stat_req.nb_rquota2_req,
                     global_worker_stat->stat_req.stat_req_rquota2,
                     RQUOTA_NB_COMMAND);

#ifdef _USE_9P
      fprintf(stats_file, "9P REQUEST,%s;%u", strdate,
              global_worker_stat->_9p_stat_req.nb_9p_req);
      for(j = 0; j < _9P_NB_COMMAND; j++)
        fprintf(stats_file, "|%u,%u,%u", 
                global_worker_stat->_9p_stat_req.stat_req_9p[j].total,
                global_worker_stat->_9p_stat_req.stat_req_9p[j].success,
                global_worker_stat->_9p_stat_req.stat_req_9p[j].failed ) ;
      fprintf(stats_file, "\n");
#endif

      fprintf(stats_file,
              "DUP_REQ_HASH,%s;%zu,%zu,%zu,%zu\n",
              strdate,
              hstat_drc_udp->entries + hstat_drc_tcp->entries,
              hstat_drc_udp->min_rbt_num_node +
              hstat_drc_tcp->min_rbt_num_node,
              hstat_drc_udp->max_rbt_num_node +
              hstat_drc_tcp->max_rbt_num_node,
              hstat_drc_udp->average_rbt_num_node +
              hstat_drc_tcp->average_rbt_num_node);

      fprintf(stats_file,
              "UIDMAP_HASH,%s;%zu,%zu,%zu,%zu\n", strdate,
              uid_map_hstat->entries, uid_map_hstat->min_rbt_num_node,
              uid_map_hstat->max_rbt_num_node,
              uid_map_hstat->average_rbt_num_node);
      fprintf(stats_file,
              "UNAMEMAP_HASH,%s;%zu,%zu,%zu,%zu\n",
              strdate, hstat_uid_reverse->entries,
              hstat_uid_reverse->min_rbt_num_node,
              hstat_uid_reverse->max_rbt_num_node,
              hstat_uid_reverse->average_rbt_num_node);

      fprintf(stats_file,
              "GIDMAP_HASH,%s;%zu,%zu,%zu,%zu\n", strdate,
              gid_map_hstat->entries,
              gid_map_hstat->min_rbt_num_node,
              gid_map_hstat->max_rbt_num_node,
              gid_map_hstat->average_rbt_num_node);
      fprintf(stats_file,
              "GNAMEMAP_HASH,%s;%zu,%zu,%zu,%zu\n",
              strdate, hstat_gid_reverse->entries,
              hstat_gid_reverse->min_rbt_num_node,
              hstat_gid_reverse->max_rbt_num_node,
              hstat_gid_reverse->average_rbt_num_node);

      fprintf(stats_file,
              "IP_NAME_HASH,%s;%zu,%zu,%zu,%zu\n",
              strdate, ip_name_hstat->entries,
              ip_name_hstat->min_rbt_num_node,
              ip_name_hstat->max_rbt_num_node,
              ip_name_hstat->average_rbt_num_node);

      /* Flush the data written */
      fprintf(stats_file, "END, ----- NO MORE STATS FOR THIS PASS ----\n");
      fflush(stats_file);

      /* Now managed IP stats dump */
      nfs_ip_stats_dump(ht_ip_stats,
                        nfs_param.core_param.nb_worker,
                        nfs_param.core_param.stats_per_client_directory);

    }                           /* while ( 1 ) */

  return NULL;
}                               /* stats_thread */
