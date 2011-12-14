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
 * \file    nfs_init.c
 * \author  $Author: leibovic $
 * \brief   The file that contain most of the init routines
 *
 * nfs_init.c : The file that contain most of the init routines.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "rpc.h"
#include "nfs_init.h"
#include "stuff_alloc.h"
#include "log_macros.h"
#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nlm4.h"
#include "rquota.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "err_cache_inode.h"
#include "cache_content.h"
#include "err_cache_content.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"
#include "SemN.h"
#include "external_tools.h"
#include "nfs4_acls.h"
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
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
time_t ServerBootTime = 0;
nfs_worker_data_t *workers_data = NULL;
verifier4 NFS4_write_verifier;  /* NFS V4 write verifier */
writeverf3 NFS3_write_verifier; /* NFS V3 write verifier */

hash_table_t *ht_ip_stats[NB_MAX_WORKER_THREAD];
nfs_start_info_t nfs_start_info;

pthread_t worker_thrid[NB_MAX_WORKER_THREAD];

pthread_t flusher_thrid[NB_MAX_FLUSHER_THREAD];
nfs_flush_thread_data_t flush_info[NB_MAX_FLUSHER_THREAD];

pthread_t rpc_dispatcher_thrid;
pthread_t stat_thrid;
pthread_t stat_exporter_thrid;
pthread_t admin_thrid;
pthread_t fcc_gc_thrid;
pthread_t sigmgr_thrid;
nfs_tcb_t gccb;

#ifdef _USE_9P
pthread_t _9p_dispatcher_thrid;
#endif

#ifdef _USE_UPCALL_SIMULATOR
pthread_t upcall_simulator_thrid;
#endif

char config_path[MAXPATHLEN];

/**
 *
 * This thread is in charge of signal management 
 *
 * @param (unused)
 * @return (never returns : never ending loop)
 *
 */
void *sigmgr_thread( void * arg )
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
      if(sigwait(&signals_to_catch, &signal_caught) != 0)
        {
          LogFullDebug(COMPONENT_THREAD,
                       "sigwait exited with error");
          continue;
        }
      if(signal_caught == SIGHUP)
        {
          LogEvent(COMPONENT_MAIN,
                   "SIGHUP_HANDLER: Received SIGHUP.... initiating export list reload");
          admin_replace_exports();
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

#ifdef _USE_MFSL
  st = MFSL_terminate();

  if(FSAL_IS_ERROR(st))
    LogCrit(COMPONENT_MAIN, "NFS EXIT: ERROR %d.%d while synchonizing MFSL",
            st.major, st.minor);
#endif

  st = FSAL_terminate();

  if(FSAL_IS_ERROR(st))
    LogCrit(COMPONENT_MAIN, "NFS EXIT: ERROR %d.%d while synchonizing FSAL",
            st.major, st.minor);

  LogDebug(COMPONENT_THREAD, "sigmgr thread exiting");

  /* Might as well exit - no need for this thread any more */
  return NULL;
}                    /* sigmgr_thread */

/**
 * nfs_prereq_init:
 * Initialize NFSd prerequisites: memory management, logging, ...
 */
void nfs_prereq_init(char *program_name, char *host_name, int debug_level, char *log_path)
{
  /* Initialize logging */
  SetNamePgm(program_name);
  SetNameFunction("main");
  SetNameHost(host_name);
  InitLogging();
  if (log_path[0] != '\0')
    SetDefaultLogging(log_path);

  if (debug_level >= 0)
    SetLogLevel(debug_level);

  /* Register error families */
  AddFamilyError(ERR_POSIX, "POSIX Errors", tab_systeme_status);
  AddFamilyError(ERR_LRU, "LRU related Errors", tab_errctx_LRU);
  AddFamilyError(ERR_HASHTABLE, "HashTable related Errors", tab_errctx_hash);
  AddFamilyError(ERR_FSAL, "FSAL related Errors", tab_errstatus_FSAL);
  AddFamilyError(ERR_CACHE_INODE, "Cache Inode related Errors",
                 tab_errstatus_cache_inode);
  AddFamilyError(ERR_CACHE_CONTENT, "Cache Content related Errors",
                 tab_errstatus_cache_content);

#ifndef _NO_BUDDY_SYSTEM

  /* Initilize memory management for this thread */
  if(BuddyInit(NULL) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogFatal(COMPONENT_INIT, "Memory manager could not be initialized");
    }
  LogDebug(COMPONENT_INIT, "Memory manager successfully initialized");

#endif
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
  printf("\tNb_MaxConcurrentGC = %u ; \n", nfs_param.core_param.nb_max_concurrent_gc);
  printf("\tDupReq_Expiration = %lu ; \n", nfs_param.core_param.expiration_dupreq);
  printf("\tCore_Dump_Size = %ld ; \n", nfs_param.core_param.core_dump_size);
  printf("\tNb_Max_Fd = %d ; \n", nfs_param.core_param.nb_max_fd);
  printf("\tStats_File_Path = %s ; \n", nfs_param.core_param.stats_file_path);
  printf("\tStats_Update_Delay = %d ; \n", nfs_param.core_param.stats_update_delay);
  printf("\tLong_Processing_Threshold = %d ; \n", nfs_param.core_param.long_processing_threshold);
  printf("\tTCP_Fridge_Expiration_Delay = %d ; \n", nfs_param.core_param.tcp_fridge_expiration_delay);
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
#ifdef _USE_SHARED_FSAL
  unsigned int i = 0 ;
  unsigned int saved_fsalid = 0 ;
  unsigned int fsalid = 0 ;
#endif

  /* Core parameters */
  nfs_param.core_param.nb_worker = NB_WORKER_THREAD_DEFAULT;
  nfs_param.core_param.nb_call_before_queue_avg = NB_REQUEST_BEFORE_QUEUE_AVG;
  nfs_param.core_param.nb_max_concurrent_gc = NB_MAX_CONCURRENT_GC;
  nfs_param.core_param.expiration_dupreq = DUPREQ_EXPIRATION;
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
#ifdef _USE_QUOTA
  nfs_param.core_param.program[P_RQUOTA] = RQUOTAPROG;
  nfs_param.core_param.port[P_RQUOTA] = RQUOTA_PORT;
#endif
  nfs_param.core_param.drop_io_errors = TRUE;
  nfs_param.core_param.drop_inval_errors = FALSE;
  nfs_param.core_param.drop_delay_errors = TRUE;
  nfs_param.core_param.core_dump_size = 0;
  nfs_param.core_param.nb_max_fd = -1;       /* Use OS's default */
  nfs_param.core_param.stats_update_delay = 60;
  nfs_param.core_param.long_processing_threshold = 10; /* seconds */
  nfs_param.core_param.tcp_fridge_expiration_delay = -1;
/* only NFSv4 is supported for the FSAL_PROXY */
#if ! defined( _USE_PROXY ) || defined ( _HANDLE_MAPPING )
  nfs_param.core_param.core_options = CORE_OPTION_NFSV3 | CORE_OPTION_NFSV4;
#else
  nfs_param.core_param.core_options = CORE_OPTION_NFSV4;
#endif                          /* _USE_PROXY */

  nfs_param.core_param.use_nfs_commit = FALSE;
  strncpy(nfs_param.core_param.stats_file_path, "/tmp/ganesha.stat", MAXPATHLEN);
  nfs_param.core_param.dump_stats_per_client = 0;
  strncpy(nfs_param.core_param.stats_per_client_directory, "/tmp", MAXPATHLEN);

  nfs_param.core_param.max_send_buffer_size = NFS_DEFAULT_SEND_BUFFER_SIZE;
  nfs_param.core_param.max_recv_buffer_size = NFS_DEFAULT_RECV_BUFFER_SIZE;

#ifdef _USE_NLM
  nfs_param.core_param.nsm_use_caller_name = FALSE;
#endif

  /* Worker parameters : LRU */
  nfs_param.worker_param.lru_param.nb_entry_prealloc = NB_PREALLOC_LRU_WORKER;
  nfs_param.worker_param.lru_param.clean_entry = clean_pending_request;
  nfs_param.worker_param.lru_param.entry_to_str = print_pending_request;

  /* Worker parameters : LRU dupreq */
  nfs_param.worker_param.lru_dupreq.nb_entry_prealloc = NB_PREALLOC_LRU_DUPREQ;
  nfs_param.worker_param.lru_dupreq.clean_entry = clean_entry_dupreq;
  nfs_param.worker_param.lru_dupreq.entry_to_str = print_entry_dupreq;

  /* Worker parameters : GC */
  nfs_param.worker_param.nb_pending_prealloc = NB_MAX_PENDING_REQUEST;
  nfs_param.worker_param.nb_before_gc = NB_REQUEST_BEFORE_GC;
  nfs_param.worker_param.nb_dupreq_prealloc = NB_PREALLOC_HASH_DUPREQ;
  nfs_param.worker_param.nb_dupreq_before_gc = NB_PREALLOC_GC_DUPREQ;

  /* Workers parameters : IP/Name values pool prealloc */
  nfs_param.worker_param.nb_ip_stats_prealloc = 20;

  /* Workers parameters : Client id pool prealloc */
  nfs_param.worker_param.nb_client_id_prealloc = 20;

#ifdef _HAVE_GSSAPI
  /* krb5 parameter */
  strncpy(nfs_param.krb5_param.principal, DEFAULT_NFS_PRINCIPAL, sizeof(nfs_param.krb5_param.principal));
  strncpy(nfs_param.krb5_param.keytab, DEFAULT_NFS_KEYTAB, sizeof(nfs_param.krb5_param.keytab));
  nfs_param.krb5_param.active_krb5 = TRUE;
  nfs_param.krb5_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.krb5_param.hash_param.alphabet_length = 10;      /* Not used for UID_MAPPER */
  nfs_param.krb5_param.hash_param.nb_node_prealloc = NB_PREALLOC_ID_MAPPER;
  nfs_param.krb5_param.hash_param.hash_func_key = gss_ctx_hash_func;
  nfs_param.krb5_param.hash_param.hash_func_rbt = gss_ctx_rbt_hash_func;
  nfs_param.krb5_param.hash_param.hash_func_both = NULL ; /* BUGAZOMEU */
  nfs_param.krb5_param.hash_param.compare_key = compare_gss_ctx;
  nfs_param.krb5_param.hash_param.key_to_str = display_gss_ctx;
  nfs_param.krb5_param.hash_param.val_to_str = display_gss_svc_data;
  nfs_param.krb5_param.hash_param.name = "KRB5 ID Mapper";
#endif

  /* NFSv4 parameter */
  nfs_param.nfsv4_param.lease_lifetime = NFS4_LEASE_LIFETIME;
  nfs_param.nfsv4_param.fh_expire = FALSE;
  nfs_param.nfsv4_param.returns_err_fh_expired = TRUE;
  nfs_param.nfsv4_param.use_open_confirm = TRUE;
  nfs_param.nfsv4_param.return_bad_stateid = TRUE;
  strncpy(nfs_param.nfsv4_param.domainname, DEFAULT_DOMAIN, MAXNAMLEN);
  strncpy(nfs_param.nfsv4_param.idmapconf, DEFAULT_IDMAPCONF, MAXPATHLEN);

  /* Worker parameters : dupreq hash table */
  nfs_param.dupreq_param.hash_param.index_size = PRIME_DUPREQ;
  nfs_param.dupreq_param.hash_param.alphabet_length = 10;    /* Xid is a numerical decimal value */
  nfs_param.dupreq_param.hash_param.nb_node_prealloc = NB_PREALLOC_HASH_DUPREQ;
  nfs_param.dupreq_param.hash_param.hash_func_key = dupreq_value_hash_func;
  nfs_param.dupreq_param.hash_param.hash_func_rbt = dupreq_rbt_hash_func;
  nfs_param.dupreq_param.hash_param.compare_key = compare_req;
  nfs_param.dupreq_param.hash_param.key_to_str = display_req_key;
  nfs_param.dupreq_param.hash_param.val_to_str = display_req_val;
  nfs_param.dupreq_param.hash_param.name = "Duplicate Request Cache";

  /*  Worker parameters : IP/name hash table */
  nfs_param.ip_name_param.hash_param.index_size = PRIME_IP_NAME;
  nfs_param.ip_name_param.hash_param.alphabet_length = 10;   /* ipaddr is a numerical decimal value */
  nfs_param.ip_name_param.hash_param.nb_node_prealloc = NB_PREALLOC_HASH_IP_NAME;
  nfs_param.ip_name_param.hash_param.hash_func_key = ip_name_value_hash_func;
  nfs_param.ip_name_param.hash_param.hash_func_rbt = ip_name_rbt_hash_func;
  nfs_param.ip_name_param.hash_param.compare_key = compare_ip_name;
  nfs_param.ip_name_param.hash_param.key_to_str = display_ip_name_key;
  nfs_param.ip_name_param.hash_param.val_to_str = display_ip_name_val;
  nfs_param.ip_name_param.hash_param.name = "IP Name";
  nfs_param.ip_name_param.expiration_time = IP_NAME_EXPIRATION;
  strncpy(nfs_param.ip_name_param.mapfile, "", MAXPATHLEN);

  /*  Worker parameters : UID_MAPPER hash table */
  nfs_param.uidmap_cache_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.uidmap_cache_param.hash_param.alphabet_length = 10;      /* Not used for UID_MAPPER */
  nfs_param.uidmap_cache_param.hash_param.nb_node_prealloc = NB_PREALLOC_ID_MAPPER;
  nfs_param.uidmap_cache_param.hash_param.hash_func_key = idmapper_value_hash_func;
  nfs_param.uidmap_cache_param.hash_param.hash_func_rbt = idmapper_rbt_hash_func;
  nfs_param.uidmap_cache_param.hash_param.compare_key = compare_idmapper;
  nfs_param.uidmap_cache_param.hash_param.key_to_str = display_idmapper_key;
  nfs_param.uidmap_cache_param.hash_param.val_to_str = display_idmapper_val;
  nfs_param.uidmap_cache_param.hash_param.name = "UID Map Cache";
  strncpy(nfs_param.uidmap_cache_param.mapfile, "", MAXPATHLEN);

  /*  Worker parameters : UNAME_MAPPER hash table */
  nfs_param.unamemap_cache_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.unamemap_cache_param.hash_param.alphabet_length = 10;    /* Not used for UID_MAPPER */
  nfs_param.unamemap_cache_param.hash_param.nb_node_prealloc = NB_PREALLOC_ID_MAPPER;
  nfs_param.unamemap_cache_param.hash_param.hash_func_key = namemapper_value_hash_func;
  nfs_param.unamemap_cache_param.hash_param.hash_func_rbt = namemapper_rbt_hash_func;
  nfs_param.unamemap_cache_param.hash_param.compare_key = compare_namemapper;
  nfs_param.unamemap_cache_param.hash_param.key_to_str = display_idmapper_val;
  nfs_param.unamemap_cache_param.hash_param.val_to_str = display_idmapper_key;
  nfs_param.unamemap_cache_param.hash_param.name = "UNAME Map Cache";
  strncpy(nfs_param.unamemap_cache_param.mapfile, "", MAXPATHLEN);

  /*  Worker parameters : GID_MAPPER hash table */
  nfs_param.gidmap_cache_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.gidmap_cache_param.hash_param.alphabet_length = 10;      /* Not used for UID_MAPPER */
  nfs_param.gidmap_cache_param.hash_param.nb_node_prealloc = NB_PREALLOC_ID_MAPPER;
  nfs_param.gidmap_cache_param.hash_param.hash_func_key = idmapper_value_hash_func;
  nfs_param.gidmap_cache_param.hash_param.hash_func_rbt = idmapper_rbt_hash_func;
  nfs_param.gidmap_cache_param.hash_param.compare_key = compare_idmapper;
  nfs_param.gidmap_cache_param.hash_param.key_to_str = display_idmapper_key;
  nfs_param.gidmap_cache_param.hash_param.val_to_str = display_idmapper_val;
  nfs_param.gidmap_cache_param.hash_param.name = "GID Map Cache";
  strncpy(nfs_param.gidmap_cache_param.mapfile, "", MAXPATHLEN);

  /*  Worker parameters : UID->GID  hash table (for RPCSEC_GSS) */
  nfs_param.uidgidmap_cache_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.uidgidmap_cache_param.hash_param.alphabet_length = 10;   /* Not used for UID_MAPPER */
  nfs_param.uidgidmap_cache_param.hash_param.nb_node_prealloc = NB_PREALLOC_ID_MAPPER;
  nfs_param.uidgidmap_cache_param.hash_param.hash_func_key =
      namemapper_value_hash_func;
  nfs_param.uidgidmap_cache_param.hash_param.hash_func_rbt = namemapper_rbt_hash_func;
  nfs_param.uidgidmap_cache_param.hash_param.compare_key = compare_namemapper;
  nfs_param.uidgidmap_cache_param.hash_param.key_to_str = display_idmapper_key;
  nfs_param.uidgidmap_cache_param.hash_param.val_to_str = display_idmapper_key;
  nfs_param.uidgidmap_cache_param.hash_param.name = "UID->GID Map Cache";

  /*  Worker parameters : GNAME_MAPPER hash table */
  nfs_param.gnamemap_cache_param.hash_param.index_size = PRIME_ID_MAPPER;
  nfs_param.gnamemap_cache_param.hash_param.alphabet_length = 10;    /* Not used for UID_MAPPER */
  nfs_param.gnamemap_cache_param.hash_param.nb_node_prealloc = NB_PREALLOC_ID_MAPPER;
  nfs_param.gnamemap_cache_param.hash_param.hash_func_key = namemapper_value_hash_func;
  nfs_param.gnamemap_cache_param.hash_param.hash_func_rbt = namemapper_rbt_hash_func;
  nfs_param.gnamemap_cache_param.hash_param.compare_key = compare_namemapper;
  nfs_param.gnamemap_cache_param.hash_param.key_to_str = display_idmapper_val;
  nfs_param.gnamemap_cache_param.hash_param.val_to_str = display_idmapper_key;
  nfs_param.gnamemap_cache_param.hash_param.name = "GNAME Map Cache";
  strncpy(nfs_param.gnamemap_cache_param.mapfile, "", MAXPATHLEN);

  /*  Worker parameters : IP/stats hash table */
  nfs_param.ip_stats_param.hash_param.index_size = PRIME_IP_STATS;
  nfs_param.ip_stats_param.hash_param.alphabet_length = 10;  /* ipaddr is a numerical decimal value */
  nfs_param.ip_stats_param.hash_param.nb_node_prealloc = NB_PREALLOC_HASH_IP_STATS;
  nfs_param.ip_stats_param.hash_param.hash_func_key = ip_stats_value_hash_func;
  nfs_param.ip_stats_param.hash_param.hash_func_rbt = ip_stats_rbt_hash_func;
  nfs_param.ip_stats_param.hash_param.compare_key = compare_ip_stats;
  nfs_param.ip_stats_param.hash_param.key_to_str = display_ip_stats_key;
  nfs_param.ip_stats_param.hash_param.val_to_str = display_ip_stats_val;
  nfs_param.ip_stats_param.hash_param.name = "IP Stats";

  /*  Worker parameters : NFSv4 Client id table */
  nfs_param.client_id_param.hash_param.index_size = PRIME_CLIENT_ID;
  nfs_param.client_id_param.hash_param.alphabet_length = 10; /* ipaddr is a numerical decimal value */
  nfs_param.client_id_param.hash_param.nb_node_prealloc = NB_PREALLOC_HASH_CLIENT_ID;
  nfs_param.client_id_param.hash_param.hash_func_key = client_id_value_hash_func;
  nfs_param.client_id_param.hash_param.hash_func_rbt = client_id_rbt_hash_func;
  nfs_param.client_id_param.hash_param.hash_func_both = NULL ;
  nfs_param.client_id_param.hash_param.compare_key = compare_client_id;
  nfs_param.client_id_param.hash_param.key_to_str = display_client_id;
  nfs_param.client_id_param.hash_param.val_to_str = display_client_id_val;
  nfs_param.client_id_param.hash_param.name = "Client ID";

  /* NFSv4 Client id reverse table */
  nfs_param.client_id_param.hash_param_reverse.index_size = PRIME_CLIENT_ID;
  nfs_param.client_id_param.hash_param_reverse.alphabet_length = 10; /* ipaddr is a numerical decimal value */
  nfs_param.client_id_param.hash_param_reverse.nb_node_prealloc =
      NB_PREALLOC_HASH_CLIENT_ID;
  nfs_param.client_id_param.hash_param_reverse.hash_func_key = NULL ;
  nfs_param.client_id_param.hash_param_reverse.hash_func_rbt = NULL ;
  nfs_param.client_id_param.hash_param_reverse.hash_func_both =
	client_id_value_both_reverse ;
  nfs_param.client_id_param.hash_param_reverse.compare_key = compare_client_id_reverse;
  nfs_param.client_id_param.hash_param_reverse.key_to_str = display_client_id_reverse;
  nfs_param.client_id_param.hash_param_reverse.val_to_str = display_client_id_val;
  nfs_param.client_id_param.hash_param_reverse.name = "Client ID Reverse";

  /* NFSv4 State Id hash */
  nfs_param.state_id_param.hash_param.index_size = PRIME_STATE_ID;
  nfs_param.state_id_param.hash_param.alphabet_length = 10;  /* ipaddr is a numerical decimal value */
  nfs_param.state_id_param.hash_param.nb_node_prealloc = NB_PREALLOC_HASH_STATE_ID;
  nfs_param.state_id_param.hash_param.hash_func_key = NULL ;
  nfs_param.state_id_param.hash_param.hash_func_rbt = NULL ;
  nfs_param.state_id_param.hash_param.hash_func_both = state_id_hash_both ;
  nfs_param.state_id_param.hash_param.compare_key = compare_state_id;
  nfs_param.state_id_param.hash_param.key_to_str = display_state_id_key;
  nfs_param.state_id_param.hash_param.val_to_str = display_state_id_val;
  nfs_param.state_id_param.hash_param.name = "State ID";

#ifdef _USE_NFS4_1
  /* NFSv4 State Id hash */
  nfs_param.session_id_param.hash_param.index_size = PRIME_STATE_ID;
  nfs_param.session_id_param.hash_param.alphabet_length = 10;        /* ipaddr is a numerical decimal value */
  nfs_param.session_id_param.hash_param.nb_node_prealloc = NB_PREALLOC_HASH_STATE_ID;
  nfs_param.session_id_param.hash_param.hash_func_key = session_id_value_hash_func;
  nfs_param.session_id_param.hash_param.hash_func_rbt = session_id_rbt_hash_func;
  nfs_param.session_id_param.hash_param.compare_key = compare_session_id;
  nfs_param.session_id_param.hash_param.key_to_str = display_session_id_key;
  nfs_param.session_id_param.hash_param.val_to_str = display_session_id_val;
  nfs_param.session_id_param.hash_param.name = "Session ID";

#ifdef _USE_PNFS
  /* pNFS parameters */
  nfs_param.pnfs_param.layoutfile.stripe_width = 1;
  nfs_param.pnfs_param.layoutfile.stripe_size = 8192;

  nfs_param.pnfs_param.layoutfile.ds_param[0].ipaddr = htonl(0x7F000001);
  strncpy(nfs_param.pnfs_param.layoutfile.ds_param[0].ipaddr_ascii, "127.0.0.1",
          MAXNAMLEN);
  nfs_param.pnfs_param.layoutfile.ds_param[0].ipport = htons(2049);
  nfs_param.pnfs_param.layoutfile.ds_param[0].prognum = 100003;
  nfs_param.pnfs_param.layoutfile.ds_param[0].id = 1;
  strncpy(nfs_param.pnfs_param.layoutfile.ds_param[0].rootpath, "/", MAXPATHLEN);

  nfs_param.pnfs_param.layoutfile.ds_param[1].ipaddr = htonl(0x7F000001);
  nfs_param.pnfs_param.layoutfile.ds_param[1].ipport = htons(2049);
  nfs_param.pnfs_param.layoutfile.ds_param[1].prognum = 100003;
  nfs_param.pnfs_param.layoutfile.ds_param[1].id = 2;
  strncpy(nfs_param.pnfs_param.layoutfile.ds_param[1].rootpath, "/", MAXPATHLEN);

#endif                          /* _USE_PNFS */

#endif                          /* _USE_NFS4_1 */

  /* NFSv4 Open Owner hash */
  nfs_param.nfs4_owner_param.hash_param.index_size = PRIME_STATE_ID;
  nfs_param.nfs4_owner_param.hash_param.alphabet_length = 10;        /* ipaddr is a numerical decimal value */
  nfs_param.nfs4_owner_param.hash_param.nb_node_prealloc = NB_PREALLOC_HASH_STATE_ID;
  nfs_param.nfs4_owner_param.hash_param.hash_func_key = nfs4_owner_value_hash_func;
  nfs_param.nfs4_owner_param.hash_param.hash_func_rbt = nfs4_owner_rbt_hash_func;
  nfs_param.nfs4_owner_param.hash_param.compare_key = compare_nfs4_owner_key;
  nfs_param.nfs4_owner_param.hash_param.key_to_str = display_nfs4_owner_key;
  nfs_param.nfs4_owner_param.hash_param.val_to_str = display_nfs4_owner_val;
  nfs_param.nfs4_owner_param.hash_param.name = "NFS4 Owner";

#ifdef _USE_NLM
  /* NSM Client hash */
  nfs_param.nsm_client_hash_param.index_size = PRIME_STATE_ID;
  nfs_param.nsm_client_hash_param.alphabet_length = 10;        /* ipaddr is a numerical decimal value */
  nfs_param.nsm_client_hash_param.nb_node_prealloc = NB_PREALLOC_HASH_STATE_ID;
  nfs_param.nsm_client_hash_param.hash_func_key = nsm_client_value_hash_func;
  nfs_param.nsm_client_hash_param.hash_func_rbt = nsm_client_rbt_hash_func;
  nfs_param.nsm_client_hash_param.compare_key = compare_nsm_client_key;
  nfs_param.nsm_client_hash_param.key_to_str = display_nsm_client_key;
  nfs_param.nsm_client_hash_param.val_to_str = display_nsm_client_val;
  nfs_param.nsm_client_hash_param.name = "NSM Client";

  /* NLM Client hash */
  nfs_param.nlm_client_hash_param.index_size = PRIME_STATE_ID;
  nfs_param.nlm_client_hash_param.alphabet_length = 10;        /* ipaddr is a numerical decimal value */
  nfs_param.nlm_client_hash_param.nb_node_prealloc = NB_PREALLOC_HASH_STATE_ID;
  nfs_param.nlm_client_hash_param.hash_func_key = nlm_client_value_hash_func;
  nfs_param.nlm_client_hash_param.hash_func_rbt = nlm_client_rbt_hash_func;
  nfs_param.nlm_client_hash_param.compare_key = compare_nlm_client_key;
  nfs_param.nlm_client_hash_param.key_to_str = display_nlm_client_key;
  nfs_param.nlm_client_hash_param.val_to_str = display_nlm_client_val;
  nfs_param.nlm_client_hash_param.name = "NLM Client";

  /* NLM Owner hash */
  nfs_param.nlm_owner_hash_param.index_size = PRIME_STATE_ID;
  nfs_param.nlm_owner_hash_param.alphabet_length = 10;        /* ipaddr is a numerical decimal value */
  nfs_param.nlm_owner_hash_param.nb_node_prealloc = NB_PREALLOC_HASH_STATE_ID;
  nfs_param.nlm_owner_hash_param.hash_func_key = nlm_owner_value_hash_func;
  nfs_param.nlm_owner_hash_param.hash_func_rbt = nlm_owner_rbt_hash_func;
  nfs_param.nlm_owner_hash_param.compare_key = compare_nlm_owner_key;
  nfs_param.nlm_owner_hash_param.key_to_str = display_nlm_owner_key;
  nfs_param.nlm_owner_hash_param.val_to_str = display_nlm_owner_val;
  nfs_param.nlm_owner_hash_param.name = "NLM Owner";
#endif

  /* Cache inode parameters : hash table */
  nfs_param.cache_layers_param.cache_param.hparam.index_size = PRIME_CACHE_INODE;
  nfs_param.cache_layers_param.cache_param.hparam.alphabet_length = 10;      /* Buffer seen as a decimal polynom */
  nfs_param.cache_layers_param.cache_param.hparam.nb_node_prealloc =
      NB_PREALLOC_HASH_CACHE_INODE;
  nfs_param.cache_layers_param.cache_param.hparam.hash_func_key = NULL ;
  nfs_param.cache_layers_param.cache_param.hparam.hash_func_rbt = NULL ;
  nfs_param.cache_layers_param.cache_param.hparam.hash_func_both =
      cache_inode_fsal_rbt_both;
  nfs_param.cache_layers_param.cache_param.hparam.compare_key =
      cache_inode_compare_key_fsal;
  nfs_param.cache_layers_param.cache_param.hparam.key_to_str = display_cache;
  nfs_param.cache_layers_param.cache_param.hparam.val_to_str = display_cache;
  nfs_param.cache_layers_param.cache_param.hparam.name = "Cache Inode";

#ifdef _USE_NLM
  /* Cache inode parameters : cookie hash table */
  nfs_param.cache_layers_param.cache_param.cookie_param.index_size = PRIME_STATE_ID;
  nfs_param.cache_layers_param.cache_param.cookie_param.alphabet_length = 10;      /* Buffer seen as a decimal polynom */
  nfs_param.cache_layers_param.cache_param.cookie_param.nb_node_prealloc = NB_PREALLOC_HASH_STATE_ID;
  nfs_param.cache_layers_param.cache_param.cookie_param.hash_func_key = lock_cookie_value_hash_func ;
  nfs_param.cache_layers_param.cache_param.cookie_param.hash_func_rbt = lock_cookie_rbt_hash_func ;
  nfs_param.cache_layers_param.cache_param.cookie_param.compare_key = compare_lock_cookie_key;
  nfs_param.cache_layers_param.cache_param.cookie_param.key_to_str = display_lock_cookie_key;
  nfs_param.cache_layers_param.cache_param.cookie_param.val_to_str = display_lock_cookie_val;
  nfs_param.cache_layers_param.cache_param.cookie_param.name = "Lock Cookie";
#endif

  /* Cache inode parameters : Garbage collection policy */
  nfs_param.cache_layers_param.gcpol.file_expiration_delay = -1;     /* No gc */
  nfs_param.cache_layers_param.gcpol.directory_expiration_delay = -1;        /* No gc */
  nfs_param.cache_layers_param.gcpol.hwmark_nb_entries = 10000;
  nfs_param.cache_layers_param.gcpol.lwmark_nb_entries = 10000;
  nfs_param.cache_layers_param.gcpol.run_interval = 3600;    /* 1h */
  nfs_param.cache_layers_param.gcpol.nb_call_before_gc = 1000;

  /* Cache inode client parameters */
  nfs_param.cache_layers_param.cache_inode_client_param.lru_param.nb_entry_prealloc =
      2048;
  nfs_param.cache_layers_param.cache_inode_client_param.lru_param.entry_to_str =
      lru_inode_entry_to_str;
  nfs_param.cache_layers_param.cache_inode_client_param.lru_param.clean_entry =
      lru_inode_clean_entry;
  nfs_param.cache_layers_param.cache_inode_client_param.nb_prealloc_entry = 1024;
  nfs_param.cache_layers_param.cache_inode_client_param.nb_pre_parent = 2048;
  nfs_param.cache_layers_param.cache_inode_client_param.nb_pre_state_v4 = 512;
  nfs_param.cache_layers_param.cache_inode_client_param.grace_period_attr   = 0;
  nfs_param.cache_layers_param.cache_inode_client_param.grace_period_link   = 0;
  nfs_param.cache_layers_param.cache_inode_client_param.grace_period_dirent = 0;
  nfs_param.cache_layers_param.cache_inode_client_param.expire_type_attr    = CACHE_INODE_EXPIRE_NEVER;
  nfs_param.cache_layers_param.cache_inode_client_param.expire_type_link    = CACHE_INODE_EXPIRE_NEVER;
  nfs_param.cache_layers_param.cache_inode_client_param.expire_type_dirent  = CACHE_INODE_EXPIRE_NEVER;
  nfs_param.cache_layers_param.cache_inode_client_param.use_test_access = 1;
  nfs_param.cache_layers_param.cache_inode_client_param.getattr_dir_invalidation = 0;
#ifdef _USE_NFS4_ACL
  nfs_param.cache_layers_param.cache_inode_client_param.attrmask = FSAL_ATTR_MASK_V4;
#else
  nfs_param.cache_layers_param.cache_inode_client_param.attrmask = FSAL_ATTR_MASK_V2_V3;
#endif
  nfs_param.cache_layers_param.cache_inode_client_param.max_fd_per_thread = 20;
  nfs_param.cache_layers_param.cache_inode_client_param.use_cache = 0;
  nfs_param.cache_layers_param.cache_inode_client_param.use_fsal_hash = 1;
  nfs_param.cache_layers_param.cache_inode_client_param.retention = 60;

  /* Data cache client parameters */
  nfs_param.cache_layers_param.cache_content_client_param.nb_prealloc_entry = 128;
  nfs_param.cache_layers_param.cache_content_client_param.flush_force_fsal = 1;
  nfs_param.cache_layers_param.cache_content_client_param.max_fd_per_thread = 20;
  nfs_param.cache_layers_param.cache_content_client_param.use_cache = 0;
  nfs_param.cache_layers_param.cache_content_client_param.retention = 60;

  strcpy(nfs_param.cache_layers_param.cache_content_client_param.cache_dir,
         "/tmp/ganesha.datacache");

  /* Data cache parameters: Garbage collection policy */
  nfs_param.cache_layers_param.dcgcpol.lifetime = -1;        /* No gc */
  nfs_param.cache_layers_param.dcgcpol.hwmark_df = 99;
  nfs_param.cache_layers_param.dcgcpol.lwmark_df = 98;
  nfs_param.cache_layers_param.dcgcpol.run_interval = 3600;  /* 1h */
  nfs_param.cache_layers_param.dcgcpol.nb_call_before_gc = 1000;
  nfs_param.cache_layers_param.dcgcpol.emergency_grace_delay = 3600; /* 1h */

#ifdef _USE_SHARED_FSAL
  saved_fsalid = FSAL_GetId() ;
  for( i = 0 ; i < nfs_param.nb_loaded_fsal ; i++ )
   {
     fsalid =  nfs_param.loaded_fsal[i] ;

     FSAL_SetId( fsalid ) ;

     /* FSAL parameters */
     nfs_param.fsal_param[fsalid].fsal_info.max_fs_calls = 30;  /* No semaphore to access the FSAL */

     FSAL_SetDefault_FSAL_parameter(&nfs_param.fsal_param[fsalid]);
     FSAL_SetDefault_FS_common_parameter(&nfs_param.fsal_param[fsalid]);
     FSAL_SetDefault_FS_specific_parameter(&nfs_param.fsal_param[fsalid]);
   }
  FSAL_SetId( saved_fsalid ) ;
#else
  /* FSAL parameters */
  nfs_param.fsal_param.fsal_info.max_fs_calls = 30;  /* No semaphore to access the FSAL */

  FSAL_SetDefault_FSAL_parameter(&nfs_param.fsal_param);
  FSAL_SetDefault_FS_common_parameter(&nfs_param.fsal_param);
  FSAL_SetDefault_FS_specific_parameter(&nfs_param.fsal_param);
#endif

#ifdef _USE_MFSL
  MFSL_SetDefault_parameter(&nfs_param.mfsl_param);
#endif

  /* Buddy parameters */
#ifndef _NO_BUDDY_SYSTEM
  Buddy_set_default_parameter(&nfs_param.buddy_param_admin);
  Buddy_set_default_parameter(&nfs_param.buddy_param_worker);
  Buddy_set_default_parameter(&nfs_param.buddy_param_tcp_mgr);
#ifdef _USE_FSAL_UP
  Buddy_set_default_parameter(&nfs_param.buddy_param_fsal_up);
#endif /* _USE_FSAL_UP */
#endif /* _NO_BUDDY_SYSTEM */

  nfs_param.pexportlist = NULL;

  /* SNMP ADM parameters */
#ifdef _SNMP_ADM_ACTIVE
  strcpy(nfs_param.extern_param.snmp_adm.snmp_agentx_socket, "");
  nfs_param.extern_param.snmp_adm.product_id = 1;
  strcpy(nfs_param.extern_param.snmp_adm.snmp_log_file, "");

  nfs_param.extern_param.snmp_adm.export_cache_stats = TRUE;
  nfs_param.extern_param.snmp_adm.export_requests_stats = TRUE;
  nfs_param.extern_param.snmp_adm.export_maps_stats = FALSE;
#ifndef _NO_BUDDY_SYSTEM
  nfs_param.extern_param.snmp_adm.export_buddy_stats = TRUE;
#endif
  nfs_param.extern_param.snmp_adm.export_nfs_calls_detail = FALSE;
  nfs_param.extern_param.snmp_adm.export_cache_inode_calls_detail = FALSE;
  nfs_param.extern_param.snmp_adm.export_fsal_calls_detail = FALSE;
#endif
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
  cache_content_status_t cache_content_status;

#ifdef _USE_SHARED_FSAL
  unsigned int i = 0 ;
  unsigned int saved_fsalid = 0 ;
  unsigned int fsalid = 0 ;
#endif

  /* First, parse the configuration file */

  config_struct = config_ParseFile(config_path);

  if(!config_struct)
    {
      LogFatal(COMPONENT_INIT, "Error while parsing %s: %s",
               config_path, config_GetErrorMsg());
    }
#ifndef _NO_BUDDY_SYSTEM

  /* load buddy parameters from conf */

  rc = Buddy_load_parameter_from_conf(config_struct, &nfs_param.buddy_param_worker);

  if(rc == 0)
    LogDebug(COMPONENT_INIT,
             "Worker's Buddy parameters read from config file");
  else if(rc == BUDDY_ERR_ENOENT)
    LogDebug(COMPONENT_INIT,
             "No Buddy parameters found in config file, using default");
  else
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing Buddy parameters");
      return -1;
    }

  rc = Buddy_load_parameter_from_conf(config_struct, &nfs_param.buddy_param_tcp_mgr);
  if(rc == 0)
    LogDebug(COMPONENT_INIT,
             "Tcp Mgr's Buddy parameters read from config file");
  else if(rc == BUDDY_ERR_ENOENT)
    LogDebug(COMPONENT_INIT,
	     "No Buddy parameters found in config file, using default");
  else
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing Buddy parameters");
      return -1;
    }

  /* Set TCP MGR specific field, so that it frees pages as fast as possible */
  nfs_param.buddy_param_tcp_mgr.keep_minimum = 0;
  nfs_param.buddy_param_tcp_mgr.keep_factor = 0;
  nfs_param.buddy_param_tcp_mgr.free_areas = TRUE;

  /* Do not use a too big page size for TCP connection manager */
  nfs_param.buddy_param_tcp_mgr.memory_area_size = 1048576LL ;

#endif

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
        LogCrit(COMPONENT_INIT,
             "No core configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
                 "core configuration read from config file");
    }

#ifdef _USE_SHARED_FSAL
  saved_fsalid = FSAL_GetId() ;
  for( i = 0 ; i < nfs_param.nb_loaded_fsal ; i++ )
   {
     fsalid =  nfs_param.loaded_fsal[i] ;
     FSAL_SetId( fsalid ) ;

     /* Load FSAL configuration from parsed file */
     fsal_status =
        FSAL_load_FSAL_parameter_from_conf(config_struct, &nfs_param.fsal_param[fsalid]);
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
          FSAL_load_FS_common_parameter_from_conf(config_struct, &nfs_param.fsal_param[fsalid]);
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
        FSAL_load_FS_specific_parameter_from_conf(config_struct, &nfs_param.fsal_param[fsalid]);
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
   }
  FSAL_SetId( saved_fsalid ) ;
#else
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
#endif

#ifdef _USE_MFSL
  /* Load FSAL configuration from parsed file */
  fsal_status = MFSL_load_parameter_from_conf(config_struct, &nfs_param.mfsl_param);
  if(FSAL_IS_ERROR(fsal_status))
    {
      if(fsal_status.major == ERR_FSAL_NOENT)
        LogDebug(COMPONENT_INIT,
	         "No MFSL parameters found in config file, using default");
      else
        {
          LogCrit(COMPONENT_INIT, "Error while parsing MFSL parameters");
          LogError(COMPONENT_INIT, ERR_FSAL, fsal_status.major, fsal_status.minor);
          return -1;
        }
    }
  else
    LogDebug(COMPONENT_INIT,
             "MFSL parameters read from config file");
#endif                          /* _USE_MFSL */

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

#ifdef _USE_PNFS
  /* Worker paramters: pNFS specific config */
  if((rc = nfs_read_pnfs_conf(config_struct, &nfs_param.pnfs_param)) < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing pNFS configuration");
      return -1;
    }
  else
    {
      /* No such stanza in configuration file */
      if(rc == 1)
        LogDebug(COMPONENT_INIT,
		 "No pNFS configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
		 "pNFS configuration read from config file");
    }
#endif                          /* _USE_PNFS */

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
                                           &nfs_param.cache_layers_param.
                                           cache_param)) != CACHE_INODE_SUCCESS)
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
                                      &nfs_param.cache_layers_param.gcpol)) !=
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
  if((cache_inode_status = cache_inode_read_conf_client_parameter(config_struct,
                                                                  &nfs_param.
                                                                  cache_layers_param.
                                                                  cache_inode_client_param))
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

  /* Data cache client parameters */
  if((cache_content_status = cache_content_read_conf_client_parameter(config_struct,
                                                                      &nfs_param.
                                                                      cache_layers_param.
                                                                      cache_content_client_param))
     != CACHE_CONTENT_SUCCESS)
    {
      if(cache_content_status == CACHE_CONTENT_NOT_FOUND)
        LogDebug(COMPONENT_INIT,
                 "No Cache Content Client configuration found, using default");
      else
        {
          LogCrit(COMPONENT_INIT,
                  "Error while parsing Cache Content Client configuration");
          return -1;
        }
    }
  else
    LogDebug(COMPONENT_INIT,
             "Cache Content Client configuration read from config file");

  if((cache_content_status =
      cache_content_read_conf_gc_policy(config_struct,
                                        &nfs_param.cache_layers_param.dcgcpol)) !=
     CACHE_CONTENT_SUCCESS)
    {
      if(cache_content_status == CACHE_CONTENT_NOT_FOUND)
        LogDebug(COMPONENT_INIT,
                 "No File Content Garbage Collection Policy configuration found, using default");
      else
        {
          LogCrit(COMPONENT_INIT,
                  "Error while parsing File Content Garbage Collection Policy configuration");
          return -1;
        }
    }
  else
    LogDebug(COMPONENT_INIT,
             "File Content Garbage Collection Policy configuration read from config file");

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
  rc = ReadExports(config_struct, &nfs_param.pexportlist);
  if(rc < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing export entries");
      return -1;
    }
  else if(rc == 0)
    {
      LogCrit(COMPONENT_INIT,
              "No export entries found in configuration file !!!");
#ifndef _USE_FUSE
      return -1;
#endif
    }

  LogEvent(COMPONENT_INIT, "Configuration file successfully parsed");

  /* freeing syntax tree : */

  config_Free(config_struct);

  return 0;

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

  if( 2*nfs_param.core_param.nb_worker  >  nfs_param.cache_layers_param.cache_param.hparam.index_size )
    {
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER: number of workers is too large compared to Cache_Inode's index size, it should be smaller than half of it");
      return 1;
    }


  if(nfs_param.worker_param.nb_before_gc <
     nfs_param.worker_param.lru_param.nb_entry_prealloc / 2)
    {
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER: worker_param.nb_before_gc is too small: %d",
              nfs_param.worker_param.nb_before_gc);
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER: It should be at least half of worker_param.lru_param.nb_entry_prealloc = %d",
              nfs_param.worker_param.lru_param.nb_entry_prealloc);

      return 1;
    }

  if(nfs_param.dupreq_param.hash_param.nb_node_prealloc <
     nfs_param.worker_param.lru_dupreq.nb_entry_prealloc)
    {
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER(dupreq): nb_node_prealloc = %d should be greater than nb_entry_prealloc = %d",
              nfs_param.dupreq_param.hash_param.nb_node_prealloc,
              nfs_param.worker_param.lru_dupreq.nb_entry_prealloc);
      return 1;
    }
#ifdef _USE_MFSL_ASYNC
  if(nfs_param.cache_layers_param.cache_inode_client_param.expire_type_attr != CACHE_INODE_EXPIRE_NEVER)
    {
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER (Cache_Inode): Attr_Expiration_Time should be Never when used with MFSL_ASYNC");
      return 1;
    }

  if(nfs_param.cache_layers_param.cache_inode_client_param.expire_type_dirent != CACHE_INODE_EXPIRE_NEVER)
    {
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER (Cache_Inode): Directory_Expiration_Time should be Never when used with MFSL_ASYNC");
      return 1;
    }

  if(nfs_param.cache_layers_param.cache_inode_client_param.expire_type_link != CACHE_INODE_EXPIRE_NEVER)
    {
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER (Cache_Inode): Symlink_Expiration_Time should be Never when used with MFSL_ASYNC");
      return 1;
    }

  if(nfs_param.cache_layers_param.cache_inode_client_param.getattr_dir_invalidation !=
     0)
    {
      LogCrit(COMPONENT_INIT,
              "BAD PARAMETER (Cache_Inode): Use_Getattr_Directory_Invalidation should be NO when used with MFSL_ASYNC");
      return 1;
    }
#endif                          /* _USE_MFSL_ASYNC */

  return 0;
}

void nfs_reset_stats(void)
{
  unsigned int i = 0;
  unsigned int j = 0;

  for(i = 0; i < nfs_param.core_param.nb_worker; i++)
    {
      workers_data[i].cache_inode_client.stat.nb_gc_lru_active = 0;
      workers_data[i].cache_inode_client.stat.nb_gc_lru_total = 0;
      workers_data[i].cache_inode_client.stat.nb_call_total = 0;

      for(j = 0; j < CACHE_INODE_NB_COMMAND; j++)
        {
          workers_data[i].cache_inode_client.stat.func_stats.nb_success[j] = 0;
          workers_data[i].cache_inode_client.stat.func_stats.nb_call[j] = 0;
          workers_data[i].cache_inode_client.stat.func_stats.nb_err_retryable[j] = 0;
          workers_data[i].cache_inode_client.stat.func_stats.nb_err_unrecover[j] = 0;
        }
    }

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
#ifndef _NO_BUDDY_SYSTEM
      memset(&workers_data[i].stats.buddy_stats, 0, sizeof(buddy_stats_t));
#endif

    }                           /* for( i = 0 ; i < nfs_param.core_param.nb_worker ; i++ ) */

}                               /* void nfs_reset_stats( void ) */

static void nfs_Start_threads(bool_t flush_datacache_mode)
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

  /* Initialisation of Threads's fridge */
  if( fridgethr_init() != 0 )
   {
     LogFatal(COMPONENT_THREAD, "can't run fridgethr_init");
   }

  /* Starting the thread dedicated to signal handling */
  if( ( rc = pthread_create( &sigmgr_thrid, &attr_thr, sigmgr_thread, (void *)NULL ) ) != 0 )
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

#ifdef _USE_NLM
  /* Start NLM threads */
  if(!flush_datacache_mode)
    nlm_startup();
#endif

  /* Starting the rpc dispatcher thread */
  if((rc =
      pthread_create(&rpc_dispatcher_thrid, &attr_thr, rpc_dispatcher_thread,
                     &nfs_param)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create rpc_dispatcher_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD, "rpc dispatcher thread was started successfully");

#ifdef _USE_9P
  /* Starting the 9p dispatcher thread */
  if((rc = pthread_create(&_9p_dispatcher_thrid, &attr_thr, _9p_dispatcher_thread, NULL ) ) != 0 )     
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create  9p dispatcher_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD, "9p dispatcher thread was started successfully");
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
      pthread_create(&stat_thrid, &attr_thr, stats_thread, (void *)workers_data)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create stats_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD, "statistics thread was started successfully");

#ifdef _USE_STAT_EXPORTER

  /* Starting the long processing threshold thread */
  if((rc =
      pthread_create(&stat_thrid, &attr_thr, long_processing_thread, (void *)workers_data)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create long_processing_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD,
           "long processing threshold thread was started successfully");

  /* Starting the stat exporter thread */
  if((rc =
      pthread_create(&stat_exporter_thrid, &attr_thr, stat_exporter_thread, (void *)workers_data)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create stat_exporter_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD,
           "statistics exporter thread was started successfully");

#endif      /*  _USE_STAT_EXPORTER */

  if(nfs_param.cache_layers_param.dcgcpol.run_interval != 0)
    {
      tcb_new(&gccb, "NFS FILE CONTENT GARBAGE COLLECTION Thread"); 
      /* Starting the nfs file content gc thread  */
      if((rc =
          pthread_create(&fcc_gc_thrid, &attr_thr, file_content_gc_thread,
                         (void *)NULL)) != 0)
        {
          LogFatal(COMPONENT_THREAD,
                   "Could not create file_content_gc_thread, error = %d (%s)",
                   errno, strerror(errno));
        }
      LogEvent(COMPONENT_THREAD,
               "file content gc thread was started successfully");
    }

#ifdef _USE_UPCALL_SIMULATOR
  /* Starts the thread that mimics upcalls from the FSAL */
   /* Starting the stats thread */
  if((rc =
      pthread_create(&upcall_simulator_thrid, &attr_thr, upcall_simulator_thread, (void *)workers_data)) != 0)
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create upcall_simulator_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD, "upcall_simulator thread was started successfully");
#endif


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
  hash_table_t *ht = NULL;      /* Cache inode main hash table */

  cache_inode_status_t cache_status;
  state_status_t state_status;
  fsal_status_t fsal_status;
  unsigned int i = 0;
  int rc = 0;
#ifdef _HAVE_GSSAPI
  gss_name_t gss_service_name;
  gss_buffer_desc gss_service_buf;
  OM_uint32 maj_stat, min_stat;
  char GssError[MAXNAMLEN];
#endif
#ifdef _USE_SHARED_FSAL
  unsigned int saved_fsalid = 0 ;
  unsigned int fsalid = 0 ;
#endif

  /* FSAL Initialisation */
#ifdef _USE_SHARED_FSAL
  saved_fsalid = FSAL_GetId() ;
  for( i = 0 ; i < nfs_param.nb_loaded_fsal ; i++ )
   {
     fsalid =  nfs_param.loaded_fsal[i] ;
     FSAL_SetId( fsalid ) ;

     fsal_status = FSAL_Init(&nfs_param.fsal_param[fsalid]);
     if(FSAL_IS_ERROR(fsal_status))
      {
         /* Failed init */
         LogMajor(COMPONENT_INIT, "FSAL library could not be initialized for fsalid %s", FSAL_fsalid2name( fsalid ) );
         exit(1);
       }
     LogInfo(COMPONENT_INIT, "FSAL library successfully initialized for fsalid %s", FSAL_fsalid2name( fsalid ) );
   }
  FSAL_SetId( fsalid ) ;
  LogInfo(COMPONENT_INIT, "All FSAL libraries successfully initialized");
#else
  fsal_status = FSAL_Init(&nfs_param.fsal_param);
  if(FSAL_IS_ERROR(fsal_status))
    {
      /* Failed init */
      LogFatal(COMPONENT_INIT, "FSAL library could not be initialized");
    }
  LogInfo(COMPONENT_INIT, "FSAL library successfully initialized");
#endif

#ifdef _USE_MFSL
  /* MFSL Initialisation */
  fsal_status = MFSL_Init(&nfs_param.mfsl_param);
  if(FSAL_IS_ERROR(fsal_status))
    {
      /* Failed init */
      LogFatal(COMPONENT_INIT, "MFSL library could not be initialized");
    }
  LogInfo(COMPONENT_INIT, "MFSL library  successfully initialized");
#endif

  /* Cache Inode Initialisation */
  if((ht =
      cache_inode_init(nfs_param.cache_layers_param.cache_param, &cache_status)) == NULL)
    {
      LogFatal(COMPONENT_INIT,
               "Cache Inode Layer could not be initialized, status=%s",
               cache_inode_err_str(cache_status));
    }

#ifdef _USE_BLOCKING_LOCKS
  if(state_lock_init(&state_status,
                     nfs_param.cache_layers_param.cache_param.cookie_param)
#else
  if(state_lock_init(&state_status)
#endif
     != STATE_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Cache Inode Layer could not be initialized, status=%s",
               state_err_str(state_status));
    }
  LogInfo(COMPONENT_INIT, "Cache Inode library successfully initialized");
  /* Initialize thread control block */
  tcb_head_init();

  /* Set the cache inode GC policy */
  cache_inode_set_gc_policy(nfs_param.cache_layers_param.gcpol);

  /* Set the cache content GC policy */
  cache_content_set_gc_policy(nfs_param.cache_layers_param.dcgcpol);

  /* If only 'basic' init for having FSAL anc Cache Inode is required, stop init now */
  if(p_start_info->flush_datacache_mode)
    {
      return;
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
      gss_service_buf.value = nfs_param.krb5_param.principal;
      gss_service_buf.length = strlen(nfs_param.krb5_param.principal) + 1;      /* The '+1' is not to be forgotten, for the '\0' at the end */

      maj_stat = gss_import_name(&min_stat,
                                 &gss_service_buf,
                                 (gss_OID) GSS_C_NT_HOSTBASED_SERVICE,
                                 &gss_service_name);
      if(maj_stat != GSS_S_COMPLETE)
        {
          log_sperror_gss(GssError, maj_stat, min_stat);
          LogFatal(COMPONENT_INIT,
                   "Error importing gss principal %s is %s",
                   nfs_param.krb5_param.principal, GssError);
        }
      LogInfo(COMPONENT_INIT,  "gss principal %s successfully set",
              nfs_param.krb5_param.principal);

      /* Set the principal to GSSRPC */
      if(!Svcauth_gss_set_svc_name(gss_service_name))
        {
          LogFatal(COMPONENT_INIT, "Impossible to set gss principal to GSSRPC");
        }

      /* Init the HashTable */
      if(Gss_ctx_Hash_Init(nfs_param.krb5_param) == -1)
        {
          LogFatal(COMPONENT_INIT, "Impossible to init GSS CTX cache");
        }
      else
        LogInfo(COMPONENT_INIT, "Gss Context Cache successfully initialized");

#ifdef HAVE_KRB5
    }                           /*  if( nfs_param.krb5_param.active_krb5 ) */
#endif /* HAVE_KRB5 */
#endif /* _HAVE_GSSAPI */

  /* RPC Initialisation - exits on failure*/
  nfs_Init_svc();
  LogInfo(COMPONENT_INIT,  "RPC ressources successfully initialized");

  /* Worker initialisation */
  if((workers_data =
      (nfs_worker_data_t *) Mem_Alloc_Label(sizeof(nfs_worker_data_t) *
                                            nfs_param.core_param.nb_worker,
                                            "nfs_worker_data_t")) == NULL)
    {
      LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
      Fatal();
    }
  memset((char *)workers_data, 0,
         sizeof(nfs_worker_data_t) * nfs_param.core_param.nb_worker);

  if(nfs_Init_gc_counter() != 0)
    {
      LogFatal(COMPONENT_INIT, "Error while initializing worker gc counter");
    }
  LogDebug(COMPONENT_INIT, "worker gc counter successfully initialized");

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

      /* Set the pointer for the Cache inode hash table */
      workers_data[i].ht = ht;

      sprintf(name, "IP Stats for worker %d", i);
      nfs_param.ip_stats_param.hash_param.name = Str_Dup(name);
      ht_ip_stats[i] = nfs_Init_ip_stats(nfs_param.ip_stats_param);

      if(ht_ip_stats[i] == NULL)
        LogFatal(COMPONENT_INIT,
                 "Error while initializing IP/stats cache #%d", i);

      workers_data[i].ht_ip_stats = ht_ip_stats[i];

      /* Allocation of the nfs request pool */
      MakePool(&workers_data[i].request_pool,
               nfs_param.worker_param.nb_pending_prealloc,
               request_data_t,
               constructor_request_data_t, NULL);
      NamePool(&workers_data[i].request_pool, "Request Data Pool %d", i);
               
      if(!IsPoolPreallocated(&workers_data[i].request_pool))
        {
          LogCrit(COMPONENT_INIT,
                  "Error while allocating request data pool #%d", i);
          LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
          Fatal();
        }

      /* Allocation of the nfs dupreq pool */
      MakePool(&workers_data[i].dupreq_pool,
               nfs_param.worker_param.nb_dupreq_prealloc,
               dupreq_entry_t, NULL, NULL);
      NamePool(&workers_data[i].dupreq_pool, "Duplicate Request Pool %d", i);

      if(!IsPoolPreallocated(&workers_data[i].dupreq_pool))
        {
          LogCrit(COMPONENT_INIT,
                  "Error while allocating duplicate request pool #%d", i);
          LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
          Fatal();
        }

      /* Allocation of the IP/name pool */
      MakePool(&workers_data[i].ip_stats_pool,
               nfs_param.worker_param.nb_ip_stats_prealloc,
               nfs_ip_stats_t, NULL, NULL);
      NamePool(&workers_data[i].ip_stats_pool, "IP Stats Cache Pool %d", i);

      if(!IsPoolPreallocated(&workers_data[i].ip_stats_pool))
        {
          LogCrit(COMPONENT_INIT,
                  "Error while allocating IP stats cache pool #%d", i);
          LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
          Fatal();
        }

      /* Initialize, but do not pre-alloc client-id pool */
      InitPool(&workers_data[i].clientid_pool,
               nfs_param.worker_param.nb_client_id_prealloc,
               nfs_client_id_t, NULL, NULL);
      NamePool(&workers_data[i].clientid_pool, "Client ID Pool %d", i);

      LogDebug(COMPONENT_INIT, "worker data #%d successfully initialized", i);
    }                           /* for i */

  /* Admin initialisation */
  nfs_Init_admin_data(ht);

  /* Set the stats to zero */
  nfs_reset_stats();

  /* Creates the pseudo fs */
  LogDebug(COMPONENT_INIT, "Now building pseudo fs");
  if((rc = nfs4_ExportToPseudoFS(nfs_param.pexportlist)) != 0)
    LogFatal(COMPONENT_INIT,
             "Error %d while initializing NFSv4 pseudo file system", rc);

  LogInfo(COMPONENT_INIT,
          "NFSv4 pseudo file system successfully initialized");

  /* Init duplicate request cache */
  LogDebug(COMPONENT_INIT, "Now building duplicate request hash table cache");
  if((rc = nfs_Init_dupreq(nfs_param.dupreq_param)) != DUPREQ_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Error %d while initializing duplicate request hash table cache",
               rc);
    }
  LogInfo(COMPONENT_INIT,
          "duplicate request hash table cache successfully initialized");

  /* Init the IP/name cache */
  LogDebug(COMPONENT_INIT, "Now building IP/name cache");
  if(nfs_Init_ip_name(nfs_param.ip_name_param) != IP_NAME_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing IP/name cache");
    }
  LogInfo(COMPONENT_INIT,
          "IP/name cache successfully initialized");

  /* Init the UID_MAPPER cache */
  LogDebug(COMPONENT_INIT, "Now building UID_MAPPER cache");
  if((idmap_uid_init(nfs_param.uidmap_cache_param) != ID_MAPPER_SUCCESS) ||
     (idmap_uname_init(nfs_param.unamemap_cache_param) != ID_MAPPER_SUCCESS))
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing UID_MAPPER cache");
    }
  LogInfo(COMPONENT_INIT,
          "UID_MAPPER cache successfully initialized");

  /* Init the UIDGID MAPPER Cache */
  LogDebug(COMPONENT_INIT,
           "Now building UIDGID MAPPER Cache (for RPCSEC_GSS)");
  if(uidgidmap_init(nfs_param.uidgidmap_cache_param) != ID_MAPPER_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
              "Error while initializing UIDGID_MAPPER cache");
    }
  LogInfo(COMPONENT_INIT,
          "UIDGID_MAPPER cache successfully initialized");

  /* Init the GID_MAPPER cache */
  LogDebug(COMPONENT_INIT, "Now building GID_MAPPER cache");
  if((idmap_gid_init(nfs_param.gidmap_cache_param) != ID_MAPPER_SUCCESS) ||
     (idmap_gname_init(nfs_param.gnamemap_cache_param) != ID_MAPPER_SUCCESS))
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing GID_MAPPER cache");
    }
  LogInfo(COMPONENT_INIT,
          "GID_MAPPER cache successfully initialized");

  /* Init the NFSv4 Clientid cache */
  LogDebug(COMPONENT_INIT, "Now building NFSv4 clientid cache");
  if(nfs_Init_client_id(nfs_param.client_id_param) != CLIENT_ID_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing NFSv4 clientid cache");
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 clientid cache successfully initialized");

  /* Init the NFSv4 Clientid cache */
  LogDebug(COMPONENT_INIT, "Now building NFSv4 clientid cache reverse");
  if(nfs_Init_client_id_reverse(nfs_param.client_id_param) != CLIENT_ID_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing NFSv4 clientid cache reverse");
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 clientid cache reverse successfully initialized");

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

  /* Create the root entries for each exported FS */
  if((rc = nfs_export_create_root_entry(nfs_param.pexportlist, ht)) != TRUE)
    {
      LogFatal(COMPONENT_INIT,
               "Error initializing Cache Inode root entries");
    }

  /* Creation of FSAL_UP threads */
  /* This thread depends on ALL parts of Ganesha being initialized. 
   * So initialize Callback interface after everything else. */
#ifdef _USE_FSAL_UP
  nfs_param.fsal_up_param.ht = ht;
  nfs_Init_FSAL_UP(); /* initalizes an event pool */
  create_fsal_up_threads();
#endif /* _USE_FSAL_UP */

  LogInfo(COMPONENT_INIT,
          "Cache Inode root entries successfully created");

}                               /* nfs_Init */

/**
 * nfs_Start_file_content_flushers: Starts the threads in charge of flushing the data cache
 *
 * Simply do all the call to pthread_create to spawn the flushers.
 * Threads are created "joinable" and scheduled as "scope system".
 * 
 * @param nb_threads Number of threads to be started.
 * 
 * @return nothing (void function) 
 *
 */
static void nfs_Start_file_content_flushers(unsigned int nb_threads)
{
  int rc = 0;
  pthread_attr_t attr_thr;
  unsigned long i = 0;

  /* Init for thread parameter (mostly for scheduling) */
  pthread_attr_init(&attr_thr);
#ifndef _IRIX_6
  pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE);
  pthread_attr_setstacksize(&attr_thr, THREAD_STACK_SIZE);
#endif

  /* Starting all of the flushers */
  for(i = 0; i < nb_threads; i++)
    {
      /* init index and stats */
      flush_info[i].thread_index = i;
      flush_info[i].nb_flushed = 0;
      flush_info[i].nb_too_young = 0;
      flush_info[i].nb_errors = 0;
      flush_info[i].nb_orphans = 0;

      if((rc =
          pthread_create(&(flusher_thrid[i]), &attr_thr, nfs_file_content_flush_thread,
                         (void *)&flush_info[i])) != 0)
        {
          LogError(COMPONENT_THREAD, ERR_SYS, ERR_PTHREAD_CREATE, rc);
          Fatal();
        }
      else
        LogInfo(COMPONENT_THREAD, "datacache flusher #%lu started", i);

    }
  LogInfo(COMPONENT_THREAD,
          "%u datacache flushers threads were started successfully",
          nb_threads);

}                               /* nfs_Start_file_content_flushers */

/**
 * nfs_start:
 * start NFS service
 */
void nfs_start(nfs_start_info_t * p_start_info)
{
  struct rlimit ulimit_data;
  cache_content_status_t content_status;
  fsal_status_t fsal_status;
  fsal_op_context_t fsal_context;
  unsigned int i;

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
  printf("---> fsal_cred_t:%lu\n", sizeof(lustrefsal_cred_t));
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
      LogDebug(COMPONENT_INIT, "I set the core size rlimit to %ld",
               nfs_param.core_param.core_dump_size);
      ulimit_data.rlim_cur = nfs_param.core_param.core_dump_size;
      ulimit_data.rlim_max = nfs_param.core_param.core_dump_size;

      if(setrlimit(RLIMIT_CORE, &ulimit_data) != 0)
        {
          LogError(COMPONENT_INIT, ERR_SYS, ERR_SETRLIMIT, errno);
          LogCrit(COMPONENT_INIT, "Impossible to set RLIMIT_CORE to %ld",
                  nfs_param.core_param.core_dump_size);
        }
    }

  /* Set up Max Open file descriptors if set */
  if(nfs_param.core_param.nb_max_fd != -1)
    {
      LogDebug(COMPONENT_INIT, "I set the max fd rlimit to %u",
                 nfs_param.core_param.nb_max_fd);
      ulimit_data.rlim_cur = nfs_param.core_param.nb_max_fd;
      ulimit_data.rlim_max = nfs_param.core_param.nb_max_fd;

      if(setrlimit(RLIMIT_NOFILE, &ulimit_data) != 0)
        {
          LogError(COMPONENT_INIT, ERR_SYS, ERR_SETRLIMIT, errno);
          LogCrit(COMPONENT_INIT, "Impossible to set RLIMIT_NOFILE to %d",
                  nfs_param.core_param.nb_max_fd);
        }
      else
        LogDebug(COMPONENT_INIT, "Setting RLIMIT_NOFILE to %d",
                 nfs_param.core_param.nb_max_fd);
    }
  else
    {
      if(getrlimit(RLIMIT_NOFILE, &ulimit_data) != 0)
        {
          LogError(COMPONENT_INIT, ERR_SYS, ERR_SETRLIMIT, errno);
          LogMajor(COMPONENT_INIT, "Impossible to get RLIMIT_NOFILE");
          Fatal();
        }
      nfs_param.core_param.nb_max_fd = ulimit_data.rlim_cur;
      LogDebug(COMPONENT_INIT, "RLIMIT_NOFILE was cur %d max %d",
               (int)ulimit_data.rlim_cur, (int)ulimit_data.rlim_max);
    }

  /* Allocate the directories for the datacache */
  if(cache_content_prepare_directories(nfs_param.pexportlist,
                                       nfs_param.cache_layers_param.
                                       cache_content_client_param.cache_dir,
                                       &content_status) != CACHE_CONTENT_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "File Content Cache directories could not be allocated");
    }
  else
    LogInfo(COMPONENT_INIT, "File Content Cache directory initialized");

  /* Print the worker parameters in log */
  Print_param_worker_in_log(&(nfs_param.worker_param));

  /* Set the server's boot time */
  ServerBootTime = time(NULL);

  /* Set the write verifiers */
  memset(NFS3_write_verifier, 0, sizeof(writeverf3));
  memcpy(NFS3_write_verifier, &ServerBootTime, sizeof(time_t));

  memset(NFS4_write_verifier, 0, sizeof(verifier4));
  memcpy(NFS4_write_verifier, &ServerBootTime, sizeof(time_t));

  /* Initialize all layers and service threads */
  nfs_Init(p_start_info);

  /* Spawns service threads */
  nfs_Start_threads(p_start_info->flush_datacache_mode);

  if(p_start_info->flush_datacache_mode)
    {

      unsigned int nb_flushed = 0;
      unsigned int nb_too_young = 0;
      unsigned int nb_errors = 0;
      unsigned int nb_orphans = 0;

      LogEvent(COMPONENT_INIT, "Starting Data Cache emergency flush");

      fsal_status = FSAL_InitClientContext(&fsal_context);

      if(FSAL_IS_ERROR(fsal_status))
        {
          LogError(COMPONENT_INIT, ERR_FSAL, fsal_status.major, fsal_status.minor);
          Fatal();
        }

      LogEvent(COMPONENT_INIT,
               "--------------------------------------------------");
      LogEvent(COMPONENT_INIT,
               "    NFS SERVER STARTED IN EMERGENCY FLUSH MODE");
      LogEvent(COMPONENT_INIT,
               "--------------------------------------------------");

#ifndef _USE_SHARED_FSAL
      /* The number of flusher sould be less than FSAL::max_fs_calls to avoid deadlocks */
      if(nfs_param.fsal_param.fsal_info.max_fs_calls > 0)
        {
          if(p_start_info->nb_flush_threads > nfs_param.fsal_param.fsal_info.max_fs_calls)
            {
              p_start_info->nb_flush_threads =
                  nfs_param.fsal_param.fsal_info.max_fs_calls;
              LogCrit(COMPONENT_THREAD,
                      "Too much flushers, there should be less flushers than FSAL::max_fs_calls. Using %u threads instead",
                      nfs_param.fsal_param.fsal_info.max_fs_calls);
            }
        }
#endif

      nfs_Start_file_content_flushers(p_start_info->nb_flush_threads);

      LogDebug(COMPONENT_THREAD, "Waiting for datacache flushers to exit");

      /* Wait for the thread to terminate */
      for(i = 0; i < p_start_info->nb_flush_threads; i++)
        {
          pthread_join(flusher_thrid[i], NULL);

          /* add this thread's stats to total count */
          nb_flushed += flush_info[i].nb_flushed;
          nb_too_young += flush_info[i].nb_too_young;
          nb_errors += flush_info[i].nb_errors;
          nb_orphans += flush_info[i].nb_orphans;

          LogDebug(COMPONENT_THREAD, "Flusher #%u terminated", i);
        }

      LogDebug(COMPONENT_MAIN, "Nbr files flushed sucessfully: %u",
               nb_flushed);
      LogDebug(COMPONENT_MAIN, "Nbr files too young          : %u",
               nb_too_young);
      LogDebug(COMPONENT_MAIN, "Nbr flush errors             : %u",
               nb_errors);
      LogDebug(COMPONENT_MAIN, "Orphan entries removed       : %u",
               nb_orphans);

      /* Tell the admin that flush is done */
      LogEvent(COMPONENT_MAIN,
               "Flush of the data cache is done, nfs daemon will now exit");
    }
  else
    {
#ifdef _USE_NLM
      /* NSM Unmonitor all */
      nsm_unmonitor_all();
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
            LogDebug(COMPONENT_INIT, "UID_MAPPER was NOT populated");
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
            LogDebug(COMPONENT_INIT, "GID_MAPPER was NOT populated");
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
            LogDebug(COMPONENT_INIT, "IP_NAME was NOT populated");
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
        }

      /* Wait for dispatcher to exit */
      LogDebug(COMPONENT_THREAD,
               "Wait for sigmgr thread to exit");
      pthread_join(sigmgr_thrid, NULL);
    }

  /* Regular exit */
  LogEvent(COMPONENT_MAIN,
           "NFS EXIT: regular exit");
  Cleanup();

  /* let main return 0 to exit */
}                               /* nfs_start */
