// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "nfs_init.h"
#include "log.h"
#include "fsal.h"
#include "rquota.h"
#include "nfs_core.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_ip_stats.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"
#include "nfs4_acls.h"
#include "nfs_rpc_callback.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include "FSAL/fsal_commonlib.h"
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
#endif /* _USE_NLM */
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
#include "netgroup_cache.h"
#include "pnfs_utils.h"
#include "mdcache.h"
#include "common_utils.h"
#include "nfs_init.h"
#include <urcu-bp.h>
#include "conf_url.h"
#include "FSAL/fsal_localfs.h"

/**
 * @brief init_complete used to indicate if ganesha is during
 * startup or not
 */
struct nfs_init nfs_init;

/* global information exported to all layers (as extern vars) */
nfs_parameter_t nfs_param;
struct _nfs_health nfs_health_;

static struct _nfs_health healthstats;

/* ServerEpoch is ServerBootTime unless overriden by -E command line option */
struct timespec nfs_ServerBootTime;
time_t nfs_ServerEpoch;

verifier4 NFS4_write_verifier;	/* NFS V4 write verifier */
writeverf3 NFS3_write_verifier;	/* NFS V3 write verifier */

/* node ID used to identify an individual node in a cluster */
int g_nodeid;

nfs_start_info_t nfs_start_info;

pthread_t admin_thrid;
pthread_t sigmgr_thrid;

tirpc_pkg_params ntirpc_pp = {
	TIRPC_DEBUG_FLAG_DEFAULT,
	0,
	SetNameFunction,
	(mem_format_t)rpc_warnx,
	gsh_free_size,
	gsh_malloc__,
	gsh_malloc_aligned__,
	gsh_calloc__,
	gsh_realloc__,
};

#ifdef _USE_9P
pthread_t _9p_dispatcher_thrid;
#endif

#ifdef _USE_9P_RDMA
pthread_t _9p_rdma_dispatcher_thrid;
#endif

#ifdef _USE_NFS_RDMA
pthread_t nfs_rdma_dispatcher_thrid;
#endif

char *nfs_config_path = GANESHA_CONFIG_PATH;

char *nfs_pidfile_path = GANESHA_PIDFILE_PATH;

/**
 * @brief Reread the configuration file to accomplish update of options.
 *
 * The following option blocks are currently supported for update:
 *
 * LOG {}
 * LOG { COMPONENTS {} }
 * LOG { FACILITY {} }
 * LOG { FORMAT {} }
 * EXPORT {}
 * EXPORT { CLIENT {} }
 *
 */

struct config_error_type err_type;

void reread_config(void)
{
	int status = 0;
	config_file_t config_struct;

	/* If no configuration file is given, then the caller must want to
	 * reparse the configuration file from startup.
	 */
	if (nfs_config_path[0] == '\0') {
		LogCrit(COMPONENT_CONFIG,
			"No configuration file was specified for reloading log config.");
		return;
	}

	/* Create a memstream for parser+processing error messages */
	if (!init_error_type(&err_type))
		return;
	/* Attempt to parse the new configuration file */
	config_struct = config_ParseFile(nfs_config_path, &err_type);
	if (!config_error_no_error(&err_type)) {
		config_Free(config_struct);
		LogCrit(COMPONENT_CONFIG,
			"Error while parsing new configuration file %s",
			nfs_config_path);
		report_config_errors(&err_type, NULL, config_errs_to_log);
		return;
	}

	/* Update the logging configuration */
	status = read_log_config(config_struct, &err_type);
	if (status < 0)
		LogCrit(COMPONENT_CONFIG, "Error while parsing LOG entries");

	/* Update the export configuration */
	status = reread_exports(config_struct, &err_type);
	if (status < 0)
		LogCrit(COMPONENT_CONFIG, "Error while parsing EXPORT entries");

	report_config_errors(&err_type, NULL, config_errs_to_log);
	config_Free(config_struct);
}

/**
 * @brief This thread is in charge of signal management
 *
 * @param[in] UnusedArg Unused
 *
 * @return NULL.
 */
static void *sigmgr_thread(void *UnusedArg)
{
	int signal_caught = 0;

	SetNameFunction("sigmgr");
	rcu_register_thread();

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
			reread_config();
#ifdef _HAVE_GSSAPI
			svcauth_gss_release_cred();
#endif /* _HAVE_GSSAPI */
		}
	}
	LogDebug(COMPONENT_THREAD, "sigmgr thread exiting");

	admin_halt();

	/* Might as well exit - no need for this thread any more */
	rcu_unregister_thread();
	return NULL;
}


static void crash_handler(int signo, siginfo_t *info, void *ctx)
{
	gsh_backtrace();
	/* re-raise the signal for the default signal handler to dump core */
	raise(signo);
}

static void install_sighandler(int signo,
			       void (*handler)(int, siginfo_t *, void *))
{
	struct sigaction sa = {};
	int ret;

	sa.sa_sigaction = handler;
	/* set SA_RESETHAND to restore default handler */
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_NODEFER;

	sigemptyset(&sa.sa_mask);

	ret = sigaction(signo, &sa, NULL);
	if (ret) {
		LogWarn(COMPONENT_INIT,
			"Install handler for signal (%s) failed",
			strsignal(signo));
	}
}

static void init_crash_handlers(void)
{
	install_sighandler(SIGSEGV, crash_handler);
	install_sighandler(SIGABRT, crash_handler);
	install_sighandler(SIGBUS, crash_handler);
	install_sighandler(SIGILL, crash_handler);
	install_sighandler(SIGFPE, crash_handler);
	install_sighandler(SIGQUIT, crash_handler);
}

/**
 * @brief Initialize NFSd prerequisites
 *
 * @param[in] program_name Name of the program
 * @param[in] host_name    Server host name
 * @param[in] debug_level  Debug level
 * @param[in] log_path     Log path
 * @param[in] dump_trace   Dump trace when segfault
 */
void nfs_prereq_init(const char *program_name, const char *host_name,
		     int debug_level, const char *log_path, bool dump_trace)
{
	healthstats.enqueued_reqs = nfs_health_.enqueued_reqs = 0;
	healthstats.dequeued_reqs = nfs_health_.dequeued_reqs = 0;

	/* Initialize logging */
	SetNamePgm(program_name);
	SetNameFunction("main");
	SetNameHost(host_name);

	init_logging(log_path, debug_level);
	if (dump_trace) {
		init_crash_handlers();
	}

	/* Redirect TI-RPC allocators, log channel */
	if (!tirpc_control(TIRPC_PUT_PARAMETERS, &ntirpc_pp)) {
		LogFatal(COMPONENT_INIT, "Setting nTI-RPC parameters failed");
	}
}

/**
 * @brief Print the nfs_parameter_structure
 */
void nfs_print_param_config(void)
{
	printf("NFS_Core_Param\n{\n");

	printf("\tNFS_Port = %u ;\n", nfs_param.core_param.port[P_NFS]);
#ifdef _USE_NFS3
	printf("\tMNT_Port = %u ;\n", nfs_param.core_param.port[P_MNT]);
#endif
	printf("\tNFS_Program = %u ;\n", nfs_param.core_param.program[P_NFS]);
	printf("\tMNT_Program = %u ;\n", nfs_param.core_param.program[P_NFS]);
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
	printf("\tBlocked_Lock_Poller_Interval = %" PRIu64 " ;\n",
	       (uint64_t) nfs_param.core_param.blocked_lock_poller_interval);

	printf("\tManage_Gids_Expiration = %" PRIu64 " ;\n",
	       (uint64_t) nfs_param.core_param.manage_gids_expiration);

	printf("\tDrop_IO_Errors = %s ;\n",
	       nfs_param.core_param.drop_io_errors ?  "true" : "false");

	printf("\tDrop_Inval_Errors = %s ;\n",
	       nfs_param.core_param.drop_inval_errors ?  "true" : "false");

	printf("\tDrop_Delay_Errors = %s ;\n",
	       nfs_param.core_param.drop_delay_errors ? "true" : "false");

	printf("\tEnable UDP = %u ;\n", nfs_param.core_param.enable_UDP);

	printf("}\n\n");
}

/**
 * @brief Load parameters from config file
 *
 * @param[in]  config_struct Parsed config file
 * @param[out] p_start_info  Startup parameters
 * @param[out] err_type error reporting state
 *
 * @return -1 on failure.
 */
int nfs_set_param_from_conf(config_file_t parse_tree,
			    nfs_start_info_t *p_start_info,
			    struct config_error_type *err_type)
{
	/*
	 * Initialize exports and clients so config parsing can use them
	 * early.
	 */
	client_pkginit();
	export_pkginit();
	server_pkginit();

	/* Core parameters */
	(void) load_config_from_parse(parse_tree,
				      &nfs_core,
				      &nfs_param.core_param,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type)) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing core configuration");
		return -1;
	}

	/* Worker paramters: ip/name hash table and expiration for each entry */
	(void) load_config_from_parse(parse_tree,
				      &nfs_ip_name,
				      NULL,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type)) {
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
				      err_type);
	if (!config_error_is_harmless(err_type)) {
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
				      err_type);
	if (!config_error_is_harmless(err_type)) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing NFSv4 specific configuration");
		return -1;
	}

#ifdef _USE_9P
	(void) load_config_from_parse(parse_tree,
				      &_9p_param_blk,
				      NULL,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type)) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing 9P specific configuration");
		return -1;
	}
#endif

	if (mdcache_set_param_from_conf(parse_tree, err_type) < 0)
		return -1;

	if (load_recovery_param_from_conf(parse_tree, err_type) < 0)
		return -1;

	if (gsh_rados_url_setup_watch() != 0) {
		LogEvent(COMPONENT_INIT, "Couldn't setup rados_urls");
		return -1;
	}

	LogEvent(COMPONENT_INIT, "Configuration file successfully parsed");

	return 0;
}

int init_server_pkgs(void)
{
	fsal_status_t fsal_status;
	state_status_t state_status;

	/* init uid2grp cache */
	uid2grp_cache_init();

	ng_cache_init(); /* netgroup cache */

	/* MDCACHE Initialisation */
	fsal_status = mdcache_pkginit();
	if (FSAL_IS_ERROR(fsal_status)) {
		LogCrit(COMPONENT_INIT,
			"MDCACHE FSAL could not be initialized, status=%s",
			fsal_err_txt(fsal_status));
		return -1;
	}

	state_status = state_lock_init();
	if (state_status != STATE_SUCCESS) {
		LogCrit(COMPONENT_INIT,
			"State Lock Layer could not be initialized, status=%s",
			state_err_str(state_status));
		return -1;
	}
	LogInfo(COMPONENT_INIT, "State lock layer successfully initialized");

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

#ifdef _USE_9P
	if (nfs_param.core_param.core_options & CORE_OPTION_9P) {
		/* Start 9P worker threads */
		rc = _9p_worker_init();
		if (rc != 0) {
			LogFatal(COMPONENT_THREAD,
				 "Could not start worker threads: %d", errno);
		}

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
	}
#endif

#ifdef _USE_9P_RDMA
	/* Starting the 9P/RDMA dispatcher thread */
	if (nfs_param.core_param.core_options & CORE_OPTION_9P) {
		rc = pthread_create(&_9p_rdma_dispatcher_thrid, &attr_thr,
				    _9p_rdma_dispatcher_thread, NULL);
		if (rc != 0) {
			LogFatal(COMPONENT_THREAD,
				 "Could not create  9P/RDMA dispatcher, error = %d (%s)",
				 errno, strerror(errno));
		}
		LogEvent(COMPONENT_THREAD,
			 "9P/RDMA dispatcher thread was started successfully");
	}
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

	pthread_attr_destroy(&attr_thr);
}

/**
 * @brief Init the nfs daemon
 *
 * @param[in] p_start_info Unused
 */

static void nfs_Init(const nfs_start_info_t *p_start_info)
{
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
	dbus_cache_init();
#endif

	/* acls cache may be needed by exports_pkginit */
	LogDebug(COMPONENT_INIT, "Now building NFSv4 ACL cache");
	if (nfs4_acls_init() != 0)
		LogFatal(COMPONENT_INIT, "Error while initializing NFSv4 ACLs");
	LogInfo(COMPONENT_INIT, "NFSv4 ACL cache successfully initialized");

	/* finish the job with exports by caching the root entries
	 */
	exports_pkginit();

	nfs41_session_pool =
	    pool_basic_init("NFSv4.1 session pool", sizeof(nfs41_session_t));

	/* If rpcsec_gss is used, set the path to the keytab */
#ifdef _HAVE_GSSAPI
#ifdef HAVE_KRB5
	if (nfs_param.krb5_param.active_krb5) {
		OM_uint32 gss_status = GSS_S_COMPLETE;

		if (strcmp(nfs_param.krb5_param.keytab, DEFAULT_NFS_KEYTAB))
			gss_status = krb5_gss_register_acceptor_identity(
						nfs_param.krb5_param.keytab);

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

#ifdef _USE_NLM
	if (nfs_param.core_param.enable_NLM) {
		/* Init The NLM Owner cache */
		LogDebug(COMPONENT_INIT, "Now building NLM Owner cache");
		if (Init_nlm_hash() != 0) {
			LogFatal(COMPONENT_INIT,
				 "Error while initializing NLM Owner cache");
		}
		LogInfo(COMPONENT_INIT,
			"NLM Owner cache successfully initialized");
		/* Init The NLM Owner cache */
		LogDebug(COMPONENT_INIT, "Now building NLM State cache");
		if (Init_nlm_state_hash() != 0) {
			LogFatal(COMPONENT_INIT,
				 "Error while initializing NLM State cache");
		}
		LogInfo(COMPONENT_INIT,
			"NLM State cache successfully initialized");
		nlm_init();
	}
#endif /* _USE_NLM */
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

	/* RPC Initialisation - exits on failure */
	nfs_Init_svc();
	LogInfo(COMPONENT_INIT, "RPC resources successfully initialized");

	/* Admin initialisation */
	nfs_Init_admin_thread();

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
	cap_value_t cap_values[] = {CAP_SYS_RESOURCE};
	cap_t cap;
	char *cap_text;
	ssize_t capstrlen = 0;
	int ret;

	if (!nfs_start_info.drop_caps) {
		/* Skip dropping caps by request */
		return;
	}

	cap = cap_get_proc();
	if (cap == NULL) {
		LogFatal(COMPONENT_INIT,
			 "cap_get_proc() failed, %s", strerror(errno));
	}

	ret = cap_set_flag(cap, CAP_EFFECTIVE,
			   sizeof(cap_values) / sizeof(cap_values[0]),
			   cap_values, CAP_CLEAR);
	if (ret != 0) {
		LogFatal(COMPONENT_INIT,
			 "cap_set_flag() failed, %s", strerror(errno));
	}

	ret = cap_set_flag(cap, CAP_PERMITTED,
			   sizeof(cap_values) / sizeof(cap_values[0]),
			   cap_values, CAP_CLEAR);
	if (ret != 0) {
		LogFatal(COMPONENT_INIT,
			 "cap_set_flag() failed, %s", strerror(errno));
	}

	ret = cap_set_flag(cap, CAP_INHERITABLE,
			   sizeof(cap_values) / sizeof(cap_values[0]),
			   cap_values, CAP_CLEAR);
	if (ret != 0) {
		LogFatal(COMPONENT_INIT,
			 "cap_set_flag() failed, %s", strerror(errno));
	}

	ret = cap_set_proc(cap);
	if (ret != 0) {
		LogFatal(COMPONENT_INIT,
			 "Failed to set capabilities for process, %s",
			 strerror(errno));
	}

	LogEvent(COMPONENT_INIT,
		 "CAP_SYS_RESOURCE was successfully removed for proper quota management in FSAL");

	/* Print newly set capabilities (same as what CLI "getpcaps" displays */
	cap_text = cap_to_text(cap, &capstrlen);
	LogEvent(COMPONENT_INIT, "currenty set capabilities are: %s",
		 cap_text);
	cap_free(cap_text);
	cap_free(cap);
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

		build_verifier.epoch = (uint64_t) nfs_ServerEpoch;

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
	nfs_Start_threads(); /* Spawns service threads */

	nfs_init_complete();

#ifdef _USE_NLM
	if (nfs_param.core_param.enable_NLM) {
		/* NSM Unmonitor all */
		nsm_unmonitor_all();
	}
#endif /* _USE_NLM */

	LogEvent(COMPONENT_INIT,
		 "-------------------------------------------------");
	LogEvent(COMPONENT_INIT, "             NFS SERVER INITIALIZED");
	LogEvent(COMPONENT_INIT,
		 "-------------------------------------------------");

	/* Set the time of NFS stat counting */
	nfs_init_stats_time();

	/* Wait for dispatcher to exit */
	LogDebug(COMPONENT_THREAD, "Wait for admin thread to exit");
	pthread_join(admin_thrid, NULL);

	/* Regular exit */
	LogEvent(COMPONENT_MAIN, "NFS EXIT: regular exit");

	Cleanup();
	/* let main return 0 to exit */
}

void nfs_init_init(void)
{
	PTHREAD_MUTEX_init(&nfs_init.init_mutex, NULL);
	PTHREAD_COND_init(&nfs_init.init_cond, NULL);
	nfs_init.init_complete = false;
}

void nfs_init_complete(void)
{
	PTHREAD_MUTEX_lock(&nfs_init.init_mutex);
	nfs_init.init_complete = true;
	pthread_cond_broadcast(&nfs_init.init_cond);
	PTHREAD_MUTEX_unlock(&nfs_init.init_mutex);
}

void nfs_init_wait(void)
{
	PTHREAD_MUTEX_lock(&nfs_init.init_mutex);
	while (!nfs_init.init_complete) {
		pthread_cond_wait(&nfs_init.init_cond, &nfs_init.init_mutex);
	}
	PTHREAD_MUTEX_unlock(&nfs_init.init_mutex);
}

int nfs_init_wait_timeout(int timeout)
{
	int rc = 0;

	PTHREAD_MUTEX_lock(&nfs_init.init_mutex);
	if (!nfs_init.init_complete) {
		struct timespec ts;

		ts.tv_sec = time(NULL) + timeout;
		ts.tv_nsec = 0;
		rc = pthread_cond_timedwait(&nfs_init.init_cond,
					    &nfs_init.init_mutex, &ts);
	}
	PTHREAD_MUTEX_unlock(&nfs_init.init_mutex);

	return rc;
}

bool nfs_health(void)
{
	uint64_t newenq, newdeq;
	uint64_t dequeue_diff, enqueue_diff;
	bool healthy;

	newenq = nfs_health_.enqueued_reqs;
	newdeq = nfs_health_.dequeued_reqs;
	enqueue_diff = newenq - healthstats.enqueued_reqs;
	dequeue_diff = newdeq - healthstats.dequeued_reqs;

	/* Consider healthy and making progress if we have dequeued some
	 * requests or there is one or less to dequeue.  Don't check
	 * enqueue_diff == 0 here, as there will be suprious warnings during
	 * times of low traffic, when an enqueue happens to coincide with the
	 * heartbeat firing.
	 */
	healthy = dequeue_diff > 0 || enqueue_diff <= 1;

	if (!healthy) {
		LogWarn(COMPONENT_DBUS,
			"Health status is unhealthy. "
			"enq new: %" PRIu64 ", old: %" PRIu64 "; "
			"deq new: %" PRIu64 ", old: %" PRIu64,
			newenq, healthstats.enqueued_reqs,
			newdeq, healthstats.dequeued_reqs);
	}

	healthstats.enqueued_reqs = newenq;
	healthstats.dequeued_reqs = newdeq;

	return healthy;
}
