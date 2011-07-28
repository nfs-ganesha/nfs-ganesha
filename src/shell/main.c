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
 * \file    main.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/23 07:42:53 $
 * \version $Revision: 1.28 $
 * \brief   main shell routine.
 *
 * $Log: main.c,v $
 * Revision 1.28  2006/02/23 07:42:53  leibovic
 * Adding -n option to shell.
 *
 * Revision 1.26  2005/08/12 11:56:59  leibovic
 * coquille.
 *
 * Revision 1.25  2005/07/26 12:54:47  leibovic
 * Multi-thread shell with synchronisation routines.
 *
 * Revision 1.24  2005/05/09 12:23:55  leibovic
 * Version 2 of ganeshell.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "shell.h"
#include "Getopt.h"
#include <libgen.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#ifdef HAVE_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define NBTHRMAX 64

time_t ServerBootTime = 0;

typedef struct shell_info__
{

  int shell_id;

  char prompt[32];
  char script_file[128];

  pthread_t thread;
  pthread_attr_t attrs;

  int status;

} shell_info_t;

shell_info_t thrlist[NBTHRMAX];

int verbose = 0;

void *LaunchShell(void *arg)
{
  int rc;

  shell_info_t *p_info = (shell_info_t *) arg;

  rc = shell_Init(verbose, p_info->script_file, p_info->prompt, p_info->shell_id);

  if(rc)
    {
      fprintf(stderr, "GANESHELL: ERROR %d in shell_Init\n", rc);
      p_info->status = rc;
      pthread_exit(&p_info->status);
    }

  rc = shell_Launch();

  if(rc)
    {
      fprintf(stderr, "GANESHELL: ERROR %d in shell_Launch\n", rc);
      p_info->status = rc;
      pthread_exit(&p_info->status);
    }

  p_info->status = 0;

  /* never reached */
  return &p_info->status;

}

int main(int argc, char **argv)
{

  static char *format = "h@vn:";
  static char *help = "Usage: %s [-h][-v][-n <nb>][Script_File1 [Script_File2]...]\n";

  int option, rc;

  int err_flag = 0;
  int flag_h = 0;

  int nb_instance = 0;

  char *prompt = "ganeshell>";
  char *script_file = NULL;

  char *progname = basename(argv[0]);

  int nb_threads = 0;

  /* Set the Boot time */
  ServerBootTime = time(NULL);

  /* disables Getopt error message */
  Opterr = 0;

  /* reinits Getopt processing */
  Optind = 1;

  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'h':
          if(flag_h)
            fprintf(stderr,
                    "%s: warning: option 'h' has been specified more than once.\n",
                    progname);
          else
            flag_h++;
          break;

        case '@':
          /* A litlle backdoor to keep track of binary versions */
          printf("%s compiled on %s at %s\n", progname, __DATE__, __TIME__);
          printf("Release = %s\n", VERSION);
          printf("Release comment = %s\n", VERSION_COMMENT);
          exit(0);
          break;

        case 'n':
          if(nb_instance)
            fprintf(stderr,
                    "%s: warning: option 'n' has been specified more than once.\n",
                    progname);
          else
            nb_instance = atoi(Optarg);

          break;

        case 'v':
          if(verbose)
            fprintf(stderr,
                    "%s: warning: option 'v' has been specified more than once.\n",
                    progname);
          else
            verbose++;
          break;

        case '?':
          fprintf(stderr, "%s: unknown option : %c\n", progname, Optopt);
          err_flag++;
          break;
        }
    }

  /* help flag */
  if(flag_h || err_flag)
    {
      fprintf(stderr, help, basename(progname));
      exit(err_flag);
    }
#ifdef HAVE_LIBREADLINE
  /* Initialize history */
  using_history();
#endif

  /* case when the 'n' option is specified */
  if(nb_instance != 0)
    {
      int i;
      /* case when there two or more threads */

      for(i = 0; i < nb_instance; i++)
        {

          if(verbose)
            fprintf(stderr, "Starting thread %d using file %s...\n",
                    nb_threads, argv[Optind]);

          thrlist[nb_threads].shell_id = nb_threads;

          snprintf(thrlist[nb_threads].prompt, 32, "ganeshell-%d>", nb_threads);
          strncpy(thrlist[nb_threads].script_file, argv[Optind], 128);

          pthread_attr_init(&thrlist[nb_threads].attrs);
          pthread_attr_setscope(&thrlist[nb_threads].attrs, PTHREAD_SCOPE_SYSTEM);

          nb_threads++;

          if(nb_threads >= NBTHRMAX)
            {
              fprintf(stderr, "GANESHELL: Too much threads (%d > %d)\n", nb_threads,
                      NBTHRMAX);
              exit(1);
            }

        }

      /* inits shell barriers */

      rc = shell_BarrierInit(nb_threads);

      if(rc)
        {
          fprintf(stderr, "GANESHELL: ERROR %d in shell_BarrierInit\n", rc);
          exit(1);
        }

      /* launching threads */

      for(i = 0; i < nb_threads; i++)
        {
          rc = pthread_create(&thrlist[i].thread,
                              &thrlist[i].attrs, LaunchShell, &thrlist[i]);

          if(rc)
            {
              fprintf(stderr, "GANESHELL: ERROR %d in pthread_create\n", rc);
              exit(1);
            }

        }

      /* waiting for thread termination */

      for(i = 0; i < nb_threads; i++)
        {
          void *ret;
          pthread_join(thrlist[i].thread, &ret);

        }

      exit(0);

    }
  /* case when there is only zero or one script file */
  else if(Optind >= (argc - 1))
    {

      if(Optind == (argc - 1))
        {
          script_file = argv[Optind];
        }

      rc = shell_Init(verbose, script_file, prompt, 0);

      if(rc)
        {
          fprintf(stderr, "GANESHELL: ERROR %d in shell_Init\n", rc);
          exit(1);
        }

      rc = shell_Launch();

      if(rc)
        {
          fprintf(stderr, "GANESHELL: ERROR %d in shell_Launch\n", rc);
          exit(1);
        }

      exit(0);

    }
  else
    {
      int i;
      /* case when there two or more threads */

      for(i = Optind; i < argc; i++)
        {

          if(verbose)
            fprintf(stderr, "Starting thread %d using file %s...\n", nb_threads, argv[i]);

          thrlist[nb_threads].shell_id = nb_threads;

          snprintf(thrlist[nb_threads].prompt, 32, "ganeshell-%d>", nb_threads);
          strncpy(thrlist[nb_threads].script_file, argv[i], 128);

          pthread_attr_init(&thrlist[nb_threads].attrs);
          pthread_attr_setscope(&thrlist[nb_threads].attrs, PTHREAD_SCOPE_SYSTEM);

          nb_threads++;

          if(nb_threads >= NBTHRMAX)
            {
              fprintf(stderr, "GANESHELL: Too much threads (%d > %d)\n", nb_threads,
                      NBTHRMAX);
              exit(1);
            }

        }

      /* inits shell barriers */

      rc = shell_BarrierInit(nb_threads);

      if(rc)
        {
          fprintf(stderr, "GANESHELL: ERROR %d in shell_BarrierInit\n", rc);
          exit(1);
        }

      /* launching threads */

      for(i = 0; i < nb_threads; i++)
        {
          rc = pthread_create(&thrlist[i].thread,
                              &thrlist[i].attrs, LaunchShell, &thrlist[i]);

          if(rc)
            {
              fprintf(stderr, "GANESHELL: ERROR %d in pthread_create\n", rc);
              exit(1);
            }

        }

      /* waiting for thread termination */

      for(i = 0; i < nb_threads; i++)
        {
          void *ret;
          pthread_join(thrlist[i].thread, &ret);

        }

      exit(0);

    }

}
