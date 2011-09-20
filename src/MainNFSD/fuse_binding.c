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

#include "log_macros.h"
#include "ganesha_fuse_wrap.h"
#include "nfs_init.h"
#include "fsal.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>             /* for sigaction */
#include <errno.h>

/* parameters for NFSd startup and default values */

static nfs_start_info_t nfs_start_info = {
  .flush_datacache_mode = FALSE,
  .nb_flush_threads = 1,
  .flush_behaviour = CACHE_CONTENT_FLUSH_AND_DELETE,
};

char log_path[MAXPATHLEN] = "";
char exec_name[MAXPATHLEN] = "ganesha-nfsd";
char host_name[MAXHOSTNAMELEN] = "localhost";
int debug_level = -1;
int detach_flag = FALSE;
int single_threaded = FALSE;
struct ganefuse_operations ops;

char ganesha_exec_path[MAXPATHLEN];

/* command line syntax */

char options[] = "h@Rds:F:S:f:L:N:";
char usage[] =
    "Usage: %s [-hds][-L <logfile>][-N <dbg_lvl>][-f <config_file>]\n"
    "\t[-h]                display this help\n"
    "\t[-s]                single-threaded (for MT-unsafe filesystems)\n"
    "\t[-L <logfile>]      set the default logfile for the daemon\n"
    "\t[-N <dbg_lvl>]      set the verbosity level\n"
    "\t[-f <config_file>]  set the config file to be used\n"
    "\t[-d]                the daemon starts in background, in a new process group\n"
    "\t[-R]                daemon will manage RPCSEC_GSS (default is no RPCSEC_GSS)\n"
    "\t[-F] <nb_flushers>  flushes the data cache with purge, but do not answer to requests\n"
    "\t[-S] <nb_flushers>  flushes the data cache without purge, but do not answer to requests\n"
    "----------------- Signals ----------------\n"
    "SIGUSR1    : Enable/Disable File Content Cache forced flush\n"
    "------------- Default Values -------------\n"
    "LogFile    : /tmp/ganesha_nfsd.log\n"
    "DebugLevel : NIV_EVENT\n" "ConfigFile : None\n";

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

int ganefuse_main(int argc, char *argv[],
                  const struct ganefuse_operations *op, void *user_data)
{
  char *tempo_exec_name = NULL;
  char localmachine[MAXHOSTNAMELEN];
  int c;
  pid_t son_pid;

  int argc_local = argc;
  char **argv_local = argv;

  /* local copy for keeping it read only */
  ops = *op;

  /* retrieve executable file's name */
  strncpy(ganesha_exec_path, argv[0], MAXPATHLEN);

  if((tempo_exec_name = strrchr(argv_local[0], '/')) != NULL)
    strcpy((char *)exec_name, tempo_exec_name + 1);

  if(*exec_name == '\0')
    strcpy((char *)exec_name, argv_local[0]);

  /* get host name */

  if(gethostname(localmachine, sizeof(localmachine)) != 0)
    {
      fprintf(stderr, "Could not get local host name, exiting...");
      exit(1);
    }
  else
    strncpy(host_name, localmachine, MAXHOSTNAMELEN);

  /* now parsing options with getopt */
  while((c = getopt(argc_local, argv_local, options)) != EOF)
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
          strncpy(config_path, optarg, MAXPATHLEN);
          break;

        case 's':
          /* single threaded */
          single_threaded = TRUE;
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

        case 'F':
          /* Flushes the data cache to the FSAL and purges the cache */
          nfs_start_info.flush_datacache_mode = TRUE;
          nfs_start_info.flush_behaviour = CACHE_CONTENT_FLUSH_AND_DELETE;
          nfs_start_info.nb_flush_threads = (unsigned int)atoi(optarg);

          if(nfs_start_info.nb_flush_threads > NB_MAX_FLUSHER_THREAD)
            nfs_start_info.nb_flush_threads = NB_MAX_FLUSHER_THREAD;
          break;

        case 'S':
          /* Flushes the data cache to the FSAL, without purging the cache */
          nfs_start_info.flush_datacache_mode = TRUE;
          nfs_start_info.flush_behaviour = CACHE_CONTENT_FLUSH_SYNC_ONLY;
          nfs_start_info.nb_flush_threads = (unsigned int)atoi(optarg);

          if(nfs_start_info.nb_flush_threads > NB_MAX_FLUSHER_THREAD)
            nfs_start_info.nb_flush_threads = NB_MAX_FLUSHER_THREAD;
          break;

        case 'h':
          /* display the help */
          fprintf(stderr, usage, exec_name);
          exit(0);
          break;

        case '?':
        default:
          /* ignore unsupported options */
          fprintf(stderr, "WARNING: unknown GANESHA NFS daemon option: %c\n",
                  (char)optopt);

        }
    }

  /* initialize memory and logging */

  nfs_prereq_init(exec_name, host_name, debug_level, log_path);

  /* Start in background, if wanted */
  if(detach_flag)
    {
      /* Step 1: forking a service process */
      switch (son_pid = fork())
        {
        case -1:
          /* Fork failed */
          LogError(COMPONENT_MAIN, ERR_SYS, ERR_FORK, errno);
          LogCrit(COMPONENT_MAIN,
                  "Could nout start nfs daemon, exiting...");
          exit(1);

        case 0:
          /* This code is within the son (that will actually work)
           * Let's make it the leader of its group of process */
          if(setsid() == -1)
            {
              LogError(COMPONENT_MAIN, ERR_SYS, ERR_SETSID, errno);
              LogCrit(COMPONENT_MAIN,
                      "Could nout start nfs daemon, exiting...");
              exit(1);
            }
          break;

        default:
          /* This code is within the father, it is useless, it must die */
          LogFullDebug(COMPONENT_MAIN,
                       "Starting a son of pid %d\n", son_pid);
          exit(0);
          break;
        }
    }

  /* Get the FSAL functions */
  FSAL_LoadFunctions();

  /* Get the FSAL consts */
  FSAL_LoadConsts();

  LogEvent(COMPONENT_MAIN,
           ">>>>>>>>>> Starting GANESHA NFS Daemon on FSAL/%s <<<<<<<<<<",
           FSAL_GetFSName());

  /* initialize default parameters */
  nfs_set_param_default();

  /* return all errors */
  nfs_param.core_param.drop_io_errors = FALSE;
  nfs_param.core_param.drop_inval_errors = FALSE;
  nfs_param.core_param.drop_delay_errors = FALSE;

  /* parse configuration file (if specified) */

  if(strlen(config_path) > 0)
    {
      if(nfs_set_param_from_conf(&nfs_start_info))
        {
          LogCrit(COMPONENT_MAIN,
                  "NFS MAIN: Error parsing configuration file.");
          exit(1);
        }
    }

  /* set filesystem relative info */

  ((fusefs_specific_initinfo_t *) &nfs_param.fsal_param.fs_specific_info)->fs_ops = &ops;
  ((fusefs_specific_initinfo_t *) &nfs_param.fsal_param.fs_specific_info)->user_data = user_data;

#ifdef _SNMP_ADM_ACTIVE
  if(!nfs_param.extern_param.snmp_adm.snmp_log_file[0])
    strcpy(nfs_param.extern_param.snmp_adm.snmp_log_file, log_path);
#endif

  /* add export by hand if no export was defined
   * in config file (always '/')
   */

  if(!nfs_param.pexportlist)
    {
      nfs_param.pexportlist = BuildDefaultExport();
      if(nfs_param.pexportlist == NULL)
        {
          LogCrit(COMPONENT_MAIN,
                  "NFS MAIN: Could not create export entry for '/'");
          exit(1);
        }
    }

  /* if this is a single threaded application, set worker count */
  if(single_threaded)
    nfs_param.core_param.nb_worker = 1;

  /* check parameters consitency */

  if(nfs_check_param_consistency())
    {
      LogMajor(COMPONENT_MAIN,
               "NFS MAIN: Inconsistent parameters found");
      LogMajor(COMPONENT_MAIN,
               "MAJOR WARNING: /!\\ | Bad Parameters could have significant impact on the daemon behavior");
      exit(1);
    }

  /* Everything seems to be OK! We can now start service threads */
  nfs_start(&nfs_start_info);

  return 0;

}
