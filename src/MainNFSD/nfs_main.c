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
 * \file    nfs_main.c
 * \author  $Author: deniel $
 * \brief   The file that contain the 'main' routine for the nfsd.
 *
 * nfs_main.c : The file that contain the 'main' routine for the nfsd.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "nfs_init.h"
#include "fsal.h"
#include "log_macros.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>             /* for sigaction */
#include <errno.h>

/* parameters for NFSd startup and default values */

nfs_start_info_t my_nfs_start_info = {
  .flush_datacache_mode = FALSE,
  .dump_default_config = FALSE,
  .nb_flush_threads = 1,
  .flush_behaviour = CACHE_CONTENT_FLUSH_AND_DELETE,
  .lw_mark_trigger = FALSE
};

char my_config_path[MAXPATHLEN] = "/etc/ganesha/ganesha.conf";
char log_path[MAXPATHLEN] = "";
char exec_name[MAXPATHLEN] = "nfs-ganesha";
char host_name[MAXHOSTNAMELEN] = "localhost";
int debug_level = -1;
int detach_flag = FALSE;
char ganesha_exec_path[MAXPATHLEN];

extern fsal_functions_t fsal_functions;
extern fsal_const_t fsal_consts;

/* command line syntax */

char options[] = "h@RTdS:F:S:P:f:L:N:";
char usage[] =
    "Usage: %s [-hd][-L <logfile>][-N <dbg_lvl>][-f <config_file>]\n"
    "\t[-h]                display this help\n"
    "\t[-L <logfile>]      set the default logfile for the daemon\n"
    "\t[-N <dbg_lvl>]      set the verbosity level\n"
    "\t[-f <config_file>]  set the config file to be used\n"
    "\t[-d]                the daemon starts in background, in a new process group\n"
    "\t[-R]                daemon will manage RPCSEC_GSS (default is no RPCSEC_GSS)\n"
    "\t[-T]                dump the default configuration on stdout\n"
    "\t[-F] <nb_flushers>  flushes the data cache with purge, but do not answer to requests\n"
    "\t[-S] <nb_flushers>  flushes the data cache without purge, but do not answer to requests\n"
    "\t[-P] <nb_flushers>  flushes the data cache with purge until lw mark is reached, then just sync. Do not answer to requests\n"
    "----------------- Signals ----------------\n"
    "SIGUSR1    : Enable/Disable File Content Cache forced flush\n"
    "SIGTERM    : Cleanly terminate the program\n"
    "------------- Default Values -------------\n"
    "LogFile    : /tmp/nfs-ganesha.log\n"
    "DebugLevel : NIV_EVENT\n" "ConfigFile : /etc/ganesha/ganesha.conf\n";

/**
 *
 * SIGCHLD signal management routine, for detecting the end of a child process
 *
 */

static void action_sigusr1(int sig)
{
  LogEvent(COMPONENT_MAIN, "SIGUSR1_HANDLER: Received SIGUSR1.... signal will be managed");

  /* Set variable force_flush_by_signal that is used in file content cache gc thread */
  if(force_flush_by_signal)
    {
      LogEvent(COMPONENT_MAIN, "SIGUSR1_HANDLER: force_flush_by_signal is set to FALSE");
      force_flush_by_signal = FALSE;
    }
  else
    {
      LogEvent(COMPONENT_MAIN, "SIGUSR1_HANDLER: force_flush_by_signal is set to TRUE");
      force_flush_by_signal = TRUE;
    }
}                               /* action_sigusr1 */

/**
 *
 * SIGTERM signal management routine, for cleanly terminating the daemon
 *
 */

static void action_sigterm(int sig)
{
  if(sig == SIGTERM)
    LogEvent(COMPONENT_MAIN, "SIGTERM_HANDLER: Received SIGTERM.... initiating daemon shutdown");
  else if(sig == SIGINT)
    LogEvent(COMPONENT_MAIN, "SIGINT_HANDLER: Received SIGINT.... initiating daemon shutdown");

  nfs_stop();

}                               /* action_sigterm */

static void action_sighup(int sig)
{
  if(sig == SIGTERM)
    LogEvent(COMPONENT_MAIN, "SIGTERM_HANDLER: Received SIGTERM.... initiating daemon shutdown");
  else if(sig == SIGINT)
    LogEvent(COMPONENT_MAIN, "SIGINT_HANDLER: Received SIGINT.... initiating daemon shutdown");
  else if(sig == SIGHUP)
    LogEvent(COMPONENT_MAIN, "SIGHUP_HANDLER: Received SIGHUP.... initiating export list reload");

  admin_replace_exports();
}                               /* action_sigsigh */

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
  char localmachine[MAXHOSTNAMELEN];
  int c;
  nfs_parameter_t nfs_param;
  pid_t son_pid;
  struct sigaction act_sigusr1;
  struct sigaction act_sigterm;
  struct sigaction act_sighup;

  char fsal_path_lib[MAXPATHLEN];

  /* retrieve executable file's name */
  strncpy(ganesha_exec_path, argv[0], MAXPATHLEN);

  if((tempo_exec_name = strrchr(argv[0], '/')) != NULL)
    strcpy((char *)exec_name, tempo_exec_name + 1);

  if(*exec_name == '\0')
    strcpy((char *)exec_name, argv[0]);

  /* get host name */
  if(gethostname(localmachine, sizeof(localmachine)) != 0)
    {
      fprintf(stderr, "Could not get local host name, exiting...");
      exit(1);
    }
  else
    strncpy(host_name, localmachine, MAXHOSTNAMELEN);

  /* now parsing options with getopt */
  while((c = getopt(argc, argv, options)) != EOF)
    {
      switch (c)
        {
        case '@':
          /* A litlle backdoor to keep track of binary versions */
          printf("%s compiled on %s at %s\n", exec_name, __DATE__, __TIME__);
          printf("Release = %s\n", VERSION);
          printf("Release comment = %s\n", VERSION_COMMENT);
          exit(0);
          break;

        case 'L':
          /* Default Log */
          strncpy(log_path, optarg, MAXPATHLEN);
          break;

        case 'N':
          /* debug level */
          debug_level = ReturnLevelAscii(optarg);
          if(debug_level == -1)
            {
              fprintf(stderr,
                      "Invalid value for option 'N': NIV_NULL, NIV_MAJ, NIV_CRIT, NIV_EVENT, NIV_DEBUG or NIV_FULL_DEBUG expected.\n");
              exit(1);
            }
          break;

        case 'f':
          /* config file */
          strncpy(my_config_path, optarg, MAXPATHLEN);
          break;

        case 'd':
          /* Detach or not detach ? */
          detach_flag = TRUE;
          break;

        case 'R':
          /* Shall we manage  RPCSEC_GSS ? */
          fprintf(stderr,
                  "\n\nThe -R flag is deprecated, use this syntax in the configuration file instead:\n\n");
          fprintf(stderr, "NFS_KRB5\n");
          fprintf(stderr, "{\n");
          fprintf(stderr, "\tPrincipalName = nfs@<your_host> ;\n");
          fprintf(stderr, "\tKeytabPath = /etc/krb5.keytab ;\n");
          fprintf(stderr, "\tActive_krb5 = TRUE ;\n");
          fprintf(stderr, "}\n\n\n");
          exit(1);
          break;

        case 'T':
          /* Dump the default configuration on stdout */
          my_nfs_start_info.dump_default_config = TRUE;
          break;

        case 'F':
          /* Flushes the data cache to the FSAL and purges the cache */
          my_nfs_start_info.flush_datacache_mode = TRUE;
          my_nfs_start_info.flush_behaviour = CACHE_CONTENT_FLUSH_AND_DELETE;
          my_nfs_start_info.nb_flush_threads = (unsigned int)atoi(optarg);
          my_nfs_start_info.lw_mark_trigger = FALSE;

          if(my_nfs_start_info.nb_flush_threads > NB_MAX_FLUSHER_THREAD)
            my_nfs_start_info.nb_flush_threads = NB_MAX_FLUSHER_THREAD;
          break;

        case 'S':
          /* Flushes the data cache to the FSAL, without purging the cache */
          my_nfs_start_info.flush_datacache_mode = TRUE;
          my_nfs_start_info.flush_behaviour = CACHE_CONTENT_FLUSH_SYNC_ONLY;
          my_nfs_start_info.nb_flush_threads = (unsigned int)atoi(optarg);
          my_nfs_start_info.lw_mark_trigger = FALSE;

          if(my_nfs_start_info.nb_flush_threads > NB_MAX_FLUSHER_THREAD)
            my_nfs_start_info.nb_flush_threads = NB_MAX_FLUSHER_THREAD;
          break;

        case 'P':
          /* Flushes the data like '-F' until low water mark is reached, then just sync */
          my_nfs_start_info.flush_datacache_mode = TRUE;
          my_nfs_start_info.flush_behaviour = CACHE_CONTENT_FLUSH_AND_DELETE;
          my_nfs_start_info.nb_flush_threads = (unsigned int)atoi(optarg);
          my_nfs_start_info.lw_mark_trigger = TRUE;

          if(my_nfs_start_info.nb_flush_threads > NB_MAX_FLUSHER_THREAD)
            my_nfs_start_info.nb_flush_threads = NB_MAX_FLUSHER_THREAD;
          break;

        case '?':
        case 'h':
        default:
          /* display the help */
          fprintf(stderr, usage, exec_name);
          exit(0);
          break;
        }
    }

  /* initialize memory and logging */
  if(nfs_prereq_init(exec_name, host_name, debug_level, log_path))
    {
      fprintf(stderr, "NFS MAIN: Error initializing NFSd prerequisites\n");
      exit(1);
    }

  /* Start in background, if wanted */
  if(detach_flag)
    {
      /* Step 1: forking a service process */
      switch (son_pid = fork())
        {
        case -1:
          /* Fork failed */
          LogError(COMPONENT_INIT, ERR_SYS, ERR_FORK, errno);
          LogMajor(COMPONENT_INIT, "Could not start nfs daemon, exiting...");
          exit(1);

        case 0:
          /* This code is within the son (that will actually work)
           * Let's make it the leader of its group of process */
          if(setsid() == -1)
            {
              LogError(COMPONENT_INIT, ERR_SYS, ERR_SETSID, errno);
	      LogMajor(COMPONENT_INIT, "Could not start nfs daemon, exiting...");
              exit(1);
            }
          break;

        default:
          /* This code is within the father, it is useless, it must die */
          LogFullDebug(COMPONENT_INIT, "Starting a son of pid %d\n", son_pid);
          exit(0);
          break;
        }
    }

  /* Set the signal handler */
  /*
  memset(&act_sigusr1, 0, sizeof(act_sigusr1));
  act_sigusr1.sa_flags = 0;
  act_sigusr1.sa_handler = action_sigusr1;
  if(sigaction(SIGUSR1, &act_sigusr1, NULL) == -1)
    {
      LogError(COMPONENT_INIT, ERR_SYS, ERR_SIGACTION, errno);
      exit(1);
    }
  else
    LogEvent(COMPONENT_INIT, NIV_EVENT, "Signal SIGUSR1 (force flush) is ready to be used");
  */

  /* Set the signal handler */
  memset(&act_sigterm, 0, sizeof(act_sigterm));
  act_sigterm.sa_flags = 0;
  act_sigterm.sa_handler = action_sigterm;
  if(sigaction(SIGTERM, &act_sigterm, NULL) == -1
     || sigaction(SIGINT, &act_sigterm, NULL) == -1)
    {
      LogError(COMPONENT_INIT, ERR_SYS, ERR_SIGACTION, errno);
      exit(1);
    }
  else
    LogEvent(COMPONENT_INIT,
	     "Signals SIGTERM and SIGINT (daemon shutdown) are ready to be used");

  /* Set the signal handler */
  memset(&act_sigterm, 0, sizeof(act_sighup));
  act_sighup.sa_flags = 0;
  act_sighup.sa_handler = action_sighup;
  if(sigaction(SIGHUP, &act_sighup, NULL) == -1)
    {
      LogError(COMPONENT_INIT, ERR_SYS, ERR_SIGACTION, errno);
      exit(1);
    }
  else
    LogEvent(COMPONENT_INIT,
                    "Signal SIGHUP (daemon export reload) is ready to be used");


#ifdef _USE_SHARED_FSAL
  if(nfs_get_fsalpathlib_conf(my_config_path, fsal_path_lib))
    {
      LogMajor(COMPONENT_INIT, "NFS MAIN: Error parsing configuration file.");
      exit(1);
    }
#endif                          /* _USE_SHARED_FSAL */

  /* Load the FSAL library (if needed) */
  if(!FSAL_LoadLibrary(fsal_path_lib))
    {
      LogMajor(COMPONENT_INIT,
	      "NFS MAIN: Could not load FSAL dynamic library %s", fsal_path_lib);
      exit(1);
    }

  /* Get the FSAL functions */
  FSAL_LoadFunctions();

  /* Get the FSAL consts */
  FSAL_LoadConsts();

  LogEvent(COMPONENT_INIT,
	   ">>>>>>>>>> Starting GANESHA NFS Daemon on FSAL/%s <<<<<<<<<<",
	   FSAL_GetFSName());

  /* initialize default parameters */

  if(nfs_set_param_default(&nfs_param))
    {
      LogMajor(COMPONENT_INIT, "NFS MAIN: Error setting default parameters.");
      exit(1);
    }

  /* parse configuration file */

  if(nfs_set_param_from_conf(&nfs_param, &my_nfs_start_info, my_config_path))
    {
      LogMajor(COMPONENT_INIT, "NFS MAIN: Error parsing configuration file.");
      exit(1);
    }

  /* check parameters consitency */

  if(nfs_check_param_consistency(&nfs_param))
    {
      LogMajor(COMPONENT_INIT, "NFS MAIN: Inconsistent parameters found");
      LogMajor(COMPONENT_INIT,
	       "MAJOR WARNING: /!\\ | Bad Parameters could have significant impact on the daemon behavior");
      exit(1);
    }

  /* Everything seems to be OK! We can now start service threads */
  nfs_start(&nfs_param, &my_nfs_start_info);

  return 0;

}
