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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file nfs_lib.c
 * @brief The file that contain the 'main' routine for the nfsd.
 *
 */
#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>		/* for sigaction */
#include <errno.h>
#include "fsal.h"
#include "log.h"
#include "nfs_init.h"
#include "nfs_exports.h"
#include "pnfs_utils.h"
#include "conf_url.h"
#include "sal_functions.h"

/**
 * @brief LTTng trace enabling magic
 *
 * Every trace include file must be added here regardless whether it
 * is actually used in this source file.  The file must also be
 * included ONLY ONCE.  Failure to do so will create interesting
 * build time failure messages.  The key bit is the definitions of
 * TRACEPOINT_DEFINE and TRACEPOINT_PROBE_DYNAMIC_LINKAGE that are here
 * to trigger the global definitions as a shared object with the right
 * (weak) symbols to make the module loading optional.
 *
 * If and when this file gets some tracepoints of its own, the include
 * here is necessary and sufficient.
 */

#ifdef USE_LTTNG
#define TRACEPOINT_DEFINE
#define TRACEPOINT_PROBE_DYNAMIC_LINKAGE

#include "gsh_lttng/logger.h"
#include "gsh_lttng/mdcache.h"
#include "gsh_lttng/nfs_rpc.h"
#include "gsh_lttng/nfs4.h"
#include "gsh_lttng/state.h"
#include "gsh_lttng/fsal_mem.h"
#endif /* USE_LTTNG */

/* parameters for NFSd startup and default values */

nfs_start_info_t my_nfs_start_info = {
	.dump_default_config = false,
	.lw_mark_trigger = false,
	.drop_caps = false
};

config_file_t nfs_config_struct;
char *nfs_host_name = "localhost";

/**
 * nfs_libmain: library initializer
 *
 * @return status to calling program by calling the exit(3C) function.
 *
 */

int nfs_libmain(const char *ganesha_conf,
		const char *lpath,
		const int debug_level)
{
	char localmachine[MAXHOSTNAMELEN + 1];
	int dsc;
	int rc;
	char *log_path = NULL;
	const char *exec_name = "nfs-ganesha";
	sigset_t signals_to_block;
	struct config_error_type err_type;

	/* Set the server's boot time and epoch */
	now(&nfs_ServerBootTime);
	nfs_ServerEpoch = (time_t) nfs_ServerBootTime.tv_sec;

	if (ganesha_conf)
		nfs_config_path = gsh_strdup(ganesha_conf);

	if (lpath)
		log_path = gsh_strdup(lpath);

	/* get host name */
	if (gethostname(localmachine, sizeof(localmachine)) != 0) {
		fprintf(stderr, "Could not get local host name, exiting...\n");
		exit(1);
	} else {
		nfs_host_name = gsh_strdup(localmachine);
		if (!nfs_host_name) {
			fprintf(stderr,
				"Unable to allocate memory for hostname, exiting...\n");
			exit(1);
		}
	}

	/* initialize memory and logging */
	nfs_prereq_init(exec_name, nfs_host_name, debug_level, log_path, false);
#if GANESHA_BUILD_RELEASE
	LogEvent(COMPONENT_MAIN, "%s Starting: Ganesha Version %s",
		 exec_name, GANESHA_VERSION);
#else
	LogEvent(COMPONENT_MAIN, "%s Starting: %s",
		 exec_name,
		 "Ganesha Version " _GIT_DESCRIBE ", built at "
		 __DATE__ " " __TIME__ " on " BUILD_HOST);
#endif

	/* initialize nfs_init */
	nfs_init_init();

	nfs_check_malloc();

	/* Make sure Linux file i/o will return with error
	 * if file size is exceeded. */
#ifdef _LINUX
	signal(SIGXFSZ, SIG_IGN);
#endif

	/* Set up for the signal handler.
	 * Blocks the signals the signal handler will handle.
	 */
	sigemptyset(&signals_to_block);
	sigaddset(&signals_to_block, SIGPIPE); /* XXX */
	if (pthread_sigmask(SIG_BLOCK, &signals_to_block, NULL) != 0)
		LogFatal(COMPONENT_MAIN,
			 "pthread_sigmask failed");

	/* init URL package */
	config_url_init();

	/* Create a memstream for parser+processing error messages */
	if (!init_error_type(&err_type))
		goto fatal_die;

	if (nfs_config_path == NULL || nfs_config_path[0] == '\0') {
		LogWarn(COMPONENT_INIT,
			"No configuration file named.");
		nfs_config_struct = NULL;
	} else
		nfs_config_struct =
			config_ParseFile(nfs_config_path, &err_type);

	if (!config_error_no_error(&err_type)) {
		char *errstr = err_type_str(&err_type);

		if (!config_error_is_harmless(&err_type)) {
			LogCrit(COMPONENT_INIT,
				 "Error %s while parsing (%s)",
				 errstr != NULL ? errstr : "unknown",
				 nfs_config_path);
			if (errstr != NULL)
				gsh_free(errstr);
			goto fatal_die;
		} else
			LogWarn(COMPONENT_INIT,
				"Error %s while parsing (%s)",
				errstr != NULL ? errstr : "unknown",
				nfs_config_path);
		if (errstr != NULL)
			gsh_free(errstr);
	}

	if (read_log_config(nfs_config_struct, &err_type) < 0) {
		LogCrit(COMPONENT_INIT,
			 "Error while parsing log configuration");
		goto fatal_die;
	}

	/* We need all the fsal modules loaded so we can have
	 * the list available at exports parsing time.
	 */
	start_fsals();

	/* parse configuration file */

	if (nfs_set_param_from_conf(nfs_config_struct,
				    &my_nfs_start_info,
				    &err_type)) {
		LogCrit(COMPONENT_INIT,
			 "Error setting parameters from configuration file.");
		goto fatal_die;
	}

	/* initialize core subsystems and data structures */
	if (init_server_pkgs() != 0) {
		LogCrit(COMPONENT_INIT,
			"Failed to initialize server packages");
		goto fatal_die;
	}

	/* Load Data Server entries from parsed file
	 * returns the number of DS entries.
	 */
	dsc = ReadDataServers(nfs_config_struct, &err_type);
	if (dsc < 0) {
		LogCrit(COMPONENT_INIT,
			"Error while parsing DS entries");
		goto fatal_die;
	}


	/* Create stable storage directory, this needs to be done before
	 * starting the recovery thread.
	 */
	rc = nfs4_recovery_init();
	if (rc) {
		LogCrit(COMPONENT_INIT,
			  "Recovery backend initialization failed!");
		goto fatal_die;
	}

	/* Start grace period */
	nfs_start_grace(NULL);

	/* Wait for enforcement to begin */
	nfs_wait_for_grace_enforcement();

	/* Load export entries from parsed file
	 * returns the number of export entries.
	 */
	rc = ReadExports(nfs_config_struct, &err_type);
	if (rc < 0) {
		LogCrit(COMPONENT_INIT,
			  "Error while parsing export entries");
		goto fatal_die;
	}
	if (rc == 0 && dsc == 0)
		LogWarn(COMPONENT_INIT,
			"No export entries found in configuration file !!!");
	report_config_errors(&err_type, NULL, config_errs_to_log);

	/* freeing syntax tree : */

	config_Free(nfs_config_struct);

	/* Everything seems to be OK! We can now start service threads */
	nfs_start(&my_nfs_start_info);

	return 0;

fatal_die:
	report_config_errors(&err_type, NULL, config_errs_to_log);
	LogFatal(COMPONENT_INIT,
		 "Fatal errors.  Server exiting...");
	/* NOT REACHED */
	return 2;
}
