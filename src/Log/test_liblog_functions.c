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
#include "log_macros.h"

#define ERR_DUMMY 3
static family_error_t tab_test_err[] = {
#define ERR_DUMMY_1  0
  {ERR_DUMMY_1, "ERR_DUMMY_1", "First Dummy Error"},
#define ERR_DUMMY_2  1
  {ERR_DUMMY_2, "ERR_DUMMY_2", "Second Dummy Error"},

  {ERR_NULL, "ERR_NULL", ""}
};

/**
 *  Tests about Log streams and special printf functions.
 */
int Test1(void *arg)
{
  char tempstr[255];

  SetNameFunction((char *)arg);

  DisplayLogFlux(stdout, "%s", "Test number 1");
  DisplayLogFlux(stdout, "%s", "Test number 2");

  LogTest("------------------------------------------------------");

  DisplayErrorFlux(stdout, ERR_SYS, ERR_FORK, ENOENT);
  DisplayErrorLog(ERR_SYS, ERR_SOCKET, EINTR);

  LogTest("------------------------------------------------------");

  DisplayErrorFlux(stdout, ERR_DUMMY, ERR_DUMMY_2, ENOENT);

  LogTest("------------------------------------------------------");

  LogTest("A numerical error : error %%d = %%J%%R, in ERR_DUMMY_2 %%J%%r");
  log_snprintf(tempstr, sizeof(tempstr), "A numerical error : error %d = %J%R, in ERR_DUMMY_2 %J%r", ERR_SIGACTION,
              ERR_SYS, ERR_SIGACTION, ERR_DUMMY, ERR_DUMMY_2);
  LogTest("%s", tempstr);
  DisplayLogFlux(stdout, "A numerical error : error %d = %J%R, in ERR_DUMMY_2 %J%r",
                 ERR_SIGACTION, ERR_SYS, ERR_SIGACTION, ERR_DUMMY, ERR_DUMMY_2);

  LogTest("------------------------------------------------------");

  LogTest("A numerical error : error %%d = %%J%%R, in ERR_DUMMY_2 %%J%%R");
  log_snprintf(tempstr, sizeof(tempstr), "A numerical error : error %d = %J%R, in ERR_DUMMY_2 %J%R", ERR_SIGACTION,
              ERR_SYS, ERR_SIGACTION, ERR_DUMMY, ERR_DUMMY_2);
  LogTest("%s", tempstr);
  LogTest("A numerical error : error %d = %J%R, in ERR_DUMMY_2 %J%R", ERR_SIGACTION,
              ERR_SYS, ERR_SIGACTION, ERR_DUMMY, ERR_DUMMY_2);
  DisplayLogFlux(stdout, "A numerical error : error %d = %J%R, in ERR_DUMMY_2 %J%R",
                 ERR_SIGACTION, ERR_SYS, ERR_SIGACTION, ERR_DUMMY, ERR_DUMMY_2);

  LogTest("------------------------------------------------------");

  DisplayErrorFlux(stderr, ERR_DUMMY, ERR_DUMMY_1, EPERM);

  LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, EINVAL);
  LogCrit(COMPONENT_INIT, "Initializing %K%V %R", ERR_SYS, ERR_MALLOC, EINVAL, EINVAL);
  errno = EINVAL;
  LogTest("\nTest %%m (undocumented for printf, but works, documented for syslog)");
  LogTest("an extra string parameter is printed to demonstrate no parameters are consumed");
  LogTest("error %m %s", "(not part of %%m)");
  LogTest("\nTest %%b, %%B, %%h (doesn't work - overloaded), %%H, %%y, and %%Y. These are odd tags:");
  LogTest("   %%b, %%B, %%h, and %%H each consume int1, str2, str3 even if not all are printed");
  LogTest("   %%y and %%Y each consume int1, str2, str3, int4, str5, str6 even if not all are printed");
  LogTest("   An extra string parameter is printed to demonstrate how the parameters are consumed");
  LogTest("test %%b %b %s", 1, "str2", "str3", "(not part of %%b)");
  LogTest("test %%B %B %s", 1, "str2", "str3", "(not part of %%B)");
  LogTest("test %%h %h %s", 1, "str2", "(not part of %%h)");
  LogTest("test %%H %H %s", 1, "str2", "str3", "(not part of %%H)");
  LogTest("test %%y %y %s", 1, "str2", "str3", 4, "str5", "str6", "(not part of %%y)");
  LogTest("test %%Y %Y %s", 1, "str2", "str3", 4, "str5", "str6", "(not part of %%Y)");
  LogTest("\nTest new tags for reporting errno values");
  LogTest("test %%w %w", EINVAL);
  LogTest("test %%W %W", EINVAL);
  LogTest("\nTest context sensitive tags");
  LogTest("%%K, %%V, and %%v go together, defaulting to ERR_SYS");
  LogTest("%%J, %%R, and %%r go together, defaulting to ERR_POSIX");
  LogTest("test %%K%%V %%K%%V %K%V %K%V", ERR_SYS, ERR_SIGACTION, ERR_POSIX, EINVAL);
  LogTest("test %%K%%v %%K%%v %K%v %K%v", ERR_SYS, ERR_SIGACTION, ERR_POSIX, EINVAL);
  LogTest("test %%J%%R %%J%%R %J%R %J%R", ERR_SYS, ERR_SIGACTION, ERR_POSIX, EINVAL);
  LogTest("test %%J%%r %%J%%r %J%r %J%r", ERR_SYS, ERR_SIGACTION, ERR_POSIX, EINVAL);
  LogTest("test %%V %%R %V %R", ERR_SIGACTION, EINVAL);
  LogTest("test %%v %%r %v %r", ERR_SIGACTION, EINVAL);

  LogTest("Tests passed successfully");

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
          SetDefaultLogging("TEST");
          InitDebug(NIV_EVENT);
          AddFamilyError(ERR_POSIX, "POSIX Errors", tab_systeme_status);
          LogTest("AddFamilyError = %d", AddFamilyError(ERR_DUMMY, "Family Dummy", tab_test_err));
          LogTest("The family which was added is %s", ReturnNameFamilyError(ERR_DUMMY));

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
          SetDefaultLogging("STDOUT");
          InitDebug(NIV_EVENT);
          LogTest("AddFamilyError = %d", AddFamilyError(ERR_DUMMY, "Family Dummy", tab_test_err));
          LogTest("The family which was added is %s", ReturnNameFamilyError(ERR_DUMMY));

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
          LogTest("%s", usage);
          exit(1);
        }

    }
  else
    {
      LogTest("%s", usage);
      exit(1);
    }

}
