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
 * Test de libaglae
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "log_functions.h"

static family_error_t tab_test_err[] = {
#define ERR_PIPO_1  0
  {ERR_PIPO_1, "ERR_PIPO_1", "Premiere Error Pipo"},
#define ERR_PIPO_2  1
  {ERR_PIPO_2, "ERR_PIPO_2", "Deuxieme Error Pipo"},

  {ERR_NULL, "ERR_NULL", ""}
};

/**
 *  Tests about Log streams and special printf functions.
 */
int Test1(void *arg)
{

  char tampon[255];
  log_t jd = LOG_INITIALIZER;
  desc_log_stream_t voie;

  SetNameFunction((char *)arg);

  /* Init d'un journal */
  strcpy(voie.path, "/dev/tty");
  AddLogStreamJd(&jd, V_FILE, voie, GANESHA_LOG_MAJOR, INF);

  voie.fd = fileno(stderr);
  AddLogStreamJd(&jd, V_FD, voie, GANESHA_LOG_CRITICAL, INF);

  voie.flux = stderr;
  AddLogStreamJd(&jd, V_STREAM, voie, GANESHA_LOG_EVENT, INF);

  AddLogStreamJd(&jd, V_SYSLOG, voie, GANESHA_LOG_MAJOR, INF);

  DisplayLogFlux(stdout, "%s", "Essai numero 1");
  DisplayLogFlux(stdout, "%s", "Essai numero 2");
  DisplayLogFlux(stdout, "Troncature impossible sur %s | %s", "un", "deux");
  DisplayLog("%s", "Essai journal numero 1");
  DisplayLog("%s", "Essai Log numero 2");
  DisplayLogFd(fileno(stderr), "%s", "Essai fd numero 1");
  DisplayLogFd(fileno(stderr), "%s", "Essai fd numero 2");
  DisplayLogPath("/dev/tty", "Essai sur un path numero 1");
  DisplayLogPath("/dev/tty", "Essai sur un path numero 2");
  DisplayLogString(tampon, "%s --> %d", "essai", 10);
  printf("%s", tampon);

  DisplayLogJdLevel(jd, GANESHA_LOG_MAJOR, "Essai sur un jd: MAJOR");
  DisplayLogJdLevel(jd, GANESHA_LOG_CRITICAL, "Essai sur un jd: CRIT");

  printf("------------------------------------------------------\n");

  DisplayErrorFlux(stdout, 0, ERR_FORK, 2);
  DisplayErrorFd(fileno(stdout), 0, ERR_MALLOC, 3);
  DisplayErrorLog(0, ERR_SOCKET, 4);
  DisplayErrorJd(jd, ERR_SYS, ERR_POPEN, 3);
  DisplayErrorStringLine(tampon, 0, ERR_SIGACTION, 1, 12345);
  printf("-->%s\n", tampon);

  printf("------------------------------------------------------\n");

  DisplayErrorFlux(stdout, 3, ERR_PIPO_2, 2);
  puts("Une erreur numerique : erreur %d = %R");
  log_sprintf(tampon, "Une erreur numerique : erreur %d = %J%R, dans err_pipo_1 %J%r", 5,
              0, 5, 3, ERR_PIPO_2);
  puts(tampon);
  DisplayLogFlux(stdout, "Une erreur numerique : erreur %d = %J%R, dans err_pipo_1 %J%r",
                 5, 0, 5, 3, ERR_PIPO_2);
  DisplayErrorFlux(stderr, 3, ERR_PIPO_1, 1);

  /* teste si le nom du thread est reste le meme depuis le debut : */
  if(strcmp(ReturnNameFunction(), (char *)arg))
    {

      printf("***** ERROR: initial function name \"%s\" differs from \"%s\" *****\n",
             (char *)arg, ReturnNameHost());
      return 1;

    }

  printf("Test reussi: Les tests sont passes avec succes\n");

  return 0;

}

void *run_Test1(void *arg)
{
  unsigned long long rc_long;

  rc_long = (unsigned long long)Test1(arg);

  return NULL ;
}

static char usage[] = "usage:\n\ttest_liblog STD|MT";

#define NB_THREADS 20

int main(int argc, char *argv[])
{

  if(argc == 2)
    {

      /* TEST 1 Standard */

      if(!strcmp(argv[1], "STD"))
        {

          int rc;

          SetNamePgm("test_liblog");
          SetNameHost("localhost");
          SetNameFileLog("/dev/tty");
          InitDebug(NIV_EVENT);
          printf("AddFamilyError = %d\n", AddFamilyError(3, "Family Pipo", tab_test_err));
          printf("La famille qui a ete ajoutee est %s\n", ReturnNameFamilyError(3));

          rc = Test1((void *)"monothread");
          return rc;

        }

      /* TEST 1 multithread */

      else if(!strcmp(argv[1], "MT"))
        {

          /* multithread test */
          pthread_attr_t th_attr[NB_THREADS];
          pthread_t threads[NB_THREADS];
          int th_index, i;
          void *result;

          SetNamePgm("test_liblog");
          SetNameHost("localhost");
          SetNameFileLog("/dev/tty");
          InitDebug(NIV_EVENT);
          printf("AddFamilyError = %d\n", AddFamilyError(3, "Family Pipo", tab_test_err));
          printf("La famille qui a ete ajoutee est %s\n", ReturnNameFamilyError(3));

          /* creation of attributes */
          for(th_index = 0; th_index < NB_THREADS; th_index++)
            {
              pthread_attr_init(&th_attr[th_index]);
              pthread_attr_setdetachstate(&th_attr[th_index], PTHREAD_CREATE_JOINABLE);
            }

          /* creation of threads with their names */
          for(i = 0; i < NB_THREADS; i++)
            {
              int rc;
              char *thread_name = malloc(256);
              snprintf(thread_name, 256, "thread %d", i);
              rc = pthread_create(&(threads[i]), &th_attr[i], run_Test1,
                                  (void *)thread_name);
            }

          /* waiting for threads termination */
          for(i = 0; i < NB_THREADS; i++)
            {
              pthread_join(threads[i], &result);
              if(result)
                return 1;
            }

          return 0;

        }

      /* unknown test */
      else
        {
          printf("%s\n", usage);
          exit(1);
        }

    }
  else
    {
      printf("%s\n", usage);
      exit(1);
    }

}
