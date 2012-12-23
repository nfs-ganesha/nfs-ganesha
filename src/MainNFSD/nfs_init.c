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
 * @file  nfs_init.c
 * @brief Most of the init routines
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
#include "nlm_util.h"
#include "nsm.h"
#include "sal_functions.h"
#include "nfs_tcb.h"
#include "nfs_tcb.h"

/* global information exported to all layers (as extern vars) */
nfs_parameter_t nfs_param =
{
  /* Core parameters */
  .core_param.nb_worker = NB_WORKER_THREAD_DEFAULT,
  .core_param.nb_call_before_queue_avg = NB_REQUEST_BEFORE_QUEUE_AVG,
  .core_param.nb_max_concurrent_gc = NB_MAX_CONCURRENT_GC,
  .core_param.drc.disabled = false,
  .core_param.drc.tcp.npart = DRC_TCP_NPART,
  .core_param.drc.tcp.size = DRC_TCP_SIZE,
  .core_param.drc.tcp.cachesz = DRC_TCP_CACHESZ,
  .core_param.drc.tcp.hiwat = DRC_TCP_HIWAT,
  .core_param.drc.tcp.recycle_npart = DRC_TCP_RECYCLE_NPART,
  .core_param.drc.tcp.checksum = DRC_TCP_CHECKSUM,
  .core_param.drc.udp.npart = DRC_UDP_NPART,
  .core_param.drc.udp.size = DRC_UDP_SIZE,
  .core_param.drc.udp.cachesz = DRC_UDP_CACHESZ,
  .core_param.drc.udp.hiwat = DRC_UDP_HIWAT,
  .core_param.drc.udp.checksum = DRC_UDP_CHECKSUM,
  .core_param.port[P_NFS] = NFS_PORT,
  .core_param.bind_addr.sin_family = AF_INET,       /* IPv4 only right now */
  .core_param.program[P_NFS] = NFS_PROGRAM,
  .core_param.program[P_MNT] = MOUNTPROG,
  .core_param.program[P_NLM] = NLMPROG,
#ifdef _USE_9P
  ._9p_param._9p_tcp_port = _9P_TCP_PORT ,
#endif
#ifdef _USE_9P_RDMA
  ._9p_param._9p_rdma_port = _9P_RDMA_PORT,
#endif
  .core_param.program[P_RQUOTA] = RQUOTAPROG,
  .core_param.port[P_RQUOTA] = RQUOTA_PORT,
  .core_param.drop_io_errors = true,
  .core_param.drop_delay_errors = true,
  .core_param.core_dump_size = -1,
  .core_param.nb_max_fd = 1024,
  .core_param.stats_update_delay = 60,
  .core_param.long_processing_threshold = 10, /* seconds */
  .core_param.decoder_fridge_expiration_delay = -1,
  .core_param.dispatch_max_reqs = 5000,
  .core_param.dispatch_max_reqs_xprt =  512,
  .core_param.core_options = CORE_OPTION_ALL_VERS,
  .core_param.stats_file_path = "/tmp/ganesha.stat",
  .core_param.stats_per_client_directory = "/tmp",
  .core_param.max_send_buffer_size = NFS_DEFAULT_SEND_BUFFER_SIZE,
  .core_param.max_recv_buffer_size = NFS_DEFAULT_RECV_BUFFER_SIZE,

  /* Workers parameters : IP/Name values pool prealloc */

#ifdef _HAVE_GSSAPI
  /* krb5 parameter */
  .krb5_param.svc.principal = DEFAULT_NFS_PRINCIPAL,
  .krb5_param.keytab = DEFAULT_NFS_KEYTAB,
  .krb5_param.ccache_dir =  DEFAULT_NFS_CCACHE_DIR,
  .krb5_param.active_krb5 = true,
#endif

  /* NFSv4 parameter */
  .nfsv4_param.lease_lifetime = NFS4_LEASE_LIFETIME,
  .nfsv4_param.returns_err_fh_expired = true,
  .nfsv4_param.return_bad_stateid = true,
  .nfsv4_param.domainname = DEFAULT_DOMAIN,
  .nfsv4_param.idmapconf = DEFAULT_IDMAPCONF,
#ifdef _USE_NFSIDMAP
  .nfsv4_param.use_getpwnam = false,
#else
  .nfsv4_param.use_getpwnam = true,
#endif

  /*  Worker parameters : IP/name hash table */
  .ip_name_param.hash_param.index_size = PRIME_IP_NAME,
  .ip_name_param.hash_param.alphabet_length = 10,   /* ipaddr is a numerical decimal value */
  .ip_name_param.hash_param.hash_func_key = ip_name_value_hash_func,
  .ip_name_param.hash_param.hash_func_rbt = ip_name_rbt_hash_func,
  .ip_name_param.hash_param.compare_key = compare_ip_name,
  .ip_name_param.hash_param.key_to_str = display_ip_name_key,
  .ip_name_param.hash_param.val_to_str = display_ip_name_val,
  .ip_name_param.hash_param.flags = HT_FLAG_NONE,
  .ip_name_param.expiration_time = IP_NAME_EXPIRATION,

  /*  Worker parameters : UID_MAPPER hash table */
  .uidmap_cache_param.hash_param.index_size = PRIME_ID_MAPPER,
  .uidmap_cache_param.hash_param.alphabet_length = 10,      /* Not used for UID_MAPPER */
  .uidmap_cache_param.hash_param.hash_func_key = idmapper_value_hash_func,
  .uidmap_cache_param.hash_param.hash_func_rbt = idmapper_rbt_hash_func,
  .uidmap_cache_param.hash_param.compare_key = compare_idmapper,
  .uidmap_cache_param.hash_param.key_to_str = display_idmapper_key,
  .uidmap_cache_param.hash_param.val_to_str = display_idmapper_val,
  .uidmap_cache_param.hash_param.flags = HT_FLAG_NONE,

  /*  Worker parameters : UNAME_MAPPER hash table */
  .unamemap_cache_param.hash_param.index_size = PRIME_ID_MAPPER,
  .unamemap_cache_param.hash_param.alphabet_length = 10,    /* Not used for UID_MAPPER */
  .unamemap_cache_param.hash_param.hash_func_key = namemapper_value_hash_func,
  .unamemap_cache_param.hash_param.hash_func_rbt = namemapper_rbt_hash_func,
  .unamemap_cache_param.hash_param.compare_key = compare_namemapper,
  .unamemap_cache_param.hash_param.key_to_str = display_idmapper_val,
  .unamemap_cache_param.hash_param.val_to_str = display_idmapper_key,
  .unamemap_cache_param.hash_param.flags = HT_FLAG_NONE,

  /*  Worker parameters : GID_MAPPER hash table */
  .gidmap_cache_param.hash_param.index_size = PRIME_ID_MAPPER,
  .gidmap_cache_param.hash_param.alphabet_length = 10,      /* Not used for UID_MAPPER */
  .gidmap_cache_param.hash_param.hash_func_key = idmapper_value_hash_func,
  .gidmap_cache_param.hash_param.hash_func_rbt = idmapper_rbt_hash_func,
  .gidmap_cache_param.hash_param.compare_key = compare_idmapper,
  .gidmap_cache_param.hash_param.key_to_str = display_idmapper_key,
  .gidmap_cache_param.hash_param.val_to_str = display_idmapper_val,
  .gidmap_cache_param.hash_param.flags = HT_FLAG_NONE,

  /*  Worker parameters : UID->GID  hash table (for RPCSEC_GSS) */
  .uidgidmap_cache_param.hash_param.index_size = PRIME_ID_MAPPER,
  .uidgidmap_cache_param.hash_param.alphabet_length = 10,   /* Not used for UID_MAPPER */
  .uidgidmap_cache_param.hash_param.hash_func_key =
      namemapper_value_hash_func,
  .uidgidmap_cache_param.hash_param.hash_func_rbt = namemapper_rbt_hash_func,
  .uidgidmap_cache_param.hash_param.compare_key = compare_namemapper,
  .uidgidmap_cache_param.hash_param.key_to_str = display_idmapper_key,
  .uidgidmap_cache_param.hash_param.val_to_str = display_idmapper_key,
  .uidgidmap_cache_param.hash_param.flags = HT_FLAG_NONE,

  /*  Worker parameters : GNAME_MAPPER hash table */
  .gnamemap_cache_param.hash_param.index_size = PRIME_ID_MAPPER,
  .gnamemap_cache_param.hash_param.alphabet_length = 10,    /* Not used for UID_MAPPER */
  .gnamemap_cache_param.hash_param.hash_func_key = namemapper_value_hash_func,
  .gnamemap_cache_param.hash_param.hash_func_rbt = namemapper_rbt_hash_func,
  .gnamemap_cache_param.hash_param.compare_key = compare_namemapper,
  .gnamemap_cache_param.hash_param.key_to_str = display_idmapper_val,
  .gnamemap_cache_param.hash_param.val_to_str = display_idmapper_key,
  .gnamemap_cache_param.hash_param.flags = HT_FLAG_NONE,

  /*  Worker parameters : IP/stats hash table */
  .ip_stats_param.hash_param.index_size = PRIME_IP_STATS,
  .ip_stats_param.hash_param.alphabet_length = 10,  /* ipaddr is a numerical decimal value */
  .ip_stats_param.hash_param.hash_func_key = ip_stats_value_hash_func,
  .ip_stats_param.hash_param.hash_func_rbt = ip_stats_rbt_hash_func,
  .ip_stats_param.hash_param.compare_key = compare_ip_stats,
  .ip_stats_param.hash_param.key_to_str = display_ip_stats_key,
  .ip_stats_param.hash_param.val_to_str = display_ip_stats_val,
  .ip_stats_param.hash_param.flags = HT_FLAG_NONE,

  /*  Worker parameters : NFSv4 Unconfirmed Client id table */
  .client_id_param.cid_unconfirmed_hash_param.index_size = PRIME_CLIENT_ID,
  .client_id_param.cid_unconfirmed_hash_param.alphabet_length = 10, /* ipaddr is a numerical decimal value */
  .client_id_param.cid_unconfirmed_hash_param.hash_func_key = client_id_value_hash_func,
  .client_id_param.cid_unconfirmed_hash_param.hash_func_rbt = client_id_rbt_hash_func,
  .client_id_param.cid_unconfirmed_hash_param.hash_func_both = NULL ,
  .client_id_param.cid_unconfirmed_hash_param.compare_key = compare_client_id,
  .client_id_param.cid_unconfirmed_hash_param.key_to_str = display_client_id_key,
  .client_id_param.cid_unconfirmed_hash_param.val_to_str = display_client_id_val,
  .client_id_param.cid_unconfirmed_hash_param.ht_name = "Unconfirmed Client ID",
  .client_id_param.cid_unconfirmed_hash_param.flags = HT_FLAG_CACHE,
  .client_id_param.cid_unconfirmed_hash_param.ht_log_component = COMPONENT_CLIENTID,

  /*  Worker parameters : NFSv4 Confirmed Client id table */
  .client_id_param.cid_confirmed_hash_param.index_size = PRIME_CLIENT_ID,
  .client_id_param.cid_confirmed_hash_param.alphabet_length = 10, /* ipaddr is a numerical decimal value */
  .client_id_param.cid_confirmed_hash_param.hash_func_key = client_id_value_hash_func,
  .client_id_param.cid_confirmed_hash_param.hash_func_rbt = client_id_rbt_hash_func,
  .client_id_param.cid_confirmed_hash_param.hash_func_both = NULL ,
  .client_id_param.cid_confirmed_hash_param.compare_key = compare_client_id,
  .client_id_param.cid_confirmed_hash_param.key_to_str = display_client_id_key,
  .client_id_param.cid_confirmed_hash_param.val_to_str = display_client_id_val,
  .client_id_param.cid_confirmed_hash_param.ht_name = "Confirmed Client ID",
  .client_id_param.cid_confirmed_hash_param.flags = HT_FLAG_CACHE,
  .client_id_param.cid_confirmed_hash_param.ht_log_component = COMPONENT_CLIENTID,

  /*  Worker parameters : NFSv4 Client Record table */
  .client_id_param.cr_hash_param.index_size = PRIME_CLIENT_ID,
  .client_id_param.cr_hash_param.alphabet_length = 10, /* ipaddr is a numerical decimal value */
  .client_id_param.cr_hash_param.hash_func_key = client_record_value_hash_func,
  .client_id_param.cr_hash_param.hash_func_rbt = client_record_rbt_hash_func,
  .client_id_param.cr_hash_param.hash_func_both = NULL ,
  .client_id_param.cr_hash_param.compare_key = compare_client_record,
  .client_id_param.cr_hash_param.key_to_str = display_client_record_key,
  .client_id_param.cr_hash_param.val_to_str = display_client_record_val,
  .client_id_param.cr_hash_param.ht_name = "Client Record",
  .client_id_param.cr_hash_param.flags = HT_FLAG_CACHE,
  .client_id_param.cr_hash_param.ht_log_component = COMPONENT_CLIENTID,

  /* NFSv4 State Id hash */
  .state_id_param.hash_param.index_size = PRIME_STATE_ID,
  .state_id_param.hash_param.alphabet_length = 10,  /* ipaddr is a numerical decimal value */
  .state_id_param.hash_param.hash_func_key = state_id_value_hash_func,
  .state_id_param.hash_param.hash_func_rbt = state_id_rbt_hash_func,
  .state_id_param.hash_param.compare_key = compare_state_id,
  .state_id_param.hash_param.key_to_str = display_state_id_key,
  .state_id_param.hash_param.val_to_str = display_state_id_val,
  .state_id_param.hash_param.flags = HT_FLAG_CACHE,

  /* NFSv4 Session Id hash */
  .session_id_param.hash_param.index_size = PRIME_STATE_ID,
  .session_id_param.hash_param.alphabet_length = 10,        /* ipaddr is a numerical decimal value */
  .session_id_param.hash_param.hash_func_key = session_id_value_hash_func,
  .session_id_param.hash_param.hash_func_rbt = session_id_rbt_hash_func,
  .session_id_param.hash_param.compare_key = compare_session_id,
  .session_id_param.hash_param.key_to_str = display_session_id_key,
  .session_id_param.hash_param.val_to_str = display_session_id_val,
  .session_id_param.hash_param.flags = HT_FLAG_CACHE,

  /* NFSv4 Open Owner hash */
  .nfs4_owner_param.hash_param.index_size = PRIME_STATE_ID,
  .nfs4_owner_param.hash_param.alphabet_length = 10,        /* ipaddr is a numerical decimal value */
  .nfs4_owner_param.hash_param.hash_func_key = nfs4_owner_value_hash_func,
  .nfs4_owner_param.hash_param.hash_func_rbt = nfs4_owner_rbt_hash_func,
  .nfs4_owner_param.hash_param.compare_key = compare_nfs4_owner_key,
  .nfs4_owner_param.hash_param.key_to_str = display_nfs4_owner_key,
  .nfs4_owner_param.hash_param.val_to_str = display_nfs4_owner_val,
  .nfs4_owner_param.hash_param.flags = HT_FLAG_CACHE,

  /* NSM Client hash */
  .nsm_client_hash_param.index_size = PRIME_STATE_ID,
  .nsm_client_hash_param.alphabet_length = 10,        /* ipaddr is a numerical decimal value */
  .nsm_client_hash_param.hash_func_key = nsm_client_value_hash_func,
  .nsm_client_hash_param.hash_func_rbt = nsm_client_rbt_hash_func,
  .nsm_client_hash_param.compare_key = compare_nsm_client_key,
  .nsm_client_hash_param.key_to_str = display_nsm_client_key,
  .nsm_client_hash_param.val_to_str = display_nsm_client_val,
  .nsm_client_hash_param.flags = HT_FLAG_NONE,

  /* NLM Client hash */
  .nlm_client_hash_param.index_size = PRIME_STATE_ID,
  .nlm_client_hash_param.alphabet_length = 10,        /* ipaddr is a numerical decimal value */
  .nlm_client_hash_param.hash_func_key = nlm_client_value_hash_func,
  .nlm_client_hash_param.hash_func_rbt = nlm_client_rbt_hash_func,
  .nlm_client_hash_param.compare_key = compare_nlm_client_key,
  .nlm_client_hash_param.key_to_str = display_nlm_client_key,
  .nlm_client_hash_param.val_to_str = display_nlm_client_val,
  .nlm_client_hash_param.flags = HT_FLAG_NONE,

  /* NLM Owner hash */
  .nlm_owner_hash_param.index_size = PRIME_STATE_ID,
  .nlm_owner_hash_param.alphabet_length = 10,        /* ipaddr is a numerical decimal value */
  .nlm_owner_hash_param.hash_func_key = nlm_owner_value_hash_func,
  .nlm_owner_hash_param.hash_func_rbt = nlm_owner_rbt_hash_func,
  .nlm_owner_hash_param.compare_key = compare_nlm_owner_key,
  .nlm_owner_hash_param.key_to_str = display_nlm_owner_key,
  .nlm_owner_hash_param.val_to_str = display_nlm_owner_val,
  .nlm_owner_hash_param.flags = HT_FLAG_NONE,

#ifdef _USE_9P
  /* 9P Owner hash */
  ._9p_owner_hash_param.index_size = PRIME_STATE_ID,
  ._9p_owner_hash_param.alphabet_length = 10,        /* ipaddr is a numerical decimal value */
  ._9p_owner_hash_param.hash_func_key = _9p_owner_value_hash_func,
  ._9p_owner_hash_param.hash_func_rbt = _9p_owner_rbt_hash_func,
  ._9p_owner_hash_param.compare_key = compare_9p_owner_key,
  ._9p_owner_hash_param.key_to_str = display_9p_owner_key,
  ._9p_owner_hash_param.val_to_str = display_9p_owner_val,
  ._9p_owner_hash_param.flags = HT_FLAG_NONE,
#endif

  /* Cache inode parameters : hash table */
  .cache_layers_param.cache_param.hparam.index_size = PRIME_CACHE_INODE,
  .cache_layers_param.cache_param.hparam.alphabet_length = 10,      /* Buffer seen as a decimal polynom */
  .cache_layers_param.cache_param.hparam.flags = HT_FLAG_CACHE,
  .cache_layers_param.cache_param.hparam.hash_func_both = cache_inode_fsal_rbt_both,
  .cache_layers_param.cache_param.hparam.compare_key = cache_inode_compare_key_fsal,
  .cache_layers_param.cache_param.hparam.key_to_str = display_cache,
  .cache_layers_param.cache_param.hparam.val_to_str = display_cache,
  .cache_layers_param.cache_param.hparam.flags = HT_FLAG_CACHE,

  /* Cache inode parameters : cookie hash table */
  .cache_layers_param.cache_param.cookie_param.index_size = PRIME_STATE_ID,
  .cache_layers_param.cache_param.cookie_param.alphabet_length = 10,      /* Buffer seen as a decimal polynom */
  .cache_layers_param.cache_param.cookie_param.hash_func_key = lock_cookie_value_hash_func ,
  .cache_layers_param.cache_param.cookie_param.hash_func_rbt = lock_cookie_rbt_hash_func ,
  .cache_layers_param.cache_param.cookie_param.compare_key = compare_lock_cookie_key,
  .cache_layers_param.cache_param.cookie_param.key_to_str = display_lock_cookie_key,
  .cache_layers_param.cache_param.cookie_param.val_to_str = display_lock_cookie_val,
  .cache_layers_param.cache_param.cookie_param.flags = HT_FLAG_NONE,

  /* Cache inode parameters : Garbage collection policy */
  .cache_layers_param.gcpol.entries_hwmark = 100000,
  .cache_layers_param.gcpol.entries_lwmark = 50000,
  .cache_layers_param.gcpol.use_fd_cache = true,
  .cache_layers_param.gcpol.lru_run_interval = 600,
  .cache_layers_param.gcpol.fd_limit_percent = 99,
  .cache_layers_param.gcpol.fd_hwmark_percent = 90,
  .cache_layers_param.gcpol.fd_lwmark_percent = 50,
  .cache_layers_param.gcpol.reaper_work = 1000,
  .cache_layers_param.gcpol.biggest_window = 40,
  .cache_layers_param.gcpol.required_progress = 5,
  .cache_layers_param.gcpol.futility_count = 8,

  /* SNMP ADM parameters */
#ifdef _SNMP_ADM_ACTIVE
  .extern_param.snmp_adm.product_id = 1,
  .extern_param.snmp_adm.export_cache_stats = true,
  .extern_param.snmp_adm.export_requests_stats = true,
#endif
};

/* ServerEpoch is ServerBootTime unless overriden by -E command line option */
time_t ServerBootTime;
time_t ServerEpoch;

nfs_worker_data_t *workers_data = NULL;
hash_table_t *fh_to_cache_entry_ht = NULL; /* Cache inode handle lookup table */
verifier4 NFS4_write_verifier;  /* NFS V4 write verifier */
writeverf3 NFS3_write_verifier; /* NFS V3 write verifier */

/* node ID used to identify an individual node in a cluster */
ushort g_nodeid = 0;

hash_table_t *ht_ip_stats[NB_MAX_WORKER_THREAD];
nfs_start_info_t nfs_start_info;

pthread_t worker_thrid[NB_MAX_WORKER_THREAD];

pthread_t flusher_thrid[NB_MAX_FLUSHER_THREAD];
nfs_flush_thread_data_t flush_info[NB_MAX_FLUSHER_THREAD];

pthread_t stat_thrid;
pthread_t stat_exporter_thrid;
pthread_t admin_thrid;
pthread_t fcc_gc_thrid;
pthread_t sigmgr_thrid;
pthread_t reaper_thrid;
pthread_t gsh_dbus_thrid;
pthread_t upp_thrid;
nfs_tcb_t gccb;

#ifdef _USE_9P
pthread_t _9p_dispatcher_thrid;
#endif

#ifdef _USE_9P_RDMA
pthread_t _9p_rdma_dispatcher_thrid ;
#endif

char config_path[MAXPATHLEN];

char pidfile_path[MAXPATHLEN] ;

/**
 * @brief This thread is in charge of signal management 
 *
 * @param[in] UnusedArg Unused
 *
 * @return NULL.
 */
void *sigmgr_thread(void *UnusedArg)
{
  SetNameFunction("sigmgr");
  int signal_caught = 0;

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

  LogDebug(COMPONENT_THREAD, "sigmgr thread exiting");

  /* Remove pid file. I do not check for status (best effort,
   * the daemon is stopping anyway */
  unlink( pidfile_path ) ;

  /* Might as well exit - no need for this thread any more */
  return NULL;
}

/**
 * @brief Initialize NFSd prerequisites
 *
 * @param[in] program_name Name of the program
 * @param[in] program_name Server host name
 * @param[in] debug_level  Debug level
 * @param[in] log_path     Log path
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
  AddFamilyError(ERR_HASHTABLE, "HashTable related Errors", tab_errctx_hash);
  AddFamilyError(ERR_FSAL, "FSAL related Errors", tab_errstatus_FSAL);
  AddFamilyError(ERR_CACHE_INODE, "Cache Inode related Errors",
		 tab_errstatus_cache_inode);
}

/**
 * @brief Print the nfs_parameter_structure
 */
void nfs_print_param_config()
{
  printf("NFS_Core_Param\n{\n");

  printf("\tNFS_Port = %u ;\n", nfs_param.core_param.port[P_NFS]);
  printf("\tMNT_Port = %u ;\n", nfs_param.core_param.port[P_MNT]);
  printf("\tNFS_Program = %u ;\n", nfs_param.core_param.program[P_NFS]);
  printf("\tMNT_Program = %u ;\n", nfs_param.core_param.program[P_NFS]);
  printf("\tNb_Worker = %u ; \n", nfs_param.core_param.nb_worker);
  printf("\tb_Call_Before_Queue_Avg = %u ; \n",
         nfs_param.core_param.nb_call_before_queue_avg);
  printf("\tNb_MaxConcurrentGC = %u ; \n",
         nfs_param.core_param.nb_max_concurrent_gc);
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
  printf("\tStats_Update_Delay = %d ; \n",
         nfs_param.core_param.stats_update_delay);
  printf("\tLong_Processing_Threshold = %d ; \n",
         nfs_param.core_param.long_processing_threshold);
  printf("\tDecoder_Fridge_Expiration_Delay = %d ; \n",
         nfs_param.core_param.decoder_fridge_expiration_delay);
  printf("\tStats_Per_Client_Directory = %s ; \n",
         nfs_param.core_param.stats_per_client_directory);

  if(nfs_param.core_param.dump_stats_per_client)
    printf("\tDump_Stats_Per_Client = true ; \n");
  else
    printf("\tDump_Stats_Per_Client = false ;\n");

  if(nfs_param.core_param.drop_io_errors)
    printf("\tDrop_IO_Errors = true ; \n");
  else
    printf("\tDrop_IO_Errors = false ;\n");

  if(nfs_param.core_param.drop_inval_errors)
    printf("\tDrop_Inval_Errors = true ; \n");
  else
    printf("\tDrop_Inval_Errors = false ;\n");

  if(nfs_param.core_param.drop_delay_errors)
    printf("\tDrop_Delay_Errors = true ; \n");
  else
    printf("\tDrop_Delay_Errors = false ;\n");

  printf("}\n\n");

  printf("NFS_Worker_Param\n{\n");
  printf("}\n\n");
}

/**
 * @brief Load parameters from config file
 *
 * @param[in]  config_file_t  Parsed config file
 * @param[out] p_start_info_t Startup parameters
 *
 * @return -1 on failure.
 */
int nfs_set_param_from_conf(config_file_t config_struct,
			    nfs_start_info_t * p_start_info)
{
  int rc;
  cache_inode_status_t cache_inode_status;


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
                 "No session id configuration found in config file, "
                 "using default");
      else
        LogDebug(COMPONENT_INIT,
                 "session id configuration read from config file");
    }

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
  rc = ReadExports(config_struct, &nfs_param.pexportlist);
  if(rc < 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while parsing export entries");
      return -1;
    }
  else if(rc == 0)
    {
      LogWarn(COMPONENT_INIT,
              "No export entries found in configuration file !!!");
    }

  LogEvent(COMPONENT_INIT, "Configuration file successfully parsed");

  return 0;
}

/**
 * @brief Check whether a given value is prime or not
 *
 * @param[in] v A given integer
 *
 * @return Whether it's prime or not.
 */
static bool is_prime (int v)
{
    int    i, m;

    if (v <= 1)
	return false;
    if (v == 2)
	return true;
    if (v % 2 == 0)
	return false;
    // dont link with libm just for this
#ifdef LINK_LIBM
    m = (int)sqrt(v);
#else
    m = v;
#endif
    for (i = 2 ; i <= m; i +=2) {
	if (v % i == 0)
	    return false;
    }
    return true;
}

/**
 * @brief Checks parameters concistency (limits, ...)
 *
 * @return 1 on failure, 0 on success.
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

  // check for parameters which need to be primes
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
      !is_prime(nfs_param.session_id_param.hash_param.index_size) ||
      !is_prime(nfs_param.nfs4_owner_param.hash_param.index_size) ||
      !is_prime(nfs_param.nsm_client_hash_param.index_size) ||
      !is_prime(nfs_param.nlm_client_hash_param.index_size) ||
      !is_prime(nfs_param.nlm_owner_hash_param.index_size) ||
      !is_prime(cache_inode_params.cookie_param.index_size) ||
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
    }                           /* for( i = 0 ; i < nfs_param.core_param.nb_worker ; i++ ) */

}

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

  /* Start State Async threads */
  state_async_thread_start();

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
  /* Starting the 9P/TCP dispatcher thread */
  if((rc = pthread_create(&_9p_dispatcher_thrid, &attr_thr,
                          _9p_dispatcher_thread, NULL ) ) != 0 )
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create  9P/TCP dispatcher, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD, "9P/TCP dispatcher thread was started successfully");
#endif

#ifdef _USE_9P_RDMA
  /* Starting the 9P/RDMA dispatcher thread */
  if((rc = pthread_create(&_9p_rdma_dispatcher_thrid, &attr_thr,
                          _9p_rdma_dispatcher_thread, NULL ) ) != 0 )
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create  9P/RDMA dispatcher, error = %d (%s)",
               errno, strerror(errno));
    }
  LogEvent(COMPONENT_THREAD, "9P/RDMA dispatcher thread was started successfully");
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
}

/**
 * @brief Init the nfs daemon
 *
 * @param[in] p_start_info Unused
 */

static void nfs_Init(const nfs_start_info_t *p_start_info)
{
  cache_inode_status_t cache_status;
  state_status_t state_status;
  unsigned int i = 0;
  int rc = 0;
#ifdef _HAVE_GSSAPI
  gss_buffer_desc gss_service_buf;
  OM_uint32 maj_stat, min_stat;
  char GssError[MAXNAMLEN];
#endif

#ifdef USE_DBUS
  /* DBUS init */
  gsh_dbus_pkginit();
#endif

  if (nfs_param.core_param.enable_FSAL_upcalls)
    {
      init_FSAL_up();
    }

  /* Cache Inode Initialisation */
  cache_status = cache_inode_init(cache_inode_params,
				  &fh_to_cache_entry_ht);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Cache Inode Layer could not be initialized, status=%s",
               cache_inode_err_str(cache_status));
    }

  /* Initialize thread control block */
  tcb_head_init();

  state_status = state_lock_init(cache_inode_params.cookie_param);
  if(state_status != STATE_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Cache Inode Layer could not be initialized, status=%s",
               state_err_str(state_status));
    }
  LogInfo(COMPONENT_INIT, "Cache Inode library successfully initialized");

  /* Cache Inode LRU (call this here, rather than as part of
     cache_inode_init() so the GC policy has been set */
  cache_inode_lru_pkginit();

  nfs41_session_pool = pool_init("NFSv4.1 session pool",
                                 sizeof(nfs41_session_t),
                                 pool_basic_substrate,
                                 NULL, NULL, NULL);

  request_pool = pool_init("Request pool",
                           sizeof(request_data_t),
                           pool_basic_substrate,
                           NULL,
                           NULL /* FASTER constructor_request_data_t */,
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
                                NULL /* FASTER constructor_nfs_request_data_t */,
                                NULL);
  if(!request_data_pool)
    {
      LogCrit(COMPONENT_INIT,
              "Error while allocating request data pool");
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

  /* Creates the pseudo fs */
  LogDebug(COMPONENT_INIT, "Now building pseudo fs");
  if((rc = nfs4_ExportToPseudoFS(nfs_param.pexportlist)) != 0)
    LogFatal(COMPONENT_INIT,
             "Error %d while initializing NFSv4 pseudo file system", rc);

  LogInfo(COMPONENT_INIT,
          "NFSv4 pseudo file system successfully initialized");

  /* Init duplicate request cache */
  dupreq2_pkginit();
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

  if (nfs_param.core_param.enable_NLM)
    {
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
    }

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

  LogDebug(COMPONENT_INIT, "Now building NFSv4 Session Id cache");
  if(nfs41_Init_session_id(nfs_param.session_id_param) != 0)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing NFSv4 Session Id cache");
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 Session Id cache successfully initialized");

  LogDebug(COMPONENT_INIT, "Now building NFSv4 ACL cache");
  if(nfs4_acls_init() != 0)
    {
      LogCrit(COMPONENT_INIT,
              "Error while initializing NFSv4 ACLs");
      exit(1);
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 ACL cache successfully initialized");

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
  if((rc = nfs_export_create_root_entry(nfs_param.pexportlist)) != true)
    {
      LogFatal(COMPONENT_INIT,
               "Error initializing Cache Inode root entries");
    }

  LogInfo(COMPONENT_INIT,
          "Cache Inode root entries successfully created");

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

  LogInfo(COMPONENT_INIT,
          "Cache Inode root entries successfully created");


     /* callback dispatch */
     nfs_rpc_cb_pkginit();
#ifdef _USE_CB_SIMULATOR
     nfs_rpc_cbsim_pkginit();
#endif      /*  _USE_CB_SIMULATOR */

}                               /* nfs_Init */

/**
 * @brief Start NFS service
 *
 * @param[in] p_start_info Startup parameters
 */
void nfs_start(nfs_start_info_t * p_start_info)
{
  struct rlimit ulimit_data;

  /* store the start info so it is available for all layers */
  nfs_start_info = *p_start_info;

  if(p_start_info->dump_default_config == true)
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

  if (nfs_param.core_param.enable_NLM)
    {
      /* NSM Unmonitor all */
      nsm_unmonitor_all();
    }

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

  /* Regular exit */
  LogEvent(COMPONENT_MAIN,
           "NFS EXIT: regular exit");

  /* if not in grace period, clean up the old state directory */
  if(!nfs_in_grace())
    nfs4_clean_old_recov_dir();

  Cleanup();

  /* let main return 0 to exit */
}
