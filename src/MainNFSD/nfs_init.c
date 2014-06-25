/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
#include "nfs_file_handle.h"
#include "nfs_exports.h"
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
#include "delayed_exec.h"
#include "client_mgr.h"
#include "export_mgr.h"
#ifdef USE_CAPS
#include <sys/capability.h>	/* For capget/capset */
#endif
#include "uid2grp.h"


/* global information exported to all layers (as extern vars) */
nfs_parameter_t nfs_param;

/* ServerEpoch is ServerBootTime unless overriden by -E command line option */
struct timespec ServerBootTime;
time_t ServerEpoch;

verifier4 NFS4_write_verifier;	/* NFS V4 write verifier */
writeverf3 NFS3_write_verifier;	/* NFS V3 write verifier */

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
pthread_t _9p_rdma_dispatcher_thrid;
#endif

char *config_path = "/etc/ganesha/ganesha.conf";

char *pidfile_path = "/var/run/ganesha.pid";

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
	while (signal_caught != SIGTERM) {
		sigset_t signals_to_catch;
		sigemptyset(&signals_to_catch);
		sigaddset(&signals_to_catch, SIGTERM);
		sigaddset(&signals_to_catch, SIGHUP);
		if (sigwait(&signals_to_catch, &signal_caught) != 0) {
			LogFullDebug(COMPONENT_THREAD,
				     "sigwait exited with error");
			continue;
		}
		if (signal_caught == SIGHUP) {
			LogEvent(COMPONENT_MAIN,
				 "SIGHUP_HANDLER: Received SIGHUP.... initiating export list reload");
			admin_replace_exports();
			reread_log_config();
			svcauth_gss_release_cred();
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
void nfs_prereq_init(char *program_name, char *host_name, int debug_level,
		     char *log_path)
{
	/* Initialize logging */
	SetNamePgm(program_name);
	SetNameFunction("main");
	SetNameHost(host_name);

	init_logging(log_path, debug_level);
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
	printf("\tNb_Worker = %u ;\n", nfs_param.core_param.nb_worker);
	printf("\tDRC_TCP_Npart = %u ;\n", nfs_param.core_param.drc.tcp.npart);
	printf("\tDRC_TCP_Size = %u ;\n", nfs_param.core_param.drc.tcp.size);
	printf("\tDRC_TCP_Cachesz = %u ;\n",
	       nfs_param.core_param.drc.tcp.cachesz);
	printf("\tDRC_TCP_Hiwat = %u ;\n", nfs_param.core_param.drc.tcp.hiwat);
	printf("\tDRC_TCP_Recycle_Npart = %u ;\n",
	       nfs_param.core_param.drc.tcp.recycle_npart);
	printf("\tDRC_TCP_Recycle_Expire_S = %u ;\n",
	       nfs_param.core_param.drc.tcp.recycle_expire_s);
	printf("\tDRC_TCP_Checksum = %u ;\n",
	       nfs_param.core_param.drc.tcp.checksum);
	printf("\tDRC_UDP_Npart = %u ;\n", nfs_param.core_param.drc.udp.npart);
	printf("\tDRC_UDP_Size = %u ;\n", nfs_param.core_param.drc.udp.size);
	printf("\tDRC_UDP_Cachesz = %u ;\n",
	       nfs_param.core_param.drc.udp.cachesz);
	printf("\tDRC_UDP_Hiwat = %u ;\n", nfs_param.core_param.drc.udp.hiwat);
	printf("\tDRC_UDP_Checksum = %u ;\n",
	       nfs_param.core_param.drc.udp.checksum);
	printf("\tDecoder_Fridge_Expiration_Delay = %" PRIu64 " ;\n",
	       nfs_param.core_param.decoder_fridge_expiration_delay);
	printf("\tDecoder_Fridge_Block_Timeout = %" PRIu64 " ;\n",
	       nfs_param.core_param.decoder_fridge_block_timeout);

	printf("\tManage_Gids_Expiration = %" PRIu64 " ;\n",
	       nfs_param.core_param.manage_gids_expiration);

	if (nfs_param.core_param.drop_io_errors)
		printf("\tDrop_IO_Errors = true ;\n");
	else
		printf("\tDrop_IO_Errors = false ;\n");

	if (nfs_param.core_param.drop_inval_errors)
		printf("\tDrop_Inval_Errors = true ;\n");
	else
		printf("\tDrop_Inval_Errors = false ;\n");

	if (nfs_param.core_param.drop_delay_errors)
		printf("\tDrop_Delay_Errors = true ;\n");
	else
		printf("\tDrop_Delay_Errors = false ;\n");

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
int nfs_set_param_from_conf(config_file_t parse_tree,
			    nfs_start_info_t *p_start_info)
{
	struct config_error_type err_type;

	/*
	 * Initialize exports and clients so config parsing can use them
	 * early.
	 */
	client_pkginit();
	export_pkginit();

	/* Core parameters */
	(void) load_config_from_parse(parse_tree,
				    &nfs_core,
				    &nfs_param.core_param,
				    true,
				    &err_type);
	if (!config_error_is_harmless(&err_type)) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing core configuration");
		return -1;
	}

	/* Worker paramters: ip/name hash table and expiration for each entry */
	(void) load_config_from_parse(parse_tree,
				    &nfs_ip_name,
				    NULL,
				    true,
				    &err_type);
	if (!config_error_is_harmless(&err_type)) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing IP/name configuration");
		return -1;
	}

#ifdef _HAVE_GSSAPI
	/* NFS kerberos5 configuration */
	(void) load_config_from_parse(parse_tree,
				    &krb5_param,
				    &nfs_param.krb5_param,
				    true,
				    &err_type);
	if (!config_error_is_harmless(&err_type)) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing NFS/KRB5 configuration for RPCSEC_GSS");
		return -1;
	}
#endif

	/* NFSv4 specific configuration */
	(void) load_config_from_parse(parse_tree,
				    &version4_param,
				    &nfs_param.nfsv4_param,
				    true,
				    &err_type);
	if (!config_error_is_harmless(&err_type)) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing NFSv4 specific configuration");
		return -1;
	}

#ifdef _USE_9P
	(void) load_config_from_parse(parse_tree,
				      &_9p_param_blk,
				      NULL,
				      true,
				      &err_type);
	if (!config_error_is_harmless(&err_type)) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing 9P specific configuration");
		return -1;
	}
#endif

	/* Cache inode client parameters */
	(void) load_config_from_parse(parse_tree,
				    &cache_inode_param_blk,
				    NULL,
				    true,
				    &err_type);
	if (!config_error_is_harmless(&err_type)) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing 9P specific configuration");
		return -1;
	}

	LogEvent(COMPONENT_INIT, "Configuration file successfully parsed");

	return 0;
}

int init_server_pkgs(void)
{
	cache_inode_status_t cache_status;
	state_status_t state_status;

	/* init uid2grp cache */
	uid2grp_cache_init();

	/* Cache Inode Initialisation */
	cache_status = cache_inode_init();
	if (cache_status != CACHE_INODE_SUCCESS) {
		LogCrit(COMPONENT_INIT,
			"Cache Inode Layer could not be initialized, status=%s",
			cache_inode_err_str(cache_status));
		return -1;
	}

	state_status = state_lock_init();
	if (state_status != STATE_SUCCESS) {
		LogCrit(COMPONENT_INIT,
			"State Lock Layer could not be initialized, status=%s",
			state_err_str(state_status));
		return -1;
	}
	LogInfo(COMPONENT_INIT, "Cache Inode library successfully initialized");

	/* Init the IP/name cache */
	LogDebug(COMPONENT_INIT, "Now building IP/name cache");
	if (nfs_Init_ip_name() != IP_NAME_SUCCESS) {
		LogCrit(COMPONENT_INIT,
			"Error while initializing IP/name cache");
		return -1;
	}
	LogInfo(COMPONENT_INIT, "IP/name cache successfully initialized");

	LogEvent(COMPONENT_INIT, "Initializing ID Mapper.");
	if (!idmapper_init()) {
		LogCrit(COMPONENT_INIT, "Failed initializing ID Mapper.");
		return -1;
	}
	LogEvent(COMPONENT_INIT, "ID Mapper successfully initialized.");
	return 0;
}

static void nfs_Start_threads(void)
{
	int rc = 0;
	pthread_attr_t attr_thr;

	LogDebug(COMPONENT_THREAD, "Starting threads");

	/* Init for thread parameter (mostly for scheduling) */
	if (pthread_attr_init(&attr_thr) != 0)
		LogDebug(COMPONENT_THREAD, "can't init pthread's attributes");

	if (pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM) != 0)
		LogDebug(COMPONENT_THREAD, "can't set pthread's scope");

	if (pthread_attr_setdetachstate(&attr_thr,
					PTHREAD_CREATE_JOINABLE) != 0)
		LogDebug(COMPONENT_THREAD, "can't set pthread's join state");

	LogEvent(COMPONENT_THREAD, "Starting delayed executor.");
	delayed_start();

	/* Starting the thread dedicated to signal handling */
	rc = pthread_create(&sigmgr_thrid, &attr_thr, sigmgr_thread, NULL);
	if (rc != 0) {
		LogFatal(COMPONENT_THREAD,
			 "Could not create sigmgr_thread, error = %d (%s)",
			 errno, strerror(errno));
	}
	LogDebug(COMPONENT_THREAD, "sigmgr thread started");

	rc = worker_init();
	if (rc != 0) {
		LogFatal(COMPONENT_THREAD, "Could not start worker threads: %d",
			 errno);
	}

	/* Start event channel service threads */
	nfs_rpc_dispatch_threads(&attr_thr);

#ifdef _USE_9P
	/* Starting the 9P/TCP dispatcher thread */
	rc = pthread_create(&_9p_dispatcher_thrid, &attr_thr,
			    _9p_dispatcher_thread, NULL);
	if (rc != 0) {
		LogFatal(COMPONENT_THREAD,
			 "Could not create  9P/TCP dispatcher, error = %d (%s)",
			 errno, strerror(errno));
	}
	LogEvent(COMPONENT_THREAD,
		 "9P/TCP dispatcher thread was started successfully");
#endif

#ifdef _USE_9P_RDMA
	/* Starting the 9P/RDMA dispatcher thread */
	rc = pthread_create(&_9p_rdma_dispatcher_thrid, &attr_thr,
			    _9p_rdma_dispatcher_thread, NULL);
	if (rc != 0) {
		LogFatal(COMPONENT_THREAD,
			 "Could not create  9P/RDMA dispatcher, error = %d (%s)",
			 errno, strerror(errno));
	}
	LogEvent(COMPONENT_THREAD,
		 "9P/RDMA dispatcher thread was started successfully");
#endif

#ifdef USE_DBUS
	/* DBUS event thread */
	rc = pthread_create(&gsh_dbus_thrid, &attr_thr, gsh_dbus_thread, NULL);
	if (rc != 0) {
		LogFatal(COMPONENT_THREAD,
			 "Could not create gsh_dbus_thread, error = %d (%s)",
			 errno, strerror(errno));
	}
	LogEvent(COMPONENT_THREAD, "gsh_dbusthread was started successfully");
#endif

	/* Starting the admin thread */
	rc = pthread_create(&admin_thrid, &attr_thr, admin_thread, NULL);
	if (rc != 0) {
		LogFatal(COMPONENT_THREAD,
			 "Could not create admin_thread, error = %d (%s)",
			 errno, strerror(errno));
	}
	LogEvent(COMPONENT_THREAD, "admin thread was started successfully");

	/* Starting the reaper thread */
	rc = reaper_init();
	if (rc != 0) {
		LogFatal(COMPONENT_THREAD,
			 "Could not create reaper_thread, error = %d (%s)",
			 errno, strerror(errno));
	}
	LogEvent(COMPONENT_THREAD, "reaper thread was started successfully");

	/* Starting the general fridge */
	rc = general_fridge_init();
	if (rc != 0) {
		LogFatal(COMPONENT_THREAD,
			 "Could not create general fridge, error = %d (%s)",
			 errno, strerror(errno));
	}
	LogEvent(COMPONENT_THREAD, "General fridge was started successfully");

}

/**
 * @brief Init the nfs daemon
 *
 * @param[in] p_start_info Unused
 */

static void nfs_Init(const nfs_start_info_t *p_start_info)
{
	int rc = 0;
#ifdef _HAVE_GSSAPI
	gss_buffer_desc gss_service_buf;
	OM_uint32 maj_stat, min_stat;
	char GssError[MAXNAMLEN + 1];
#endif

#ifdef USE_DBUS
	/* DBUS init */
	gsh_dbus_pkginit();
	dbus_export_init();
	dbus_client_init();
#endif

	/* Cache Inode LRU (call this here, rather than as part of
	   cache_inode_init() so the GC policy has been set */
	rc = cache_inode_lru_pkginit();
	if (rc != 0) {
		LogFatal(COMPONENT_INIT,
			 "Unable to initialize LRU subsystem: %d.", rc);
	}

	/* acls cache may be needed by exports_pkginit */
	LogDebug(COMPONENT_INIT, "Now building NFSv4 ACL cache");
	if (nfs4_acls_init() != 0)
		LogFatal(COMPONENT_INIT, "Error while initializing NFSv4 ACLs");
	LogInfo(COMPONENT_INIT, "NFSv4 ACL cache successfully initialized");

	/* finish the job with exports by caching the root entries
	 */
	exports_pkginit();

	nfs41_session_pool =
	    pool_init("NFSv4.1 session pool", sizeof(nfs41_session_t),
		      pool_basic_substrate, NULL, NULL, NULL);
	if (!nfs41_session_pool)
		LogFatal(COMPONENT_INIT,
			 "Error while allocating NFSv4.1 session pool");

	request_pool =
	    pool_init("Request pool", sizeof(request_data_t),
		      pool_basic_substrate, NULL,
		      NULL /* FASTER constructor_request_data_t */ ,
		      NULL);
	if (!request_pool)
		LogFatal(COMPONENT_INIT,
			 "Error while allocating request pool");

	request_data_pool =
	    pool_init("Request Data Pool", sizeof(nfs_request_data_t),
		      pool_basic_substrate, NULL,
		      NULL /* FASTER constructor_nfs_request_data_t */ ,
		      NULL);
	if (!request_data_pool)
		LogFatal(COMPONENT_INIT,
			"Error while allocating request data pool");

	dupreq_pool =
	    pool_init("Duplicate Request Pool", sizeof(dupreq_entry_t),
		      pool_basic_substrate, NULL, NULL, NULL);
	if (!(dupreq_pool))
		LogFatal(COMPONENT_INIT,
			"Error while allocating duplicate request pool");

	/* If rpcsec_gss is used, set the path to the keytab */
#ifdef _HAVE_GSSAPI
#ifdef HAVE_KRB5
	if (nfs_param.krb5_param.active_krb5) {
		OM_uint32 gss_status = GSS_S_COMPLETE;

		if (*nfs_param.krb5_param.keytab != '\0')
			gss_status =
			    krb5_gss_register_acceptor_identity(nfs_param.
								krb5_param.
								keytab);

		if (gss_status != GSS_S_COMPLETE) {
			log_sperror_gss(GssError, gss_status, 0);
			LogFatal(COMPONENT_INIT,
				 "Error setting krb5 keytab to value %s is %s",
				 nfs_param.krb5_param.keytab, GssError);
		}
		LogInfo(COMPONENT_INIT,
			"krb5 keytab path successfully set to %s",
			nfs_param.krb5_param.keytab);
#endif				/* HAVE_KRB5 */

		/* Set up principal to be use for GSSAPPI within GSSRPC/KRB5 */
		gss_service_buf.value = nfs_param.krb5_param.svc.principal;
		gss_service_buf.length =
			strlen(nfs_param.krb5_param.svc.principal) + 1;
		/* The '+1' is not to be forgotten, for the '\0' at the end */

		maj_stat = gss_import_name(&min_stat, &gss_service_buf,
					   (gss_OID) GSS_C_NT_HOSTBASED_SERVICE,
					   &nfs_param.krb5_param.svc.gss_name);
		if (maj_stat != GSS_S_COMPLETE) {
			log_sperror_gss(GssError, maj_stat, min_stat);
			LogFatal(COMPONENT_INIT,
				 "Error importing gss principal %s is %s",
				 nfs_param.krb5_param.svc.principal, GssError);
		}

		if (nfs_param.krb5_param.svc.gss_name == GSS_C_NO_NAME)
			LogInfo(COMPONENT_INIT,
				"Regression:  svc.gss_name == GSS_C_NO_NAME");

		LogInfo(COMPONENT_INIT, "gss principal \"%s\" successfully set",
			nfs_param.krb5_param.svc.principal);

		/* Set the principal to GSSRPC */
		if (!svcauth_gss_set_svc_name
		    (nfs_param.krb5_param.svc.gss_name)) {
			LogFatal(COMPONENT_INIT,
				 "Impossible to set gss principal to GSSRPC");
		}

		/* Don't release name until shutdown, it will be used by the
		 * backchannel. */

#ifdef HAVE_KRB5
	}			/*  if( nfs_param.krb5_param.active_krb5 ) */
#endif				/* HAVE_KRB5 */
#endif				/* _HAVE_GSSAPI */

	/* RPC Initialisation - exits on failure */
	nfs_Init_svc();
	LogInfo(COMPONENT_INIT, "RPC ressources successfully initialized");

	/* Admin initialisation */
	nfs_Init_admin_thread();

	/* Init the NFSv4 Clientid cache */
	LogDebug(COMPONENT_INIT, "Now building NFSv4 clientid cache");
	if (nfs_Init_client_id() !=
	    CLIENT_ID_SUCCESS) {
		LogFatal(COMPONENT_INIT,
			 "Error while initializing NFSv4 clientid cache");
	}
	LogInfo(COMPONENT_INIT,
		"NFSv4 clientid cache successfully initialized");

	/* Init duplicate request cache */
	dupreq2_pkginit();
	LogInfo(COMPONENT_INIT,
		"duplicate request hash table cache successfully initialized");

	/* Init The NFSv4 State id cache */
	LogDebug(COMPONENT_INIT, "Now building NFSv4 State Id cache");
	if (nfs4_Init_state_id() != 0) {
		LogFatal(COMPONENT_INIT,
			 "Error while initializing NFSv4 State Id cache");
	}
	LogInfo(COMPONENT_INIT,
		"NFSv4 State Id cache successfully initialized");

	/* Init The NFSv4 Open Owner cache */
	LogDebug(COMPONENT_INIT, "Now building NFSv4 Owner cache");
	if (Init_nfs4_owner() != 0) {
		LogFatal(COMPONENT_INIT,
			 "Error while initializing NFSv4 Owner cache");
	}
	LogInfo(COMPONENT_INIT,
		"NFSv4 Open Owner cache successfully initialized");

	if (nfs_param.core_param.enable_NLM) {
		/* Init The NLM Owner cache */
		LogDebug(COMPONENT_INIT, "Now building NLM Owner cache");
		if (Init_nlm_hash() != 0) {
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
	if (Init_9p_hash() != 0) {
		LogFatal(COMPONENT_INIT,
			 "Error while initializing 9P Owner cache");
	}
	LogInfo(COMPONENT_INIT, "9P Owner cache successfully initialized");
#endif

	LogDebug(COMPONENT_INIT, "Now building NFSv4 Session Id cache");
	if (nfs41_Init_session_id() != 0) {
		LogFatal(COMPONENT_INIT,
			 "Error while initializing NFSv4 Session Id cache");
	}
	LogInfo(COMPONENT_INIT,
		"NFSv4 Session Id cache successfully initialized");


#ifdef _USE_9P
	LogDebug(COMPONENT_INIT, "Now building 9P resources");
	if (_9p_init()) {
		LogCrit(COMPONENT_INIT,
			"Error while initializing 9P Resources");
		exit(1);
	}
	LogInfo(COMPONENT_INIT, "9P resources successfully initialized");
#endif				/* _USE_9P */

	/* Creates the pseudo fs */
	LogDebug(COMPONENT_INIT, "Now building pseudo fs");

	create_pseudofs();

	LogInfo(COMPONENT_INIT,
		"NFSv4 pseudo file system successfully initialized");

	/* Save Ganesha thread credentials with Frank's routine for later use */
	fsal_save_ganesha_credentials();

	/* Create stable storage directory, this needs to be done before
	 * starting the recovery thread.
	 */
	nfs4_create_recov_dir();

	/* initialize grace and read in the client IDs */
	nfs4_init_grace();
	nfs4_load_recov_clids(NULL);

	/* Start grace period */
	nfs4_start_grace(NULL);

	/* callback dispatch */
	nfs_rpc_cb_pkginit();
#ifdef _USE_CB_SIMULATOR
	nfs_rpc_cbsim_pkginit();
#endif				/*  _USE_CB_SIMULATOR */

}				/* nfs_Init */

#ifdef USE_CAPS
/**
 * @brief Lower my capabilities (privs) so quotas work right
 *
 * This will/should be moved to set_credentials where it belongs
 * Deal with capabilities in order to remove CAP_SYS_RESOURCE (needed
 * for proper management of data quotas)
 */

static void lower_my_caps(void)
{
	struct __user_cap_header_struct caphdr = {
		.version = _LINUX_CAPABILITY_VERSION
	};
	cap_user_data_t capdata;
	ssize_t capstrlen = 0;
	cap_t my_cap;
	char *cap_text;
	int capsz;

	(void) capget(&caphdr, NULL);
	switch (caphdr.version) {
	case _LINUX_CAPABILITY_VERSION_1:
		capsz = _LINUX_CAPABILITY_U32S_1;
		break;
	case _LINUX_CAPABILITY_VERSION_2:
		capsz = _LINUX_CAPABILITY_U32S_2;
		break;
	default:
		abort(); /* can't happen */
	}

	capdata = gsh_calloc(capsz, sizeof(struct __user_cap_data_struct));
	caphdr.pid = getpid();

	if (capget(&caphdr, capdata) != 0)
		LogFatal(COMPONENT_INIT,
			 "Failed to query capabilities for process, errno=%u",
			 errno);

	/* Set the capability bitmask to remove CAP_SYS_RESOURCE */
	if (capdata->effective & CAP_TO_MASK(CAP_SYS_RESOURCE))
		capdata->effective &= ~CAP_TO_MASK(CAP_SYS_RESOURCE);

	if (capdata->permitted & CAP_TO_MASK(CAP_SYS_RESOURCE))
		capdata->permitted &= ~CAP_TO_MASK(CAP_SYS_RESOURCE);

	if (capdata->inheritable & CAP_TO_MASK(CAP_SYS_RESOURCE))
		capdata->inheritable &= ~CAP_TO_MASK(CAP_SYS_RESOURCE);

	if (capset(&caphdr, capdata) != 0)
		LogFatal(COMPONENT_INIT,
			 "Failed to set capabilities for process, errno=%u",
			 errno);
	else
		LogEvent(COMPONENT_INIT,
			 "CAP_SYS_RESOURCE was successfully removed for proper quota management in FSAL");

	/* Print newly set capabilities (same as what CLI "getpcaps" displays */
	my_cap = cap_get_proc();
	cap_text = cap_to_text(my_cap, &capstrlen);
	LogEvent(COMPONENT_INIT, "currenty set capabilities are: %s",
		 cap_text);
	cap_free(cap_text);
	cap_free(my_cap);
	gsh_free(capdata);
}
#endif

/**
 * @brief Start NFS service
 *
 * @param[in] p_start_info Startup parameters
 */
void nfs_start(nfs_start_info_t *p_start_info)
{
	/* store the start info so it is available for all layers */
	nfs_start_info = *p_start_info;

	if (p_start_info->dump_default_config == true) {
		nfs_print_param_config();
		exit(0);
	}

	/* Make sure Ganesha runs with a 0000 umask. */
	umask(0000);

	{
		/* Set the write verifiers */
		union {
			verifier4 NFS4_write_verifier;
			writeverf3 NFS3_write_verifier;
			uint64_t epoch;
		} build_verifier;

		build_verifier.epoch = (uint64_t) ServerEpoch;

		memcpy(NFS3_write_verifier, build_verifier.NFS3_write_verifier,
		       sizeof(NFS3_write_verifier));
		memcpy(NFS4_write_verifier, build_verifier.NFS4_write_verifier,
		       sizeof(NFS4_write_verifier));
	}

#ifdef USE_CAPS
	lower_my_caps();
#endif

	/* Initialize all layers and service threads */
	nfs_Init(p_start_info);

	/* Spawns service threads */
	nfs_Start_threads();

	if (nfs_param.core_param.enable_NLM) {
		/* NSM Unmonitor all */
		nsm_unmonitor_all();
	}

	LogEvent(COMPONENT_INIT,
		 "-------------------------------------------------");
	LogEvent(COMPONENT_INIT, "             NFS SERVER INITIALIZED");
	LogEvent(COMPONENT_INIT,
		 "-------------------------------------------------");

	/* Wait for dispatcher to exit */
	LogDebug(COMPONENT_THREAD, "Wait for admin thread to exit");
	pthread_join(admin_thrid, NULL);

	/* Regular exit */
	LogEvent(COMPONENT_MAIN, "NFS EXIT: regular exit");

	/* if not in grace period, clean up the old state directory */
	if (!nfs_in_grace())
		nfs4_clean_old_recov_dir(v4_old_dir);

	Cleanup();

	/* let main return 0 to exit */
}
