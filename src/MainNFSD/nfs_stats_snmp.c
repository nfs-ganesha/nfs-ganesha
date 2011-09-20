/**
 * Export statistics via SNMP.
 * @see nfs_stats_thread.c
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

/* function name resolution */
#include "fsal.h"
#include "cache_inode.h"
#include "nfs_stat.h"

#include "nfs_core.h"
#include "nfs_stat.h"
#include "nfs_exports.h"
#include "external_tools.h"
#include "snmp_adm.h"

#include "stuff_alloc.h"
#include "common_utils.h"
#include "log_macros.h"

#define  CONF_SNMP_ADM_LABEL  "SNMP_ADM"
/* case unsensitivity */
#define STRCMP   strcasecmp

static int config_ok = 0;

char *nfsv2_function_names[] = {
  "NFSv2_null", "NFSv2_getattr", "NFSv2_setattr", "NFSv2_root",
  "NFSv2_lookup", "NFSv2_readlink", "NFSv2_read", "NFSv2_writecache",
  "NFSv2_write", "NFSv2_create", "NFSv2_remove", "NFSv2_rename",
  "NFSv2_link", "NFSv2_symlink", "NFSv2_mkdir", "NFSv2_rmdir",
  "NFSv2_readdir", "NFSv2_statfs"
};

char *nfsv3_function_names[] = {
  "NFSv3_null", "NFSv3_getattr", "NFSv3_setattr", "NFSv3_lookup",
  "NFSv3_access", "NFSv3_readlink", "NFSv3_read", "NFSv3_write",
  "NFSv3_create", "NFSv3_mkdir", "NFSv3_symlink", "NFSv3_mknod",
  "NFSv3_remove", "NFSv3_rmdir", "NFSv3_rename", "NFSv3_link",
  "NFSv3_readdir", "NFSv3_readdirplus", "NFSv3_fsstat",
  "NFSv3_fsinfo", "NFSv3_pathconf", "NFSv3_commit"
};

char *nfsv4_function_names[] = {
  "NFSv4_null", "NFSv4_compound"
};

char *mnt_function_names[] = {
  "MNT_null", "MNT_mount", "MNT_dump", "MNT_umount", "MNT_umountall", "MNT_export"
};

char *rquota_functions_names[] = {
  "rquota_Null", "rquota_getquota", "rquota_getquotaspecific", "rquota_setquota",
  "rquota_setquotaspecific"
};

#ifndef _NO_BUDDY_SYSTEM
buddy_stats_t global_buddy_stat;
#endif

int get_snmpadm_conf(config_file_t in_config, external_tools_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;
  config_item_t item;



   /* Get the config BLOCK */
 if((block = config_FindItemByName(in_config, CONF_SNMP_ADM_LABEL)) == NULL)
    {
      /* cannot read item */
      LogCrit(COMPONENT_CONFIG,
              "SNMP_ADM: Cannot read item \"%s\" from configuration file",
              CONF_SNMP_ADM_LABEL);
      /* Expected to be a block */
      return ENOENT;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
     {
       LogCrit(COMPONENT_CONFIG,
               "SNMP_ADM: Cannot read item \"%s\" from configuration file",
               CONF_SNMP_ADM_LABEL);
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
                  "SNMP_ADM: ERROR reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_FS_SPECIFIC);
          return err;
        }

      /* what parameter is it ? */

      if(!STRCMP(key_name, "Snmp_Agentx_Socket"))
        {
          strncpy(out_parameter->snmp_adm.snmp_agentx_socket, key_value, MAXPATHLEN);
        }
      else if(!STRCMP(key_name, "Product_Id"))
        {
          out_parameter->snmp_adm.product_id = atoi(key_value);
        }
      else if(!STRCMP(key_name, "Snmp_adm_log"))
        {
          strncpy(out_parameter->snmp_adm.snmp_log_file, key_value, MAXPATHLEN);
        }
      else if(!STRCMP(key_name, "Export_cache_stats"))
        {
          int bool = StrToBoolean(key_value);
          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "SNMP_ADM: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return EINVAL;
            }
          out_parameter->snmp_adm.export_cache_stats = bool;
        }
      else if(!STRCMP(key_name, "Export_requests_stats"))
        {
          int bool = StrToBoolean(key_value);
          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "SNMP_ADM: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return EINVAL;
            }
          out_parameter->snmp_adm.export_requests_stats = bool;
        }
      else if(!STRCMP(key_name, "Export_maps_stats"))
        {
          int bool = StrToBoolean(key_value);
          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "SNMP_ADM: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return EINVAL;
            }
          out_parameter->snmp_adm.export_maps_stats = bool;
        }
      else if(!STRCMP(key_name, "Export_buddy_stats"))
        {
          int bool = StrToBoolean(key_value);
          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "SNMP_ADM: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return EINVAL;
            }
          out_parameter->snmp_adm.export_buddy_stats = bool;
        }
      else if(!STRCMP(key_name, "Export_nfs_calls_detail"))
        {
          int bool = StrToBoolean(key_value);
          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "SNMP_ADM: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return EINVAL;
            }
          out_parameter->snmp_adm.export_nfs_calls_detail = bool;
        }
      else if(!STRCMP(key_name, "Export_cache_inode_calls_detail"))
        {
          int bool = StrToBoolean(key_value);
          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "SNMP_ADM: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return EINVAL;
            }
          out_parameter->snmp_adm.export_cache_inode_calls_detail = bool;
        }
      else if(!STRCMP(key_name, "Export_FSAL_calls_detail"))
        {
          int bool = StrToBoolean(key_value);
          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "SNMP_ADM: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              return EINVAL;
            }
          out_parameter->snmp_adm.export_fsal_calls_detail = bool;
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "SNMP_ADM LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_FS_SPECIFIC);
          return EINVAL;
        }
    }
  config_ok = 1;
  return 0;
}

/* SNMPADM Get Set*/

static int getuptime(snmp_adm_type_union * param, void *opt)
{
  param->time = time(NULL) - ServerBootTime;
  return 0;
}

static int get_inode_stat_nb(snmp_adm_type_union * param, void *opt)
{
  long cs = (long)opt;
  param->integer = 0;
  unsigned int i;

  switch (cs)
    {
    case 0:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].cache_inode_client.stat.nb_gc_lru_active;
      break;
    case 1:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].cache_inode_client.stat.nb_gc_lru_total;
      break;
    case 2:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].cache_inode_client.stat.nb_call_total;
      break;
    default:
      return 1;

    }
  return 0;
}

static int get_inode_stat_func_stat(snmp_adm_type_union * param, void *opt)
{
  long j = ((long)opt) / 4;
  long stat = ((long)opt) % 4;

  unsigned int i;

  param->integer = 0;

  switch (stat)
    {
    case 0:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer +=
            workers_data[i].cache_inode_client.stat.func_stats.nb_success[j];
      break;
    case 1:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].cache_inode_client.stat.func_stats.nb_call[j];
      break;
    case 2:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer +=
            workers_data[i].cache_inode_client.stat.func_stats.nb_err_retryable[j];
      break;
    case 3:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer +=
            workers_data[i].cache_inode_client.stat.func_stats.nb_err_unrecover[j];
      break;
    default:
      return 1;
    }
  return 0;
}

static int get_hash(snmp_adm_type_union * param, void *opt)
{
  hash_stat_t hstat, hstat_reverse;
  long cs = (long)opt;

  /* check mask, the structure contain 16 elements, mask is 0xF0 */
  if((cs & 0xF0) == 0x00)
    {
      /* Pinting the cache inode hash stat */
      /* This is done only on worker[0]: the hashtable is shared and worker 0 always exists */
      HashTable_GetStats(workers_data[0].ht, &hstat);
    }
  else if((cs & 0xF0) == 0x10)
    {
      nfs_dupreq_get_stats(&hstat);
    }
  else if((cs & 0xF0) == 0x20)
    {
      idmap_get_stats(UIDMAP_TYPE, &hstat, &hstat_reverse);
    }
  else if((cs & 0xF0) == 0x30)
    {
      idmap_get_stats(UIDMAP_TYPE, &hstat, &hstat_reverse);
      memcpy(&hstat, &hstat_reverse, sizeof(hash_stat_t));
    }
  else if((cs & 0xF0) == 0x40)
    {
      idmap_get_stats(GIDMAP_TYPE, &hstat, &hstat_reverse);
    }
  else if((cs & 0xF0) == 0x50)
    {
      idmap_get_stats(GIDMAP_TYPE, &hstat, &hstat_reverse);
      memcpy(&hstat, &hstat_reverse, sizeof(hash_stat_t));
    }
  else if((cs & 0xF0) == 0x60)
    {
      nfs_ip_name_get_stats(&hstat);
    }
  else
    {
      return 1;
    }
  /* position in the group */
  cs &= 0x0F;
  switch (cs)
    {
    case 0:
      param->integer = hstat.dynamic.nb_entries;
      break;
    case 1:
      param->integer = hstat.computed.min_rbt_num_node;
      break;
    case 2:
      param->integer = hstat.computed.max_rbt_num_node;
      break;
    case 3:
      param->integer = hstat.computed.average_rbt_num_node;
      break;
    case 4:
      param->integer = hstat.dynamic.ok.nb_set;
      break;
    case 5:
      param->integer = hstat.dynamic.notfound.nb_set;
      break;
    case 6:
      param->integer = hstat.dynamic.err.nb_set;
      break;
    case 7:
      param->integer = hstat.dynamic.ok.nb_test;
      break;
    case 8:
      param->integer = hstat.dynamic.notfound.nb_test;
      break;
    case 9:
      param->integer = hstat.dynamic.err.nb_test;
      break;
    case 10:
      param->integer = hstat.dynamic.ok.nb_get;
      break;
    case 11:
      param->integer = hstat.dynamic.notfound.nb_get;
      break;
    case 12:
      param->integer = hstat.dynamic.err.nb_get;
      break;
    case 13:
      param->integer = hstat.dynamic.ok.nb_del;
      break;
    case 14:
      param->integer = hstat.dynamic.notfound.nb_del;
      break;
    case 15:
      param->integer = hstat.dynamic.err.nb_del;
      break;
    default:
      return 1;
    }
  return 0;

}

static int get_workerstat(snmp_adm_type_union * param, void *opt)
{
  long cs = (long)opt;
  unsigned int i;

  param->integer = 0;

  switch (cs)
    {
    case 0:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.nb_total_req;
      break;
    case 1:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.nb_udp_req;
      break;
    case 2:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.nb_tcp_req;
      break;
    case 3:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.nb_mnt1_req;
      break;
    case 4:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.nb_mnt3_req;
      break;
    case 5:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.nb_nfs2_req;
      break;
    case 6:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.nb_nfs3_req;
      break;
    case 7:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.nb_nfs4_req;
      break;
    default:
      return 1;
    }
  return 0;
}

#define MIN_NOT_SET 0xFFFFFFFF

static int get_pending(snmp_adm_type_union * param, void *opt_arg)
{
  long cs = (long)opt_arg;
  unsigned int i;

  unsigned min_pending_request = MIN_NOT_SET;
  unsigned max_pending_request = 0;
  unsigned total_pending_request = 0;
  unsigned len_pending_request = 0;

  for(i = 0; i < nfs_param.core_param.nb_worker; i++)
    {
      len_pending_request =
          workers_data[i].pending_request->nb_entry -
          workers_data[i].pending_request->nb_invalid;

      if((len_pending_request < min_pending_request)
         || (min_pending_request == MIN_NOT_SET))
        min_pending_request = len_pending_request;

      if(len_pending_request > max_pending_request)
        max_pending_request = len_pending_request;

      total_pending_request += len_pending_request;
    }

  switch (cs)
    {
    case 0:
      param->integer = min_pending_request;
      break;
    case 1:
      param->integer = max_pending_request;
      break;
    case 2:
      param->integer = total_pending_request;
      break;
    case 3:
      param->integer = total_pending_request / nfs_param.core_param.nb_worker;
      break;
    default:
      return 1;
    }
  return 0;
}

static int get_mnt1(snmp_adm_type_union * param, void *opt_arg)
{
  long cmd = ((long)opt_arg) / 3;
  long stat = ((long)opt_arg) % 3;

  unsigned int i;

  param->integer = 0;

  switch (stat)
    {
    case 0:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_mnt1[cmd].total;
      break;
    case 1:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_mnt1[cmd].success;
      break;
    case 2:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_mnt1[cmd].dropped;
      break;
    default:
      return 1;
    }
  return 0;
}

static int get_mnt3(snmp_adm_type_union * param, void *opt_arg)
{
  long cmd = ((long)opt_arg) / 3;
  long stat = ((long)opt_arg) % 3;

  unsigned int i;

  param->integer = 0;

  switch (stat)
    {
    case 0:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_mnt3[cmd].total;
      break;
    case 1:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_mnt3[cmd].success;
      break;
    case 2:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_mnt3[cmd].dropped;
      break;
    default:
      return 1;
    }
  return 0;
}

static int get_nfs2(snmp_adm_type_union * param, void *opt_arg)
{
  long cmd = ((long)opt_arg) / 3;
  long stat = ((long)opt_arg) % 3;

  unsigned int i;

  param->integer = 0;

  switch (stat)
    {
    case 0:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_nfs2[cmd].total;
      break;
    case 1:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_nfs2[cmd].success;
      break;
    case 2:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_nfs2[cmd].dropped;
      break;
    default:
      return 1;
    }
  return 0;
}

static int get_nfs3(snmp_adm_type_union * param, void *opt_arg)
{
  long cmd = ((long)opt_arg) / 3;
  long stat = ((long)opt_arg) % 3;

  unsigned int i;

  param->integer = 0;

  switch (stat)
    {
    case 0:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_nfs3[cmd].total;
      break;
    case 1:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_nfs3[cmd].success;
      break;
    case 2:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_nfs3[cmd].dropped;
      break;
    default:
      return 1;
    }
  return 0;
}

static int get_nfs4(snmp_adm_type_union * param, void *opt_arg)
{
  long cmd = ((long)opt_arg) / 3;
  long stat = ((long)opt_arg) % 3;

  unsigned int i;

  param->integer = 0;

  switch (stat)
    {
    case 0:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_nfs4[cmd].total;
      break;
    case 1:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_nfs4[cmd].success;
      break;
    case 2:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.stat_req.stat_req_nfs4[cmd].dropped;
      break;
    default:
      return 1;
    }
  return 0;
}

static int get_fsal(snmp_adm_type_union * param, void *opt_arg)
{
  long cmd = ((long)opt_arg) / 4;
  long stat = ((long)opt_arg) % 4;

  unsigned int i;

  param->integer = 0;

  switch (stat)
    {
    case 0:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.fsal_stats.func_stats.nb_call[cmd];
      break;
    case 1:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer += workers_data[i].stats.fsal_stats.func_stats.nb_success[cmd];
      break;
    case 2:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer +=
            workers_data[i].stats.fsal_stats.func_stats.nb_err_retryable[cmd];
      break;
    case 3:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->integer +=
            workers_data[i].stats.fsal_stats.func_stats.nb_err_unrecover[cmd];
      break;
    default:
      return 1;
    }
  return 0;
}

#ifndef _NO_BUDDY_SYSTEM

static int get_buddy(snmp_adm_type_union * param, void *opt_arg)
{
  long cs = (long)opt_arg;
  unsigned int i;

  param->bigint = 0;
  switch (cs)
    {
    case 0:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->bigint += workers_data[i].stats.buddy_stats.TotalMemSpace;
      break;
    case 1:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->bigint += workers_data[i].stats.buddy_stats.StdMemSpace;
      break;
    case 2:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->bigint += workers_data[i].stats.buddy_stats.ExtraMemSpace;
      break;
    case 3:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->bigint += workers_data[i].stats.buddy_stats.StdUsedSpace;
      break;
    case 4:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->bigint += workers_data[i].stats.buddy_stats.StdUsedSpace;

      param->bigint /= nfs_param.core_param.nb_worker;

      break;
    case 5:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        if(workers_data[i].stats.buddy_stats.StdUsedSpace > param->bigint)
          param->bigint = workers_data[i].stats.buddy_stats.StdUsedSpace;
      break;
    case 6:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->bigint += workers_data[i].stats.buddy_stats.NbStdPages;
      break;
    case 7:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->bigint += workers_data[i].stats.buddy_stats.NbStdUsed;
      break;
    case 8:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        param->bigint += workers_data[i].stats.buddy_stats.NbStdUsed;

      param->bigint /= nfs_param.core_param.nb_worker;

      break;
    case 9:
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
        if(workers_data[i].stats.buddy_stats.NbStdUsed > param->bigint)
          param->bigint = workers_data[i].stats.buddy_stats.NbStdUsed;
      break;
    case 10:
    case 11:
      strcpy(param->string, "filename to dump to");
      break;
    default:
      return 1;
    }
  return 0;
}

#ifdef _DEBUG_MEMLEAKS

static int set_buddy(const snmp_adm_type_union * param, void *opt_arg)
{
  long cs = (long)opt_arg;

  switch (cs)
    {
    case 10:
      {
        int rc;
        FILE *output = fopen(param->string, "w");
        if (output == NULL)
          {
            LogCrit(COMPONENT_MEMLEAKS,
                    "Open of %s failed, error=%s(%d)",
                    param->string, strerror(errno), errno);
            return 1;
          }
        BuddyDumpAll(output);
        rc = fclose(output);
        LogEvent(COMPONENT_MEMLEAKS,
                 "Dumped buddy memory to %s, rc=%d", param->string, rc);
      }
      break;
    case 11:
      {
        int rc;
        FILE *output = fopen(param->string, "w");
        if (output == NULL)
          {
            LogCrit(COMPONENT_MEMLEAKS,
                    "Open of %s failed, error=%s(%d)",
                    param->string, strerror(errno), errno);
            return 1;
          }
        BuddyDumpPools(output);
        rc = fclose(output);
        LogEvent(COMPONENT_MEMLEAKS,
                 "Dumped buddy pools to %s, rc=%d", param->string, rc);
      }
      break;
    default:
      return 1;
    }
  return 0;
}

#endif

#endif

static register_get_set snmp_export_stat_general[] = {
  {"uptime", "Server uptime in sec", SNMP_ADM_TIMETICKS, SNMP_ADM_ACCESS_RO, getuptime,
   NULL, NULL}
};

#define SNMPADM_STAT_GENERAL_COUNT 1

static register_get_set snmp_export_stat_cache[] = {

  {"cache_nb_gc_lru_active", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_inode_stat_nb, NULL, (void *)0},
  {"cache_nb_gc_lru_total", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_inode_stat_nb, NULL, (void *)1},
  {"cache_nb_call_total", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_inode_stat_nb, NULL, (void *)2},

  {"cache_nb_entries", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x00},
  {"cache_min_rbt_num_node", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x01},
  {"cache_max_rbt_num_node", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x02},
  {"cache_avg_rbt_num_node", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x03},
  {"cache_nbset_ok", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x04},
  {"cache_nbset_notfound", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x05},
  {"cache_nbset_err", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x06},
  {"cache_nbtest_ok", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x07},
  {"cache_nbtest_notfound", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x08},
  {"cache_nbtest_err", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x09},
  {"cache_nbget_ok", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x0A},
  {"cache_nbget_notfound", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x0B},
  {"cache_nbget_err", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x0C},
  {"cache_nbdel_ok", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x0D},
  {"cache_nbdel_notfound", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x0E},
  {"cache_nbdel_err", "cache_inode", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x0F}
};

#define SNMPADM_STAT_CACHE_COUNT 19

static register_get_set snmp_export_stat_req[] = {

  {"workers_nb_total_req", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_workerstat, NULL, (void *)0},
  {"workers_nb_udp_req", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_workerstat, NULL, (void *)1},
  {"workers_nb_tcp_req", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_workerstat, NULL, (void *)2},
  {"workers_nb_mnt1_req", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_workerstat, NULL, (void *)3},
  {"workers_nb_mnt3_req", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_workerstat, NULL, (void *)4},
  {"workers_nb_nfs2_req", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_workerstat, NULL, (void *)5},
  {"workers_nb_nfs3_req", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_workerstat, NULL, (void *)6},
  {"workers_nb_nfs4_req", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_workerstat, NULL, (void *)7},

  {"min_pending_requests", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_pending, NULL, (void *)0},
  {"max_pending_requests", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_pending, NULL, (void *)1},
  {"total_pending_requests", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_pending, NULL, (void *)2},
  {"average_pending_requests", "NFS/MOUNT STATISTICS", SNMP_ADM_INTEGER,
   SNMP_ADM_ACCESS_RO, get_pending, NULL, (void *)3},

  {"dupreq_nb_entries", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x10},
  {"dupreq_min_rbt_num_node", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x11},
  {"dupreq_max_rbt_num_node", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x12},
  {"dupreq_avg_rvt_num_node", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x13},
  {"dupreq_nbset_ok", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x14},
  {"dupreq_nbset_notfound", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x15},
  {"dupreq_nbset_err", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x16},
  {"dupreq_nbtest_ok", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x17},
  {"dupreq_nbtest_notfound", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x18},
  {"dupreq_nbtest_err", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x19},
  {"dupreq_nbget_ok", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x1A},
  {"dupreq_nbget_notfound", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x1B},
  {"dupreq_nbget_err", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x1C},
  {"dupreq_nbdel_ok", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x1D},
  {"dupreq_nbdel_notfound", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x1E},
  {"dupreq_nbdel_err", "DUP_REQ_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x1F}
};

#define SNMPADM_STAT_REQ_COUNT 28

static register_get_set snmp_export_stat_maps[] = {

  {"uidmap_nb_entries", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x20},
  {"uidmap_min_rbt_num_node", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x21},
  {"uidmap_max_rbt_num_node", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x22},
  {"uidmap_avg_rvt_num_node", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x23},
  {"uidmap_nbset_ok", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x24},
  {"uidmap_nbset_notfound", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x25},
  {"uidmap_nbset_err", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x26},
  {"uidmap_nbtest_ok", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x27},
  {"uidmap_nbtest_notfound", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x28},
  {"uidmap_nbtest_err", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x29},
  {"uidmap_nbget_ok", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x2A},
  {"uidmap_nbget_notfound", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x2B},
  {"uidmap_nbget_err", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x2C},
  {"uidmap_nbdel_ok", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x2D},
  {"uidmap_nbdel_notfound", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x2E},
  {"uidmap_nbdel_err", "UIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x2F},

  {"unamemap_nb_entries", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x30},
  {"unamemap_min_rbt_num_node", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x31},
  {"unamemap_max_rbt_num_node", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x32},
  {"unamemap_avg_rvt_num_node", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x33},
  {"unamemap_nbset_ok", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x34},
  {"unamemap_nbset_notfound", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x35},
  {"unamemap_nbset_err", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x36},
  {"unamemap_nbtest_ok", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x37},
  {"unamemap_nbtest_notfound", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x38},
  {"unamemap_nbtest_err", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x39},
  {"unamemap_nbget_ok", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x3A},
  {"unamemap_nbget_notfound", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x3B},
  {"unamemap_nbget_err", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x3C},
  {"unamemap_nbdel_ok", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x3D},
  {"unamemap_nbdel_notfound", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x3E},
  {"unamemap_nbdel_err", "UNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x3F},

  {"gidmap_nb_entries", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x40},
  {"gidmap_min_rbt_num_node", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x41},
  {"gidmap_max_rbt_num_node", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x42},
  {"gidmap_avg_rvt_num_node", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x43},
  {"gidmap_nbset_ok", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x44},
  {"gidmap_nbset_notfound", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x45},
  {"gidmap_nbset_err", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x46},
  {"gidmap_nbtest_ok", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x47},
  {"gidmap_nbtest_notfound", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x48},
  {"gidmap_nbtest_err", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x49},
  {"gidmap_nbget_ok", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x4A},
  {"gidmap_nbget_notfound", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x4B},
  {"gidmap_nbget_err", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x4C},
  {"gidmap_nbdel_ok", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash, NULL,
   (void *)0x4D},
  {"gidmap_nbdel_notfound", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x4E},
  {"gidmap_nbdel_err", "GIDMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x4F},

  {"gnamemap_nb_entries", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x50},
  {"gnamemap_min_rbt_num_node", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x51},
  {"gnamemap_max_rbt_num_node", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x52},
  {"gnamemap_avg_rvt_num_node", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x53},
  {"gnamemap_nbset_ok", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x54},
  {"gnamemap_nbset_notfound", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x55},
  {"gnamemap_nbset_err", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x56},
  {"gnamemap_nbtest_ok", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x57},
  {"gnamemap_nbtest_notfound", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x58},
  {"gnamemap_nbtest_err", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x59},
  {"gnamemap_nbget_ok", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x5A},
  {"gnamemap_nbget_notfound", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x5B},
  {"gnamemap_nbget_err", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x5C},
  {"gnamemap_nbdel_ok", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x5D},
  {"gnamemap_nbdel_notfound", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x5E},
  {"gnamemap_nbdel_err", "GNAMEMAP_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x5F},

  {"ipname_nb_entries", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x60},
  {"ipname_min_rbt_num_node", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x61},
  {"ipname_max_rbt_num_node", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x62},
  {"ipname_avg_rvt_num_node", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x63},
  {"ipname_nbset_ok", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x64},
  {"ipname_nbset_notfound", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x65},
  {"ipname_nbset_err", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x66},
  {"ipname_nbtest_ok", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x67},
  {"ipname_nbtest_notfound", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x68},
  {"ipname_nbtest_err", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x69},
  {"ipname_nbget_ok", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x6A},
  {"ipname_nbget_notfound", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x6B},
  {"ipname_nbget_err", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x6C},
  {"ipname_nbdel_ok", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x6D},
  {"ipname_nbdel_notfound", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO,
   get_hash, NULL, (void *)0x6E},
  {"ipname_nbdel_err", "IP_NAME_HASH", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RO, get_hash,
   NULL, (void *)0x6F}

};

#define SNMPADM_STAT_MAPS_COUNT 80

#ifndef _NO_BUDDY_SYSTEM
static register_get_set snmp_export_stat_buddy[] = {
  {"buddy_total_mem_space", "BUDDY_MEMORY", SNMP_ADM_BIGINT, SNMP_ADM_ACCESS_RO,
   get_buddy, NULL, (void *)0},
  {"buddy_std_mem_space", "BUDDY_MEMORY", SNMP_ADM_BIGINT, SNMP_ADM_ACCESS_RO, get_buddy,
   NULL, (void *)1},
  {"buddy_extra_mem_space", "BUDDY_MEMORY", SNMP_ADM_BIGINT, SNMP_ADM_ACCESS_RO, get_buddy,
   NULL, (void *)2},
  {"buddy_std_used_space", "BUDDY_MEMORY", SNMP_ADM_BIGINT, SNMP_ADM_ACCESS_RO, get_buddy,
   NULL, (void *)3},
  {"buddy_std_used_space_thr_avg", "BUDDY_MEMORY", SNMP_ADM_BIGINT, SNMP_ADM_ACCESS_RO,
   get_buddy, NULL, (void *)4},
  {"buddy_std_used_space_thr_max", "BUDDY_MEMORY", SNMP_ADM_BIGINT, SNMP_ADM_ACCESS_RO,
   get_buddy, NULL, (void *)5},
  {"buddy_std_pages", "BUDDY_MEMORY", SNMP_ADM_BIGINT, SNMP_ADM_ACCESS_RO, get_buddy, NULL,
   (void *)6},
  {"buddy_std_used_pages", "BUDDY_MEMORY", SNMP_ADM_BIGINT, SNMP_ADM_ACCESS_RO, get_buddy,
   NULL, (void *)7},
  {"buddy_std_used_pages_thr_avg", "BUDDY_MEMORY", SNMP_ADM_BIGINT, SNMP_ADM_ACCESS_RO,
   get_buddy, NULL, (void *)8},
  {"buddy_std_used_pages_thr_max", "BUDDY_MEMORY", SNMP_ADM_BIGINT, SNMP_ADM_ACCESS_RO,
   get_buddy, NULL, (void *)9},
#ifdef _DEBUG_MEMLEAKS
  {"buddy_dump_to_file", "BUDDY_MEMORY", SNMP_ADM_STRING, SNMP_ADM_ACCESS_RW,
   get_buddy, set_buddy, (void *)10},
  {"buddy_dump_pools_to_file",  "BUDDY_MEMORY", SNMP_ADM_STRING, SNMP_ADM_ACCESS_RW,
   get_buddy, set_buddy, (void *)11},
#endif
};

#ifdef _DEBUG_MEMLEAKS
#define SNMPADM_STAT_BUDDY_COUNT 12
#else
#define SNMPADM_STAT_BUDDY_COUNT 10
#endif

#endif                          /* _NO_BUDDY_SYSTEM */

static void create_dyn_cache_stat(register_get_set ** p_dyn_gs, int *p_dyn_gs_count)
{
  long j;

  *p_dyn_gs_count = 4 * CACHE_INODE_NB_COMMAND;
  *p_dyn_gs =
      (register_get_set *) Mem_Alloc(4 * CACHE_INODE_NB_COMMAND *
                                     sizeof(register_get_set));

  for(j = 0; j < 4 * CACHE_INODE_NB_COMMAND; j += 4)
    {
      (*p_dyn_gs)[j + 0].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 0].label, 256, "%s_nb_success",
               cache_inode_function_names[j / 4]);
      (*p_dyn_gs)[j + 0].desc = "Number of success calls to inode cache for this command";
      (*p_dyn_gs)[j + 0].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 0].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 0].getter = get_inode_stat_func_stat;
      (*p_dyn_gs)[j + 0].setter = NULL;
      (*p_dyn_gs)[j + 0].opt_arg = (void *)(j + 0);

      (*p_dyn_gs)[j + 1].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 1].label, 256, "%s_nb_call",
               cache_inode_function_names[j / 4]);
      (*p_dyn_gs)[j + 1].desc = "Number of calls to inode cache for this command";
      (*p_dyn_gs)[j + 1].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 1].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 1].getter = get_inode_stat_func_stat;
      (*p_dyn_gs)[j + 1].setter = NULL;
      (*p_dyn_gs)[j + 1].opt_arg = (void *)(j + 1);

      (*p_dyn_gs)[j + 2].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 2].label, 256, "%s_nb_retryable",
               cache_inode_function_names[j / 4]);
      (*p_dyn_gs)[j + 2].desc =
          "Number of retryable calls to inode cache for this command";
      (*p_dyn_gs)[j + 2].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 2].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 2].getter = get_inode_stat_func_stat;
      (*p_dyn_gs)[j + 2].setter = NULL;
      (*p_dyn_gs)[j + 2].opt_arg = (void *)(j + 2);

      (*p_dyn_gs)[j + 3].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 3].label, 256, "%s_nb_unrecover",
               cache_inode_function_names[j / 4]);
      (*p_dyn_gs)[j + 3].desc =
          "Number of unrecover calls to inode cache for this command";
      (*p_dyn_gs)[j + 3].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 3].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 3].getter = get_inode_stat_func_stat;
      (*p_dyn_gs)[j + 3].setter = NULL;
      (*p_dyn_gs)[j + 3].opt_arg = (void *)(j + 3);
    }
}

static void create_dyn_mntv1_stat(register_get_set ** p_dyn_gs, int *p_dyn_gs_count)
{
  long j;

  *p_dyn_gs_count = 3 * MNT_V1_NB_COMMAND;
  *p_dyn_gs =
      (register_get_set *) Mem_Alloc(3 * MNT_V1_NB_COMMAND * sizeof(register_get_set));

  for(j = 0; j < 3 * MNT_V1_NB_COMMAND; j += 3)
    {
      (*p_dyn_gs)[j + 0].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 0].label, 256, "%sV1_total", mnt_function_names[j / 3]);
      (*p_dyn_gs)[j + 0].desc = "Number of mnt1 commands";
      (*p_dyn_gs)[j + 0].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 0].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 0].getter = get_mnt1;
      (*p_dyn_gs)[j + 0].setter = NULL;
      (*p_dyn_gs)[j + 0].opt_arg = (void *)(j + 0);

      (*p_dyn_gs)[j + 1].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 1].label, 256, "%sV1_success", mnt_function_names[j / 3]);
      (*p_dyn_gs)[j + 1].desc = "Number of success for this mnt1 command";
      (*p_dyn_gs)[j + 1].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 1].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 1].getter = get_mnt1;
      (*p_dyn_gs)[j + 1].setter = NULL;
      (*p_dyn_gs)[j + 1].opt_arg = (void *)(j + 1);

      (*p_dyn_gs)[j + 2].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 2].label, 256, "%sV1_dropped", mnt_function_names[j / 3]);
      (*p_dyn_gs)[j + 2].desc = "Number of drop for this mnt1 command";
      (*p_dyn_gs)[j + 2].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 2].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 2].getter = get_mnt1;
      (*p_dyn_gs)[j + 2].setter = NULL;
      (*p_dyn_gs)[j + 2].opt_arg = (void *)(j + 2);
    }
}

static void create_dyn_mntv3_stat(register_get_set ** p_dyn_gs, int *p_dyn_gs_count)
{
  long j;

  *p_dyn_gs_count = 3 * MNT_V3_NB_COMMAND;
  *p_dyn_gs =
      (register_get_set *) Mem_Alloc(3 * MNT_V3_NB_COMMAND * sizeof(register_get_set));

  for(j = 0; j < 3 * MNT_V3_NB_COMMAND; j += 3)
    {
      (*p_dyn_gs)[j + 0].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 0].label, 256, "%sV3_total", mnt_function_names[j / 3]);
      (*p_dyn_gs)[j + 0].desc = "Number of mnt3 commands";;
      (*p_dyn_gs)[j + 0].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 0].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 0].getter = get_mnt3;
      (*p_dyn_gs)[j + 0].setter = NULL;
      (*p_dyn_gs)[j + 0].opt_arg = (void *)(j + 0);

      (*p_dyn_gs)[j + 1].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 1].label, 256, "%sV3_success", mnt_function_names[j / 3]);
      (*p_dyn_gs)[j + 1].desc = "Number of success for this mnt3 command";
      (*p_dyn_gs)[j + 1].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 1].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 1].getter = get_mnt3;
      (*p_dyn_gs)[j + 1].setter = NULL;
      (*p_dyn_gs)[j + 1].opt_arg = (void *)(j + 1);

      (*p_dyn_gs)[j + 2].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 2].label, 256, "%sV3_dropped", mnt_function_names[j / 3]);
      (*p_dyn_gs)[j + 2].desc = "Number of drop for this mnt3 command";
      (*p_dyn_gs)[j + 2].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 2].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 2].getter = get_mnt3;
      (*p_dyn_gs)[j + 2].setter = NULL;
      (*p_dyn_gs)[j + 2].opt_arg = (void *)(j + 2);
    }
}

static void create_dyn_nfsv2_stat(register_get_set ** p_dyn_gs, int *p_dyn_gs_count)
{
  long j;

  *p_dyn_gs_count = 3 * NFS_V2_NB_COMMAND;
  *p_dyn_gs =
      (register_get_set *) Mem_Alloc(3 * NFS_V2_NB_COMMAND * sizeof(register_get_set));

  for(j = 0; j < 3 * NFS_V2_NB_COMMAND; j += 3)
    {
      (*p_dyn_gs)[j + 0].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 0].label, 256, "%s_total", nfsv2_function_names[j / 3]);
      (*p_dyn_gs)[j + 0].desc = "Number of nfs2 commands";
      (*p_dyn_gs)[j + 0].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 0].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 0].getter = get_nfs2;
      (*p_dyn_gs)[j + 0].setter = NULL;
      (*p_dyn_gs)[j + 0].opt_arg = (void *)(j + 0);

      (*p_dyn_gs)[j + 1].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 1].label, 256, "%s_success", nfsv2_function_names[j / 3]);
      (*p_dyn_gs)[j + 1].desc = "Number of success for this nfs2 command";
      (*p_dyn_gs)[j + 1].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 1].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 1].getter = get_nfs2;
      (*p_dyn_gs)[j + 1].setter = NULL;
      (*p_dyn_gs)[j + 1].opt_arg = (void *)(j + 1);

      (*p_dyn_gs)[j + 2].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 2].label, 256, "%s_dropped", nfsv2_function_names[j / 3]);
      (*p_dyn_gs)[j + 2].desc = "Number of drop for this nfsv2 command";
      (*p_dyn_gs)[j + 2].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 2].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 2].getter = get_nfs2;
      (*p_dyn_gs)[j + 2].setter = NULL;
      (*p_dyn_gs)[j + 2].opt_arg = (void *)(j + 2);
    }
}

static void create_dyn_nfsv3_stat(register_get_set ** p_dyn_gs, int *p_dyn_gs_count)
{
  long j;

  *p_dyn_gs_count = 3 * NFS_V3_NB_COMMAND;
  *p_dyn_gs =
      (register_get_set *) Mem_Alloc(3 * NFS_V3_NB_COMMAND * sizeof(register_get_set));

  for(j = 0; j < 3 * NFS_V3_NB_COMMAND; j += 3)
    {
      (*p_dyn_gs)[j + 0].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 0].label, 256, "%s_total", nfsv3_function_names[j / 3]);
      (*p_dyn_gs)[j + 0].desc = "Number of nfs3 commands";
      (*p_dyn_gs)[j + 0].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 0].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 0].getter = get_nfs3;
      (*p_dyn_gs)[j + 0].setter = NULL;
      (*p_dyn_gs)[j + 0].opt_arg = (void *)(j + 0);

      (*p_dyn_gs)[j + 1].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 1].label, 256, "%s_success", nfsv3_function_names[j / 3]);
      (*p_dyn_gs)[j + 1].desc = "Number of success for this nfsv3 command";
      (*p_dyn_gs)[j + 1].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 1].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 1].getter = get_nfs3;
      (*p_dyn_gs)[j + 1].setter = NULL;
      (*p_dyn_gs)[j + 1].opt_arg = (void *)(j + 1);

      (*p_dyn_gs)[j + 2].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 2].label, 256, "%s_dropped", nfsv3_function_names[j / 3]);
      (*p_dyn_gs)[j + 2].desc = "Number of drop for this nfsv3 command";
      (*p_dyn_gs)[j + 2].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 2].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 2].getter = get_nfs3;
      (*p_dyn_gs)[j + 2].setter = NULL;
      (*p_dyn_gs)[j + 2].opt_arg = (void *)(j + 2);
    }
}

static void create_dyn_nfsv4_stat(register_get_set ** p_dyn_gs, int *p_dyn_gs_count)
{
  long j;

  *p_dyn_gs_count = 3 * NFS_V4_NB_COMMAND;
  *p_dyn_gs =
      (register_get_set *) Mem_Alloc(3 * NFS_V4_NB_COMMAND * sizeof(register_get_set));

  for(j = 0; j < 3 * NFS_V4_NB_COMMAND; j += 3)
    {
      (*p_dyn_gs)[j + 0].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 0].label, 256, "%s_total", nfsv4_function_names[j / 3]);
      (*p_dyn_gs)[j + 0].desc = "Number of nfs4 commands";
      (*p_dyn_gs)[j + 0].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 0].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 0].getter = get_nfs4;
      (*p_dyn_gs)[j + 0].setter = NULL;
      (*p_dyn_gs)[j + 0].opt_arg = (void *)(j + 0);

      (*p_dyn_gs)[j + 1].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 1].label, 256, "%s_success", nfsv4_function_names[j / 3]);
      (*p_dyn_gs)[j + 1].desc = "Number of success for this nfsv4 command";
      (*p_dyn_gs)[j + 1].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 1].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 1].getter = get_nfs4;
      (*p_dyn_gs)[j + 1].setter = NULL;
      (*p_dyn_gs)[j + 1].opt_arg = (void *)(j + 1);

      (*p_dyn_gs)[j + 2].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 2].label, 256, "%s_dropped", nfsv4_function_names[j / 3]);
      (*p_dyn_gs)[j + 2].desc = "Number of drop for this nfsv4 command";
      (*p_dyn_gs)[j + 2].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 2].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 2].getter = get_nfs4;
      (*p_dyn_gs)[j + 2].setter = NULL;
      (*p_dyn_gs)[j + 2].opt_arg = (void *)(j + 2);
    }
}

static void create_dyn_fsal_stat(register_get_set ** p_dyn_gs, int *p_dyn_gs_count)
{
  long j;

  *p_dyn_gs_count = 4 * FSAL_NB_FUNC;
  *p_dyn_gs = (register_get_set *) Mem_Alloc(4 * FSAL_NB_FUNC * sizeof(register_get_set));

  for(j = 0; j < 4 * FSAL_NB_FUNC; j += 4)
    {
      (*p_dyn_gs)[j + 0].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 0].label, 256, "%s_nb_call", fsal_function_names[j / 4]);
      (*p_dyn_gs)[j + 0].desc = "Number of total calls to FSAL for this function";
      (*p_dyn_gs)[j + 0].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 0].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 0].getter = get_fsal;
      (*p_dyn_gs)[j + 0].setter = NULL;
      (*p_dyn_gs)[j + 0].opt_arg = (void *)(j + 0);

      (*p_dyn_gs)[j + 1].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 1].label, 256, "%s_nb_success",
               fsal_function_names[j / 4]);
      (*p_dyn_gs)[j + 1].desc = "Number of success calls to FSAL for this function";
      (*p_dyn_gs)[j + 1].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 1].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 1].getter = get_fsal;
      (*p_dyn_gs)[j + 1].setter = NULL;
      (*p_dyn_gs)[j + 1].opt_arg = (void *)(j + 1);

      (*p_dyn_gs)[j + 2].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 2].label, 256, "%s_nb_ret", fsal_function_names[j / 4]);
      (*p_dyn_gs)[j + 2].desc = "Number of retryable calls to FSAL for this function";
      (*p_dyn_gs)[j + 2].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 2].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 2].getter = get_fsal;
      (*p_dyn_gs)[j + 2].setter = NULL;
      (*p_dyn_gs)[j + 2].opt_arg = (void *)(j + 2);

      (*p_dyn_gs)[j + 3].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j + 3].label, 256, "%s_nb_unrec", fsal_function_names[j / 4]);
      (*p_dyn_gs)[j + 3].desc = "Number of unrecover calls to FSAL for this function";
      (*p_dyn_gs)[j + 3].type = SNMP_ADM_INTEGER;
      (*p_dyn_gs)[j + 3].access = SNMP_ADM_ACCESS_RO;
      (*p_dyn_gs)[j + 3].getter = get_fsal;
      (*p_dyn_gs)[j + 3].setter = NULL;
      (*p_dyn_gs)[j + 3].opt_arg = (void *)(j + 3);
    }
}

static void create_dyn_log_control(register_get_set ** p_dyn_gs, int *p_dyn_gs_count)
{
  long j;

  *p_dyn_gs_count = COMPONENT_COUNT;
  *p_dyn_gs = (register_get_set *) Mem_Alloc((COMPONENT_COUNT) * sizeof(register_get_set));

  for(j = 0; j < COMPONENT_COUNT; j ++)
    {
      (*p_dyn_gs)[j + 0].label = Mem_Alloc(256 * sizeof(char));
      snprintf((*p_dyn_gs)[j].label, 256, "%s", LogComponents[j].comp_name);
      (*p_dyn_gs)[j].desc = "Log level for this component";
      (*p_dyn_gs)[j].type = SNMP_ADM_STRING;
      (*p_dyn_gs)[j].access = SNMP_ADM_ACCESS_RW;
      (*p_dyn_gs)[j].getter = getComponentLogLevel;
      (*p_dyn_gs)[j].setter = setComponentLogLevel;
      (*p_dyn_gs)[j].opt_arg = (void *)j;
    }
}

static void free_dyn(register_get_set * dyn, int count)
{
  int i;
  for(i = 0; i < count; i++)
    Mem_Free(dyn[i].label);
  Mem_Free(dyn);
}

/**
 * Start snmp thread.
 * @return 0 on success.
 */
int stats_snmp(nfs_worker_data_t * workers_data_local)
{
  int rc = 0;

  workers_data = workers_data_local;

  register_get_set *dyn_gs;
  int dyn_gs_count;

  SetNameFunction("stat_snmp");

  if(!config_ok)
    {
      LogCrit(COMPONENT_INIT,
              "Loading configuration has failed, SNMP_ADM is not activated");
      return 1;
    }

  /* set SNMP admin library's configuration  */
  if((rc = snmp_adm_config_daemon(nfs_param.extern_param.snmp_adm.snmp_agentx_socket,
                                  nfs_param.extern_param.snmp_adm.snmp_log_file,
                                  nfs_param.extern_param.snmp_adm.product_id)))
    {
      LogCrit(COMPONENT_INIT,
              "Error setting SNMP admin interface configuration");
      return 1;
    }

  /* always register general statistics */
  if((rc =
      snmp_adm_register_get_set_function(STAT_OID, snmp_export_stat_general,
                                         SNMPADM_STAT_GENERAL_COUNT)))
    {
      LogCrit(COMPONENT_INIT,
              "Error registering statistic variables to SNMP");
      return 2;
    }

  if(nfs_param.extern_param.snmp_adm.export_cache_stats)
    {
      if((rc =
          snmp_adm_register_get_set_function(STAT_OID, snmp_export_stat_cache,
                                             SNMPADM_STAT_CACHE_COUNT)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering statistic variables to SNMP");
          return 2;
        }
    }

  if(nfs_param.extern_param.snmp_adm.export_requests_stats)
    {
      if((rc =
          snmp_adm_register_get_set_function(STAT_OID, snmp_export_stat_req,
                                             SNMPADM_STAT_REQ_COUNT)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering statistic variables to SNMP");
          return 2;
        }
    }

  if(nfs_param.extern_param.snmp_adm.export_maps_stats)
    {
      if((rc =
          snmp_adm_register_get_set_function(STAT_OID, snmp_export_stat_maps,
                                             SNMPADM_STAT_MAPS_COUNT)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering statistic variables to SNMP");
          return 2;
        }
    }
#ifndef _NO_BUDDY_SYSTEM
  if(nfs_param.extern_param.snmp_adm.export_buddy_stats)
    {
      if((rc =
          snmp_adm_register_get_set_function(STAT_OID, snmp_export_stat_buddy,
                                             SNMPADM_STAT_BUDDY_COUNT)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering statistic variables to SNMP");
          return 2;
        }
    }
#endif                          /* _NO_BUDDY_SYSTEM */

  if(nfs_param.extern_param.snmp_adm.export_cache_inode_calls_detail)
    {
      create_dyn_cache_stat(&dyn_gs, &dyn_gs_count);

      if((rc = snmp_adm_register_get_set_function(STAT_OID, dyn_gs, dyn_gs_count)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering dynamic cache statistic variables to SNMP");
          return 2;
        }
      free_dyn(dyn_gs, dyn_gs_count);
    }

  if(nfs_param.extern_param.snmp_adm.export_nfs_calls_detail)
    {
      create_dyn_mntv1_stat(&dyn_gs, &dyn_gs_count);

      if((rc = snmp_adm_register_get_set_function(STAT_OID, dyn_gs, dyn_gs_count)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering mntv1 statistic variables to SNMP");
          return 2;
        }

      free_dyn(dyn_gs, dyn_gs_count);

      create_dyn_mntv3_stat(&dyn_gs, &dyn_gs_count);

      if((rc = snmp_adm_register_get_set_function(STAT_OID, dyn_gs, dyn_gs_count)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering mntv3 statistic variables to SNMP");
          return 2;
        }

      free_dyn(dyn_gs, dyn_gs_count);

      create_dyn_nfsv2_stat(&dyn_gs, &dyn_gs_count);

      if((rc = snmp_adm_register_get_set_function(STAT_OID, dyn_gs, dyn_gs_count)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering nfsv2 statistic variables to SNMP");
          return 2;
        }

      free_dyn(dyn_gs, dyn_gs_count);

      create_dyn_nfsv3_stat(&dyn_gs, &dyn_gs_count);

      if((rc = snmp_adm_register_get_set_function(STAT_OID, dyn_gs, dyn_gs_count)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering nfsv3 statistic variables to SNMP");
          return 2;
        }

      free_dyn(dyn_gs, dyn_gs_count);

      create_dyn_nfsv4_stat(&dyn_gs, &dyn_gs_count);

      if((rc = snmp_adm_register_get_set_function(STAT_OID, dyn_gs, dyn_gs_count)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering nfsv4 statistic variables to SNMP");
          return 2;
        }

      free_dyn(dyn_gs, dyn_gs_count);
    }

  if(nfs_param.extern_param.snmp_adm.export_fsal_calls_detail)
    {
      create_dyn_fsal_stat(&dyn_gs, &dyn_gs_count);

      if((rc = snmp_adm_register_get_set_function(STAT_OID, dyn_gs, dyn_gs_count)))
        {
          LogCrit(COMPONENT_INIT,
                  "Error registering nfsv4 statistic variables to SNMP");
          return 2;
        }

      free_dyn(dyn_gs, dyn_gs_count);
    }

  /*
   * Set up logging snmp adm control
   */

  /* always register general logging variables */
  create_dyn_log_control(&dyn_gs, &dyn_gs_count);

  if((rc = snmp_adm_register_get_set_function(LOG_OID, dyn_gs, dyn_gs_count)))
    {
      LogCrit(COMPONENT_INIT,
              "Error registering logging component variables to SNMP");
      return 2;
    }

  free_dyn(dyn_gs, dyn_gs_count);

#ifdef _ERROR_INJECTION
  rc = init_error_injector();
  if(rc != 0)
    return rc;
#endif

  /* finally, start the admin thread */
  if((rc = snmp_adm_start()))
    {
      LogCrit(COMPONENT_INIT,
              "Error starting SNMP administration service");
      return 3;
    }

  return 0;

}

      /* Now managed IP stats dump */
/* FIXME 
      nfs_ip_stats_dump(  ht_ip_stats, 
                          nfs_param.core_param.nb_worker, 
                          nfs_param.core_param.stats_per_client_directory  ) ;
*/
