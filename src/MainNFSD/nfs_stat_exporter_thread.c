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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include "nfs_core.h"
#include "nfs_stat.h"
#include "nfs_exports.h"
#include "nodelist.h"
#include "stuff_alloc.h"
#include "fsal.h"
#include "rpc.h"

#define DEFAULT_PORT "10401"

#define BACKLOG 10

#define  CONF_STAT_EXPORTER_LABEL  "STAT_EXPORTER"
#define STRCMP   strcasecmp

/* Make sure this is <= the same macro in support/exports.c */
#define EXPORT_MAX_CLIENTS 20
#define EXPORT_MAX_CLIENTLEN 256        /* client name len */

int stat_export_check_access(struct sockaddr_storage *pssaddr,
                             exportlist_client_t *clients,
                             exportlist_client_entry_t * pclient_found)
{
  sockaddr_t *psockaddr_in;
#ifdef _USE_TIRPC_IPV6
  struct sockaddr_in6 *psockaddr_in6;
  static char ten_bytes_all_0[10];
  static unsigned two_bytes_all_1 = 0xFFFF;
  char ip6string[MAXHOSTNAMELEN];
  memset(ten_bytes_all_0, 0, 10);
#endif
  char ipstring[SOCK_NAME_MAX];

  psockaddr_in = (sockaddr_t *)pssaddr;

  /* For now, no matching client is found */
  memset(pclient_found, 0, sizeof(exportlist_client_entry_t));

#ifdef _USE_TIPRC_IPV6
  if(psockaddr_in->sin_family == AF_INET)
    {
#endif                          /* _USE_TIRPC_IPV6 */
      /* Convert IP address into a string for wild character access checks. */
      sprint_sockip(psockaddr_in, ipstring, sizeof(ipstring));
      if(ipstring == NULL)
        {
          LogCrit(COMPONENT_MAIN,
                  "Stat Export Check Access: Could not convert the IPv4 address to a character string.");
          return FALSE;
        }
      if(export_client_match
         (psockaddr_in, ipstring, clients, pclient_found, EXPORT_OPTION_READ_ACCESS | EXPORT_OPTION_WRITE_ACCESS))
        return TRUE;
#ifdef _USE_TIRPC_IPV6
    }
  else
    {
      psockaddr_in6 = (struct sockaddr_in6 *)pssaddr;
      if(isFulldebug(COMPONENT_MAIN))
        {
          char txtaddrv6[100];

          inet_ntop(psockaddr_in6->sin6_family,
                    psockaddr_in6->sin6_addr.s6_addr, txtaddrv6, 100);
          LogFullDebug(COMPONENT_MAIN,
                       "Client has IPv6 adress = %s", txtaddrv6);
        }
      /* If the client socket is IPv4, then it is wrapped into a   ::ffff:a.b.c.d IPv6 address. We check this here
       * This kind of adress is shaped like this:
       * |---------------------------------------------------------------|
       * |   80 bits = 10 bytes  | 16 bits = 2 bytes | 32 bits = 4 bytes |
       * |---------------------------------------------------------------|
       * |            0          |        FFFF       |    IPv4 address   |
       * |---------------------------------------------------------------|   */
      if(!memcmp(psockaddr_in6->sin6_addr.s6_addr, ten_bytes_all_0, 10) &&
         !memcmp((char *)(psockaddr_in6->sin6_addr.s6_addr + 10),
                 (char *)&two_bytes_all_1, 2))
        {
          /* Convert IP address into a string for wild character access checks. */
          inet_ntop(psockaddr_in->sin6_family, &psockaddr_in->sin6_addr,
                    ip6string, INET6_ADDRSTRLEN);
          if(ip6string == NULL)
            {
              LogCrit(COMPONENT_MAIN,
                      "Error: Could not convert the IPv6 address to a character string.");
              return FALSE;
            }
          /* This is an IPv4 address mapped to an IPv6 one. Extract the IPv4 address and proceed with IPv4 autentication */
          memcpy((char *)&addr, (char *)(psockaddr_in6->sin6_addr.s6_addr + 12), 4);

          /* Proceed with IPv4 dedicated function */
          /* else, check if any access only export matches this client */
          if(export_client_match
             (addr, ip6string, clients, pclient_found, EXPORT_OPTION_READ_ACCESS | EXPORT_OPTION_WRITE_ACCESS))
            return TRUE;
        }
      if(export_client_matchv6
         (&(psockaddr_in6->sin6_addr), clients, pclient_found, EXPORT_OPTION_READ_ACCESS | EXPORT_OPTION_WRITE_ACCESS))
        return TRUE;
    }
#endif                          /* _USE_TIRPC_IPV6 */
  /* If this point is reached, no matching entry was found */
  return FALSE;

}                               /* stat_export_check_access */

static int parseAccessParam_for_statexporter(char *var_name, char *var_value,
					     exportlist_client_t *clients)
{
  int rc, err_flag = FALSE;
  char *expended_node_list;

  /* temp array of clients */
  char *client_list[EXPORT_MAX_CLIENTS];
  int idx;
  int count;

  /* expends host[n-m] notations */
  count =
    nodelist_common_condensed2extended_nodelist(var_value, &expended_node_list);

  if(count <= 0)
    {
      err_flag = TRUE;
      LogCrit(COMPONENT_CONFIG,
              "STAT_EXPORT_ACCESS: ERROR: Invalid format for client list in EXPORT::%s definition",
              var_name);

      return -1;
    }
  else if(count > EXPORT_MAX_CLIENTS)
    {
      err_flag = TRUE;
      LogCrit(COMPONENT_CONFIG,
              "STAT_EXPORT_ACCESS: ERROR: Client list too long (%d>%d)",
              count, EXPORT_MAX_CLIENTS);
      return -1;
    }

  /* allocate clients strings  */
  for(idx = 0; idx < count; idx++)
    {
      client_list[idx] = (char *)Mem_Alloc(EXPORT_MAX_CLIENTLEN);
      client_list[idx][0] = '\0';
    }

  /*
   * Search for coma-separated list of hosts, networks and netgroups
   */
  rc = nfs_ParseConfLine(client_list, count,
                         expended_node_list, find_comma, find_endLine);

  /* free the buffer the nodelist module has allocated */
  free(expended_node_list);

  if(rc < 0)
    {
      err_flag = TRUE;
      LogCrit(COMPONENT_CONFIG,
              "STAT_EXPORT_ACCESS: ERROR: Client list too long (>%d)", count);

      /* free client strings */
      for(idx = 0; idx < count; idx++)
        Mem_Free((caddr_t) client_list[idx]);

      return rc;
    }

  rc = nfs_AddClientsToClientArray( clients, rc,
                                    (char **)client_list, EXPORT_OPTION_READ_ACCESS | EXPORT_OPTION_WRITE_ACCESS);
  if(rc != 0)
    {
      err_flag = TRUE;
      LogCrit(COMPONENT_CONFIG,
              "STAT_EXPORT_ACCESS: ERROR: Invalid client found in \"%s\"",
              var_value);

      /* free client strings */
      for(idx = 0; idx < count; idx++)
        Mem_Free((caddr_t) client_list[idx]);

      return rc;
    }

  /* everything is OK */

  /* free client strings */
  for(idx = 0; idx < count; idx++)
    Mem_Free((caddr_t) client_list[idx]);

  return rc;
}


int get_stat_exporter_conf(config_file_t in_config, external_tools_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;
  config_item_t item;

  strncpy(out_parameter->stat_export.export_stat_port, DEFAULT_PORT, MAXPORTLEN);

   /* Get the config BLOCK */
 if((block = config_FindItemByName(in_config, CONF_STAT_EXPORTER_LABEL)) == NULL)
    {
      /* cannot read item */
      LogCrit(COMPONENT_CONFIG,
              "STAT_EXPORTER: Cannot read item \"%s\" from configuration file",
              CONF_STAT_EXPORTER_LABEL);
      /* Expected to be a block */
      return ENOENT;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
     {
       LogCrit(COMPONENT_CONFIG,
               "STAT_EXPORTER: Cannot read item \"%s\" from configuration file",
               CONF_STAT_EXPORTER_LABEL);
      /* Expected to be a block */
       return ENOENT;
     }

  /* makes an iteration on the (key, value) couplets */
  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
       /* retrieve key's name */
      item = config_GetItemByIndex(block, var_index);
      err = config_GetKeyValue(item, &key_name, &key_value);

      if(err)
        {
          LogCrit(COMPONENT_CONFIG,
                  "STAT_EXPORTER: ERROR reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_FS_SPECIFIC);
          return err;
        }

      if(!STRCMP(key_name, "Access"))
        {
	  parseAccessParam_for_statexporter(key_name, key_value,
					    &(out_parameter->stat_export.allowed_clients));
        }
      else if(!STRCMP(key_name, "Port"))
        {
          strncpy(out_parameter->stat_export.export_stat_port, key_value, MAXPORTLEN);
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "STAT_EXPORTER LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_FS_SPECIFIC);
          return EINVAL;
        }
    }
  return 0;
}

int merge_stats(nfs_request_stat_item_t *global_stat_items,
                nfs_request_stat_item_t **workers_stat_items, int function_index, int detail_flag)
{
  int rc = ERR_STAT_NO_ERROR;
  unsigned int i = 0;

  for(i = 0; i < nfs_param.core_param.nb_worker; i++)
    {
      if(i == 0)
        {
          global_stat_items[function_index].total =
              workers_stat_items[i][function_index].total;
          global_stat_items[function_index].success =
              workers_stat_items[i][function_index].success;
          global_stat_items[function_index].dropped =
              workers_stat_items[i][function_index].dropped;
          global_stat_items[function_index].tot_latency =
              workers_stat_items[i][function_index].tot_latency;
          global_stat_items[function_index].min_latency =
              workers_stat_items[i][function_index].min_latency;
          global_stat_items[function_index].max_latency =
              workers_stat_items[i][function_index].max_latency;
          if(detail_flag)
            {
              global_stat_items[function_index].tot_await_time =
                  workers_stat_items[i][function_index].tot_await_time;
            }
        }
      else
        {
          global_stat_items[function_index].total +=
              workers_stat_items[i][function_index].total;
          global_stat_items[function_index].success +=
              workers_stat_items[i][function_index].success;
          global_stat_items[function_index].dropped +=
              workers_stat_items[i][function_index].dropped;
          global_stat_items[function_index].tot_latency +=
              workers_stat_items[i][function_index].tot_latency;
          set_min_latency(&(global_stat_items[function_index]),
              workers_stat_items[i][function_index].min_latency);
          set_max_latency(&(global_stat_items[function_index]),
              workers_stat_items[i][function_index].max_latency);
          if(detail_flag)
            {
              global_stat_items[function_index].tot_await_time +=
                  workers_stat_items[i][function_index].tot_await_time;
            }
        }
    }

  return rc;
}

int write_stats(char *stat_buf, int num_cmds, char **function_names, nfs_request_stat_item_t *global_stat_items, int detail_flag)
{
  int rc = ERR_STAT_NO_ERROR;

  char *offset = NULL;
  unsigned int i = 0;
  unsigned int tot_calls =0, tot_latency = 0;
  unsigned int tot_await_time = 0;
  float tot_latency_ms = 0;
  float tot_await_time_ms = 0;
  char *name = NULL;
  char *ver = NULL;
  char *call = NULL;
  char *saveptr = NULL;

  offset = stat_buf;
  for(i = 0; i < num_cmds; i++)
    {
      tot_calls = global_stat_items[i].total;
      tot_latency = global_stat_items[i].tot_latency;
      tot_latency_ms = (float)((float)tot_latency / (float)1000);
      if(detail_flag)
        {
          tot_await_time = global_stat_items[i].tot_await_time;
          tot_await_time_ms = (float)((float)tot_await_time / (float)1000);
        }


      /* Extract call name from function name. */
      name = Str_Dup(function_names[i]);
      ver = strtok_r(name, "_", &saveptr);
      call = strtok_r(NULL, "_", &saveptr);

      if(detail_flag)
        sprintf(offset, "_%s_ %u %.2f %.2f", call, tot_calls, tot_latency_ms, tot_await_time_ms);
      else
        sprintf(offset, "_%s_ %u %.2f", call, tot_calls, tot_latency_ms);
      offset += strlen(stat_buf);
      if(i != num_cmds - 1)
        {
          sprintf(offset, "%s", " ");
          offset += 1;
        }

      Mem_Free(name);
    }

  return rc;
}

int merge_nfs_stats(char *stat_buf, nfs_stat_client_req_t *stat_client_req,
                    nfs_worker_stat_t *global_data, nfs_worker_data_t *workers_data)
{
  int rc = ERR_STAT_NO_ERROR;

  unsigned int i = 0;
  unsigned int num_cmds = 0;
  nfs_request_stat_item_t *global_stat_items = NULL;
  nfs_request_stat_item_t *workers_stat_items[nfs_param.core_param.nb_worker];
  char **function_names = NULL;

  switch(stat_client_req->nfs_version)
    {
      case 2:
        num_cmds = NFS_V2_NB_COMMAND;
        global_stat_items = (global_data->stat_req.stat_req_nfs2);
        for(i = 0; i < nfs_param.core_param.nb_worker; i++)
          {
            workers_stat_items[i] = (workers_data[i].stats.stat_req.stat_req_nfs2);
          }
        function_names = nfsv2_function_names;
      break;

      case 3:
        num_cmds = NFS_V3_NB_COMMAND;
        global_stat_items = (global_data->stat_req.stat_req_nfs3);
        for(i = 0; i < nfs_param.core_param.nb_worker; i++)
          {
            workers_stat_items[i] = (workers_data[i].stats.stat_req.stat_req_nfs3);
          }
        function_names = nfsv3_function_names;
      break;

      case 4:
        num_cmds = NFS_V4_NB_COMMAND;
        global_stat_items = (global_data->stat_req.stat_req_nfs4);
        for(i = 0; i < nfs_param.core_param.nb_worker; i++)
          {
            workers_stat_items[i] = (workers_data[i].stats.stat_req.stat_req_nfs4);
          }
        function_names = nfsv4_function_names;
      break;

      default:
        // TODO: Invalid NFS version handling
        LogCrit(COMPONENT_MAIN, "Error: Invalid NFS version.");
      break;
    }

  switch(stat_client_req->stat_type)
    {
      case PER_SERVER:
        for(i = 0; i < num_cmds; i++)
          {
            rc = merge_stats(global_stat_items, workers_stat_items, i, 0);
          }
        rc = write_stats(stat_buf, num_cmds, function_names, global_stat_items, 0);
      break;

      case PER_SERVER_DETAIL:
        for(i = 0; i < num_cmds; i++)
          {
            rc = merge_stats(global_stat_items, workers_stat_items, i, 1);
          }
        rc = write_stats(stat_buf, num_cmds, function_names, global_stat_items, 1);
      break;

      case PER_CLIENT:
      break;

      case PER_SHARE:
      break;

      case PER_CLIENTSHARE:
      break;

      default:
        // TODO: Invalid stat type handling
        LogCrit(COMPONENT_MAIN, "Error: Invalid stat type.");
      break;
        }

  return rc;
}

int process_stat_request(void *addr, int new_fd)
{
  int rc = ERR_STAT_NO_ERROR;

  char cmd_buf[4096];

  char stat_buf[4096];
  char *token = NULL;
  char *key = NULL;
  char *value = NULL;
  char *saveptr1 = NULL;
  char *saveptr2 = NULL;

  nfs_worker_data_t *workers_data = addr;
  nfs_worker_stat_t global_worker_stat;
  nfs_stat_client_req_t stat_client_req;
  memset(&stat_client_req, 0, sizeof(nfs_stat_client_req_t));
  memset(cmd_buf, 0, 4096);

  if((rc = recv(new_fd, cmd_buf, 4096, 0)) == -1)
    LogError(COMPONENT_MAIN, ERR_SYS, errno, rc);

  /* Parse command options. */
  token = strtok_r(cmd_buf, ",", &saveptr1);
  while(token != NULL)
  {
    key = strtok_r(token, "=", &saveptr2);
    value = strtok_r(NULL, "=", &saveptr2);

    if(key != NULL && value != NULL)
    {
      if(strcmp(key, "version") == 0)
      {
        stat_client_req.nfs_version = atoi(value);
      }
      else if(strcmp(key, "type") == 0)
      {
        if(strcmp(value, "all") == 0)
          {
            stat_client_req.stat_type = PER_SERVER;
          }
        else if(strcmp(value, "all_detail") == 0)
          {
            stat_client_req.stat_type = PER_SERVER_DETAIL;
          }
      }
    }

    token = strtok_r(NULL, ",", &saveptr1);
  }

  memset(stat_buf, 0, 4096);
  merge_nfs_stats(stat_buf, &stat_client_req, &global_worker_stat, workers_data);
  if((rc = send(new_fd, stat_buf, 4096, 0)) == -1)
    LogError(COMPONENT_MAIN, ERR_SYS, errno, rc);

  close(new_fd);

  return rc;
}

int check_permissions() {
  return 0;
}

void *stat_exporter_thread(void *addr)
{
  int sockfd, new_fd;
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr;
  socklen_t sin_size;
  int yes = 1;
  char s[INET6_ADDRSTRLEN];
  int rc;
  exportlist_client_entry_t pclient_found;

  SetNameFunction("statistics_exporter");

  memset(&hints, 0, sizeof hints);

#ifndef _USE_TIRPC_IPV6
  hints.ai_family = AF_INET;
#else
  hints.ai_family = AF_INET6;
#endif
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if((rc = getaddrinfo(NULL, nfs_param.extern_param.stat_export.export_stat_port, &hints, &servinfo)) != 0)
    {
      LogCrit(COMPONENT_MAIN, "getaddrinfo: %s", gai_strerror(rc));
      return NULL;
    }
  for(p = servinfo; p != NULL; p = p->ai_next)
    {
      if((sockfd = socket(p->ai_family, p->ai_socktype,
                          p->ai_protocol)) == -1)
        {
          LogError(COMPONENT_MAIN, ERR_SYS, errno, sockfd);
          continue;
        }

      if((rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                          sizeof(int))) == -1)
        {
          LogError(COMPONENT_MAIN, ERR_SYS, errno, rc);
          return NULL;
        }

      if((rc = bind(sockfd, p->ai_addr, p->ai_addrlen)) == -1)
        {
          close(sockfd);
          LogError(COMPONENT_MAIN, ERR_SYS, errno, rc);
          continue;
        }

      break;
    }

  if(p == NULL)
    {
      LogCrit(COMPONENT_MAIN, "server: failed to bind");
      return NULL;
    }

  freeaddrinfo(servinfo);
  if((rc = listen(sockfd, BACKLOG)) == -1)
    {
      LogError(COMPONENT_MAIN, ERR_SYS, errno, rc);
      return NULL;
    }
  LogInfo(COMPONENT_MAIN, "Stat export server: Waiting for connections...");

  while(1)
    {
      sin_size = sizeof their_addr;
      new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
      if(new_fd == -1)
        {
          LogError(COMPONENT_MAIN, ERR_SYS, errno, new_fd);
          continue;
        }

      sprint_sockip((sockaddr_t *)&their_addr, s, sizeof s);

      if (stat_export_check_access(&their_addr,
                                   &(nfs_param.extern_param.stat_export.allowed_clients),
                                   &pclient_found)) {
        LogDebug(COMPONENT_MAIN, "Stat export server: Access granted to %s", s);
        process_stat_request(addr, new_fd);
      } else {
        LogWarn(COMPONENT_MAIN, "Stat export server: Access denied to %s", s);
      }
    }                           /* while ( 1 ) */

  return NULL;
}                               /* stat_exporter_thread */

void *long_processing_thread(void *addr)
{
  nfs_worker_data_t *workers_data = (nfs_worker_data_t *) addr;
  struct timeval timer_end;
  struct timeval timer_diff;
  int i;

  SetNameFunction("long_processing");

  while(1)
    {
      sleep(1);
      gettimeofday(&timer_end, NULL);

      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        {
          if(workers_data[i].timer_start.tv_sec == 0)
            continue;
          timer_diff = time_diff(workers_data[i].timer_start, timer_end);
          if(timer_diff.tv_sec == nfs_param.core_param.long_processing_threshold)
            LogEvent(COMPONENT_DISPATCH,
                     "Worker#%d: Function %s has been running for %llu.%.6llu seconds",
                     i, workers_data[i].pfuncdesc->funcname,
                     (unsigned long long)timer_diff.tv_sec,
                     (unsigned long long)timer_diff.tv_usec);
        }
    }

  return NULL;
}                               /* long_processing_thread */
