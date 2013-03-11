/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * \file    nfs_ip_stats.c
 * \date    $Date: 2006/01/24 13:49:12 $
 * \version $Revision: 1.4 $
 * \brief   The management of the IP stats per machines.
 *
 * nfs_ip_stats.c : The management of the IP stats per machines.
 *
 * $Header: /cea/S/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/support/nfs_ip_stats.c,v 1.4 2006/01/24 13:49:12 leibovic Exp $
 *
 * $Log: nfs_ip_stats.c,v $
 * Revision 1.4  2006/01/24 13:49:12  leibovic
 * Adding missing includes.
 *
 * Revision 1.3  2006/01/20 07:39:23  leibovic
 * Back to the previous version.
 *
 * Revision 1.1  2005/11/30 09:28:42  deniel
 * IP/stats cache per thread (no bottleneck) is complete
 *
 *
 *
 */
#include "config.h"
#include "HashTable.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include "nfs_ip_stats.h"
#include <stdlib.h>
#include <string.h>

uint32_t ip_stats_value_hash_func(hash_parameter_t * p_hparam,
                                           struct gsh_buffdesc * buffclef)
{
  return hash_sockaddr((sockaddr_t *)buffclef->addr, IGNORE_PORT) % p_hparam->index_size;
}

uint64_t ip_stats_rbt_hash_func(hash_parameter_t * p_hparam,
                                         struct gsh_buffdesc * buffclef)
{
  return hash_sockaddr((sockaddr_t *)buffclef->addr, IGNORE_PORT);
}

/**
 *
 * compare_ip_stats: compares the ip address stored in the key buffers.
 *
 * compare the ip address stored in the key buffers. This function is to be used as 'compare_key' field in 
 * the hashtable storing the nfs duplicated requests. 
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different. 
 *
 */
int compare_ip_stats(struct gsh_buffdesc * buff1, struct gsh_buffdesc * buff2)
{
  return (cmp_sockaddr(buff1->addr, buff2->addr, IGNORE_PORT) != 0) ? 0 : 1;
}

int display_ip_stats_key(struct gsh_buffdesc * pbuff, char *str)
{
  sockaddr_t *addr = (sockaddr_t *)(pbuff->addr);

  sprint_sockaddr(addr, str, HASHTABLE_DISPLAY_STRLEN);
  return strlen(str);
}

int display_ip_stats_val(struct gsh_buffdesc * pbuff, char *str)
{
  nfs_ip_stats_t *ip_stats = (nfs_ip_stats_t *)(pbuff->addr);

  return snprintf(str, HASHTABLE_DISPLAY_STRLEN,
                  "calls %u nfs2 %u nfs3 %u nfs4 %u mnt1 %u mnt3 %u",
                  ip_stats->nb_call,
                  ip_stats->nb_req_nfs2,
                  ip_stats->nb_req_nfs3,
                  ip_stats->nb_req_nfs4,
                  ip_stats->nb_req_mnt1,
                  ip_stats->nb_req_mnt3);
}

int nfs_ip_stats_add(hash_table_t * ht_ip_stats,
                     sockaddr_t * ipaddr, pool_t *ip_stats_pool)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffdata;
  nfs_ip_stats_t *g = NULL;
  sockaddr_t *pipaddr = NULL;

  /* Do nothing if configuration disables IP_Stats */
  if(nfs_param.core_param.dump_stats_per_client == 0)
    return IP_STATS_SUCCESS;

  /* Entry to be cached */
  g = pool_alloc(ip_stats_pool, NULL);

  if(g == NULL)
    return IP_STATS_INSERT_MALLOC_ERROR;

  if((pipaddr = gsh_malloc(sizeof(sockaddr_t))) == NULL)
    {
      pool_free(ip_stats_pool, g);
      return IP_STATS_INSERT_MALLOC_ERROR;
    }

  /* I have to keep an integer as key, I wil use the pointer
   * buffkey->addr for this, this also means that buffkey->len will
   * be 0 */
  memcpy(pipaddr, ipaddr, sizeof(sockaddr_t));

  buffkey.addr = pipaddr;
  buffkey.len = sizeof(sockaddr_t);

  /* I build the data with the request pointer that should be in state 'IN USE' */
  g->nb_call = 0;
  g->nb_req_nfs2 = 0;
  g->nb_req_nfs3 = 0;
  g->nb_req_nfs4 = 0;
  g->nb_req_mnt1 = 0;
  g->nb_req_mnt3 = 0;

  memset(g->req_mnt1, 0, MNT_V1_NB_COMMAND * sizeof(int));
  memset(g->req_mnt3, 0, MNT_V3_NB_COMMAND * sizeof(int));
  memset(g->req_nfs2, 0, NFS_V2_NB_COMMAND * sizeof(int));
  memset(g->req_nfs3, 0, NFS_V3_NB_COMMAND * sizeof(int));

  buffdata.addr = (caddr_t) g;
  buffdata.len = sizeof(nfs_ip_stats_t);

  if(HashTable_Set(ht_ip_stats, &buffkey, &buffdata) != HASHTABLE_SUCCESS)
    return IP_STATS_INSERT_MALLOC_ERROR;

  return IP_STATS_SUCCESS;
}                               /* nfs_ip_stats_add */

int nfs_ip_stats_incr(hash_table_t * ht_ip_stats,
                      sockaddr_t * ipaddr,
                      unsigned int nfs_prog,
                      unsigned int mnt_prog, struct svc_req *ptr_req)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;
  int status;
  nfs_ip_stats_t *g;

  buffkey.addr = (caddr_t) ipaddr;
  buffkey.len = sizeof(sockaddr_t);

  /* Do nothing if configuration disables IP_Stats */
  if(nfs_param.core_param.dump_stats_per_client == 0)
    return IP_STATS_SUCCESS;

  if(HashTable_Get(ht_ip_stats, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      g = (nfs_ip_stats_t *) buffval.addr;
      g->nb_call += 1;

      status = IP_STATS_SUCCESS;

      if(ptr_req->rq_prog == nfs_prog)
        {
          switch (ptr_req->rq_vers)
            {
            case NFS_V2:
              g->nb_req_nfs2 += 1;
              g->req_nfs2[ptr_req->rq_proc] += 1;
              break;

            case NFS_V3:
              g->nb_req_nfs3 += 1;
              g->req_nfs3[ptr_req->rq_proc] += 1;
              break;

            case NFS_V4:
              g->nb_req_nfs4 += 1;
              break;
            }
        }
      else if(ptr_req->rq_prog == mnt_prog)
        {
          switch (ptr_req->rq_vers)
            {
            case MOUNT_V1:
              g->nb_req_mnt1 += 1;
              g->req_mnt1[ptr_req->rq_proc] += 1;
              break;

            case MOUNT_V3:
              g->nb_req_mnt3 += 1;
              g->req_mnt3[ptr_req->rq_proc] += 1;
              break;
            }
        }
    }
  else
    {
      status = IP_STATS_NOT_FOUND;
    }
  return status;
}                               /* nfs_ip_stats_incr */


int nfs_ip_stats_get(hash_table_t * ht_ip_stats,
                     sockaddr_t * ipaddr, nfs_ip_stats_t ** g)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;
  int status;

  buffkey.addr = (caddr_t) ipaddr;
  buffkey.len = sizeof(sockaddr_t);

  /* Do nothing if configuration disables IP_Stats */
  if(nfs_param.core_param.dump_stats_per_client == 0)
    return IP_STATS_SUCCESS;

  if(HashTable_Get(ht_ip_stats, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      *g = (nfs_ip_stats_t *) buffval.addr;

      status = IP_STATS_SUCCESS;
    }
  else
    {
      status = IP_STATS_NOT_FOUND;
    }
  return status;
}                               /* nfs_ip_stats_get */

int nfs_ip_stats_remove(hash_table_t * ht_ip_stats,
                        sockaddr_t * ipaddr, pool_t *ip_stats_pool)
{
  struct gsh_buffdesc buffkey, old_key, old_value;
  int status = IP_STATS_SUCCESS;
  nfs_ip_stats_t *g = NULL;

  buffkey.addr = (caddr_t) ipaddr;
  buffkey.len = sizeof(sockaddr_t);

  /* Do nothing if configuration disables IP_Stats */
  if(nfs_param.core_param.dump_stats_per_client == 0)
    return IP_STATS_SUCCESS;

  if(HashTable_Del(ht_ip_stats, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      gsh_free(old_key.addr);
      g = old_value.addr;
      pool_free(ip_stats_pool, g);
    }
  else
    {
      status = IP_STATS_NOT_FOUND;
    }

  return status;
}                               /* nfs_ip_stats_remove */

/**
 *
 * nfs_Init_ip_stats: Init the hashtable for IP stats cache.
 *
 * Perform all the required initialization for hashtable IP stats cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
hash_table_t *nfs_Init_ip_stats(hash_parameter_t *param)
{
  hash_table_t *ht_ip_stats;

  if((ht_ip_stats = HashTable_Init(param)) == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS IP_STATS: Cannot init IP stats cache");
      return NULL;
    }

  return ht_ip_stats;
}                               /* nfs_Init_ip_stats */

void nfs_ip_stats_dump(hash_table_t ** ht_ip_stats,
                       unsigned int nb_worker, char *path_stat)
{
  struct rbt_node *it;
  struct rbt_head *tete_rbt;
  struct hash_data *addr = NULL;
  unsigned int i = 0;
  unsigned int j = 0;
  unsigned int k = 0;
  nfs_ip_stats_t *g[NB_MAX_WORKER_THREAD];
  nfs_ip_stats_t ip_stats_aggreg;
  // enough to hold an IPv4 or IPv6 address as a string
  char ipaddrbuf[40];
  char ifpathdump[MAXPATHLEN];
  sockaddr_t * ipaddr;
  time_t current_time;
  struct tm current_time_struct;
  char strdate[1024];
  FILE *flushipstat = NULL;

  /* Do nothing if configuration disables IP_Stats */
  if(nfs_param.core_param.dump_stats_per_client == 0)
    return;

  /* Compute the current time */
  current_time = time(NULL);
  memcpy(&current_time_struct, localtime(&current_time), sizeof(current_time_struct));
  snprintf(strdate, 1024, "%u, %.2d/%.2d/%.4d %.2d:%.2d:%.2d ",
           (unsigned int)current_time,
           current_time_struct.tm_mday,
           current_time_struct.tm_mon + 1,
           1900 + current_time_struct.tm_year,
           current_time_struct.tm_hour,
           current_time_struct.tm_min, current_time_struct.tm_sec);

  /* All clients are supposed to have call at least one time worker #0
   * we loop on every client in the HashTable */
  for(i = 0; i < ht_ip_stats[0]->parameter.index_size; i++)
    {
      tete_rbt = &ht_ip_stats[0]->partitions[i].rbt;
      RBT_LOOP(tete_rbt, it)
      {
        addr = it->rbt_opaq;

        ipaddr = (sockaddr_t *) addr->key.addr;

        sprint_sockaddr(ipaddr, ipaddrbuf, sizeof(ipaddrbuf));

        snprintf(ifpathdump, MAXPATHLEN, "%s/stats_nfs-%s", path_stat, ipaddrbuf);

        if((flushipstat = fopen(ifpathdump, "a")) == NULL)
          return;

        /* Collect stats for each worker and aggregate them */
        memset(&ip_stats_aggreg, 0, sizeof(ip_stats_aggreg));
        for(j = 0; j < nb_worker; j++)
          {
            if(nfs_ip_stats_get(ht_ip_stats[j],
                                ipaddr, &g[j]) != IP_STATS_SUCCESS)
              {
                fclose(flushipstat);
                return;
              }
            ip_stats_aggreg.nb_call += (g[j])->nb_call;

            ip_stats_aggreg.nb_req_nfs2 += (g[j])->nb_req_nfs2;
            ip_stats_aggreg.nb_req_nfs3 += (g[j])->nb_req_nfs3;
            ip_stats_aggreg.nb_req_nfs4 += (g[j])->nb_req_nfs4;
            ip_stats_aggreg.nb_req_mnt1 += (g[j])->nb_req_mnt1;
            ip_stats_aggreg.nb_req_mnt3 += (g[j])->nb_req_mnt3;

            for(k = 0; k < MNT_V1_NB_COMMAND; k++)
              ip_stats_aggreg.req_mnt1[k] += (g[j])->req_mnt1[k];

            for(k = 0; k < MNT_V3_NB_COMMAND; k++)
              ip_stats_aggreg.req_mnt3[k] += (g[j])->req_mnt3[k];

            for(k = 0; k < NFS_V2_NB_COMMAND; k++)
              ip_stats_aggreg.req_nfs2[k] += (g[j])->req_nfs2[k];

            for(k = 0; k < NFS_V3_NB_COMMAND; k++)
              ip_stats_aggreg.req_nfs3[k] += (g[j])->req_nfs3[k];
          }

        /* Write stats to file */
        fprintf(flushipstat, "NFS/MOUNT STATISTICS,%s;%u|%u,%u,%u,%u,%u\n",
                strdate,
                ip_stats_aggreg.nb_call,
                ip_stats_aggreg.nb_req_mnt1,
                ip_stats_aggreg.nb_req_mnt3,
                ip_stats_aggreg.nb_req_nfs2,
                ip_stats_aggreg.nb_req_nfs3, ip_stats_aggreg.nb_req_nfs4);

        fprintf(flushipstat, "MNT V1 REQUEST,%s;%u|", strdate,
                ip_stats_aggreg.nb_req_mnt1);
        for(k = 0; k < MNT_V1_NB_COMMAND - 1; k++)
          fprintf(flushipstat, "%u,", ip_stats_aggreg.req_mnt1[k]);
        fprintf(flushipstat, "%u\n", ip_stats_aggreg.req_mnt1[MNT_V1_NB_COMMAND - 1]);

        fprintf(flushipstat, "MNT V3 REQUEST,%s;%u|", strdate,
                ip_stats_aggreg.nb_req_mnt3);
        for(k = 0; k < MNT_V3_NB_COMMAND - 1; k++)
          fprintf(flushipstat, "%u,", ip_stats_aggreg.req_mnt3[k]);
        fprintf(flushipstat, "%u\n", ip_stats_aggreg.req_mnt3[MNT_V3_NB_COMMAND - 1]);

        fprintf(flushipstat, "NFS V2 REQUEST,%s;%u|", strdate,
                ip_stats_aggreg.nb_req_nfs2);
        for(k = 0; k < NFS_V2_NB_COMMAND - 1; k++)
          fprintf(flushipstat, "%u,", ip_stats_aggreg.req_nfs2[k]);
        fprintf(flushipstat, "%u\n", ip_stats_aggreg.req_nfs2[NFS_V2_NB_COMMAND - 1]);

        fprintf(flushipstat, "NFS V3 REQUEST,%s;%u|", strdate,
                ip_stats_aggreg.nb_req_nfs3);
        for(k = 0; k < NFS_V3_NB_COMMAND - 1; k++)
          fprintf(flushipstat, "%u,", ip_stats_aggreg.req_nfs3[k]);
        fprintf(flushipstat, "%u\n", ip_stats_aggreg.req_nfs3[NFS_V3_NB_COMMAND - 1]);

        fprintf(flushipstat, "END, ----- NO MORE STATS FOR THIS PASS ----\n");

        fflush(flushipstat);

        /* Check next client */
        RBT_INCREMENT(it);

        fclose(flushipstat);
      }

    }
}                               /* nfs_ip_stats_dump */
