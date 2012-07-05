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
#include "config.h"
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
#include "fridgethr.h"
#include "idmapper.h"
#include "client_mgr.h"
#include "export_mgr.h"
#include "delayed_exec.h"

extern struct fridgethr *req_fridge;

/* global information exported to all layers (as extern vars) */
nfs_parameter_t nfs_param =
{
  /* Core parameters */
  .core_param.nb_worker = NB_WORKER_THREAD_DEFAULT,
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
  .core_param.rpc.debug_flags = TIRPC_DEBUG_FLAGS,
  .core_param.rpc.max_connections = 1024,
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
  .core_param.long_processing_threshold = 10, /* seconds */
  .core_param.decoder_fridge_expiration_delay = -1,
  .core_param.decoder_fridge_block_timeout = -1,
  .core_param.dispatch_max_reqs = 5000,
  .core_param.dispatch_max_reqs_xprt =  512,
  .core_param.core_options = CORE_OPTION_ALL_VERS,
  .core_param.rpc.max_send_buffer_size = NFS_DEFAULT_SEND_BUFFER_SIZE,
  .core_param.rpc.max_recv_buffer_size = NFS_DEFAULT_RECV_BUFFER_SIZE,
  .core_param.enable_FSAL_upcalls = true,
  .core_param.enable_NLM = true,
  .core_param.enable_RQUOTA = true,


  /* Workers parameters : IP/Name values pool prealloc */

#ifdef _HAVE_GSSAPI
  /* krb5 parameter */
  .krb5_param.svc.principal = DEFAULT_NFS_PRINCIPAL,
  .krb5_param.keytab = DEFAULT_NFS_KEYTAB,
  .krb5_param.ccache_dir =  DEFAULT_NFS_CCACHE_DIR,
  .krb5_param.active_krb5 = true,
#endif

  /* NFSv4 parameter */
  .nfsv4_param.graceless = false,
  .nfsv4_param.lease_lifetime = LEASE_LIFETIME_DEFAULT,
  .nfsv4_param.fh_expire = false,
  .nfsv4_param.return_bad_stateid = true,
  .nfsv4_param.domainname = DOMAINNAME_DEFAULT,
  .nfsv4_param.idmapconf = IDMAPCONF_DEFAULT,
  .nfsv4_param.allow_numeric_owners = true,
#ifdef USE_NFSIDMAP
  .nfsv4_param.use_getpwnam = false,
#else
  .nfsv4_param.use_getpwnam = true,
#endif

  /*  Worker parameters : IP/name hash table */
  .ip_name_param.hash_param.index_size = PRIME_IP_NAME,
  .ip_name_param.hash_param.hash_func_key = ip_name_value_hash_func,
  .ip_name_param.hash_param.hash_func_rbt = ip_name_rbt_hash_func,
  .ip_name_param.hash_param.compare_key = compare_ip_name,
  .ip_name_param.hash_param.key_to_str = display_ip_name_key,
  .ip_name_param.hash_param.val_to_str = display_ip_name_val,
  .ip_name_param.hash_param.flags = HT_FLAG_NONE,
  .ip_name_param.expiration_time = IP_NAME_EXPIRATION,

  /*  Worker parameters : NFSv4 Unconfirmed Client id table */
  .client_id_param.cid_unconfirmed_hash_param.index_size = PRIME_STATE,
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
  .client_id_param.cid_confirmed_hash_param.index_size = PRIME_STATE,
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
  .client_id_param.cr_hash_param.index_size = PRIME_STATE,
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
  .state_id_param.index_size = PRIME_STATE,
  .state_id_param.hash_func_key = state_id_value_hash_func,
  .state_id_param.hash_func_rbt = state_id_rbt_hash_func,
  .state_id_param.compare_key = compare_state_id,
  .state_id_param.key_to_str = display_state_id_key,
  .state_id_param.val_to_str = display_state_id_val,
  .state_id_param.flags = HT_FLAG_CACHE,

  /* NFSv4 Session Id hash */
  .session_id_param.index_size = PRIME_STATE,
  .session_id_param.hash_func_key = session_id_value_hash_func,
  .session_id_param.hash_func_rbt = session_id_rbt_hash_func,
  .session_id_param.compare_key = compare_session_id,
  .session_id_param.key_to_str = display_session_id_key,
  .session_id_param.val_to_str = display_session_id_val,
  .session_id_param.flags = HT_FLAG_CACHE,

  /* NFSv4 Open Owner hash */
  .nfs4_owner_param.index_size = PRIME_STATE,
  .nfs4_owner_param.hash_func_key = nfs4_owner_value_hash_func,
  .nfs4_owner_param.hash_func_rbt = nfs4_owner_rbt_hash_func,
  .nfs4_owner_param.compare_key = compare_nfs4_owner_key,
  .nfs4_owner_param.key_to_str = display_nfs4_owner_key,
  .nfs4_owner_param.val_to_str = display_nfs4_owner_val,
  .nfs4_owner_param.flags = HT_FLAG_CACHE,

  /* NSM Client hash */
  .nsm_client_hash_param.index_size = PRIME_STATE,
  .nsm_client_hash_param.hash_func_key = nsm_client_value_hash_func,
  .nsm_client_hash_param.hash_func_rbt = nsm_client_rbt_hash_func,
  .nsm_client_hash_param.compare_key = compare_nsm_client_key,
  .nsm_client_hash_param.key_to_str = display_nsm_client_key,
  .nsm_client_hash_param.val_to_str = display_nsm_client_val,
  .nsm_client_hash_param.flags = HT_FLAG_NONE,

  /* NLM Client hash */
  .nlm_client_hash_param.index_size = PRIME_STATE,
  .nlm_client_hash_param.hash_func_key = nlm_client_value_hash_func,
  .nlm_client_hash_param.hash_func_rbt = nlm_client_rbt_hash_func,
  .nlm_client_hash_param.compare_key = compare_nlm_client_key,
  .nlm_client_hash_param.key_to_str = display_nlm_client_key,
  .nlm_client_hash_param.val_to_str = display_nlm_client_val,
  .nlm_client_hash_param.flags = HT_FLAG_NONE,

  /* NLM Owner hash */
  .nlm_owner_hash_param.index_size = PRIME_STATE,
  .nlm_owner_hash_param.hash_func_key = nlm_owner_value_hash_func,
  .nlm_owner_hash_param.hash_func_rbt = nlm_owner_rbt_hash_func,
  .nlm_owner_hash_param.compare_key = compare_nlm_owner_key,
  .nlm_owner_hash_param.key_to_str = display_nlm_owner_key,
  .nlm_owner_hash_param.val_to_str = display_nlm_owner_val,
  .nlm_owner_hash_param.flags = HT_FLAG_NONE,

#ifdef _USE_9P
  /* 9P Owner hash */
  ._9p_owner_hash_param.index_size = PRIME_STATE,
  ._9p_owner_hash_param.hash_func_key = _9p_owner_value_hash_func,
  ._9p_owner_hash_param.hash_func_rbt = _9p_owner_rbt_hash_func,
  ._9p_owner_hash_param.compare_key = compare_9p_owner_key,
  ._9p_owner_hash_param.key_to_str = display_9p_owner_key,
  ._9p_owner_hash_param.val_to_str = display_9p_owner_val,
  ._9p_owner_hash_param.flags = HT_FLAG_NONE,
#endif

  /* Cache inode parameters : cookie hash table */
  .cache_param.cookie_param.index_size = PRIME_STATE,
  .cache_param.cookie_param.hash_func_key = lock_cookie_value_hash_func ,
  .cache_param.cookie_param.hash_func_rbt = lock_cookie_rbt_hash_func ,
  .cache_param.cookie_param.compare_key = compare_lock_cookie_key,
  .cache_param.cookie_param.key_to_str = display_lock_cookie_key,
  .cache_param.cookie_param.val_to_str = display_lock_cookie_val,
  .cache_param.cookie_param.flags = HT_FLAG_NONE,

  .cache_param.nparts = 7,
  .cache_param.expire_type_attr = CACHE_INODE_EXPIRE_NEVER,
  .cache_param.expire_type_link = CACHE_INODE_EXPIRE_NEVER,
  .cache_param.expire_type_dirent = CACHE_INODE_EXPIRE_NEVER,
  .cache_param.getattr_dir_invalidation = false,

  /* Cache inode parameters : Garbage collection policy */
  .cache_param.entries_hwmark = 100000,
  .cache_param.entries_lwmark = 50000,
  .cache_param.use_fd_cache = true,
  .cache_param.lru_run_interval = 600,
  .cache_param.fd_limit_percent = 99,
  .cache_param.fd_hwmark_percent = 90,
  .cache_param.fd_lwmark_percent = 50,
  .cache_param.reaper_work = 1000,
  .cache_param.biggest_window = 40,
  .cache_param.required_progress = 5,
  .cache_param.futility_count = 8,

};

/* ServerEpoch is ServerBootTime unless overriden by -E command line option */
struct timespec ServerBootTime;
time_t ServerEpoch;

verifier4 NFS4_write_verifier;  /* NFS V4 write verifier */
writeverf3 NFS3_write_verifier; /* NFS V3 write verifier */

/* node ID used to identify an individual node in a cluster */
ushort g_nodeid = 0;

nfs_start_info_t nfs_start_info;

pthread_t admin_thrid;
pthread_t sigmgr_thrid;
pthread_t gsh_dbus_thrid;

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
  LogDebug(COMPONENT_THREAD, "sigmgr thread exiting");

  admin_halt();

  /* Might as well exit - no need for this thread any more */
  return NULL;
}

/**
 * @brief Initialize NFSd prerequisites
 *
 * @param[in] program_name Name of the program
 * @param[in] host_name    Server host name
 * @param[in] debug_level  Debug level
 * @param[in] log_path     Log path
 */
void nfs_prereq_init(char *program_name,
		     char *host_name,
		     int debug_level,
		     char *log_path)
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
  printf("\tLong_Processing_Threshold = %"PRIu64" ; \n",
         nfs_param.core_param.long_processing_threshold);
  printf("\tDecoder_Fridge_Expiration_Delay = %"PRIu64" ; \n",
         nfs_param.core_param.decoder_fridge_expiration_delay);
  printf("\tDecoder_Fridge_Block_Timeout = %"PRIu64" ; \n",
	 nfs_param.core_param.decoder_fridge_block_timeout);

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
 * @param[in]  config_struct Parsed config file
 * @param[out] p_start_info  Startup parameters
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
        LogEvent(COMPONENT_INIT,
             "No core configuration found in config file, using default");
      else
        LogDebug(COMPONENT_INIT,
                 "core configuration read from config file");
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

  /* Cache inode client parameters */
  if((cache_inode_status
      = cache_inode_read_conf_parameter(config_struct,
                                        &nfs_param.cache_param))
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
      !is_prime(nfs_param.client_id_param.cid_unconfirmed_hash_param.index_size) ||
      !is_prime(nfs_param.client_id_param.cid_confirmed_hash_param.index_size) ||
      !is_prime(nfs_param.client_id_param.cr_hash_param.index_size) ||
      !is_prime(nfs_param.state_id_param.index_size) ||
      !is_prime(nfs_param.session_id_param.index_size) ||
      !is_prime(nfs_param.nfs4_owner_param.index_size) ||
      !is_prime(nfs_param.nsm_client_hash_param.index_size) ||
      !is_prime(nfs_param.nlm_client_hash_param.index_size) ||
      !is_prime(nfs_param.nlm_owner_hash_param.index_size) ||
      !is_prime(nfs_param.cache_param.cookie_param.index_size))
  {
      LogCrit(COMPONENT_INIT, "BAD PARAMETER(s) : expected primes");
  }

  return 0;
}

static void nfs_Start_threads(void)
{
  int rc = 0;
  pthread_attr_t attr_thr;

  LogDebug(COMPONENT_THREAD,
           "Starting threads");

  /* Init for thread parameter (mostly for scheduling) */
  if(pthread_attr_init(&attr_thr) != 0)
    LogDebug(COMPONENT_THREAD, "can't init pthread's attributes");

  if(pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
    LogDebug(COMPONENT_THREAD, "can't set pthread's scope");

  if(pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE) != 0)
    LogDebug(COMPONENT_THREAD, "can't set pthread's join state");

  LogEvent(COMPONENT_THREAD,
	   "Starting delayed executor.");
  delayed_start();

  /* Starting the thread dedicated to signal handling */
  if( ( rc = pthread_create( &sigmgr_thrid, &attr_thr, sigmgr_thread, NULL ) ) != 0 )
    {
      LogFatal(COMPONENT_THREAD,
               "Could not create sigmgr_thread, error = %d (%s)",
               errno, strerror(errno));
    }
  LogDebug(COMPONENT_THREAD,
           "sigmgr thread started");

  rc = worker_init();
  if (rc != 0)
    {
      LogFatal(COMPONENT_THREAD,
	       "Could not start worker threads: %d",
	       errno);
    }

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

  /* Starting the reaper thread */
  rc = reaper_init();
  if(rc != 0)
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
  gsh_client_init();
  gsh_export_init();  /* here for now since triggered by dbus stats */

  if (nfs_param.core_param.enable_FSAL_upcalls)
    {
      rc = fsal_up_init();
      if (rc != 0)
	{
	  LogFatal(COMPONENT_INIT,
		   "FSAL upcall system could not be initialized: %d",
		   rc);
	}
    }

  /* Cache Inode Initialisation */
  cache_status = cache_inode_init();
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Cache Inode Layer could not be initialized, status=%s",
               cache_inode_err_str(cache_status));
    }

  state_status = state_lock_init(nfs_param.cache_param.cookie_param);
  if(state_status != STATE_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "State Lock Layer could not be initialized, status=%s",
               state_err_str(state_status));
    }
  LogInfo(COMPONENT_INIT, "Cache Inode library successfully initialized");

  /* Cache Inode LRU (call this here, rather than as part of
     cache_inode_init() so the GC policy has been set */
  rc = cache_inode_lru_pkginit();
  if (rc != 0) {
	  LogFatal(COMPONENT_INIT,
		   "Unable to initialize LRU subsystem: %d.",
		   rc);
  }

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

  /* Admin initialisation */
  nfs_Init_admin_thread();

  LogEvent(COMPONENT_INIT,
	   "Initializing IdMapper.");
  if (!idmapper_init())
    LogFatal(COMPONENT_INIT,
	     "Failed initializing IdMapper.");
  else
    LogEvent(COMPONENT_INIT,
	     "IdMapper successfully initialized.");

  /* Init the NFSv4 Clientid cache */
  LogDebug(COMPONENT_INIT, "Now building NFSv4 clientid cache");
  if(nfs_Init_client_id(&nfs_param.client_id_param) != CLIENT_ID_SUCCESS)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing NFSv4 clientid cache");
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 clientid cache successfully initialized");

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

  /* Init The NFSv4 State id cache */
  LogDebug(COMPONENT_INIT, "Now building NFSv4 State Id cache");
  if(nfs4_Init_state_id(&nfs_param.state_id_param) != 0)
    {
      LogFatal(COMPONENT_INIT,
               "Error while initializing NFSv4 State Id cache");
    }
  LogInfo(COMPONENT_INIT,
          "NFSv4 State Id cache successfully initialized");

  /* Init The NFSv4 Open Owner cache */
  LogDebug(COMPONENT_INIT, "Now building NFSv4 Owner cache");
  if(Init_nfs4_owner(&nfs_param.nfs4_owner_param) != 0)
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
  if(nfs41_Init_session_id(&nfs_param.session_id_param) != 0)
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
      LogDebug(COMPONENT_INIT, "core size rlimit set to %ld",
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

  if(nfs_param.ip_name_param.mapfile[0] == '\0')
    {
      LogDebug(COMPONENT_INIT, "No Hosts Map file is used");
    }
  else
    {
      LogEvent(COMPONENT_INIT, "Populating IP_NAME with file %s",
               nfs_param.ip_name_param.mapfile);
      if(nfs_ip_name_populate(nfs_param.ip_name_param.mapfile) != IP_NAME_SUCCESS)
        LogDebug(COMPONENT_INIT, "IP_NAME was NOT populated");
    }

  LogEvent(COMPONENT_INIT,
	   "-------------------------------------------------");
  LogEvent(COMPONENT_INIT,
	   "             NFS SERVER INITIALIZED");
  LogEvent(COMPONENT_INIT,
	   "-------------------------------------------------");

  /* Wait for dispatcher to exit */
  LogDebug(COMPONENT_THREAD,
           "Wait for admin thread to exit");
  pthread_join(admin_thrid, NULL);

  /* Regular exit */
  LogEvent(COMPONENT_MAIN,
           "NFS EXIT: regular exit");

  /* if not in grace period, clean up the old state directory */
  if(!nfs_in_grace())
    nfs4_clean_old_recov_dir();

  Cleanup();

  /* let main return 0 to exit */
}
