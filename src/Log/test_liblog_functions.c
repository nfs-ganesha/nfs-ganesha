/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
void *Test1(void *arg)
{

  char tampon[255];
  log_t jd = LOG_INITIALIZER;
  desc_log_stream_t voie;

  SetNameFunction((char *)arg);

  /* Init d'un journal */
  strcpy(voie.path, "/dev/tty");
  AddLogStreamJd(&jd, V_FILE, voie, LOG_MAJOR, INF);

  voie.fd = fileno(stderr);
  AddLogStreamJd(&jd, V_FD, voie, LOG_CRITICAL, INF);

  voie.flux = stderr;
  AddLogStreamJd(&jd, V_STREAM, voie, LOG_EVENT, INF);

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

  DisplayLogJdLevel(jd, LOG_MAJOR, "Essai sur un jd: MAJOR");
  DisplayLogJdLevel(jd, LOG_CRITICAL, "Essai sur un jd: CRIT");

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
      return (void *)1;

    }

  printf("Test reussi: Les tests sont passes avec succes\n");

  return (void *)0;

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

          rc = (int)Test1((void *)"monothread");
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
              rc = pthread_create(&(threads[i]), &th_attr[i], Test1, (void *)thread_name);
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
