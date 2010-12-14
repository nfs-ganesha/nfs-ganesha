#ifndef _EXTERNAL_TOOLS_H
#define _EXTERNAL_TOOLS_H

#include "nfs_exports.h"

#define MAXPORTLEN 15

typedef struct snmp_adm_parameter__
{
  char snmp_agentx_socket[MAXPATHLEN];
  int product_id;
  char snmp_log_file[MAXPATHLEN];

  int export_cache_stats;
  int export_requests_stats;
  int export_maps_stats;
  int export_buddy_stats;

  int export_nfs_calls_detail;
  int export_cache_inode_calls_detail;
  int export_fsal_calls_detail;


} snmp_adm_parameter_t;

typedef struct stat_exporter_parameter__
{
  char export_stat_port[MAXPORTLEN];
  exportlist_client_t allowed_clients;
} stat_exporter_parameter_t;

typedef struct external_tools_parameter__
{
  snmp_adm_parameter_t snmp_adm;
  stat_exporter_parameter_t stat_export;
} external_tools_parameter_t;

int get_snmpadm_conf(config_file_t in_config, external_tools_parameter_t * out_parameter);

#endif
