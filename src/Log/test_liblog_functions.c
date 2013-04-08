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

#pragma GCC diagnostic ignored "-Wformat"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define ERR_DUMMY 3
static family_error_t tab_test_err[] = {
#define ERR_DUMMY_1  0
  {ERR_DUMMY_1, "ERR_DUMMY_1", "First Dummy Error"},
#define ERR_DUMMY_2  1
  {ERR_DUMMY_2, "ERR_DUMMY_2", "Second Dummy Error"},

  {ERR_NULL, "ERR_NULL", ""}
};

void TestAlways(int expect, char *buff, log_components_t component, char *string)
{
  char compare[2048];
  sprintf(compare, "%s: %s", LogComponents[component].comp_str, string);
  buff[0] = '\0';
  LogAlways(component, "%s", string);
  if ((expect && (strcmp(compare, buff) != 0)) || (!expect && (buff[0] != '\0')))
    {
      LogTest("FAILURE: %s produced \"%s\" expected \"%s\"",
              string, buff, compare);
      exit(1);
    }
  LogTest("SUCCESS: %s produced \"%s\"", string, buff);
}

void TestMajor(int expect, char *buff, log_components_t component, char *string)
{
  char compare[2048];
  sprintf(compare, "%s: MAJOR ERROR: %s", LogComponents[component].comp_str, string);
  buff[0] = '\0';
  LogMajor(component, "%s", string);
  if ((expect && (strcmp(compare, buff) != 0)) || (!expect && (buff[0] != '\0')))
  {
      LogTest("FAILURE: %s produced \"%s\" expected \"%s\"",
              string, buff, compare);
      exit(1);
    }
  if (expect)
    LogTest("SUCCESS: %s produced \"%s\"", string, buff);
  else
    LogTest("SUCCESS: %s didn't produce anything", string);
}

void TestCrit(int expect, char *buff, log_components_t component, char *string)
{
  char compare[2048];
  sprintf(compare, "%s: CRITICAL ERROR: %s", LogComponents[component].comp_str, string);
  buff[0] = '\0';
  LogCrit(component, "%s", string);
  if ((expect && (strcmp(compare, buff) != 0)) || (!expect && (buff[0] != '\0')))
  {
      LogTest("FAILURE: %s produced \"%s\" expected \"%s\"",
              string, buff, compare);
      exit(1);
    }
  if (expect)
    LogTest("SUCCESS: %s produced \"%s\"", string, buff);
  else
    LogTest("SUCCESS: %s didn't produce anything", string);
}

void TestEvent(int expect, char *buff, log_components_t component, char *string)
{
  char compare[2048];
  sprintf(compare, "%s: EVENT: %s", LogComponents[component].comp_str, string);
  buff[0] = '\0';
  LogEvent(component, "%s", string);
  if ((expect && (strcmp(compare, buff) != 0)) || (!expect && (buff[0] != '\0')))
  {
      LogTest("FAILURE: %s produced \"%s\" expected \"%s\"",
              string, buff, compare);
      exit(1);
    }
  if (expect)
    LogTest("SUCCESS: %s produced \"%s\"", string, buff);
  else
    LogTest("SUCCESS: %s didn't produce anything", string);
}

void TestDebug(int expect, char *buff, log_components_t component, char *string)
{
  char compare[2048];
  sprintf(compare, "%s: DEBUG: %s", LogComponents[component].comp_str, string);
  buff[0] = '\0';
  LogDebug(component, "%s", string);
  
  if ((expect && (strcmp(compare, buff) != 0)) || (!expect && (buff[0] != '\0')))
    {
      LogTest("FAILURE: %s produced \"%s\" expected \"%s\"",
              string, buff, compare);
      exit(1);
    }
  if (expect)
    LogTest("SUCCESS: %s produced \"%s\"", string, buff);
  else
    LogTest("SUCCESS: %s didn't produce anything", string);
}

void TestFullDebug(int expect, char *buff, log_components_t component, char *string)
{
  char compare[2048];
  sprintf(compare, "%s: FULLDEBUG: %s", LogComponents[component].comp_str, string);
  buff[0] = '\0';
  LogFullDebug(component, "%s", string);
  if ((expect && (strcmp(compare, buff) != 0)) || (!expect && (buff[0] != '\0')))
    {
      LogTest("FAILURE: %s produced \"%s\" expected \"%s\"",
              string, buff, compare);
      exit(1);
    }
  if (expect)
    LogTest("SUCCESS: %s produced \"%s\"", string, buff);
  else
    LogTest("SUCCESS: %s didn't produce anything", string);
}

/**
 *  Tests about Log streams and special printf functions.
 */
void Test1(char *str, char *file)
{
  char tempstr[2048];
  struct display_buffer buffer = {sizeof(tempstr), tmpstr, tmpstr};
  int  i;

  SetComponentLogFile(COMPONENT_INIT, "STDOUT");
  LogAlways(COMPONENT_INIT, "%s", "Starting Log Tests");
  LogTest("My PID = %d", getpid());

  LogTest("------------------------------------------------------");
  LogTest("Test conversion of log levels between string and integer");
  for (i = NIV_NULL; i < NB_LOG_LEVEL; i++)
    {
      int j;
      if (strcmp(tabLogLevel[i].str, ReturnLevelInt(i)) != 0)
        {
          LogTest("FAILURE: Log level %d did not convert to %s, it converted to %s",
                  i, tabLogLevel[i].str, ReturnLevelInt(i));
          exit(1);
        }
      j = ReturnLevelAscii(tabLogLevel[i].str);
      if (j != i)
        {
          LogTest("FAILURE: Log level %s did not convert to %d, it converted to %d",
                  tabLogLevel[i].str, i, j);
          exit(1);
        }
    }

  LogTest("------------------------------------------------------");

  LogTest("\nTesting possible environment variable");
  LogTest("COMPONENT_MEMCORRUPT debug level is %s",
          ReturnLevelInt(LogComponents[COMPONENT_MEMCORRUPT].comp_log_level));
  LogFullDebug(COMPONENT_MEMCORRUPT,
               "This should appear if environment is set properly");

  LogTest("------------------------------------------------------");
  LogTest("Send some messages to various files");
  SetComponentLogFile(COMPONENT_DISPATCH, "STDERR");
  LogEvent(COMPONENT_DISPATCH, "This should go to stderr");
  SetComponentLogFile(COMPONENT_DISPATCH, "STDOUT");
  LogEvent(COMPONENT_DISPATCH, "This should go to stdout");
  SetComponentLogFile(COMPONENT_DISPATCH, "SYSLOG");
  LogEvent(COMPONENT_DISPATCH, "This should go to syslog (verf = %s)", str);
  LogTest("About to set %s", file);
  SetComponentLogFile(COMPONENT_DISPATCH, file);
  LogTest("Got it set");
  LogEvent(COMPONENT_DISPATCH, "This should go to %s", file);

  /*
   * Set up for tests that will verify what was actually produced by log messages.
   * This is used to test log levels and to test the log_vnsprintf function.
   */
  /* TODO FSF: this can be done by setting the right header flags and peeking at
   * the context buffer.
   */
  SetComponentLogBuffer(COMPONENT_MAIN, &buffer);
  SetComponentLogBuffer(COMPONENT_INIT, &buffer);

#ifdef _SNMP_ADM_ACTIVE
  {
    snmp_adm_type_union param;
    int rc;
    strcpy(param.string, "FAILED");

    LogTest("------------------------------------------------------");
    LogTest("Test SNMP functions");
    SetLevelDebug(NIV_DEBUG);

    rc = getComponentLogLevel(&param, (void *)COMPONENT_ALL);
    LogTest("getComponentLogLevel(&param, (void *)COMPONENT_ALL) rc=%d result=%s",
            rc, param.string);
    if (rc != 0)
    {
      LogTest("FAILURE");
      exit(1);
    }
    strcpy(param.string, "NIV_EVENT");
    rc = setComponentLogLevel(&param, (void *)COMPONENT_MAIN);
    LogTest("setComponentLogLevel(&param, (void *)COMPONENT_MAIN) rc=%d", rc);
    if (rc != 0)
    {
      LogTest("FAILURE");
      exit(1);
    }
    TestAlways    (TRUE,  tempstr, COMPONENT_MAIN, "LogAlways (should print)");
    TestMajor     (TRUE,  tempstr, COMPONENT_MAIN, "LogMajor (should print)");
    TestCrit      (TRUE,  tempstr, COMPONENT_MAIN, "LogCrit (should print)");
    TestEvent     (TRUE,  tempstr, COMPONENT_MAIN, "LogEvent (should print)");
    TestDebug     (FALSE, tempstr, COMPONENT_MAIN, "LogDebug (shouldn't print)");
    TestFullDebug (FALSE, tempstr, COMPONENT_MAIN, "LogFullDebug (shouldn't print)");
    TestAlways    (TRUE,  tempstr, COMPONENT_INIT, "LogAlways (should print)");
    TestMajor     (TRUE,  tempstr, COMPONENT_INIT, "LogMajor (should print)");
    TestCrit      (TRUE,  tempstr, COMPONENT_INIT, "LogCrit (should print)");
    TestEvent     (TRUE,  tempstr, COMPONENT_INIT, "LogEvent (should print)");
    TestDebug     (TRUE,  tempstr, COMPONENT_INIT, "LogDebug (should print)");
    TestFullDebug (FALSE, tempstr, COMPONENT_INIT, "LogFullDebug (shouldn't print)");
  }
#endif /* _SNMP_ADM_ACTIVE */

  LogTest("------------------------------------------------------");
  LogTest("Test all levels of log filtering");
  SetComponentLogLevel(COMPONENT_MAIN, NIV_NULL);
  TestAlways    (TRUE,  tempstr, COMPONENT_MAIN, "LogAlways (should print)");
  TestMajor     (FALSE, tempstr, COMPONENT_MAIN, "LogMajor (shouldn't print)");
  TestCrit      (FALSE, tempstr, COMPONENT_MAIN, "LogCrit (shouldn't print)");
  TestEvent     (FALSE, tempstr, COMPONENT_MAIN, "LogEvent (shouldn't print)");
  TestDebug     (FALSE, tempstr, COMPONENT_MAIN, "LogDebug (shouldn't print)");
  TestFullDebug (FALSE, tempstr, COMPONENT_MAIN, "LogFullDebug (shouldn't print)");
  SetComponentLogLevel(COMPONENT_MAIN, NIV_MAJOR);
  TestAlways    (TRUE,  tempstr, COMPONENT_MAIN, "LogAlways (should print)");
  TestMajor     (TRUE,  tempstr, COMPONENT_MAIN, "LogMajor (should print)");
  TestCrit      (FALSE, tempstr, COMPONENT_MAIN, "LogCrit (shouldn't print)");
  TestEvent     (FALSE, tempstr, COMPONENT_MAIN, "LogEvent (shouldn't print)");
  TestDebug     (FALSE, tempstr, COMPONENT_MAIN, "LogDebug (shouldn't print)");
  TestFullDebug (FALSE, tempstr, COMPONENT_MAIN, "LogFullDebug (shouldn't print)");
  SetComponentLogLevel(COMPONENT_MAIN, NIV_CRIT);
  TestAlways    (TRUE,  tempstr, COMPONENT_MAIN, "LogAlways (should print)");
  TestMajor     (TRUE,  tempstr, COMPONENT_MAIN, "LogMajor (should print)");
  TestCrit      (TRUE,  tempstr, COMPONENT_MAIN, "LogCrit (should print)");
  TestEvent     (FALSE, tempstr, COMPONENT_MAIN, "LogEvent (shouldn't print)");
  TestDebug     (FALSE, tempstr, COMPONENT_MAIN, "LogDebug (shouldn't print)");
  TestFullDebug (FALSE, tempstr, COMPONENT_MAIN, "LogFullDebug (shouldn't print)");
  SetComponentLogLevel(COMPONENT_MAIN, NIV_EVENT);
  TestAlways    (TRUE,  tempstr, COMPONENT_MAIN, "LogAlways (should print)");
  TestMajor     (TRUE,  tempstr, COMPONENT_MAIN, "LogMajor (should print)");
  TestCrit      (TRUE,  tempstr, COMPONENT_MAIN, "LogCrit (should print)");
  TestEvent     (TRUE,  tempstr, COMPONENT_MAIN, "LogEvent (should print)");
  TestDebug     (FALSE, tempstr, COMPONENT_MAIN, "LogDebug (shouldn't print)");
  TestFullDebug (FALSE, tempstr, COMPONENT_MAIN, "LogFullDebug (shouldn't print)");
  SetComponentLogLevel(COMPONENT_MAIN, NIV_DEBUG);
  TestAlways    (TRUE,  tempstr, COMPONENT_MAIN, "LogAlways (should print)");
  TestMajor     (TRUE,  tempstr, COMPONENT_MAIN, "LogMajor (should print)");
  TestCrit      (TRUE,  tempstr, COMPONENT_MAIN, "LogCrit (should print)");
  TestEvent     (TRUE,  tempstr, COMPONENT_MAIN, "LogEvent (should print)");
  TestDebug     (TRUE,  tempstr, COMPONENT_MAIN, "LogDebug (should print)");
  TestFullDebug (FALSE, tempstr, COMPONENT_MAIN, "LogFullDebug (shouldn't print)");
  SetComponentLogLevel(COMPONENT_MAIN, NIV_FULL_DEBUG);
  TestAlways    (TRUE,  tempstr, COMPONENT_MAIN, "LogAlways (should print)");
  TestMajor     (TRUE,  tempstr, COMPONENT_MAIN, "LogMajor (should print)");
  TestCrit      (TRUE,  tempstr, COMPONENT_MAIN, "LogCrit (should print)");
  TestEvent     (TRUE,  tempstr, COMPONENT_MAIN, "LogEvent (should print)");
  TestDebug     (TRUE,  tempstr, COMPONENT_MAIN, "LogDebug (should print)");
  TestFullDebug (TRUE,  tempstr, COMPONENT_MAIN, "LogFullDebug (should print)");
}

void run_Tests(int all, char *arg, char *str, char *file)
{
  SetNameFunction(arg);

  if (all)
    {
    Test1(str, file);
    }
}

void *run_MT_Tests(void *arg)
{
  run_Tests(FALSE, (char *)arg, "none", NULL);

  return NULL ;
}

static char usage[] = "usage:\n\ttest_liblog STD|MT\n";

#define NB_THREADS 20

int main(int argc, char *argv[])
{

  if(argc >= 2)
    {

      /* TEST 1 Standard */

      if(!strcmp(argv[1], "STD"))
        {
          char *str = "No extra string provided";
          char *file = NULL;

          if (argc >= 3)
            str = argv[2];

          if (argc >= 4)
            file = argv[3];

          SetNamePgm("test_liblog");
          SetNameHost("localhost");
          SetDefaultLogging("TEST");
          InitLogging();
          AddFamilyError(ERR_POSIX, "POSIX Errors", tab_systeme_status);
          LogTest("AddFamilyError = %d",
                  AddFamilyError(ERR_DUMMY, "Family Dummy", tab_test_err));
          LogTest("The family which was added is %s",
                  ReturnNameFamilyError(ERR_DUMMY));

          run_Tests(TRUE,  "monothread", str, file);
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
          InitLogging();
          AddFamilyError(ERR_POSIX, "POSIX Errors", tab_systeme_status);
          LogTest("AddFamilyError = %d",
                  AddFamilyError(ERR_DUMMY, "Family Dummy", tab_test_err));
          LogTest("The family which was added is %s",
                  ReturnNameFamilyError(ERR_DUMMY));

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
              snprintf(thread_name, 256, "thread %3d", i);
              rc = pthread_create(&(threads[i]), &th_attr[i], run_MT_Tests,
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
          fprintf(stderr, "%s", usage);
          exit(1);
        }

    }
  else
    {
      fprintf(stderr, "%s", usage);
      exit(1);
    }
  return 0;
}
