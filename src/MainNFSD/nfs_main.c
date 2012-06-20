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
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>             /* for sigaction */
#include <errno.h>
#ifdef _PNFS
#include "fsal_pnfs.h"
#endif /* _PNFS */

/* parameters for NFSd startup and default values */

nfs_start_info_t my_nfs_start_info = {
  .dump_default_config = FALSE,
  .lw_mark_trigger = FALSE
};

char *my_config_path = "/etc/ganesha/ganesha.conf";
char my_pidfile[] = "/var/run/ganesha.pid";
char log_path[MAXPATHLEN] = "";
char exec_name[MAXPATHLEN] = "nfs-ganesha";
char host_name[MAXHOSTNAMELEN] = "localhost";
int debug_level = -1;
int detach_flag = FALSE;
char ganesha_exec_path[MAXPATHLEN];

/* command line syntax */

char options[] = "h@RTdS:F:S:P:f:L:N:E:p:";
char usage[] =
    "Usage: %s [-hd][-L <logfile>][-N <dbg_lvl>][-f <config_file>]\n"
    "\t[-h]                display this help\n"
    "\t[-L <logfile>]      set the default logfile for the daemon\n"
    "\t[-N <dbg_lvl>]      set the verbosity level\n"
    "\t[-f <config_file>]  set the config file to be used\n"
    "\t[-p <pid_file>]     set the pid file\n"
    "\t[-d]                the daemon starts in background, in a new process group\n"
    "\t[-R]                daemon will manage RPCSEC_GSS (default is no RPCSEC_GSS)\n"
    "\t[-T]                dump the default configuration on stdout\n"
    "\t[-E] <epoch<]       overrides ServerBootTime for ServerEpoch\n"
    "----------------- Signals ----------------\n"
    "SIGUSR1    : Enable/Disable File Content Cache forced flush\n"
    "SIGTERM    : Cleanly terminate the program\n"
    "------------- Default Values -------------\n"
    "LogFile    : /tmp/nfs-ganesha.log\n"
    "PidFile    : /var/run/ganesha.pid\n"
    "DebugLevel : NIV_EVENT\n" "ConfigFile : /etc/ganesha/ganesha.conf\n";

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
  int pidfile;
#ifndef HAVE_DAEMON
  pid_t son_pid;
#endif
  sigset_t signals_to_block;

  /* Set the server's boot time and epoch */
  ServerBootTime = time(NULL);
  ServerEpoch    = ServerBootTime;

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

  strcpy(config_path, my_config_path);

  strcpy( pidfile_path, my_pidfile ) ;

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
          printf("Git HEAD = %s\n", _GIT_HEAD_COMMIT ) ;
          printf("Git Describe = %s\n", _GIT_DESCRIBE ) ;
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
                      "Invalid value for option 'N': NIV_NULL, NIV_MAJ, NIV_CRIT, NIV_EVENT, NIV_DEBUG, NIV_MID_DEBUG or NIV_FULL_DEBUG expected.\n");
              exit(1);
            }
          break;

        case 'f':
          /* config file */
          strncpy(config_path, optarg, MAXPATHLEN);
          break;

        case 'p':
          /* PID file */
          strncpy( pidfile_path, optarg, MAXPATHLEN ) ;
          break ;

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

        case 'E':
          ServerEpoch = (time_t) atoll(optarg);
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
  nfs_prereq_init(exec_name, host_name, debug_level, log_path);

  /* Start in background, if wanted */
  if(detach_flag)
    {
#ifdef HAVE_DAEMON
        /* daemonize the process (fork, close xterm fds,
         * detach from parent process) */
        if (daemon(0, 0))
          LogFatal(COMPONENT_MAIN,
                   "Error detaching process from parent: %s",
                   strerror(errno));
#else
      /* Step 1: forking a service process */
      switch (son_pid = fork())
        {
        case -1:
          /* Fork failed */
          LogFatal(COMPONENT_MAIN,
                   "Could not start nfs daemon (fork error %d (%s)",
                   errno, strerror(errno));
          break;

        case 0:
          /* This code is within the son (that will actually work)
           * Let's make it the leader of its group of process */
          if(setsid() == -1)
            {
	      LogFatal(COMPONENT_MAIN,
	               "Could not start nfs daemon (setsid error %d (%s)",
	               errno, strerror(errno));
            }
          break;

        default:
          /* This code is within the father, it is useless, it must die */
          LogFullDebug(COMPONENT_MAIN, "Starting a son of pid %d", son_pid);
          exit(0);
          break;
        }
#endif
    }

  /* Make sure Linux file i/o will return with error if file size is exceeded. */
#ifdef _LINUX
  signal(SIGXFSZ, SIG_IGN);
#endif

  /* Echo PID into pidfile */
  if( ( pidfile = open(pidfile_path, O_CREAT|O_RDWR, 0644)) == -1 )
   {
     LogFatal( COMPONENT_MAIN, "Can't open pid file %si for writing", pidfile_path ) ;
   }
  else
   {
     char linebuf[1024];
     struct flock lk;

     /* Try to obtain a lock on the file */
     lk.l_type = F_WRLCK;
     lk.l_whence = SEEK_SET;
     lk.l_start = (off_t)0;
     lk.l_len = (off_t)0;
     if (fcntl(pidfile, F_SETLK, &lk) == -1)
       LogFatal( COMPONENT_MAIN, "Ganesha already started");

     /* Put pid into file, then close it */
     (void) snprintf(linebuf, sizeof(linebuf), "%u\n", getpid() ) ;
     if (write(pidfile, linebuf, strlen(linebuf)) == -1)
       LogCrit( COMPONENT_MAIN, "Couldn't write pid to file %s",
           pidfile_path);
   }

  /* Set up for the signal handler.
   * Blocks the signals the signal handler will handle.
   */
  sigemptyset(&signals_to_block);
  sigaddset(&signals_to_block, SIGTERM);
  sigaddset(&signals_to_block, SIGHUP);
  sigaddset(&signals_to_block, SIGPIPE);
  if(pthread_sigmask(SIG_BLOCK, &signals_to_block, NULL) != 0)
    LogFatal(COMPONENT_MAIN,
             "Could not start nfs daemon, pthread_sigmask failed");

  /* Set the parameter to 0 before doing anything */
  memset((char *)&nfs_param, 0, sizeof(nfs_parameter_t));

  /* Get the FSAL functions */
  FSAL_LoadFunctions();

  /* Get the FSAL consts */
  FSAL_LoadConsts();

#ifdef _PNFS_MDS
  FSAL_LoadMDSFunctions();
#endif /* _PNFS_MDS */
#ifdef _PNFS_DS
  FSAL_LoadDSFunctions();
#endif

  LogEvent(COMPONENT_MAIN,
           ">>>>>>>>>>--------------------------------------- <<<<<<<<<<" ) ;

  LogEvent(COMPONENT_MAIN,
           ">>>>>>>>>> Starting GANESHA NFS Daemon on FSAL/%s <<<<<<<<<<",
           FSAL_GetFSName());

  LogEvent(COMPONENT_MAIN,
           ">>>>>>>>>> Version " VERSION "-" RELEASE DIST_VER " <<<<<<<<<<");

  LogEvent(COMPONENT_MAIN,
           ">>>>>>>>>>--------------------------------------- <<<<<<<<<<" ) ;

  /* initialize default parameters */

  nfs_set_param_default();

  /* parse configuration file */

  if(nfs_set_param_from_conf(&my_nfs_start_info))
    {
      LogFatal(COMPONENT_INIT, "Error parsing configuration file.");
    }

  /* check parameters consitency */

  if(nfs_check_param_consistency())
    {
      LogFatal(COMPONENT_INIT,
	       "Inconsistent parameters found. Exiting..." ) ;
    }

  /* Everything seems to be OK! We can now start service threads */
  nfs_start(&my_nfs_start_info);

  return 0;

}
