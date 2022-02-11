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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file nfs_main.c
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
#include "gsh_rpc.h"
#include "nfs_init.h"
#include "nfs_exports.h"
#include "pnfs_utils.h"
#include "config_parsing.h"
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
#include "gsh_lttng/fsal_gluster.h"
#include "gsh_lttng/fsal_ceph.h"
#endif /* USE_LTTNG */

/* parameters for NFSd startup and default values */

static nfs_start_info_t my_nfs_start_info = {
	.dump_default_config = false,
	.lw_mark_trigger = false,
	.drop_caps = true
};

config_file_t nfs_config_struct;
char *nfs_host_name = "localhost";

/* command line syntax */

static const char options[] = "v@L:N:f:p:FRTE:Ch";
static const char usage[] =
	"Usage: %s [-hd][-L <logfile>][-N <dbg_lvl>][-f <config_file>]\n"
	"\t[-v]                display version information\n"
	"\t[-L <logfile>]      set the default logfile for the daemon\n"
	"\t[-N <dbg_lvl>]      set the verbosity level\n"
	"\t[-f <config_file>]  set the config file to be used\n"
	"\t[-p <pid_file>]     set the pid file\n"
	"\t[-F]                the program stays in foreground\n"
	"\t[-R]                daemon will manage RPCSEC_GSS (default is no RPCSEC_GSS)\n"
	"\t[-T]                dump the default configuration on stdout\n"
	"\t[-E] <epoch>]       overrides ServerBootTime for ServerEpoch\n"
	"\t[-C]                dump trace when segfault\n"
	"\t[-h]                display this help\n"
	"----------------- Signals ----------------\n"
	"SIGHUP     : Reload LOG and EXPORT config\n"
	"SIGTERM    : Cleanly terminate the program\n"
	"------------- Default Values -------------\n"
	"LogFile    : SYSLOG\n"
	"PidFile    : "GANESHA_PIDFILE_PATH"\n"
	"DebugLevel : NIV_EVENT\n" "ConfigFile : "GANESHA_CONFIG_PATH"\n";

static inline char *main_strdup(const char *var, const char *str)
{
	char *s = strdup(str);

	if (s == NULL) {
		fprintf(stderr, "strdup failed for %s value %s\n", var, str);
		abort();
	}

	return s;
}

/**
 * main: simply the main function.
 *
 * The 'main' function as in every C program.
 *
 * @param argc number of arguments
 * @param argv array of arguments
 *
 * @return status to calling program by calling the exit(3C) function.
 *
 */

int main(int argc, char *argv[])
{
	char *tempo_exec_name = NULL;
	char localmachine[MAXHOSTNAMELEN + 1];
	int c;
	int dsc;
	int rc;
	int pidfile;
	char *log_path = NULL;
	char *exec_name = "nfs-ganesha";
	int debug_level = -1;
	int detach_flag = true;
	bool dump_trace = false;
#ifndef HAVE_DAEMON
	int dev_null_fd = 0;
	pid_t son_pid;
#endif
	sigset_t signals_to_block;
	struct config_error_type err_type;

	/* Set the server's boot time and epoch */
	now(&nfs_ServerBootTime);
	nfs_ServerEpoch = (time_t) nfs_ServerBootTime.tv_sec;
	srand(nfs_ServerEpoch);

	tempo_exec_name = strrchr(argv[0], '/');
	if (tempo_exec_name != NULL)
		exec_name = main_strdup("exec_name", tempo_exec_name + 1);

	if (*exec_name == '\0')
		exec_name = argv[0];

	/* get host name */
	if (gethostname(localmachine, sizeof(localmachine)) != 0) {
		fprintf(stderr, "Could not get local host name, exiting...\n");
		exit(1);
	} else {
		nfs_host_name = main_strdup("host_name", localmachine);
	}

	/* now parsing options with getopt */
	while ((c = getopt(argc, argv, options)) != EOF) {
		switch (c) {
		case 'v':
		case '@':
			printf("NFS-Ganesha Release = V%s\n", GANESHA_VERSION);
#if !GANESHA_BUILD_RELEASE
			/* A little backdoor to keep track of binary versions */
			printf("%s compiled on %s at %s\n", exec_name, __DATE__,
			       __TIME__);
			printf("Release comment = %s\n", VERSION_COMMENT);
			printf("Git HEAD = %s\n", _GIT_HEAD_COMMIT);
			printf("Git Describe = %s\n", _GIT_DESCRIBE);
#endif
			exit(0);
			break;

		case 'L':
			/* Default Log */
			log_path = main_strdup("log_path", optarg);
			break;

		case 'N':
			/* debug level */
			debug_level = ReturnLevelAscii(optarg);
			if (debug_level == -1) {
				fprintf(stderr,
					"Invalid value for option 'N': NIV_NULL, NIV_MAJ, NIV_CRIT, NIV_EVENT, NIV_DEBUG, NIV_MID_DEBUG or NIV_FULL_DEBUG expected.\n");
				exit(1);
			}
			break;

		case 'f':
			/* config file */

			nfs_config_path = main_strdup("config_path", optarg);
			break;

		case 'p':
			/* PID file */
			nfs_pidfile_path = main_strdup("pidfile_path", optarg);
			break;

		case 'F':
			/* Don't detach, foreground mode */
			detach_flag = false;
			break;

		case 'R':
			/* Shall we manage  RPCSEC_GSS ? */
			fprintf(stderr,
				"\n\nThe -R flag is deprecated, use this syntax in the configuration file instead:\n\n");
			fprintf(stderr, "NFS_KRB5\n");
			fprintf(stderr, "{\n");
			fprintf(stderr,
				"\tPrincipalName = nfs@<your_host> ;\n");
			fprintf(stderr, "\tKeytabPath = /etc/krb5.keytab ;\n");
			fprintf(stderr, "\tActive_krb5 = true ;\n");
			fprintf(stderr, "}\n\n\n");
			exit(1);
			break;

		case 'T':
			/* Dump the default configuration on stdout */
			my_nfs_start_info.dump_default_config = true;
			break;

		case 'C':
			dump_trace = true;
			break;

		case 'E':
			nfs_ServerEpoch = (time_t) atoll(optarg);
			break;

		case 'h':
			fprintf(stderr, usage, exec_name);
			exit(0);

		default: /* '?' */
			fprintf(stderr, "Try '%s -h' for usage\n", exec_name);
			exit(1);
		}
	}

	/* initialize memory and logging */
	nfs_prereq_init(exec_name, nfs_host_name, debug_level, log_path,
			dump_trace);
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

	/* Start in background, if wanted */
	if (detach_flag) {
#ifdef HAVE_DAEMON
		/* daemonize the process (fork, close xterm fds,
		 * detach from parent process) */
		if (daemon(0, 0))
			LogFatal(COMPONENT_MAIN,
				 "Error detaching process from parent: %s",
				 strerror(errno));

		/* In the child process, change the log header
		 * if not, the header will contain the parent's pid */
		set_const_log_str();
#else
		/* Step 1: forking a service process */
		switch (son_pid = fork()) {
		case -1:
			/* Fork failed */
			LogFatal(COMPONENT_MAIN,
				 "Could not start nfs daemon (fork error %d (%s)",
				 errno, strerror(errno));
			break;

		case 0:
			/* This code is within the son (that will actually work)
			 * Let's make it the leader of its group of process */
			if (setsid() == -1) {
				LogFatal(COMPONENT_MAIN,
					 "Could not start nfs daemon (setsid error %d (%s)",
					 errno, strerror(errno));
			}

			/* stdin, stdout and stderr should not refer to a tty
			 * I close 0, 1 & 2  and redirect them to /dev/null */
			dev_null_fd = open("/dev/null", O_RDWR);
			if (dev_null_fd < 0)
				LogFatal(COMPONENT_MAIN,
					 "Could not open /dev/null: %d (%s)",
					 errno, strerror(errno));

			if (close(STDIN_FILENO) == -1)
				LogEvent(COMPONENT_MAIN,
					 "Error while closing stdin: %d (%s)",
					  errno, strerror(errno));
			else {
				LogEvent(COMPONENT_MAIN, "stdin closed");
				dup(dev_null_fd);
			}

			if (close(STDOUT_FILENO) == -1)
				LogEvent(COMPONENT_MAIN,
					 "Error while closing stdout: %d (%s)",
					  errno, strerror(errno));
			else {
				LogEvent(COMPONENT_MAIN, "stdout closed");
				dup(dev_null_fd);
			}

			if (close(STDERR_FILENO) == -1)
				LogEvent(COMPONENT_MAIN,
					 "Error while closing stderr: %d (%s)",
					  errno, strerror(errno));
			else {
				LogEvent(COMPONENT_MAIN, "stderr closed");
				dup(dev_null_fd);
			}

			if (close(dev_null_fd) == -1)
				LogFatal(COMPONENT_MAIN,
					 "Could not close tmp fd to /dev/null: %d (%s)",
					 errno, strerror(errno));

			/* In the child process, change the log header
			 * if not, the header will contain the parent's pid */
			set_const_log_str();
			break;

		default:
			/* This code is within the parent process,
			 * it is useless, it must die */
			LogFullDebug(COMPONENT_MAIN,
				     "Starting a child of pid %d",
				     son_pid);
			exit(0);
			break;
		}
#endif
	}

	/* Make sure Linux file i/o will return with error
	 * if file size is exceeded. */
#ifdef _LINUX
	signal(SIGXFSZ, SIG_IGN);
#endif

	/* Echo PID into pidfile */
	pidfile = open(nfs_pidfile_path, O_CREAT | O_RDWR, 0644);
	if (pidfile == -1) {
		LogFatal(COMPONENT_MAIN, "Can't open pid file %s for writing",
			 nfs_pidfile_path);
	} else {
		struct flock lk;

		/* Try to obtain a lock on the file */
		lk.l_type = F_WRLCK;
		lk.l_whence = SEEK_SET;
		lk.l_start = (off_t) 0;
		lk.l_len = (off_t) 0;
		if (fcntl(pidfile, F_SETLK, &lk) == -1)
			LogFatal(COMPONENT_MAIN, "Ganesha already started");

		/* Put pid into file, then sync it */
		if (dprintf(pidfile, "%u\n", getpid()) < 0 ||
		    fsync(pidfile) < 0) {
			close(pidfile);
			LogCrit(COMPONENT_MAIN,
				"Couldn't write pid to file %s error %s (%d)",
				nfs_pidfile_path, strerror(errno), errno);
		}
	}

	/* Set up for the signal handler.
	 * Blocks the signals the signal handler will handle.
	 */
	sigemptyset(&signals_to_block);
	sigaddset(&signals_to_block, SIGTERM);
	sigaddset(&signals_to_block, SIGHUP);
	sigaddset(&signals_to_block, SIGPIPE);
	if (pthread_sigmask(SIG_BLOCK, &signals_to_block, NULL) != 0)
		LogFatal(COMPONENT_MAIN,
			 "Could not start nfs daemon, pthread_sigmask failed");

	/* init URL package */
	config_url_init();

	/* Create a memstream for parser+processing error messages */
	if (!init_error_type(&err_type))
		goto fatal_die;

	/* Parse the configuration file so we all know what is going on. */

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

	if (tempo_exec_name)
		free(exec_name);
	if (log_path)
		free(log_path);

	close(pidfile);

	return 0;

fatal_die:
	report_config_errors(&err_type, NULL, config_errs_to_log);

	if (tempo_exec_name)
		free(exec_name);
	if (log_path)
		free(log_path);

	close(pidfile);

	/* systemd journal won't display our errors without this */
	sleep(1);

	LogFatal(COMPONENT_INIT,
		 "Fatal errors.  Server exiting...");
	/* NOT REACHED */
	return 2;
}
