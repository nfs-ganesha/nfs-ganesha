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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    nfs_init.c
 * \brief   The file that contain most of the init routines
 *
 * The file that contain most of the init routines.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "ganesha_rpc.h"
#include "nfs_init.h"
#include "log.h"
#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nlm4.h"
#include "rquota.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "err_cache_inode.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"
#include "SemN.h"
#include "external_tools.h"
#include "nfs4_acls.h"
#include "nfs_rpc_callback.h"
#ifdef USE_DBUS
#include "ganesha_dbus.h"
#endif
#ifdef _USE_CB_SIMULATOR
#include "nfs_rpc_callback_simulator.h"
#endif
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#ifdef _USE_NLM
#include "nlm_util.h"
#include "nsm.h"
#endif
#include "sal_functions.h"
#include "nfs_tcb.h"
#include "nfs_tcb.h"

/* global information exported to all layers (as extern vars) */

/* nfs_parameter_t      nfs_param = {0}; */
nfs_parameter_t nfs_param;

/* ServerEpoch is ServerBootTime unless overriden by -E command line option */
time_t ServerBootTime;
time_t ServerEpoch;

nfs_worker_data_t *workers_data = NULL;
hash_table_t *fh_to_cache_entry_ht = NULL; /* Cache inode handle lookup table */
verifier4 NFS4_write_verifier;  /* NFS V4 write verifier */
writeverf3 NFS3_write_verifier; /* NFS V3 write verifier */

/* node ID used to identify an individual node in a cluster */
int g_nodeid = 0;

hash_table_t *ht_ip_stats[NB_MAX_WORKER_THREAD];
nfs_start_info_t nfs_start_info;

pthread_t worker_thrid[NB_MAX_WORKER_THREAD];

pthread_t flusher_thrid[NB_MAX_FLUSHER_THREAD];
nfs_flush_thread_data_t flush_info[NB_MAX_FLUSHER_THREAD];

pthread_t stat_thrid;
pthread_t stat_exporter_thrid;
pthread_t admin_thrid;
pthread_t sigmgr_thrid;
pthread_t reaper_thrid;
#ifdef SONAS
pthread_t recovery_thrid;
#endif
pthread_t gsh_dbus_thrid;
pthread_t upp_thrid;
nfs_tcb_t gccb;

#ifdef _USE_9P
pthread_t _9p_dispatcher_thrid;
#endif

#ifdef _USE_UPCALL_SIMULATOR
pthread_t upcall_simulator_thrid;
#endif

#ifdef _USE_NFSIDMAP
pthread_mutex_t idmap_conf_mtx;
#endif

char config_path[MAXPATHLEN];

char pidfile_path[MAXPATHLEN] ;

extern char v4_old_dir[PATH_MAX];

/**
 *
 * This thread is in charge of signal management 
 *
 * @param (unused)
 * @return (never returns : never ending loop)
 *
 */
void *sigmgr_thread( void * UnusedArg )
{
  SetNameFunction("sigmgr");
  int signal_caught = 0;
  fsal_status_t st;

  /* Loop until we catch SIGTERM */
  while(signal_caught != SIGTERM)
    {
      sigset_t signals_to_catch;
      sigemptyset(&signals_to_catch);
      sigaddset(&signals_to_catch, SIGTERM);
      sigaddset(&signals_to_catch, SIGHUP);
      sigaddset(&signals_to_catch, SIGUSR1);
      if(sigwait(&signals_to_catch, &signal_caught) != 0)
        {
          LogFullDebug(COMPONENT_THREAD,
                       "sigwait exited with error");
          continue;
        }
      if(signal_caught == SIGUSR1)
        {
          LogEvent(COMPONENT_MAIN,
                   "SIGUSR1_HANDLER: Received SIGUSR1... starting Grace on node - %d", g_nodeid);
          nfs4_start_grace(NULL);
        }
      if(signal_caught == SIGHUP)
        {
          LogEvent(COMPONENT_MAIN,
                   "SIGHUP_HANDLER: Received SIGHUP.... initiating export list reload");
          admin_replace_exports();
          reread_log_config();
        }
    }

  LogEvent(COMPONENT_MAIN, "NFS EXIT: stopping NFS service");
  LogDebug(COMPONENT_THREAD, "Stopping worker threads");

  if(pause_threads(PAUSE_SHUTDOWN) != PAUSE_EXIT)
    LogDebug(COMPONENT_THREAD,
             "Unexpected return code from pause_threads");
  else
    LogDebug(COMPONENT_THREAD,
             "Done waiting for worker threads to exit");

  LogEvent(COMPONENT_MAIN, "NFS EXIT: synchonizing FSAL");

  st = FSAL_terminate();

  if(FSAL_IS_ERROR(st))
    LogCrit(COMPONENT_MAIN, "NFS EXIT: ERROR %d.%d while synchonizing FSAL",
            st.major, st.minor);

  LogDebug(COMPONENT_THREAD, "sigmgr thread exiting");

  /* Remove pid file. I do not check for status (best effort, 
   * the daemon is stopping anyway */
  unlink( pidfile_path ) ;

  /* Might as well exit - no need for this thread any more */
  return NULL;
}                    /* sigmgr_thread */

/**
 * nfs_prereq_init:
 * Initialize NFSd prerequisites: memory management, logging, ...
 */
void nfs_prereq_init(char *program_name, char *host_name)
{
  /* Initialize logging */
  SetNamePgm(program_name);
  SetNameFunction("main");
  SetNameHost(host_name);

  InitLogging();

  /* Register error families */
  AddFamilyError(ERR_POSIX, "POSIX Errors", tab_systeme_status);
  AddFamilyError(ERR_HASHTABLE, "HashTable related Errors", tab_errctx_hash);
  AddFamilyError(ERR_FSAL, "FSAL related Errors", tab_errstatus_FSAL);
  AddFamilyError(ERR_CACHE_INODE, "Cache Inode related Errors",
                 tab_errstatus_cache_inode);
}

/**
 * nfs_print_param_config
 * print a nfs_parameter_structure under the format of the configuration file 
 */
void nfs_print_param_config()
{
  printf("NFS_Core_Param\n{\n");

  printf("\tNFS_Port = %u ;\n", nfs_param.core_param.port[P_NFS]);
  printf("\tMNT_Port = %u ;\n", nfs_param.core_param.port[P_MNT]);
  printf("\tNFS_Program = %u ;\n", nfs_param.core_param.program[P_NFS]);
  printf("\tMNT_Program = %u ;\n", nfs_param.core_param.program[P_NFS]);
  printf("\tNb_Worker = %u ; \n", nfs_param.core_param.nb_worker);
  printf("\tb_Call_Before_Queue_Avg = %u ; \n", nfs_param.core_param.nb_call_before_queue_avg);
 printf("\tDRC_TCP_Npart = %u ; \n", nfs_param.core_param.drc.tcp.npart);
 printf("\tDRC_TCP_Size = %u ; \n", nfs_param.core_param.drc.tcp.size);
 printf("\tDRC_TCP_Cachesz = %u ; \n", nfs_param.core_param.drc.tcp.cachesz);
 printf("\tDRC_TCP_Hiwat = %u ; \n", nfs_param.core_param.drc.tcp.hiwat);
 printf("\tDRC_TCP_Recycle_Npart = %u ; \n",
        nfs_param.core_param.drc.tcp.recycle_npart);
 printf("\tDRC_TCP_Recycle_Expire_S = %u ; \n",
        nfs_param.core_param.drc.tcp.recycle_expire_s);
 printf("\tDRC_TCP_Checksum = %u ; \n",
        nfs_param.core_param.drc.tcp.checksum);
 printf("\tDRC_UDP_Npart = %u ; \n", nfs_param.core_param.drc.udp.npart);
 printf("\tDRC_UDP_Size = %u ; \n", nfs_param.core_param.drc.udp.size);
 printf("\tDRC_UDP_Cachesz = %u ; \n", nfs_param.core_param.drc.udp.cachesz);
 printf("\tDRC_UDP_Hiwat = %u ; \n", nfs_param.core_param.drc.udp.hiwat);
 printf("\tDRC_UDP_Checksum = %u ; \n",
        nfs_param.core_param.drc.udp.checksum);
  printf("\tCore_Dump_Size = %ld ; \n", nfs_param.core_param.core_dump_size);
  printf("\tNb_Max_Fd = %d ; \n", nfs_param.core_param.nb_max_fd);
  printf("\tStats_File_Path = %s ; \n", nfs_param.core_param.stats_file_path);
  printf("\tStats_Update_Delay = %d ; \n", nfs_param.core_param.stats_update_delay);
  printf("\tLong_Processing_Threshold = %d ; \n", nfs_param.core_param.long_processing_threshold);
  printf("\tDecoder_Fridge_Expiration_Delay = %d ; \n", nfs_param.core_param.decoder_fridge_expiration_delay);
  printf("\tStats_Per_Client_Directory = %s ; \n",
         nfs_param.core_param.stats_per_client_directory);

  if(nfs_param.core_param.dump_stats_per_client)
    printf("\tDump_Stats_Per_Client = TRUE ; \n");
  else
    printf("\tDump_Stats_Per_Client = FALSE ;\n");

  if(nfs_param.core_param.drop_io_errors)
    printf("\tDrop_IO_Errors = TRUE ; \n");
  else
    printf("\tDrop_IO_Errors = FALSE ;\n");

  if(nfs_param.core_param.drop_inval_errors)
    printf("\tDrop_Inval_Errors = TRUE ; \n");
  else
    printf("\tDrop_Inval_Errors = FALSE ;\n");

  if(nfs_param.core_param.drop_delay_errors)
    printf("\tDrop_Delay_Errors = TRUE ; \n");
  else
    printf("\tDrop_Delay_Errors = FALSE ;\n");

  printf("}\n\n");

  printf("NFS_Worker_Param\n{\n");
  printf("}\n\n");
}                               /* nfs_print_param_config */

/**
 * nfs_set_param_default:
 * Set p_nfs_param structure to default parameters.
 */
void nfs_set_param_default()
{
  /* Core parameters */
  nfs_param.core_param.nb_worker = NB_WORKER_THREAD_DEFAULT;
  nfs_param.core_param.nb_call_before_queue_avg = NB_REQUEST_BEFORE_QUEUE_AVG;
  nfs_param.core_param.drc.tcp.npart = DRC_TCP_NPART;
  nfs_param.core_param.drc.tcp.size = DRC_TCP_SIZE;
  nfs_param.core_param.drc.tcp.cachesz = DRC_TCP_CACHESZ;
  nfs_param.core_param.drc.tcp.hiwat = DRC_TCP_HIWAT;
  nfs_param.core_param.drc.tcp.recycle_npart = DRC_TCP_RECYCLE_NPART;
  nfs_param.core_param.drc.tcp.checksum = DRC_TCP_CHECKSUM;
  nfs_param.core_param.drc.udp.npart = DRC_UDP_NPART;
  nfs_param.core_param.drc.udp.size = DRC_UDP_SIZE;
  nfs_param.core_param.drc.udp.cachesz = DRC_UDP_CACHESZ;
  nfs_param.core_param.drc.udp.hiwat = DRC_UDP_HIWAT;
  nfs_param.core_param.drc.udp.checksum = DRC_UDP_CHECKSUM;
  nfs_param.core_param.port[P_NFS] = NFS_PORT;
  nfs_param.core_param.port[P_MNT] = 0;
  nfs_param.core_param.bind_addr.sin_family = AF_INET;       /* IPv4 only right now */
  nfs_param.core_param.bind_addr.sin_addr.s_addr = INADDR_ANY;       /* All the interfaces on the machine are used */
  nfs_param.core_param.bind_addr.sin_port = 0;       /* No port specified */
  nfs_param.core_param.program[P_NFS] = NFS_PROGRAM;
  nfs_param.core_param.program[P_MNT] = MOUNTPROG;
#ifdef _USE_NLM
  nfs_param.core_param.program[P_NLM] = NLMPROG;
  nfs_param.core_param.port[P_NLM] = 0;
#endif
#ifdef _USE_9P
  nfs_param._9p_param._9p_port = _9P_PORT ;
#endif
#ifdef _USE_RQUOTA
  nfs_param.core_param.program[P_RQUOTA] = RQUOTAPROG;
  nfs_param.core_param.port[P_RQUOTA] = RQUOTA_PORT;
#endif
  nfs_param.core_param.drop_io_errors = TRUE;
  nfs_param.core_param.drop_inval_errors = FALSE;
  nfs_param.core_param.drop_delay_errors = TRUE;
  nfs_param.core_param.core_dump_size = -1;
  nfs_param.core_param.nb_max_fd = 1024;
  nfs_param.core_param.stats_update_delay = 60;
  nfs_param.core_param.long_processing_threshold = 10; /* seconds */
  nfs_param.core_param.long_processing_threshold_msec = 10 * MSEC_PER_SEC; /* miliseconds */
  nfs_param.core_param.decoder_fridge_expiration_delay = -1;
/* only NFSv4 is supported for the FSAL_PROXY */
#if ! defined( _USE_PROXY ) || defined ( _HANDLE_MAPPING )
  nfs_param.core_param.core_options = CORE_OPTION_NFSV3 | CORE_OPTION_NFSV4;
#else
  nfs_param.core_param.core_options = CORE_OPTION_NFSV4;
#endif                          /* _USE_PROXY */

  nfs_param.core_param.use_nfs_commit = FALSE;
  if(strmaxcpy(nfs_param.core_param.stats_file_path,
               "/tmp/ganesha.stat",
               sizeof(nfs_param.core_param.stats_file_path)) == -1)
    LogFatal(COMPONENT_INIT,
             "default stats file path is too long");
  nfs_param.core_param.dump_stats_per_client = 0;
  if(strmaxcpy(nfs_param.core_param.stats_per_client_directory,
               "/tmp",
               sizeof(nfs_param.core_param.stats_per_client_directory)) == -1)
    LogFatal(COMPONENT_INIT,
             "default stats per client path is too long");
  nfs_param.core_param.max_send_buffer_size = NFS_DEFAULT_SEND_BUFFER_SIZE;
  nfs_param.core_param.max_recv_buffer_size = NFS_DEFAULT_RECV_BUFFER_SIZE;

#ifdef _USE_NLM
  nfs_param.core_param.nsm_use_caller_name = FALSE;
#endif

  nfs_param.core_param.clustered = FALSE;

  /* Dispatch quotas */
  nfs_param.core_param.dispatch_max_reqs =  5000;
  nfs_param.core_param.dispatch_max_reqs_xprt =  512;

#ifdef _HAVE_GSSAPI
  /* krb5 parameter */
  if(strmaxcpy(nfs_param.krb5_param.svc.principal,
               DEFAULT_NFS_PRINCIPAL,
               sizeof(nfs_param.krb5_param.svc.principal)) == -1)
    LogFatal(COMPONENT_INIT,
             "default kerberos principal too long");
  if(strmaxcpy(nfs_param.krb5_param.keytab,
               DEFAULT_NFS_KEYTAB,
               sizeof(nfs_param.krb5_param.keytab)) == -1)
    LogFatal(COMPONENT_INIT,
             "default kerberos keytab too long");
  if(strmaxcpy(nfs_param.krb5_param.ccache_dir,
               DEFAULT_NFS_CCACHE_DIR,
               sizeof(nfs_param.krb5_param.ccache_dir)) == -1)
    LogFatal(COMPONENT_INIT,
             "default kerberos ccache_dir too long");
  nfs_param.krb5_param.active_krb5 = TRUE;
#endif

  /* NFSv4 parameter */
  nfs_param.nfsv4_param.lease_lifetime = NFS4_LEASE_LIFETIME;
  nfs_param.nfsv4_param.fh_expire = FALSE;
  nfs_param.nfsv4_param.returns_err_fh_expired = TRUE;
  nfs_param.nfsv4_param.return_bad_stateid = TRUE;
  if(strmaxcpy(nfs_param.nfsv4_param.domainname,
               DEFAULT_DOMAIN,
               sizeof(nfs_param.nfsv4_param.domainname)) == -1)
    LogFatal(COMPONENT_INIT,
             "DEFAULT_DOMAIN is too long");
  if(strmaxcpy(nfs_param.nfsv4_param.idmapconf,
               DEFAULT_IDMAPCONF,
               sizeof(nfs_param.nfsv4_param.idmapconf)) == -1)
    LogFatal(COMPONENT_INIT,
             "DEFAULT_IDMAPCONF is too long");

  /*  Worker parameters : IP/name hash table */
  nfs_param.ip_name_param.hash_param.index_size = PRIME_IP_NAME;
  nfs_param.ip_name_param.hash_param.alphabet_length = 10;   /* ipaddr is a numerical decimal value */
  nfs_param.ip_name_param.hash_param.hash_func_key = ip_name_value_hash_func;
  nfs_param.ip_name_param.hash_param.hash_func_rbt = ip_name_rbt_hash_func;
  nfs_param.ip_name_param.hash_param.compare_key = compare_ip_name;
  nfs_param.ip_name_param.hash_param.key_to_str = display_ip_name_key;
  nfs_param.ip_name_param.hash_param.val_to_str = display_ip_name_val;
  nfs_param.ip_name_param.hash_param.ht_name = "IP Name";
  nfs_param.ip_name_param.hash_param.flags = HT_FLAG_NONE;
  nfs_param.ip_name_param.hash_param.ht_log_component = COMPONENT_DISPATCH;
  nfs_param.ip_name_param.expiration_time = IP_NAME_EXPIRATION;
  nfs_param.ip_name_param.mapfile[0] = '\0';

  /*  Worker parameters : UID_MAPPER hash table */
  nfs_param.uidmap_cache_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.uidmap_cache_param.hash_param.alphabet_length = 10;      /* Not used for UID_MAPPER */
  nfs_param.uidmap_cache_param.hash_param.hash_func_key = name_value_hash_func;
  nfs_param.uidmap_cache_param.hash_param.hash_func_rbt = name_rbt_hash_func;
  nfs_param.uidmap_cache_param.hash_param.compare_key = compare_name;
  nfs_param.uidmap_cache_param.hash_param.key_to_str = display_idmapper_name;
  nfs_param.uidmap_cache_param.hash_param.val_to_str = display_idmapper_id;
  nfs_param.uidmap_cache_param.hash_param.ht_name = "UID Map Cache";
  nfs_param.uidmap_cache_param.hash_param.flags = HT_FLAG_NONE;
  nfs_param.uidmap_cache_param.hash_param.ht_log_component = COMPONENT_IDMAPPER;
  nfs_param.uidmap_cache_param.mapfile[0] = '\0';

  /*  Worker parameters : UNAME_MAPPER hash table */
  nfs_param.unamemap_cache_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.unamemap_cache_param.hash_param.alphabet_length = 10;    /* Not used for UID_MAPPER */
  nfs_param.unamemap_cache_param.hash_param.hash_func_key = id_value_hash_func;
  nfs_param.unamemap_cache_param.hash_param.hash_func_rbt = id_rbt_hash_func;
  nfs_param.unamemap_cache_param.hash_param.compare_key = compare_id;
  nfs_param.unamemap_cache_param.hash_param.key_to_str = display_idmapper_id;
  nfs_param.unamemap_cache_param.hash_param.val_to_str = display_idmapper_name;
  nfs_param.unamemap_cache_param.hash_param.ht_name = "UNAME Map Cache";
  nfs_param.unamemap_cache_param.hash_param.flags = HT_FLAG_NONE;
  nfs_param.unamemap_cache_param.hash_param.ht_log_component = COMPONENT_IDMAPPER;
  nfs_param.unamemap_cache_param.mapfile[0] = '\0';

  /*  Worker parameters : GID_MAPPER hash table */
  nfs_param.gidmap_cache_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.gidmap_cache_param.hash_param.alphabet_length = 10;      /* Not used for UID_MAPPER */
  nfs_param.gidmap_cache_param.hash_param.hash_func_key = name_value_hash_func;
  nfs_param.gidmap_cache_param.hash_param.hash_func_rbt = name_rbt_hash_func;
  nfs_param.gidmap_cache_param.hash_param.compare_key = compare_name;
  nfs_param.gidmap_cache_param.hash_param.key_to_str = display_idmapper_name;
  nfs_param.gidmap_cache_param.hash_param.val_to_str = display_idmapper_id;
  nfs_param.gidmap_cache_param.hash_param.ht_name = "GID Map Cache";
  nfs_param.gidmap_cache_param.hash_param.flags = HT_FLAG_NONE;
  nfs_param.gidmap_cache_param.hash_param.ht_log_component = COMPONENT_IDMAPPER;
  nfs_param.gidmap_cache_param.mapfile[0] = '\0';

  /*  Worker parameters : UID->GID  hash table (for RPCSEC_GSS) */
  nfs_param.uidgidmap_cache_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.uidgidmap_cache_param.hash_param.alphabet_length = 10;   /* Not used for UID_MAPPER */
  nfs_param.uidgidmap_cache_param.hash_param.hash_func_key = id_value_hash_func;
  nfs_param.uidgidmap_cache_param.hash_param.hash_func_rbt = id_rbt_hash_func;
  nfs_param.uidgidmap_cache_param.hash_param.compare_key = compare_id;
  nfs_param.uidgidmap_cache_param.hash_param.key_to_str = display_idmapper_id;
  nfs_param.uidgidmap_cache_param.hash_param.val_to_str = display_idmapper_id;
  nfs_param.uidgidmap_cache_param.hash_param.ht_name = "UID->GID Map Cache";
  nfs_param.uidgidmap_cache_param.hash_param.flags = HT_FLAG_NONE;
  nfs_param.uidgidmap_cache_param.hash_param.ht_log_component = COMPONENT_IDMAPPER;

  /*  Worker parameters : GNAME_MAPPER hash table */
  nfs_param.gnamemap_cache_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.gnamemap_cache_param.hash_param.alphabet_length = 10;    /* Not used for UID_MAPPER */
  nfs_param.gnamemap_cache_param.hash_param.hash_func_key = id_value_hash_func;
  nfs_param.gnamemap_cache_param.hash_param.hash_func_rbt = id_rbt_hash_func;
  nfs_param.gnamemap_cache_param.hash_param.compare_key = compare_id;
  nfs_param.gnamemap_cache_param.hash_param.key_to_str = display_idmapper_id;
  nfs_param.gnamemap_cache_param.hash_param.val_to_str = display_idmapper_name;
  nfs_param.gnamemap_cache_param.hash_param.ht_name = "GNAME Map Cache";
  nfs_param.gnamemap_cache_param.hash_param.flags = HT_FLAG_NONE;
  nfs_param.gnamemap_cache_param.hash_param.ht_log_component = COMPONENT_IDMAPPER;
  nfs_param.gnamemap_cache_param.mapfile[0] = '\0';

  /*  Worker parameters : IP/stats hash table */
  nfs_param.ip_stats_param.hash_param.index_size = PRIME_IP_STATS;
  nfs_param.ip_stats_param.hash_param.alphabet_length = 10;  /* ipaddr is a numerical decimal value */
  nfs_param.ip_stats_param.hash_param.hash_func_key = ip_stats_value_hash_func;
  nfs_param.ip_stats_param.hash_param.hash_func_rbt = ip_stats_rbt_hash_func;
  nfs_param.ip_stats_param.hash_param.compare_key = compare_ip_stats;
  nfs_param.ip_stats_param.hash_param.key_to_str = display_ip_stats_key;
  nfs_param.ip_stats_param.hash_param.val_to_str = display_ip_stats_val;
  nfs_param.ip_stats_param.hash_param.ht_name = "IP Stats";
  nfs_param.ip_stats_param.hash_param.flags = HT_FLAG_NONE;
  nfs_param.ip_stats_param.hash_param.ht_log_component = COMPONENT_DISPATCH;

  /*  Worker parameters : NFSv4 Unconfirmed Client id table */
  nfs_param.client_id_param.cid_unconfirmed_hash_param.index_size = PRIME_CLIENT_ID;
  nfs_param.client_id_param.cid_unconfirmed_hash_param.alphabet_length = 10; /* ipaddr is a numerical decimal value */
  nfs_param.client_id_param.cid_unconfirmed_hash_param.hash_func_key = client_id_value_hash_func;
  nfs_param.client_id_param.cid_unconfirmed_hash_param.hash_func_rbt = client_id_rbt_hash_func;
  nfs_param.client_id_param.cid_unconfirmed_hash_param.hash_func_both = NULL ;
  nfs_param.client_id_param.cid_unconfirmed_hash_param.compare_key = compare_client_id;
  nfs_param.client_id_param.cid_unconfirmed_hash_param.key_to_str = display_client_id_key;
  nfs_param.client_id_param.cid_unconfirmed_hash_param.val_to_str = display_client_id_val;
  nfs_param.client_id_param.cid_unconfirmed_hash_param.ht_name = "Unconfirmed Client ID";
  nfs_param.client_id_param.cid_unconfirmed_hash_param.flags = HT_FLAG_CACHE;
  nfs_param.client_id_param.cid_unconfirmed_hash_param.ht_log_component = COMPONENT_CLIENTID;

  /*  Worker parameters : NFSv4 Confirmed Client id table */
  nfs_param.client_id_param.cid_confirmed_hash_param.index_size = PRIME_CLIENT_ID;
  nfs_param.client_id_param.cid_confirmed_hash_param.alphabet_length = 10; /* ipaddr is a numerical decimal value */
  nfs_param.client_id_param.cid_confirmed_hash_param.hash_func_key = client_id_value_hash_func;
  nfs_param.client_id_param.cid_confirmed_hash_param.hash_func_rbt = client_id_rbt_hash_func;
  nfs_param.client_id_param.cid_confirmed_hash_param.hash_func_both = NULL ;
  nfs_param.client_id_param.cid_confirmed_hash_param.compare_key = compare_client_id;
  nfs_param.client_id_param.cid_confirmed_hash_param.key_to_str = display_client_id_key;
  nfs_param.client_id_param.cid_confirmed_hash_param.val_to_str = display_client_id_val;
  nfs_param.client_id_param.cid_confirmed_hash_param.ht_name = "Confirmed Client ID";
  nfs_param.client_id_param.cid_confirmed_hash_param.flags = HT_FLAG_CACHE;
  nfs_param.client_id_param.cid_confirmed_hash_param.ht_log_component = COMPONENT_CLIENTID;

  /*  Worker parameters : NFSv4 Client Record table */
  nfs_param.client_id_param.cr_hash_param.index_size = PRIME_CLIENT_ID;
  nfs_param.client_id_param.cr_hash_param.alphabet_length = 10; /* ipaddr is a numerical decimal value */
  nfs_param.client_id_param.cr_hash_param.hash_func_key = client_record_value_hash_func;
  nfs_param.client_id_param.cr_hash_param.hash_func_rbt = client_record_rbt_hash_func;
  nfs_param.client_id_param.cr_hash_param.hash_func_both = NULL ;
  nfs_param.client_id_param.cr_hash_param.compare_key = compare_client_record;
  nfs_param.client_id_param.cr_hash_param.key_to_str = display_client_record_key;
  nfs_param.client_id_param.cr_hash_param.val_to_str = display_client_record_val;
  nfs_param.client_id_param.cr_hash_param.ht_name = "Client Record";
  nfs_param.client_id_param.cr_hash_param.flags = HT_FLAG_CACHE;
  nfs_param.client_id_param.cr_hash_param.ht_log_component = COMPONENT_CLIENTID;

  /* NFSv4 State Id hash */
  nfs_param.state_id_param.hash_param.index_size = PRIME_STATE_ID;
  /* ipaddr is a numerical decimal value */
  nfs_param.state_id_param.hash_param.alphabet_length = 10;
  nfs_param.state_id_param.hash_param.hash_func_key = state_id_value_hash_func;
  nfs_param.state_id_param.hash_param.hash_func_rbt = state_id_rbt_hash_func;
  nfs_param.state_id_param.hash_param.hash_func_both = NULL;
  nfs_param.state_id_param.hash_param.compare_key = compare_state_id;
  nfs_param.state_id_param.hash_param.key_to_str = display_state_id_key;
  nfs_param.state_id_param.hash_param.val_to_str = display_state_id_val;
  nfs_param.state_id_param.hash_param.ht_name = "State ID";
  nfs_param.state_id_param.hash_param.flags = HT_FLAG_CACHE;
  nfs_param.state_id_param.hash_param.ht_log_component = COMPONENT_STATE;

#ifdef _USE_NFS4_1
  /* NFSv4 Session Id hash */
  nfs_param.session_id_param.hash_param.index_size = PRIME_STATE_ID;
  /* ipaddr is a numerical decimal value */
  nfs_param.session_id_param.hash_param.alphabet_length = 10;
  nfs_param.session_id_param.hash_param.hash_func_key
       = session_id_value_hash_func;
  nfs_param.session_id_param.hash_param.hash_func_rbt
       = session_id_rbt_hash_func;
  nfs_param.session_id_param.hash_param.compare_key = compare_session_id;
  nfs_param.session_id_param.hash_param.key_to_str = display_session_id_key;
  nfs_param.session_id_param.hash_param.val_to_str = display_session_id_val;
  nfs_param.session_id_param.hash_param.ht_name = "Session ID";
  nfs_param.session_id_param.hash_param.flags = HT_FLAG_CACHE;
  nfs_param.session_id_param.hash_param.ht_log_component = COMPONENT_SESSIONS;

#endif                          /* _USE_NFS4_1 */

  /* NFSv4 Pseudo FS <-> nodeid hash */
  nfs_param.nfs4_pseudofs_param.hash_param.index_size = PRIME_STATE_ID;
  nfs_param.nfs4_pseudofs_param.hash_param.alphabet_length = 10;
  nfs_param.nfs4_pseudofs_param.hash_param.hash_func_key = nfs4_pseudo_value_hash_func;
  nfs_param.nfs4_pseudofs_param.hash_param.hash_func_rbt = nfs4_pseudo_rbt_hash_func;
  nfs_param.nfs4_pseudofs_param.hash_param.compare_key = compare_nfs4_pseudo_key;
  nfs_param.nfs4_pseudofs_param.hash_param.key_to_str = display_pseudo_key;
  nfs_param.nfs4_pseudofs_param.hash_param.val_to_str = display_pseudo_val;
  nfs_param.nfs4_pseudofs_param.hash_param.ht_name = "NFS4 PseudoFS nodeid";
  nfs_param.nfs4_pseudofs_param.hash_param.flags = HT_FLAG_CACHE;
  nfs_param.nfs4_pseudofs_param.hash_param.ht_log_component = COMPONENT_NFS_V4_PSEUDO;

  /* NFSv4 Open Owner hash */
  nfs_param.nfs4_owner_param.hash_param.index_size = PRIME_STATE_ID;
  nfs_param.nfs4_owner_param.hash_param.alphabet_length = 10;        /* ipaddr is a numerical decimal value */
  nfs_param.nfs4_owner_param.hash_param.hash_func_key = nfs4_owner_value_hash_func;
  nfs_param.nfs4_owner_param.hash_param.hash_func_rbt = nfs4_owner_rbt_hash_func;
  nfs_param.nfs4_owner_param.hash_param.compare_key = compare_nfs4_owner_key;
  nfs_param.nfs4_owner_param.hash_param.key_to_str = display_nfs4_owner_key;
  nfs_param.nfs4_owner_param.hash_param.val_to_str = display_nfs4_owner_val;
  nfs_param.nfs4_owner_param.hash_param.ht_name = "NFS4 Owner";
  nfs_param.nfs4_owner_param.hash_param.flags = HT_FLAG_CACHE;
  nfs_param.nfs4_owner_param.hash_param.ht_log_component = COMPONENT_STATE;

#ifdef _USE_NLM
  /* NSM Client hash */
  nfs_param.nsm_client_hash_param.index_size = PRIME_STATE_ID;
  nfs_param.nsm_client_hash_param.alphabet_length = 10;        /* ipaddr is a numerical decimal value */
  nfs_param.nsm_client_hash_param.hash_func_key = nsm_client_value_hash_func;
  nfs_param.nsm_client_hash_param.hash_func_rbt = nsm_client_rbt_hash_func;
  nfs_param.nsm_client_hash_param.compare_key = compare_nsm_client_key;
  nfs_param.nsm_client_hash_param.key_to_str = display_nsm_client_key;
  nfs_param.nsm_client_hash_param.val_to_str = display_nsm_client_val;
  nfs_param.nsm_client_hash_param.ht_name = "NSM Client";
  nfs_param.nsm_client_hash_param.flags = HT_FLAG_NONE;
  nfs_param.nsm_client_hash_param.ht_log_component = COMPONENT_STATE;

  /* NLM Client hash */
  nfs_param.nlm_client_hash_param.index_size = PRIME_STATE_ID;
  nfs_param.nlm_client_hash_param.alphabet_length = 10;        /* ipaddr is a numerical decimal value */
  nfs_param.nlm_client_hash_param.hash_func_key = nlm_client_value_hash_func;
  nfs_param.nlm_client_hash_param.hash_func_rbt = nlm_client_rbt_hash_func;
  nfs_param.nlm_client_hash_param.compare_key = compare_nlm_client_key;
  nfs_param.nlm_client_hash_param.key_to_str = display_nlm_client_key;
  nfs_param.nlm_client_hash_param.val_to_str = display_nlm_client_val;
  nfs_param.nlm_client_hash_param.ht_name = "NLM Client";
  nfs_param.nlm_client_hash_param.flags = HT_FLAG_NONE;
  nfs_param.nlm_client_hash_param.ht_log_component = COMPONENT_STATE;

  /* NLM Owner hash */
  nfs_param.nlm_owner_hash_param.index_size = PRIME_STATE_ID;
  nfs_param.nlm_owner_hash_param.alphabet_length = 10;        /* ipaddr is a numerical decimal value */
  nfs_param.nlm_owner_hash_param.hash_func_key = nlm_owner_value_hash_func;
  nfs_param.nlm_owner_hash_param.hash_func_rbt = nlm_owner_rbt_hash_func;
  nfs_param.nlm_owner_hash_param.compare_key = compare_nlm_owner_key;
  nfs_param.nlm_owner_hash_param.key_to_str = display_nlm_owner_key;
  nfs_param.nlm_owner_hash_param.val_to_str = display_nlm_owner_val;
  nfs_param.nlm_owner_hash_param.ht_name = "NLM Owner";
  nfs_param.nlm_owner_hash_param.flags = HT_FLAG_NONE;
  nfs_param.nlm_owner_hash_param.ht_log_component = COMPONENT_STATE;
#endif
#ifdef _USE_9P
  /* 9P Lock Owner hash */
  nfs_param._9p_owner_hash_param.index_size = PRIME_STATE_ID;
  nfs_param._9p_owner_hash_param.alphabet_length = 10;        /* ipaddr is a numerical decimal value */
  nfs_param._9p_owner_hash_param.hash_func_key = _9p_owner_value_hash_func;
  nfs_param._9p_owner_hash_param.hash_func_rbt = _9p_owner_rbt_hash_func;
  nfs_param._9p_owner_hash_param.compare_key = compare_9p_owner_key;
  nfs_param._9p_owner_hash_param.key_to_str = display_9p_owner_key;
  nfs_param._9p_owner_hash_param.val_to_str = display_9p_owner_val;
  nfs_param._9p_owner_hash_param.ht_name = "9P Owner";
#endif 

  /* Cache inode parameters : hash table */
  cache_inode_params.hparam.index_size = PRIME_CACHE_INODE;
  cache_inode_params.hparam.alphabet_length = 10;
  cache_inode_params.hparam.hash_func_key = NULL;
  cache_inode_params.hparam.hash_func_rbt = NULL;
  cache_inode_params.hparam.hash_func_both = cache_inode_fsal_rbt_both;
  cache_inode_params.hparam.compare_key = cache_inode_compare_key_fsal;
  cache_inode_params.hparam.key_to_str = display_cache_inode_key;
  cache_inode_params.hparam.val_to_str = NULL; /* value display not implemented */
  cache_inode_params.hparam.ht_name = "Cache Inode";
  cache_inode_params.hparam.flags = HT_FLAG_CACHE;
  cache_inode_params.hparam.ht_log_component = COMPONENT_CACHE_INODE;

#ifdef _USE_NLM
  /* Cache inode parameters : cookie hash table */
  cache_inode_params.cookie_param.index_size = PRIME_STATE_ID;
  cache_inode_params.cookie_param.alphabet_length = 10;
  cache_inode_params.cookie_param.hash_func_key = lock_cookie_value_hash_func;
  cache_inode_params.cookie_param.hash_func_rbt = lock_cookie_rbt_hash_func ;
  cache_inode_params.cookie_param.compare_key = compare_lock_cookie_key;
  cache_inode_params.cookie_param.key_to_str = display_lock_cookie_key;
  cache_inode_params.cookie_param.val_to_str = display_lock_cookie_val;
  cache_inode_params.cookie_param.ht_name = "Lock Cookie";
  cache_inode_params.cookie_param.flags = HT_FLAG_NONE;
  cache_inode_params.cookie_param.ht_log_component = COMPONENT_STATE;
#endif

  /* Cache inode parameters: Garbage collection policy */
  cache_inode_gc_policy.entries_hwmark = 100000;
  cache_inode_gc_policy.entries_lwmark = 50000;
  cache_inode_gc_policy.use_fd_cache = TRUE;
  cache_inode_gc_policy.lru_run_interval = 600;
  cache_inode_gc_policy.fd_limit_percent = 99;
  cache_inode_gc_policy.reaper_work = 1000;
  cache_inode_gc_policy.fd_hwmark_percent = 90;
  cache_inode_gc_policy.fd_lwmark_percent = 50;
  cache_inode_gc_policy.biggest_window = 40;
  cache_inode_gc_policy.required_progress = 5;
  cache_inode_gc_policy.futility_count = 8;

  cache_inode_params.grace_period_attr   = 0;
  cache_inode_params.grace_period_link   = 0;
  cache_inode_params.grace_period_dirent = 0;
  cache_inode_params.expire_type_attr    = CACHE_INODE_EXPIRE_NEVER;
  cache_inode_params.expire_type_link    = CACHE_INODE_EXPIRE_NEVER;
  cache_inode_params.expire_type_dirent  = CACHE_INODE_EXPIRE_NEVER;
  cache_inode_params.getattr_dir_invalidation = 0;
#ifdef _USE_NFS4_ACL
  cache_inode_params.attrmask = FSAL_ATTR_MASK_V4;
#else
  cache_inode_params.attrmask = FSAL_ATTR_MASK_V2_V3;
#endif
  cache_inode_params.use_fsal_hash = 1;

  /* FSAL parameters */
  nfs_param.fsal_param.fsal_info.max_fs_calls = 30;  /* No semaphore to access the FSAL */

  FSAL_SetDefault_FSAL_parameter(&nfs_param.fsal_param);
  FSAL_SetDefault_FS_common_parameter(&nfs_param.fsal_param);
  FSAL_SetDefault_FS_specific_parameter(&nfs_param.fsal_param);

  init_glist(&exportlist);
  nfs_param.pexportlist = &exportlist;

  /* SNMP ADM parameters */
#ifdef _SNMP_ADM_ACTIVE
  nfs_param.extern_param.snmp_adm.snmp_agentx_socket[0] = '\0';
  nfs_param.extern_param.snmp_adm.product_id = 1;
  nfs_param.extern_param.snmp_adm.snmp_log_file[0] = '\0';

  nfs_param.extern_param.snmp_adm.export_cache_stats = TRUE;
  nfs_param.extern_param.snmp_adm.export_requests_stats = TRUE;
  nfs_param.extern_param.snmp_adm.export_maps_stats = FALSE;
  nfs_param.extern_param.snmp_adm.export_nfs_calls_detail = FALSE;
  nfs_param.extern_param.snmp_adm.export_fsal_calls_detail = FALSE;
#endif
  init_glist(&nfs_param.extern_param.stat_export.allowed_clients.client_list);
}                               /* nfs_set_param_default */

/**
 * nfs_set_param_from_conf:
 * Load parameters from config file.
 */
int nfs_set_param_from_conf(nfs_start_info_t * p_start_info)
{
  config_file_t config_struct;
  int rc;
  fsal_status_t fsal_status;
  cache_inode_status_t cache_inode_status;

  /* First, parse the configuration file */

  config_struct = config_ParseFile(config_path);

  if(!config_struct)
    {
      LogFatal(COMPONENT_INIT, "Error while parsing %s: %s",
               config_path, config_GetErrorMsg());
    }

  if((rc = read_log_config(config_struct)) < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing log configuration");
      return -1;
    }

  /* Core parameters */
  if((rc = nfs_read_core_conf(config_struct, &nfs_param.core_param)) < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing core configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogEvent(COMPONENT_INIT,
             "No core configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
                 "core configuration read from config file");
    }

  /* Load FSAL configuration from parsed file */
  fsal_status =
      FSAL_load_FSAL_parameter_from_conf(config_struct, &nfs_param.fsal_param);
  if(FSAL_IS_ERROR(fsal_status))
    {
      if(fsal_status.major == ERR_FSAL_NOENT)
        LogDebug(COMPONENT_INIT,
		 "No FSAL parameters found in config file, using default");
      else
        {
          LogCrit(COMPONENT_INIT, "Error while parsing FSAL parameters");
          LogError(COMPONENT_INIT, ERR_FSAL, fsal_status.major, fsal_status.minor);
          return -1;
        }
    }
  else
    LogDebug(COMPONENT_INIT,
             "FSAL parameters read from config file");

  /* Load FSAL configuration from parsed file */
  fsal_status =
      FSAL_load_FS_common_parameter_from_conf(config_struct, &nfs_param.fsal_param);
  if(FSAL_IS_ERROR(fsal_status))
    {
      if(fsal_status.major == ERR_FSAL_NOENT)
        LogDebug(COMPONENT_INIT,
		 "No FS common configuration found in config file, using default");
      else
        {
          LogCrit(COMPONENT_INIT,
                  "Error while parsing FS common configuration");
          LogError(COMPONENT_INIT, ERR_FSAL, fsal_status.major, fsal_status.minor);
          return -1;
        }
    }
  else
    LogDebug(COMPONENT_INIT,
             "FS comon configuration read from config file");

  /* Load FSAL configuration from parsed file */
  fsal_status =
      FSAL_load_FS_specific_parameter_from_conf(config_struct, &nfs_param.fsal_param);
  if(FSAL_IS_ERROR(fsal_status))
    {
      if(fsal_status.major == ERR_FSAL_NOENT)
        LogDebug(COMPONENT_INIT,
		 "No FS specific configuration found in config file, using default");
      else
        {
          LogCrit(COMPONENT_INIT,
                  "Error while parsing FS specific configuration");
          LogError(COMPONENT_INIT, ERR_FSAL, fsal_status.major, fsal_status.minor);
          return -1;
        }
    }
  else
    LogDebug(COMPONENT_INIT,
             "FS specific configuration read from config file");

  /* Workers parameters */
  if((rc = nfs_read_worker_conf(config_struct, &nfs_param.worker_param)) < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing workers configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT,
		 "No workers configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
                 "workers configuration read from config file");
    }

  /* Worker parameters : dupreq hash table */
  if((rc = nfs_read_dupreq_hash_conf(config_struct, &nfs_param.dupreq_param)) < 0)
    {
      LogCrit(COMPONENT_INIT,
	      "Error while parsing duplicate request hash table configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT,
		 "No duplicate request hash table configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
                 "duplicate request hash table configuration read from config file");
    }

  /* Worker paramters: ip/name hash table and expiration for each entry */
  if((rc = nfs_read_ip_name_conf(config_struct, &nfs_param.ip_name_param)) < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing IP/name configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT,
		 "No IP/name configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
                 "IP/name configuration read from config file");
    }

  /* Worker paramters: uid_mapper hash table, same config for uid and uname resolution */
  if(((rc = nfs_read_uidmap_conf(config_struct, &nfs_param.uidmap_cache_param)) < 0)
     || ((rc = nfs_read_uidmap_conf(config_struct, &nfs_param.unamemap_cache_param)) <
         0))
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing UID_MAPPER configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT,
		 "No UID_MAPPER configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
                 "UID_MAPPER configuration read from config file");
    }

  /* Worker paramters: gid_mapper hash table, same config for gid and gname resolution */
  if(((rc = nfs_read_gidmap_conf(config_struct, &nfs_param.gidmap_cache_param)) < 0)
     || ((rc = nfs_read_gidmap_conf(config_struct, &nfs_param.gnamemap_cache_param)) <
         0))
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing GID_MAPPER configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT,
		 "No GID_MAPPER configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
		 "GID_MAPPER configuration read from config file");
    }

  /* Worker paramters: client_id hash table */
  if((rc = nfs_read_client_id_conf(config_struct, &nfs_param.client_id_param)) < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing Client id configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT,
		 "No Client id configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
		 "Client id configuration read from config file");
    }

  /* Worker paramters: state_id hash table */
  if((rc = nfs_read_state_id_conf(config_struct, &nfs_param.state_id_param)) < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing State id configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT,
		 "No state id configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
		 "state id configuration read from config file");
    }

#ifdef _USE_NFS4_1
  /* Worker paramters: session_id hash table */
  if((rc = nfs_read_session_id_conf(config_struct, &nfs_param.session_id_param)) < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing session id configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT, 
		 "No session id configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
		 "session id configuration read from config file");
    }

#endif                          /* _USE_NFS4_1 */

#ifdef _HAVE_GSSAPI
  /* NFS kerberos5 configuration */
  if((rc = nfs_read_krb5_conf(config_struct, &nfs_param.krb5_param)) < 0)
    {
      LogCrit(COMPONENT_INIT,
	      "Error while parsing NFS/KRB5 configuration for RPCSEC_GSS");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT,
		 "No NFS/KRB5 configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
		 "NFS/KRB5 configuration read from config file");
    }
#endif

  /* NFSv4 specific configuration */
  if((rc = nfs_read_version4_conf(config_struct, &nfs_param.nfsv4_param)) < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing NFSv4 specific configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT,
                 "No NFSv4 specific configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
                 "NFSv4 specific configuration read from config file");
    }

#ifdef _USE_9P
  if( ( rc = _9p_read_conf( config_struct,
                            &nfs_param._9p_param ) ) < 0 )
    {
        if( rc == -2 )
          LogDebug(COMPONENT_INIT,
                   "No 9P configuration found, using default");
        else
          {
	     LogCrit( COMPONENT_INIT,
	   	      "Error while parsing 9P configuration" ) ;
             return -1 ;
          }
    }
#endif

  /* Cache inode parameters : hash table */
  if((cache_inode_status =
      cache_inode_read_conf_hash_parameter(config_struct,
                                           &cache_inode_params))
     != CACHE_INODE_SUCCESS)
    {
      if(cache_inode_status == CACHE_INODE_NOT_FOUND)
        LogDebug(COMPONENT_INIT,
                 "No Cache Inode Hash Table configuration found, using default");
      else
        {
          LogCrit(COMPONENT_INIT,
                  "Error while parsing Cache Inode Hash Table configuration");
          return -1;
        }
    }
  else
    LogDebug(COMPONENT_INIT,
             "Cache Inode Hash Table configuration read from config file");

  /* Cache inode parameters : Garbage collection policy */
  if((cache_inode_status =
      cache_inode_read_conf_gc_policy(config_struct,
                                      &cache_inode_gc_policy)) !=
     CACHE_INODE_SUCCESS)
    {
      if(cache_inode_status == CACHE_INODE_NOT_FOUND)
        LogDebug(COMPONENT_INIT,
                 "No Cache Inode Garbage Collection Policy configuration found, using default");
      else
        {
          LogCrit(COMPONENT_INIT,
                  "Error while parsing Cache Inode Garbage Collection Policy configuration");
          return -1;
        }
    }
  else
    LogDebug(COMPONENT_INIT,
             "Cache Inode Garbage Collection Policy configuration read from config file");

  /* Cache inode client parameters */
  if((cache_inode_status
      = cache_inode_read_conf_parameter(config_struct,
                                        &cache_inode_params))
     != CACHE_INODE_SUCCESS)
    {
      if(cache_inode_status == CACHE_INODE_NOT_FOUND)
        LogDebug(COMPONENT_INIT,
                 "No Cache Inode Client configuration found, using default");
      else
        {
          LogCrit(COMPONENT_INIT,
                  "Error while parsing Cache Inode Client configuration");
          return 1;
        }
    }
  else
    LogDebug(COMPONENT_INIT,
             "Cache Inode Client configuration read from config file");

#ifdef _SNMP_ADM_ACTIVE
  if(get_snmpadm_conf(config_struct, &nfs_param.extern_param) != 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error loading SNMP_ADM configuration");
      return -1;
    }
  else
    {
      LogDebug(COMPONENT_INIT,
               "snmp_adm configuration read from config file");
    }
#endif                          /* _SNMP_ADM_ACTIVE */

#ifdef _USE_STAT_EXPORTER
  if(get_stat_exporter_conf(config_struct, &nfs_param.extern_param) != 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error loading STAT_EXPORTER configuration");
      return -1;
    }
  else
      LogDebug(COMPONENT_INIT,
               "STAT_EXPORTER configuration read from config file");
#endif                          /* _USE_STAT_EXPORTER */

  /* Load export entries from parsed file
   * returns the number of export entries.
   */
  rc = ReadExports(config_struct, nfs_param.pexportlist);
  if(rc < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing export entries");
    }
  else if(rc == 0)
    {
      LogWarn(COMPONENT_INIT,
              "No export entries found in configuration file !!!");
    }

  LogEvent(COMPONENT_INIT, "Configuration file successfully parsed");

  /* freeing syntax tree : */

  config_Free(config_struct);

  return 0;
}

/**
 * is_prime : check whether a given value is prime or not
 */
static int is_prime (int  v)
{
    int    i, m;

    if (v <= 1)
	return FALSE;
    if (v == 2)
	return TRUE;
    if (v % 2 == 0)
	return FALSE;
    // dont link with libm just for this
#ifdef LINK_LIBM
    m = (int)sqrt(v);
#else
    m = v;
#endif
    for (i = 2 ; i <= m; i +=2) {
	if (v % i == 0)
	    return FALSE;
    }
    return TRUE;
}

/**
 * nfs_check_param_consistency:
 * Checks parameters concistency (limits, ...)
 */
int nfs_check_param_consistency()
{

  /** @todo BUGAZOMEU: check we don't have twice the same export id in the export list */

  if(nfs_param.core_param.nb_worker <= 0)
    {
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER: There must be more than %d workers",
              nfs_param.core_param.nb_worker);
      return 1;
    }

  if(nfs_param.core_param.nb_worker > NB_MAX_WORKER_THREAD)
    {
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER: number of workers is limited to %d",
              NB_MAX_WORKER_THREAD);
      return 1;
    }

#if 0
/* XXXX this seems somewhat the obvious of what I would have reasoned.
 * Where we had a thread for every connection (but sharing a single
 * fdset for select), dispatching on a small, fixed worker pool, we
 * now had an arbitrary, fixed work pool, with flexible event
 * channels.
 */
  if (2*nfs_param.core_param.nb_worker >
      nfs_param.cache_layers_param.cache_param.hparam.index_size )
    {
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER: number of workers is too large compared to "
              "Cache_Inode's index size, it should be smaller than "
              "half of it");
      return 1;
    }
#endif

  /* check for parameters which need to be primes */
  if (!is_prime(nfs_param.ip_name_param.hash_param.index_size) ||
      !is_prime(nfs_param.uidmap_cache_param.hash_param.index_size) ||
      !is_prime(nfs_param.unamemap_cache_param.hash_param.index_size) ||
      !is_prime(nfs_param.gidmap_cache_param.hash_param.index_size) ||
      !is_prime(nfs_param.uidgidmap_cache_param.hash_param.index_size) ||
      !is_prime(nfs_param.gnamemap_cache_param.hash_param.index_size) ||
      !is_prime(nfs_param.ip_stats_param.hash_param.index_size) ||
      !is_prime(nfs_param.client_id_param.cid_unconfirmed_hash_param.index_size) ||
      !is_prime(nfs_param.client_id_param.cid_confirmed_hash_param.index_size) ||
      !is_prime(nfs_param.client_id_param.cr_hash_param.index_size) ||
      !is_prime(nfs_param.state_id_param.hash_param.index_size) ||
#ifdef _USE_NFS4_1
      !is_prime(nfs_param.session_id_param.hash_param.index_size) ||
#endif
      !is_prime(nfs_param.nfs4_owner_param.hash_param.index_size) ||
#ifdef _USE_NLM
      !is_prime(nfs_param.nsm_client_hash_param.index_size) ||
      !is_prime(nfs_param.nlm_client_hash_param.index_size) ||
      !is_prime(nfs_param.nlm_owner_hash_param.index_size) ||
      !is_prime(cache_inode_params.cookie_param.index_size) ||
#endif
      !is_prime(cache_inode_params.hparam.index_size))
  {
      LogCrit(COMPONENT_INIT, "BAD PARAMETER(s) : expected primes");
  }

  return 0;
}

void nfs_reset_stats(void)
{
  unsigned int i = 0;
  unsigned int j = 0;

  for(i = 0; i < nfs_param.core_param.nb_worker; i++)
    {
      workers_data[i].stats.nb_total_req = 0;
      workers_data[i].stats.nb_udp_req = 0;
      workers_data[i].stats.nb_tcp_req = 0;
      workers_data[i].stats.stat_req.nb_mnt1_req = 0;
      workers_data[i].stats.stat_req.nb_mnt3_req = 0;
      workers_data[i].stats.stat_req.nb_nfs2_req = 0;
      workers_data[i].stats.stat_req.nb_nfs3_req = 0;
      workers_data[i].stats.stat_req.nb_nfs4_req = 0;

      for(j = 0; j < MNT_V1_NB_COMMAND; j++)
        {
          workers_data[i].stats.stat_req.stat_req_mnt1[j].total = 0;
          workers_data[i].stats.stat_req.stat_req_mnt1[j].success = 0;
          workers_data[i].stats.stat_req.stat_req_mnt1[j].dropped = 0;

        }

      for(j = 0; j < MNT_V3_NB_COMMAND; j++)
        {
          workers_data[i].stats.stat_req.stat_req_mnt3[j].total = 0;
          workers_data[i].stats.stat_req.stat_req_mnt3[j].success = 0;
          workers_data[i].stats.stat_req.stat_req_mnt3[j].dropped = 0;
        }

      for(j = 0; j < NFS_V2_NB_COMMAND; j++)
        {
          workers_data[i].stats.stat_req.stat_req_nfs2[j].total = 0;
          workers_data[i].stats.stat_req.stat_req_nfs2[j].success = 0;
          workers_data[i].stats.stat_req.stat_req_nfs2[j].dropped = 0;
        }

      for(j = 0; j < NFS_V3_NB_COMMAND; j++)
        {
          workers_data[i].stats.stat_req.stat_req_nfs3[j].total = 0;
          workers_data[i].stats.stat_req.stat_req_nfs3[j].success = 0;
          workers_data[i].stats.stat_req.stat_req_nfs3[j].dropped = 0;
        }

      for(j = 0; j < NFS_V4_NB_COMMAND; j++)
        {
          workers_data[i].stats.stat_req.stat_req_nfs4[j].total = 0;
          workers_data[i].stats.stat_req.stat_req_nfs4[j].success = 0;
          workers_data[i].stats.stat_req.stat_req_nfs4[j].dropped = 0;
        }

      for(j = 0; j < NFS_V40_NB_OPERATION; j++)
        {
          workers_data[i].stats.stat_req.stat_op_nfs40[j].total = 0;
          workers_data[i].stats.stat_req.stat_op_nfs40[j].success = 0;
          workers_data[i].stats.stat_req.stat_op_nfs40[j].failed = 0;
        }

      for(j = 0; j < NFS_V41_NB_OPERATION; j++)
        {
          workers_data[i].stats.stat_req.stat_op_nfs41[j].total = 0;
          workers_data[i].stats.stat_req.stat_op_nfs41[j].success = 0;
          workers_data[i].stats.stat_req.stat_op_nfs41[j].failed = 0;
        }

      workers_data[i].stats.last_stat_update = 0;
      memset(&workers_data[i].stats.fsal_stats, 0, sizeof(fsal_statistics_t));
    }                           /* for( i = 0 ; i < nfs_param.core_param.nb_worker ; i++ ) */

}                               /* void nfs_reset_stats( void ) */

static void nfs_Start_threads(void)
{
  int rc = 0;
  pthread_attr_t attr_thr;
  unsigned long i = 0;

  LogDebug(COMPONENT_THREAD,
           "Starting threads");

  /* Init for thread parameter (mostly for scheduling) */
  if(pthread_attr_init(&attr_thr) != 0)
    LogDebug(COMPONENT_THREAD, "can't init pthread's attributes");

  if(pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
    LogDebug(COMPONENT_THREAD, "can't set pthread's scope");

  if(pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE) != 0)
    LogDebug(COMPONENT_THREAD, "can't set pthread's join state");

  if(pthread_attr_setstacksize(&attr_thr, THREAD_STACK_SIZE) != 0)
    LogDebug(COMPONENT_THREAD, "can't set pthread's stack size");

  /* Starting the thread dedicated to signal handling */
  if( ( rc = pthread_create( &sigmgr_thrid, &attr_thr, sigmgr_thread, NULL ) ) != 0 )
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create sigmgr_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogDebug(COMPONENT_THREAD,
           "sigmgr thread started");

  /* Starting all of the worker thread */
  for(i = 0; i < nfs_param.core_param.nb_worker; i++)
    {
      if((rc =
          pthread_create(&(worker_thrid[i]), &attr_thr, worker_thread, (void *)i)) != 0)
        {
          LogFatal(COMPONENT_THREAD,
                   "Could not create worker_thread #%lu, error = %d (%s)",
                   i, errno, strerror(errno));
        }
    }
  LogEvent(COMPONENT_THREAD,
           "%d worker threads were started successfully",
           nfs_param.core_param.nb_worker);

#ifdef _USE_BLOCKING_LOCKS
  /* Start State Async threads */
  state_async_thread_start();
#endif

  /*
   * Now that all TCB controlled threads (workers, NLM,
   * sigmgr) were created, lets wait for them to fully
   * initialze __before__ we create the threads that listen
   * for incoming requests.
   */
  wait_for_threads_to_awaken();

  /* Start event channel service threads */
  nfs_rpc_dispatch_threads(&attr_thr);

#ifdef _USE_9P
  /* Starting the 9p dispatcher thread */
  if((rc = pthread_create(&_9p_dispatcher_thrid, &attr_thr,
                          _9p_dispatcher_thread, NULL ) ) != 0 )
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create  9p dispatcher_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD, "9p dispatcher thread was started successfully");
#endif

#ifdef USE_DBUS
      /* DBUS event thread */
      if((rc = pthread_create(&gsh_dbus_thrid, &attr_thr, gsh_dbus_thread,
                              NULL ) ) != 0 )     
	{
            LogFatal(COMPONENT_THREAD,
                     "Could not create gsh_dbus_thread, error = %d (%s)",
                     errno, strerror(errno));
	}
      LogEvent(COMPONENT_THREAD, "gsh_dbusthread was started successfully");
#endif

  /* Starting the admin thread */
  if((rc = pthread_create(&admin_thrid, &attr_thr, admin_thread, NULL)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create admin_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD, "admin thread was started successfully");

  /* Starting the stats thread */
  if((rc =
      pthread_create(&stat_thrid, &attr_thr, stats_thread, NULL)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create stats_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD, "statistics thread was started successfully");

#ifdef _USE_STAT_EXPORTER

  /* Starting the long processing threshold thread */
  if((rc =
      pthread_create(&stat_thrid, &attr_thr, long_processing_thread, NULL)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create long_processing_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD,
           "long processing threshold thread was started successfully");

  /* Starting the stat exporter thread */
  if((rc =
      pthread_create(&stat_exporter_thrid, &attr_thr, stat_exporter_thread, NULL)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create stat_exporter_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD,
           "statistics exporter thread was started successfully");

#endif      /*  _USE_STAT_EXPORTER */

  /* Starting the reaper thread */
  if((rc =
      pthread_create(&reaper_thrid, &attr_thr, reaper_thread, NULL)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create reaper_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD,
           "reaper thread was started successfully");

#ifdef SONAS
  /* Starting the recovery thread */
  if((rc =
      pthread_create(&recovery_thrid, &attr_thr, recovery_thread, NULL)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create recovery_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD,
           "recovery thread was started successfully");
#endif

#ifdef _USE_UPCALL_SIMULATOR
  /* Starts the thread that mimics upcalls from the FSAL */
   /* Starting the stats thread */
  if((rc =
      pthread_create(&upcall_simulator_thrid, &attr_thr, upcall_simulator_thread, NULL)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create upcall_simulator_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD, "upcall_simulator thread was started successfully");
#endif

#ifdef _USE_FSAL_UP
  /* Starting the fsal_up_process thread */
 if((rc =
     pthread_create(&upp_thrid, &attr_thr, fsal_up_process_thread, NULL)) != 0)
   {
     LogFatal(COMPONENT_THREAD,
              "Could not create fsal_up_process_thread, error = %d (%s)",
              errno, strerror(errno));
   }
 LogEvent(COMPONENT_THREAD,
          "fsal_up_process_thread was started successfully");
#endif /* _USE_FSAL_UP */

}                               /* nfs_Start_threads */

/**
 * nfs_Init: Init the nfs daemon 
 *
 * Init the nfs daemon by making all the init operation at all levels of the daemon. 
 * 
 * @param emergency_flush_flag [INPUT] tell if the daemon is started for forcing a data cache flush (in this case, init will be done partially).
 * 
 * @return nothing (void function) but function exists if error is found.
 *
 */

static void nfs_Init(const nfs_start_info_t * p_start_info)
{
  cache_inode_status_t cache_status;
  state_status_t state_status;
  fsal_status_t fsal_status;
  unsigned int i = 0;
  int rc = 0;
#ifdef _HAVE_GSSAPI
  gss_buffer_desc gss_service_buf;
  OM_uint32 maj_stat, min_stat;
  char GssError[MAXNAMLEN+1];
#endif

  /* FSAL Initialisation */
  fsal_status = FSAL_Init(&nfs_param.fsal_param);
  if(FSAL_IS_ERROR(fsal_status))
    {
      /* Failed init */
      LogFatal(COMPONENT_INIT, "FSAL library could not be initialized");
    }
  LogInfo(COMPONENT_INIT, "FSAL library successfully initialized");

#ifdef USE_DBUS
  /* DBUS init */
  gsh_dbus_pkginit();
#endif

  /* Cache Inode Initialisation */
  if((fh_to_cache_entry_ht =
      cache_inode_init(cache_inode_params, &cache_status)) == NULL)
    {
      LogFatal(COMPONENT_INIT,
               "Cache Inode Layer could not be initialized, status=%s",
               cache_inode_err_str(cache_status));
    }

  /* Initialize thread control block */
  tcb_head_init();

#ifdef _USE_BLOCKING_LOCKS
  if(state_lock_init(&state_status,
                     cache_inode_params.cookie_param)
     != STATE_SUCCESS)
#else
  if(state_lock_init(&state_status)
     != STATE_SUCCESS)
#endif
    {
      LogFatal(COMPONENT_INIT,
               "State Lock Layer could not be initialized, status=%s",
               state_err_str(state_status));
    }
  LogInfo(COMPONENT_INIT, "Cache Inode library successfully initialized");

  /* Cache Inode LRU (call this here, rather than as part of
     cache_inode_init() so the GC policy has been set */
  cache_inode_lru_pkginit();

#ifdef _USE_NFS4_1
  nfs41_session_pool = pool_init("NFSv4.1 session pool",
                                 sizeof(nfs41_session_t),
                                 pool_basic_substrate,
                                 NULL, NULL, NULL);
#endif /* _USE_NFS4_1 */

  request_pool = pool_init("Request pool",
                           sizeof(request_data_t),
                           pool_basic_substrate,
                           NULL,
                           NULL,
                           NULL);
  if(!request_pool)
    {
      LogCrit(COMPONENT_INIT,
              "Error while allocating request pool");
      LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
      Fatal();
    }

  request_data_pool = pool_init("Request Data Pool",
                                sizeof(nfs_request_data_t),
                                pool_basic_substrate,
                                NULL,
                                NULL,
                                NULL);
  if(!request_data_pool)
    {
      LogCrit(COMPONENT_INIT,
              "Error while allocating request data pool");
      LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
      Fatal();
    }

  nfs_res_pool = pool_init("nfs_res_t pool",
			   sizeof(nfs_res_t),
			   pool_basic_substrate,
			   NULL, NULL, NULL);
  if (unlikely(! (nfs_res_pool))) {
	  LogCrit(COMPONENT_INIT,
                "Error while allocating nfs_res_t pool");
	  LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
        Fatal();
  }

  dupreq_pool = pool_init("Duplicate Request Pool",
                          sizeof(dupreq_entry_t),
                          pool_basic_substrate,
                          NULL, NULL, NULL);
  if(!(dupreq_pool))
    {
      LogCrit(COMPONENT_INIT,
              "Error while allocating duplicate request pool");
      LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
      Fatal();
    }

  ip_stats_pool = pool_init("IP Stats Cache Pool",
                            sizeof(nfs_ip_stats_t),
                            pool_basic_substrate,
                            NULL, NULL, NULL);

  if(!(ip_stats_pool))
    {
      LogCrit(COMPONENT_INIT,
              "Error while allocating IP stats cache pool");
      LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
      Fatal();
    }

#ifdef _USE_ASYNC_CACHE_INODE
  /* Start the TAD and synclets for writeback cache inode */
  cache_inode_async_init(nfs_param.cache_layers_param.cache_inode_client_param);
#endif

  /* If rpcsec_gss is used, set the path to the keytab */
#ifdef _HAVE_GSSAPI
#ifdef HAVE_KRB5
  if(nfs_param.krb5_param.active_krb5)
    {
      OM_uint32 gss_status = GSS_S_COMPLETE;

      if(nfs_param.krb5_param.keytab[0] != '\0')
        gss_status = krb5_gss_register_acceptor_identity(nfs_param.krb5_param.keytab);

      if(gss_status != GSS_S_COMPLETE)
        {
          log_sperror_gss(GssError, gss_status, 0);
          LogFatal(COMPONENT_INIT,
                   "Error setting krb5 keytab to value %s is %s",
                   nfs_param.krb5_param.keytab, GssError);
        }
      LogInfo(COMPONENT_INIT, "krb5 keytab path successfully set to %s",
              nfs_param.krb5_param.keytab);
#endif /* HAVE_KRB5 */

      /* Set up principal to be use for GSSAPPI within GSSRPC/KRB5 */
      gss_service_buf.value = nfs_param.krb5_param.svc.principal;
      gss_service_buf.length = strlen(nfs_param.krb5_param.svc.principal) + 1;      /* The '+1' is not to be forgotten, for the '\0' at the end */

      maj_stat = gss_import_name(&min_stat,
                                 &gss_service_buf,
                                 (gss_OID) GSS_C_NT_HOSTBASED_SERVICE,
                                 &nfs_param.krb5_param.svc.gss_name);
      if(maj_stat != GSS_S_COMPLETE)
        {
          log_sperror_gss(GssError, maj_stat, min_stat);
          LogFatal(COMPONENT_INIT,
                   "Error importing gss principal %s is %s",
                   nfs_param.krb5_param.svc.principal, GssError);
        }

      if (nfs_param.krb5_param.svc.gss_name == GSS_C_NO_NAME)
          LogInfo(COMPONENT_INIT, "Regression:  svc.gss_name == GSS_C_NO_NAME");

      LogInfo(COMPONENT_INIT,  "gss principal \"%s\" successfully set",
              nfs_param.krb5_param.svc.principal);

      /* Set the principal to GSSRPC */
      if(! svcauth_gss_set_svc_name(nfs_param.krb5_param.svc.gss_name))
        {
          LogFatal(COMPONENT_INIT, "Impossible to set gss principal to GSSRPC");
        }

      /* Don't release name until shutdown, it will be used by the
       * backchannel. */

#ifdef HAVE_KRB5
    }                           /*  if( nfs_param.krb5_param.active_krb5 ) */
#endif /* HAVE_KRB5 */
#endif /* _HAVE_GSSAPI */

  /* RPC Initialisation - exits on failure*/
  nfs_Init_svc();
  LogInfo(COMPONENT_INIT,  "RPC ressources successfully initialized");

  /* Worker initialisation */
  if((workers_data =
      gsh_calloc(nfs_param.core_param.nb_worker,
                 sizeof(nfs_worker_data_t))) == NULL)
    {
      LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
      Fatal();
    }

  LogDebug(COMPONENT_INIT, "Initializing workers data structure");

  for(i = 0; i < nfs_param.core_param.nb_worker; i++)
    {
      char name[256];

      /* Set the index (mostly used for debug purpose */
      workers_data[i].worker_index = i;

      /* Fill in workers fields (semaphores and other stangenesses */
      if(nfs_Init_worker_data(&(workers_data[i])) != 0)
        LogFatal(COMPONENT_INIT,
                 "Error while initializing worker data #%d", i);

      sprintf(name, "IP Stats for worker %d", i);
      nfs_param.ip_stats_param.hash_param.ht_name = gsh_strdup(name);
      ht_ip_stats[i] = nfs_Init_ip_stats(nfs_param.ip_stats_param);

      if(ht_ip_stats[i] == NULL)
        LogFatal(COMPONENT_INIT,
                 "Error while initializing IP/stats cache #%d", i);

      workers_data[i].ht_ip_stats = ht_ip_stats[i];
      LogDebug(COMPONENT_INIT, "worker data #%d successfully initialized", i);
    }                           /* for i */

  /* Admin initialisation */
  nfs_Init_admin_data();

  /* Set the stats to zero */
  nfs_reset_stats();

  /* Init duplicate request cache */
  dupreq2_pkginit();
  LogInfo(COMPONENT_INIT,
          "duplicate request cache successfully initialized");

  /* Init the IP/name cache */
  LogDebug(COMPONENT_INIT, "Now building IP/name cache");
  if(nfs_Init_ip_name(nfs_param.ip_name_param) != IP_NAME_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing IP/name cache");
    }
  LogInfo(COMPONENT_INIT,
          "IP/name cache successfully initialized");

  idmapper_init();

  /* Init the NFSv4 Clientid cache */
  LogDebug(COMPONENT_INIT, "Now building NFSv4 clientid cache");
  if(nfs_Init_client_id(&nfs_param.client_id_param) != CLIENT_ID_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing NFSv4 clientid cache");
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 clientid cache successfully initialized");

  /* Init The NFSv4 State id cache */
  LogDebug(COMPONENT_INIT, "Now building NFSv4 State Id cache");
  if(nfs4_Init_state_id(nfs_param.state_id_param) != 0)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing NFSv4 State Id cache");
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 State Id cache successfully initialized");

  /* Init The NFSv4 Open Owner cache */
  LogDebug(COMPONENT_INIT, "Now building NFSv4 Owner cache");
  if(Init_nfs4_owner(nfs_param.nfs4_owner_param) != 0)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing NFSv4 Owner cache");
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 Open Owner cache successfully initialized");

#ifdef _USE_NLM
  /* Init The NLM Owner cache */
  LogDebug(COMPONENT_INIT, "Now building NLM Owner cache");
  if(Init_nlm_hash() != 0)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing NLM Owner cache");
    }
  LogInfo(COMPONENT_INIT,
          "NLM Owner cache successfully initialized");
  nlm_init();
#endif

#ifdef _USE_9P
  /* Init the 9P lock owner cache */
  LogDebug(COMPONENT_INIT, "Now building 9P Owner cache");
  if(Init_9p_hash() != 0)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing 9P Owner cache");
    }
  LogInfo(COMPONENT_INIT,
          "9P Owner cache successfully initialized");
#endif 

#ifdef _USE_NFS4_1
  LogDebug(COMPONENT_INIT, "Now building NFSv4 Session Id cache");
  if(nfs41_Init_session_id(nfs_param.session_id_param) != 0)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing NFSv4 Session Id cache");
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 Session Id cache successfully initialized");
#endif

#ifdef _USE_NFS4_ACL
  LogDebug(COMPONENT_INIT, "Now building NFSv4 ACL cache");
  if(nfs4_acls_init() != 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while initializing NFSv4 ACLs");
      exit(1);
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 ACL cache successfully initialized");
#endif                          /* _USE_NFS4_ACL */

#ifdef _USE_9P
  LogDebug(COMPONENT_INIT, "Now building 9P resources");
  if( _9p_init( &nfs_param._9p_param ) )
    {
      LogCrit(COMPONENT_INIT,
              "Error while initializing 9P Resources");
      exit(1);
    }
  LogInfo(COMPONENT_INIT,
          "9P resources successfully initialized");
#endif /* _USE_9P */

#ifdef _USE_FSAL_UP
  /* Initialize FSAL UP queue and event pool */
  nfs_Init_FSAL_UP();
#endif /* _USE_FSAL_UP */

  /* Create the root entries for each exported FS */
  if((rc = nfs_export_create_root_entry(nfs_param.pexportlist)) != TRUE)
    {
      LogCrit(COMPONENT_INIT,
              "Error initializing Cache Inode root entries");
    }

  LogInfo(COMPONENT_INIT,
          "Cache Inode root entries successfully created");

  if(Init_nfs4_pseudo(nfs_param.nfs4_pseudofs_param) != 0)
    {
      LogFatal(COMPONENT_INIT, "Error while initializing NFSv4 Pseudofs cache");
    }
  LogInfo(COMPONENT_INIT, "NFSv4 Pseudofs cache successfully initialized");

  /* Creates the pseudo fs */
  LogDebug(COMPONENT_INIT, "Now building pseudo fs");
  if((rc = nfs4_ExportToPseudoFS(nfs_param.pexportlist)) != 0)
    LogFatal(COMPONENT_INIT,
             "Error %d while initializing NFSv4 pseudo file system", rc);

  LogInfo(COMPONENT_INIT,
          "NFSv4 pseudo file system successfully initialized");

  /* Create stable storage directory, this needs to be done before
   * starting the recovery thread.
   */
  nfs4_create_recov_dir();

  /* initialize grace and read in the client IDs */
  nfs4_init_grace();
  nfs4_load_recov_clids(0);

  /* Start grace period */
  nfs4_start_grace(NULL);

     /* callback dispatch */
     nfs_rpc_cb_pkginit();
#ifdef _USE_CB_SIMULATOR
     nfs_rpc_cbsim_pkginit();
#endif      /*  _USE_CB_SIMULATOR */

}                               /* nfs_Init */

/**
 * nfs_start:
 * start NFS service
 */
void nfs_start(nfs_start_info_t * p_start_info)
{
  struct rlimit ulimit_data;
  int in_grace __attribute__((unused));

#if 0
  /* Will remain as long as all FSAL are not yet in new format */
  printf("---> fsal_handle_t:%lu\n", sizeof(cephfsal_handle_t));
  printf("---> fsal_op_context_t:%lu\n", sizeof(cephfsal_op_context_t));
  printf("---> fsal_file_t:%lu\n", sizeof(cephfsal_file_t));
  printf("---> fsal_dir_t:%lu\n", sizeof(cephfsal_dir_t));
  printf("---> fsal_export_context_t:%lu\n", sizeof(cephfsal_export_context_t));
  printf("---> fsal_cookie_t:%lu\n", sizeof(cephfsal_cookie_t));
  printf("---> fs_specific_initinfo_t:%lu\n", sizeof(cephfs_specific_initinfo_t));
#endif
#if 0
  /* Will remain as long as all FSAL are not yet in new format */
  printf("---> fsal_handle_t:%lu\n", sizeof(vfsfsal_handle_t));
  printf("---> fsal_op_context_t:%lu\n", sizeof(vfsfsal_op_context_t));
  printf("---> fsal_file_t:%lu\n", sizeof(vfsfsal_file_t));
  printf("---> fsal_dir_t:%lu\n", sizeof(vfsfsal_dir_t));
  printf("---> fsal_export_context_t:%lu\n", sizeof(vfsfsal_export_context_t));
  printf("---> fsal_cookie_t:%lu\n", sizeof(vfsfsal_cookie_t));
  printf("---> fs_specific_initinfo_t:%lu\n", sizeof(vfsfs_specific_initinfo_t));
#endif
#if 0
  /* Will remain as long as all FSAL are not yet in new format */
  printf("---> fsal_handle_t:%lu\n", sizeof(proxyfsal_handle_t));
  printf("---> fsal_op_context_t:%lu\n", sizeof(proxyfsal_op_context_t));
  printf("---> fsal_file_t:%lu\n", sizeof(proxyfsal_file_t));
  printf("---> fsal_dir_t:%lu\n", sizeof(proxyfsal_dir_t));
  printf("---> fsal_export_context_t:%lu\n", sizeof(proxyfsal_export_context_t));
  printf("---> fsal_cookie_t:%lu\n", sizeof(proxyfsal_cookie_t));
  printf("---> fs_specific_initinfo_t:%lu\n", sizeof(proxyfs_specific_initinfo_t));
#endif
#if 0
  /* Will remain as long as all FSAL are not yet in new format */
  printf("---> fsal_handle_t:%lu\n", sizeof(xfsfsal_handle_t));
  printf("---> fsal_op_context_t:%lu\n", sizeof(xfsfsal_op_context_t));
  printf("---> fsal_file_t:%lu\n", sizeof(xfsfsal_file_t));
  printf("---> fsal_dir_t:%lu\n", sizeof(xfsfsal_dir_t));
  printf("---> fsal_export_context_t:%lu\n", sizeof(xfsfsal_export_context_t));
  printf("---> fsal_cookie_t:%lu\n", sizeof(xfsfsal_cookie_t));
  printf("---> fs_specific_initinfo_t:%lu\n", sizeof(xfsfs_specific_initinfo_t));
#endif
#if 0
  /* Will remain as long as all FSAL are not yet in new format */
  printf("---> fsal_handle_t:%lu\n", sizeof(zfsfsal_handle_t));
  printf("---> fsal_op_context_t:%lu\n", sizeof(zfsfsal_op_context_t));
  printf("---> fsal_file_t:%lu\n", sizeof(zfsfsal_file_t));
  printf("---> fsal_dir_t:%lu\n", sizeof(zfsfsal_dir_t));
  printf("---> fsal_export_context_t:%lu\n", sizeof(zfsfsal_export_context_t));
  printf("---> fsal_cookie_t:%lu\n", sizeof(zfsfsal_cookie_t));
  printf("---> fs_specific_initinfo_t:%lu\n", sizeof(zfsfs_specific_initinfo_t));
#endif
#if 0
  /* Will remain as long as all FSAL are not yet in new format */
  printf("---> fsal_handle_t:%lu\n", sizeof(lustrefsal_handle_t));
  printf("---> fsal_op_context_t:%lu\n", sizeof(lustrefsal_op_context_t));
  printf("---> fsal_file_t:%lu\n", sizeof(lustrefsal_file_t));
  printf("---> fsal_dir_t:%lu\n", sizeof(lustrefsal_dir_t));
  printf("---> fsal_export_context_t:%lu\n", sizeof(lustrefsal_export_context_t));
  printf("---> fsal_cookie_t:%lu\n", sizeof(lustrefsal_cookie_t));
  printf("---> fs_specific_initinfo_t:%lu\n", sizeof(lustrefs_specific_initinfo_t));
#endif
#if 0
  /* Will remain as long as all FSAL are not yet in new format */
  printf("---> fsal_handle_t:%lu\n", sizeof(hpssfsal_handle_t));
  printf("---> fsal_op_context_t:%lu\n", sizeof(hpssfsal_op_context_t));
  printf("---> fsal_file_t:%lu\n", sizeof(hpssfsal_file_t));
  printf("---> fsal_dir_t:%lu\n", sizeof(hpssfsal_dir_t));
  printf("---> fsal_export_context_t:%lu\n", sizeof(hpssfsal_export_context_t));
  printf("---> fsal_cookie_t:%lu\n", sizeof(hpssfsal_cookie_t));
  printf("---> fs_specific_initinfo_t:%lu\n", sizeof(hpssfs_specific_initinfo_t));
  printf("---> fsal_cred_t:%lu\n", sizeof(hpssfsal_cred_t));
#endif
#if 0
  /* Will remain as long as all FSAL are not yet in new format */
  printf("---> fsal_handle_t:%lu\n", sizeof(snmpfsal_handle_t));
  printf("---> fsal_op_context_t:%lu\n", sizeof(snmpfsal_op_context_t));
  printf("---> fsal_file_t:%lu\n", sizeof(snmpfsal_file_t));
  printf("---> fsal_dir_t:%lu\n", sizeof(snmpfsal_dir_t));
  printf("---> fsal_export_context_t:%lu\n", sizeof(snmpfsal_export_context_t));
  printf("---> fsal_cookie_t:%lu\n", sizeof(snmpfsal_cookie_t));
  printf("---> fs_specific_initinfo_t:%lu\n", sizeof(snmpfs_specific_initinfo_t));
#endif

  /* store the start info so it is available for all layers */
  nfs_start_info = *p_start_info;

  if(p_start_info->dump_default_config == TRUE)
    {
      nfs_print_param_config();
      exit(0);
    }

  /* Set the Core dump size if set */
  if(nfs_param.core_param.core_dump_size != -1)
    {
      LogInfo(COMPONENT_INIT, "core size rlimit set to %ld",
              nfs_param.core_param.core_dump_size);
      ulimit_data.rlim_cur = nfs_param.core_param.core_dump_size;
      ulimit_data.rlim_max = nfs_param.core_param.core_dump_size;

      if(setrlimit(RLIMIT_CORE, &ulimit_data) != 0)
        {
          LogCrit(COMPONENT_INIT,
                  "Impossible to set RLIMIT_CORE to %ld, error %s(%d)",
                  nfs_param.core_param.core_dump_size,
                  strerror(errno), errno);
        }
    }
  else
    {
      if(getrlimit(RLIMIT_CORE, &ulimit_data) != 0)
        {
          LogCrit(COMPONENT_INIT,
                  "Impossible to read RLIMIT_CORE, error %s(%d)",
                  strerror(errno), errno);
        }
      else
        {
          LogInfo(COMPONENT_INIT, "core size rlimit is %ld",
                  ulimit_data.rlim_cur);
        }
    }

  /* Print the worker parameters in log */
  Print_param_worker_in_log(&(nfs_param.worker_param));

  {
    /* Set the write verifiers */
    union
    {
      verifier4  NFS4_write_verifier;  /* NFS V4 write verifier */
      writeverf3 NFS3_write_verifier; /* NFS V3 write verifier */
      uint64_t   epoch;
    } build_verifier;

    build_verifier.epoch = (uint64_t) ServerEpoch;

    memcpy(NFS3_write_verifier, build_verifier.NFS3_write_verifier, sizeof(NFS3_write_verifier));
    memcpy(NFS4_write_verifier, build_verifier.NFS4_write_verifier, sizeof(NFS4_write_verifier));
  }
  /* Initialize all layers and service threads */
  nfs_Init(p_start_info);

  /* Spawns service threads */
  nfs_Start_threads();

#ifdef _USE_NLM
  /* NSM Unmonitor all */
  nsm_unmonitor_all();
#endif

#ifdef _USE_NFSIDMAP
  pthread_mutex_init(&idmap_conf_mtx, NULL);
#endif

  /* Populate the ID_MAPPER file with mapping file if needed */
  if(nfs_param.uidmap_cache_param.mapfile[0] == '\0')
    {
      LogDebug(COMPONENT_INIT, "No Uid Map file is used");
    }
  else
    {
      LogDebug(COMPONENT_INIT, "Populating UID_MAPPER with file %s",
               nfs_param.uidmap_cache_param.mapfile);
      if(idmap_populate(nfs_param.uidmap_cache_param.mapfile, UIDMAP_TYPE) !=
         ID_MAPPER_SUCCESS)
        LogEvent(COMPONENT_INIT, "UID_MAPPER was NOT populated");
    }

  if(nfs_param.gidmap_cache_param.mapfile[0] == '\0')
    {
      LogDebug(COMPONENT_INIT, "No Gid Map file is used");
    }
  else
    {
      LogDebug(COMPONENT_INIT, "Populating GID_MAPPER with file %s",
               nfs_param.uidmap_cache_param.mapfile);
      if(idmap_populate(nfs_param.gidmap_cache_param.mapfile, GIDMAP_TYPE) !=
         ID_MAPPER_SUCCESS)
        LogEvent(COMPONENT_INIT, "GID_MAPPER was NOT populated");
    }

  if(nfs_param.ip_name_param.mapfile[0] == '\0')
    {
      LogDebug(COMPONENT_INIT, "No Hosts Map file is used");
    }
  else
    {
      LogDebug(COMPONENT_INIT, "Populating IP_NAME with file %s",
               nfs_param.ip_name_param.mapfile);
      if(nfs_ip_name_populate(nfs_param.ip_name_param.mapfile) != IP_NAME_SUCCESS)
        LogEvent(COMPONENT_INIT, "IP_NAME was NOT populated");
    }

  /* Wait for the threads to complete their init step */
  if(wait_for_threads_to_awaken() != PAUSE_OK)
    {
      /* Not quite sure what to do here... */
    }
  else
    {
      LogEvent(COMPONENT_INIT,
               "-------------------------------------------------");
      LogEvent(COMPONENT_INIT,
               "             NFS SERVER INITIALIZED");
      LogEvent(COMPONENT_INIT,
               "-------------------------------------------------");

      in_grace = nfs_in_grace();
    }

  /* Wait for dispatcher to exit */
  LogDebug(COMPONENT_THREAD,
           "Wait for sigmgr thread to exit");
  pthread_join(sigmgr_thrid, NULL);

  /* Regular exit */
  LogEvent(COMPONENT_MAIN,
           "NFS EXIT: regular exit");

  /* if not in grace period, clean up the old state directory */
  if(!nfs_in_grace())
    nfs4_clean_old_recov_dir(v4_old_dir);

  Cleanup();

  /* let main return 0 to exit */
}                               /* nfs_start */
