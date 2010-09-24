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

extern nfs_parameter_t nfs_param;

#define PORT "10401"

#define BACKLOG 10

void sigchld_handler(int s)
{
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

void *get_in_addr(struct sockaddr *sa)
{
  if(sa->sa_family == AF_INET)
    {
      return &(((struct sockaddr_in*)sa)->sin_addr);
    }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
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
  float tot_latency_ms;
  float tot_await_time_ms;
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
      name = strdup(function_names[i]);
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

      free(name);
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
	LogMajor(COMPONENT_MAIN, "NFS VERSION: %d", stat_client_req.nfs_version);
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
  struct sigaction sa;
  int yes = 1;
  char s[INET6_ADDRSTRLEN];
  int rc;

  SetNameFunction("statistics_exporter");

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if((rc = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0)
    {
      LogCrit(COMPONENT_MAIN, "getaddrinfo: %s\n", gai_strerror(rc));
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

  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if((rc = sigaction(SIGCHLD, &sa, NULL)) == -1)
    {
      LogError(COMPONENT_MAIN, ERR_SYS, errno, rc);
      return NULL;
    }

  LogEvent(COMPONENT_MAIN, "Stat export server: Waiting for connections...");

  while(1)
    {
      sin_size = sizeof their_addr;
      new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
      if(new_fd == -1)
        {
	  LogError(COMPONENT_MAIN, ERR_SYS, errno, new_fd);
          continue;
        }
      inet_ntop(their_addr.ss_family,
                get_in_addr((struct sockaddr *)&their_addr),
                s, sizeof s);

      /* security!! */
      process_stat_request(addr, new_fd);
    }                           /* while ( 1 ) */

  return NULL;
}                               /* stat_exporter_thread */
