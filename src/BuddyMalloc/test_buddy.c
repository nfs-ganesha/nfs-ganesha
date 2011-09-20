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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "BuddyMalloc.h"
#include <pthread.h>
#include "log_macros.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <malloc.h>

#define MEM_SIZE  1000000LL
#define NB_THREADS 30
#define NB_STR    100

buddy_parameter_t parameter = {

  MEM_SIZE,
  FALSE,
  FALSE,
  FALSE,
  -1,
  -1
};

buddy_parameter_t parameter_realloc = {

  MEM_SIZE,
  TRUE,
  TRUE,
  TRUE,
  3,
  5
};

#define MEM_SIZE_SMALL 10000LL

buddy_parameter_t parameter_realloc_small = {

  MEM_SIZE_SMALL,
  TRUE,
  TRUE,
  TRUE,
  2,
  5
};

typedef struct string_info
{

  char *str;
  int len;

} string_info;

static int my_rand()
{
  /* rand is in the range [0:2^15[ */
  return (rand() << 16) + rand();
}

static void print_mallinfo()
{
  struct mallinfo system_mem_info;

  system_mem_info = mallinfo();

  LogTest("---- Mallinfo ----");
  LogTest("Total %d", system_mem_info.arena);
  LogTest("NbOrdBlocks %d", system_mem_info.ordblks);
  LogTest("NbSmallBlocks %d", system_mem_info.smblks);

  LogTest("UsedOrdBlocks %d", system_mem_info.uordblks);
  LogTest("UsedSmallBlocks %d", system_mem_info.usmblks);

}

static struct timeval time_diff(struct timeval time_from, struct timeval time_to)
{

  struct timeval result;

  if(time_to.tv_usec < time_from.tv_usec)
    {
      result.tv_sec = time_to.tv_sec - time_from.tv_sec - 1;
      result.tv_usec = 1000000 + time_to.tv_usec - time_from.tv_usec;
    }
  else
    {
      result.tv_sec = time_to.tv_sec - time_from.tv_sec;
      result.tv_usec = time_to.tv_usec - time_from.tv_usec;
    }

  return result;

}

/* Allocate and write into memory. */
void *TEST1(void *arg)
{

  int th = (long)arg;
  int i, rc;
  string_info strings[NB_STR];

  LogTest("%d:BuddyInit(%llu)=%d",
          th, MEM_SIZE, rc = BuddyInit(&parameter));

  if(rc)
    exit(1);

  for(i = 0; i < NB_STR; i++)
    {
      unsigned int j;
      size_t len;

      len = (unsigned long)my_rand() % 100;
      if(len == 0)
        len = 1;

      strings[i].str = BuddyMalloc(len);

      if(!strings[i].str)
        {
          LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %lld : %d *****",
                  th, (long long int)len, BuddyErrno);
          strings[i].len = 0;
          continue;
        }

      strings[i].len = len;

      for(j = 0; j < len - 1; j++)
        {
          strings[i].str[j] = '0' + j;
        }
      strings[i].str[j] = '\0';

      usleep(1000);             /* for mixing threads actions */

    }

  /* now, check for integrity */

  for(i = 0; i < NB_STR; i++)
    {
      if(isFullDebug(COMPONENT_MEMALLOC))
        LogTest("%ld>%ld:%d:%s",
                (long int) th, (long int) strings[i].len, (int) strlen(strings[i].str), strings[i].str);
      if(strings[i].len - 1 != (int)strlen(strings[i].str))
        LogTest("************ INTEGRITY ERROR !!! ************");

      usleep(1000);             /* for mixing threads actions */

      /* Freeing block */
      BuddyFree(strings[i].str);

    }

  LogTest("BUDDY_ERRNO=%d", BuddyErrno);

  /* Final config */
  BuddyDumpMem(stdout);

#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  /* destroy thread resources */
  if((rc = BuddyDestroy()))
    {
      LogTest("ERROR in BuddyDestroy: %d", rc);
    }

  return NULL;

}

#define NB_LOOP2 10000

/* Malloc/Free loop */
void *TEST2(void *arg)
{

  int th = (long)arg;

  int i, rc;
  struct timeval tv1, tv2, tv3;
  size_t total;

  LogTest("%d:BuddyInit(%llu)=%d",
          th, MEM_SIZE, rc = BuddyInit(&parameter));

  if(rc)
    exit(1);

  total = MEM_SIZE;

  gettimeofday(&tv1, NULL);

  for(i = 0; i < NB_LOOP2; i++)
    {

      size_t len;
      caddr_t ptr;

      len = my_rand() % total;
      if(len == 0)
        len = 1;

      ptr = BuddyMalloc(len);

      if(!ptr)
        {
          LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %llu : %d *****",
                  th, (unsigned long long)len, BuddyErrno);

          /* Final config */
          BuddyDumpMem(stdout);

#ifdef _DEBUG_MEMLEAKS
          DisplayMemoryMap(stdout);
#endif

          exit(1);
        }

      BuddyFree(ptr);
    }

  gettimeofday(&tv2, NULL);

  tv3 = time_diff(tv1, tv2);

  LogTest("%d: %d Malloc/Free in %lu.%.6lu s",
          th, NB_LOOP2, tv3.tv_sec, tv3.tv_usec);

  LogTest("BUDDY_ERRNO=%d", BuddyErrno);

  /* Final config */
  BuddyDumpMem(stdout);

#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  /* destroy thread resources */
  if((rc = BuddyDestroy()))
    {
      LogTest("ERROR in BuddyDestroy: %d", rc);
    }

  return NULL;

}

#define NB_LOOP3 100

/* Test alignement */
void *TEST3(void *arg)
{

  int th = (long)arg;

  int i, rc;
  size_t total = MEM_SIZE / 10;

  LogTest("%d:BuddyInit(%llu)=%d", th, MEM_SIZE, rc = BuddyInit(&parameter));

  if(rc)
    exit(1);

  for(i = 0; i < NB_LOOP3; i++)
    {

      size_t len;
      caddr_t ptr;

      len = my_rand() % total;
      if(len == 0)
        len = 1;

      ptr = BuddyMalloc(len);

      if(!ptr)
        {
          LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %llu :%d *****",
                  th, (unsigned long long)len, BuddyErrno);
          break;
        }

      usleep(1000);             /* for mixing threads actions */

      /* print hexa part of adress */
      if(isFullDebug(COMPONENT_MEMALLOC))
        LogTest("%p", ptr);

      /* 32 bits alignment test */
      if((unsigned long)ptr & 3)
        LogTest("%d:32 bits alignment ERROR", th);
      /* 64 bits alignment test */
      if((unsigned long)ptr & 7)
        LogTest("%d:64 bits alignment ERROR", th);

    }

  /* Final config */
  BuddyDumpMem(stdout);

#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  /* destroy thread resources */
  if((rc = BuddyDestroy()))
    {
      LogTest("ERROR in BuddyDestroy: %d", rc);
    }

  return NULL;

}

#define NB_SIMULTANEOUS 4

/* Test realloc */
void *TEST4(void *arg)
{

  int th = (long)arg;

  int rc;
  unsigned int i, j;
  size_t total = 4 * MEM_SIZE_SMALL;
  size_t current = 2;
  size_t old;
  char *pointer[NB_SIMULTANEOUS];
  char *new_pointer[NB_SIMULTANEOUS];

  LogTest("%d:BuddyInit(%llu)=%d", th, MEM_SIZE, rc =
          BuddyInit(&parameter_realloc_small));

  if(rc)
    exit(1);

  /* initial allocation */
  for(j = 0; j < NB_SIMULTANEOUS; j++)
    {

      pointer[j] = BuddyMalloc(current);

      if(!pointer[j])
        {
          LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %llu :%d *****",
                  th, (unsigned long long)current, BuddyErrno);
          BuddyDumpMem(stdout);
#ifdef _DEBUG_MEMLEAKS
          DisplayMemoryMap(stdout);
#endif
          return NULL;
        }

      /* initial affectation */
      for(i = 0; i < current; i++)
        {
          pointer[j][i] = (char)(i % 256);
        }

    }

  /* Final config */
  BuddyDumpMem(stdout);
#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  old = current;
  current <<= 1;

  while(current < total)
    {

      /* realloc the pointer */
      for(j = 0; j < NB_SIMULTANEOUS; j++)
        {

          new_pointer[j] = BuddyRealloc(pointer[j], current);

          if(!new_pointer[j])
            {
              LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %llu :%d *****",
                      th, (unsigned long long)current, BuddyErrno);
              BuddyDumpMem(stdout);
#ifdef _DEBUG_MEMLEAKS
              DisplayMemoryMap(stdout);
#endif

              return NULL;
            }

          /* verifies the content of the new area */
          for(i = 0; i < old; i++)
            {
              if(new_pointer[j][i] != (char)(i % 256))
                {
                  LogTest("%d:**** INTEGRITY ERROR : ptr[%d] != %d *****",
                          th, i, i % 256);
                }
            }

          /* sets the content of the new area */
          for(i = old; i < current; i++)
            {
              new_pointer[j][i] = (char)(i % 256);
            }

          pointer[j] = new_pointer[j];
        }

      old = current;
      current <<= 1;

      usleep(1000);             /* for mixing threads actions */

    }

  LogTest("BUDDY_ERRNO=%d", BuddyErrno);

  /* Final config */
  BuddyDumpMem(stdout);

#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  /* destroy thread resources */
  if((rc = BuddyDestroy()))
    {
      LogTest("ERROR in BuddyDestroy: %d", rc);
    }

  return NULL;

}

/* Allocate using calloc and write into memory. */
void *TEST5(void *arg)
{

  int th = (long)arg;
  int i, rc;
  string_info strings[NB_STR];

  LogTest("%d:BuddyInit(%llu)=%d",
          th, MEM_SIZE, rc = BuddyInit(&parameter));

  if(rc)
    exit(1);

  for(i = 0; i < NB_STR; i++)
    {
      int j;
      int len;

      len = (unsigned long)my_rand() % 100;
      if(len == 0)
        len = 1;

      strings[i].str = BuddyCalloc(len, sizeof(char));

      if(!strings[i].str)
        {
          LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %d : %d *****",
                  th, len, BuddyErrno);
          strings[i].len = 0;
          continue;
        }

      strings[i].len = len;

      for(j = 0; j < len - 1; j++)
        {
          /* verifies that it was NULL */
          if(strings[i].str[j])
            {
              LogTest("%d:**** MEMSET ERROR : string[%d].str[%d] != 0 *****",
                      th, i, j);
            }
          strings[i].str[j] = '0' + j;
        }

      if(strings[i].str[j])
        LogTest("%d:**** MEMSET ERROR : string[%d].str[%d] != 0 *****",
                th, i, j);
      strings[i].str[j] = '\0';

      usleep(1000);             /* for mixing threads actions */

    }

  LogTest("BUDDY_ERRNO=%d", BuddyErrno);

  /* Now check for integrity */

  for(i = 0; i < NB_STR; i++)
    {
      if(isFullDebug(COMPONENT_MEMALLOC))
        LogTest("%d>%d:%d:%s",
                th, strings[i].len, (int) strlen(strings[i].str), strings[i].str);
      if(strings[i].len - 1 != (int)strlen(strings[i].str))
        LogTest("************ INTEGRITY ERROR !!! ************");

      usleep(1000);             /* for mixing threads actions */

      /* Freeing block */
      BuddyFree(strings[i].str);

    }

  LogTest("BUDDY_ERRNO=%d", BuddyErrno);

  /* starts the test again, now that the memory is "dirty" */

  for(i = 0; i < NB_STR; i++)
    {
      int j;
      int len;

      len = (unsigned long)my_rand() % 100;
      if(len == 0)
        len = 1;

      strings[i].str = BuddyCalloc(len, sizeof(char));

      if(!strings[i].str)
        {
          LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %ld : %d *****",
                  th, (long int) len, BuddyErrno);
          strings[i].len = 0;
          continue;
        }

      strings[i].len = len;

      for(j = 0; j < len - 1; j++)
        {
          /* verifies that it was NULL */
          if(strings[i].str[j])
            {
              LogTest("%d:**** MEMSET ERROR : string[%d].str[%d] != 0 *****",
                      th, i, j);
            }
          strings[i].str[j] = '0' + j;
        }

      if(strings[i].str[j])
        LogTest("%d:**** MEMSET ERROR : string[%d].str[%d] != 0 *****",
                th, i, j);
      strings[i].str[j] = '\0';

      usleep(1000);             /* for mixing threads actions */

    }

  LogTest("BUDDY_ERRNO=%d", BuddyErrno);

  /* Now check for integrity */

  for(i = 0; i < NB_STR; i++)
    {
      if(isFullDebug(COMPONENT_MEMALLOC))
        LogTest("%ld>%ld:%d:%s",
                (long int) th, (long int) strings[i].len, (int) strlen(strings[i].str), strings[i].str);
      if(strings[i].len - 1 != (int)strlen(strings[i].str))
        LogTest("************ INTEGRITY ERROR !!! ************");

      usleep(1000);             /* for mixing threads actions */

      /* Freeing block */
      BuddyFree(strings[i].str);

    }

  LogTest("BUDDY_ERRNO=%d", BuddyErrno);

  /* Final config */
  BuddyDumpMem(stdout);

#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  /* destroy thread resources */
  if((rc = BuddyDestroy()))
    {
      LogTest("ERROR in BuddyDestroy: %d", rc);
    }

  return NULL;

}                               /* TEST5 */

#define NB_PAGES 3

/* TEST6:
 * Tests "on_demand_alloc".
 * This test randomly allocates NB_PAGES * MEM_SIZE.
 * And prints its stats.
 */
void *TEST6(void *arg)
{

  int th = (long)arg;

  int rc;

  size_t min_alloc = MEM_SIZE / 10;
  size_t max_alloc = 3 * MEM_SIZE / 4;

  size_t total = 0;

  LogTest("%d:BuddyInit(%llu)=%d",
          th, MEM_SIZE, rc = BuddyInit(&parameter_realloc));

  if(rc)
    exit(1);

  /* Final config */
  if(isFullDebug(COMPONENT_MEMALLOC))
    BuddyDumpMem(stdout);
#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  while(total < NB_PAGES * MEM_SIZE)
    {

      size_t len;
      caddr_t ptr;

      len = (unsigned long)my_rand() % (max_alloc - min_alloc) + min_alloc;

      ptr = BuddyMalloc(len);

      if(!ptr)
        {
          LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %llu :%d *****",
                  th, (unsigned long long)len, BuddyErrno);
          exit(1);
        }

      total += len;

      /* Final config */
      if(isFullDebug(COMPONENT_MEMALLOC))
        BuddyDumpMem(stdout);
#ifdef _DEBUG_MEMLEAKS
      DisplayMemoryMap(stdout);
#endif

      usleep(1000);             /* for mixing threads actions */

    }

  /* destroy thread resources */
  if((rc = BuddyDestroy()))
    {
      LogTest("ERROR in BuddyDestroy: %d", rc);
    }

  return NULL;

}

#define NB_LOOP7  100
#define NB_ITEM7  10

/* TEST7:
 * Tests "on_demand_alloc" and extra_alloc.
 * This randomly alloc/free some memory
 * and print its sate.
 */
void *TEST7(void *arg)
{

  int th = (long)arg;

  int i, index, rc;

  size_t min_alloc = MEM_SIZE / 5;
  size_t max_alloc = 2 * MEM_SIZE;      /* 3*MEM_SIZE/4; */

  caddr_t table_alloc[NB_ITEM7];

  for(i = 0; i < NB_ITEM7; i++)
    table_alloc[i] = NULL;

  print_mallinfo();

  LogTest("%d:BuddyInit(%llu)=%d",
          th, MEM_SIZE, rc = BuddyInit(&parameter_realloc));

  if(rc)
    exit(1);

  /* Final config */
  if(isFullDebug(COMPONENT_MEMALLOC))
    BuddyDumpMem(stdout);
#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  for(i = 0; i < NB_LOOP7; i++)
    {

      size_t len;
      caddr_t ptr;

      /* randomly choose a slot */

      index = (unsigned long)my_rand() % NB_ITEM7;

      if(table_alloc[index] == NULL)
        {
          /* the slot is empty, alloc some memory */

          len = (unsigned long)my_rand() % (max_alloc - min_alloc) + min_alloc;

          if(isFullDebug(COMPONENT_MEMALLOC))
            LogTest("---------- BuddyMalloc( %lu ) ---------", (long unsigned int) len);

          ptr = BuddyMalloc(len);

          if(!ptr)
            {
              LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %llu :%d *****",
                      th, (unsigned long long)len, BuddyErrno);
              exit(1);
            }

          table_alloc[index] = ptr;

        }
      else
        {
          if(isFullDebug(COMPONENT_MEMALLOC))
            LogTest("---------- BuddyFree( %p ) ---------",
                    table_alloc[index]);

          /* The slot is not empty, we free it. */

          BuddyFree(table_alloc[index]);

          table_alloc[index] = NULL;

        }

      /* Final config */
      if(isFullDebug(COMPONENT_MEMALLOC))
        BuddyDumpMem(stdout);
#ifdef _DEBUG_MEMLEAKS
      DisplayMemoryMap(stdout);
#endif

      usleep(1000);             /* for mixing threads actions */

    }

  /* print final config */
  LogTest("---------- Thread %d ---------", th);
  BuddyDumpMem(stdout);
#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  print_mallinfo();

  /* Free everything */
  for(index = 0; index < NB_ITEM7; index++)
    {
      if(table_alloc[index])
        BuddyFree(table_alloc[index]);
    }

  /* print final config */
  LogTest("---------- Thread %d ---------", th);
  BuddyDumpMem(stdout);

#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  print_mallinfo();

  /* destroy thread resources */
  if((rc = BuddyDestroy()))
    {
      LogTest("ERROR in BuddyDestroy: %d", rc);
    }

  return NULL;

}

#define NB_LOOP8  5000
#define NB_ITEM8  30

/* TEST8:
 * Tests "on_demand_alloc" and garbage collecting.
 * This randomly alloc/free some memory blocks
 * and output a graph that indicates used pages statistics.
 */
void *TEST8(void *arg)
{

  int th = (long)arg;

  int i, index, rc;

  unsigned int last_used = 0;
  unsigned int last_pages = 0;

  buddy_stats_t stats;

  size_t min_alloc = MEM_SIZE_SMALL / 5;
  size_t max_alloc = 3 * MEM_SIZE_SMALL / 4;

  caddr_t table_alloc[NB_ITEM8];

  for(i = 0; i < NB_ITEM8; i++)
    table_alloc[i] = NULL;

  LogTest("%d:BuddyInit(%llu)=%d", th, MEM_SIZE_SMALL, rc =
         BuddyInit(&parameter_realloc_small));

  if(rc)
    exit(1);

  /* Final config */
  if(isFullDebug(COMPONENT_MEMALLOC))
    BuddyDumpMem(stdout);
#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif

  LogTest("ThreadId;TotalSize;UsedSize;NbPages;UsedPages");

  for(i = 0; i < NB_LOOP8; i++)
    {

      size_t len;
      caddr_t ptr;

      /* randomly choose a slot */

      index = (unsigned long)my_rand() % NB_ITEM8;

      if(table_alloc[index] == NULL)
        {
          /* the slot is empty, alloc some memory */

          len = (unsigned long)my_rand() % (max_alloc - min_alloc) + min_alloc;

          if(isFullDebug(COMPONENT_MEMALLOC))
            LogTest("---------- BuddyMalloc( %lu ) ---------", (long unsigned int) len);

          ptr = BuddyMalloc(len);

          if(!ptr)
            {
              LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %llu :%d *****",
                      th, (unsigned long long)len, BuddyErrno);
              exit(1);
            }

          table_alloc[index] = ptr;

        }
      else
        {
          if(isFullDebug(COMPONENT_MEMALLOC))
            LogTest("---------- BuddyFree( %p ) ---------",
                    table_alloc[index]);

          /* The slot is not empty, we free it. */

          BuddyFree(table_alloc[index]);

          table_alloc[index] = NULL;

        }

      /* Final config */
      if(isFullDebug(COMPONENT_MEMALLOC))
        BuddyDumpMem(stdout);
#ifdef _DEBUG_MEMLEAKS
      DisplayMemoryMap(stdout);
#endif

      /* Prints csv statistics */
      BuddyGetStats(&stats);

      if(isFullDebug(COMPONENT_MEMALLOC))
        LogTest("%d;%lu;%lu;%u;%u;",
                th, (long unsigned int) stats.StdMemSpace,  (long unsigned int) stats.StdUsedSpace,
                stats.NbStdPages, stats.NbStdUsed);
      else
        if((stats.NbStdPages != last_pages) || (stats.NbStdUsed != last_used))
          {
            LogTest("%d;%u;%u;", th, stats.NbStdPages, stats.NbStdUsed);
            last_pages = stats.NbStdPages;
            last_used = stats.NbStdUsed;
          }

      usleep(1000);             /* for mixing threads actions */

    }

  /* destroy thread resources */
  if((rc = BuddyDestroy()))
    {
      LogTest("ERROR in BuddyDestroy: %d", rc);
    }

  return NULL;

}

/* TEST9:
 * test block labels.
 */
void *TEST9(void *arg)
{

#ifdef _DEBUG_MEMLEAKS

  int th = (long)arg;
  int i, rc;

  char labels[NB_STR][64];
  string_info strings[NB_STR];

  LogTest("%d:BuddyInit(%llu)=%d", th, MEM_SIZE, rc = BuddyInit(&parameter));

  if(rc)
    exit(1);

  for(i = 0; i < NB_STR; i++)
    {
      int len;

      len = (unsigned long)my_rand() % 100;
      if(len == 0)
        len = 1;

      snprintf(labels[i], 64, "%d-%d-%d", th, i, len);

      strings[i].str = BuddyMalloc_Autolabel(len, __FILE__, __FUNCTION__, __LINE__, labels[i]);

      if(!strings[i].str)
        {
          LogTest("%d:**** NOT ENOUGH MEMORY TO ALLOCATE %d : %d *****",
                  th, len, BuddyErrno);
          strings[i].len = 0;
          continue;
        }

      strings[i].len = len;

      usleep(1000);             /* for mixing threads actions */

    }
  LogTest("========== END OF ALLOCATION =============");

  BuddyDumpMem(stdout);

  LogTest("_DEBUG_MEMLEAKS enabled");
  BuddyLabelsSummary(COMPONENT_MEMLEAKS);

  LogTest("Number of blocks with the label %s: %d", labels[0],
         BuddyCountDebugLabel(labels[0]));

  /* get labels for each string */
  for(i = 0; i < NB_STR; i++)
    {
      LogTest("%d: Label[%d]= %s = %s", th, i, labels[i],
             BuddyGetDebugLabel(strings[i].str));

      /* Freeing block */
      BuddyFree(strings[i].str);
    }

  /* destroy thread resources */
  if((rc = BuddyDestroy()))
    {
      LogTest("ERROR in BuddyDestroy: %d", rc);
    }
#endif

  return NULL;

}

#define NB_ITEMA  10
#define NB_LOOPA  100

pthread_mutex_t testA_mutex = PTHREAD_MUTEX_INITIALIZER;
caddr_t tab_alloc_testA[NB_ITEMA];

#define P( __mutex__ ) pthread_mutex_lock( &__mutex__ )
#define V( __mutex__ ) pthread_mutex_unlock( &__mutex__ )

void *TESTA(void *arg)
{

  int th = (long)arg;

  unsigned int slot;
  unsigned long duration;
  size_t len;
  size_t total = MEM_SIZE / 10;
  int nloop, rc;

  LogTest("%d:BuddyInit(%llu)=%d",
          th, MEM_SIZE, rc = BuddyInit(&parameter_realloc));

  for(nloop = 0; nloop < NB_LOOPA; nloop++)
    {

      /* find a free place and allocates some memory */
      slot = (unsigned int)my_rand() % NB_ITEMA;
      do
        {

          P(testA_mutex);

          /* exits loop if the slot is empty */
          if(tab_alloc_testA[slot] == NULL)
            break;

          /* else, sleep for a while */
          V(testA_mutex);
          usleep(1000);
          slot = (slot + 1) % NB_ITEMA;

        }
      while(1);

      len = my_rand() % total;
      if(len == 0)
        len = 1;

      tab_alloc_testA[slot] = BuddyMalloc(len);

      LogTest("Thread %d allocated slot %u = %p",
              th, slot, tab_alloc_testA[slot]);

      /* unlock the table */
      V(testA_mutex);

      /* sleep for a while */
      duration = (unsigned long)my_rand() % 1000;
      usleep(duration);

      /* then, find an allocated block and free it */
      slot = (unsigned int)my_rand() % NB_ITEMA;
      do
        {

          P(testA_mutex);

          /* exits loop if the slot is empty */
          if(tab_alloc_testA[slot] != NULL)
            break;

          /* else, sleep for a while */
          V(testA_mutex);
          usleep(1000);

          slot = (slot + 1) % NB_ITEMA;

        }
      while(1);

      LogTest("Thread %d frees slot %u = %p",
              th, slot, tab_alloc_testA[slot]);

      BuddyFree(tab_alloc_testA[slot]);

      tab_alloc_testA[slot] = NULL;

      V(testA_mutex);

    }

  BuddyDumpMem(stdout);

#ifdef _DEBUG_MEMLEAKS
  DisplayMemoryMap(stdout);
#endif
  /* destroy thread resources */
  if ((rc = BuddyDestroy()))
        LogTest("ERROR in BuddyDestroy: %d", rc );
  else
        LogTest("All resources released successfully");

  return NULL;

}

/* TESTB:
 * integrity tests.
 */
void *TESTB(void *arg)
{

  int th = (long)arg;
  int rc;

  caddr_t pointer, p1, p2;

  LogTest("%d:BuddyInit(%llu)=%d",
          th, MEM_SIZE, rc = BuddyInit(&parameter));

  /* tests on a good adress */

  pointer = BuddyMalloc(1024);

  if(isFullDebug(COMPONENT_MEMCORRUPT))
    {
      LogTest("--> Checking an good address %p", pointer);
      BuddyCheck(pointer, 0);
    }

  LogTest("--> Trying to free a good address %p", pointer);

  BuddyFree(pointer);

  /* tests on a stack adress */

  pointer = (caddr_t) & arg;

  if(isFullDebug(COMPONENT_MEMCORRUPT))
    {
      LogTest("--> Checking an invalid address %p", pointer);
      BuddyCheck(pointer, 0);
    }

  LogTest("--> Trying to free an invalid address %p", pointer);

  BuddyFree(pointer);

  /* tests on a malloc adress */

  p1 = malloc(1024);
  p2 = malloc(1024);
  if (p1 > p2)
    pointer = p1;
  else
    pointer = p2;

  if(isFullDebug(COMPONENT_MEMCORRUPT))
    {
      LogTest("--> Checking a libc malloc address %p", pointer);
      BuddyCheck(pointer, 0);
    }

  LogTest("--> Trying to free a libc malloc address %p", pointer);

  BuddyFree(pointer);

  free(p1);
  free(p2);

  /* tests on a null adress */

  pointer = NULL;

  if(isFullDebug(COMPONENT_MEMCORRUPT))
    {
      LogTest("--> Checking a NULL address %p", pointer);
      BuddyCheck(pointer, 0);
    }

  LogTest("--> Trying to free a NULL address %p", pointer);

  BuddyFree(pointer);

  /* destroy thread resources */
  if((rc = BuddyDestroy()))
    {
      LogTest("ERROR in BuddyDestroy: %d", rc);
    }

  return NULL;

}

static char usage[] =
    "Usage :\n"
    "\ttest_buddy <test_name>\n\n"
    "\twhere <test_name> is:\n"
    "\t\t1[mt] : init/malloc/free/integrity tests (mt: multithreaded test)\n"
    "\t\t2[mt] : performance test for malloc/free (mt: multithreaded test)\n"
    "\t\t3[mt] : alignement test (mt: multithreaded test)\n"
    "\t\t4[mt] : realloc test (mt: multithreaded test)\n"
    "\t\t5[mt] : calloc test (mt: multithreaded test)\n"
    "\t\t6[mt] : dynamic page allocation test (mt: multithreaded test)\n"
    "\t\t7[mt] : dynamic page alloc/free test (mt: multithreaded test)\n"
    "\t\t8[mt] : garbage collection stats (mt: multithreaded test)\n"
    "\t\t9[mt] : debug labels (mt: multithreaded test)\n"
    "\t\tA     : multithreaded alloc/free on shared memory segments\n"
    "\t\tB[mt] : memory corruption tests\n";

/* Multithread launch macro */
#define LAUNCH_THREADS( _function_ , _nb_threads_ ) do {\
\
              long i;\
              pthread_t threads[_nb_threads_];\
              for (i=0;i<_nb_threads_;i++){\
                pthread_create(&(threads[i]),&th_attr[i],_function_,(void*)i);\
              }\
              \
              for (i=0;i<_nb_threads_;i++){\
                pthread_join(threads[i],NULL);\
              }\
            } while(0)

int main(int argc, char **argv)
{

  pthread_attr_t th_attr[NB_THREADS];
  int th_index;

  SetDefaultLogging("TEST");
  SetNamePgm("test_buddy");
  InitLogging();

  for(th_index = 0; th_index < NB_THREADS; th_index++)
    {
      pthread_attr_init(&th_attr[th_index]);
/*    pthread_attr_setscope(&th_attr[th_index],PTHREAD_SCOPE_SYSTEM);*/
      pthread_attr_setdetachstate(&th_attr[th_index], PTHREAD_CREATE_JOINABLE);
    }

  if(argc <= 1)
    {
      LogTest("%s", usage);
      exit(1);
    }

  srand(time(NULL) + getpid());

  if(!strcmp(argv[1], "1"))
    TEST1(0);

  else if(!strcmp(argv[1], "2"))
    TEST2(0);

  else if(!strcmp(argv[1], "3"))
    TEST3(0);

  else if(!strcmp(argv[1], "4"))
    TEST4(0);

  else if(!strcmp(argv[1], "5"))
    TEST5(0);

  else if(!strcmp(argv[1], "6"))
    TEST6(0);

  else if(!strcmp(argv[1], "7"))
    TEST7(0);

  else if(!strcmp(argv[1], "8"))
    TEST8(0);

  else if(!strcmp(argv[1], "9"))
    TEST9(0);

  else if(!strcmp(argv[1], "B"))
    TESTB(0);

  else if(!strcmp(argv[1], "1mt"))
    LAUNCH_THREADS(TEST1, NB_THREADS);

  else if(!strcmp(argv[1], "2mt"))
    LAUNCH_THREADS(TEST2, NB_THREADS);

  else if(!strcmp(argv[1], "3mt"))
    LAUNCH_THREADS(TEST3, NB_THREADS);

  else if(!strcmp(argv[1], "4mt"))
    LAUNCH_THREADS(TEST4, NB_THREADS);

  else if(!strcmp(argv[1], "5mt"))
    LAUNCH_THREADS(TEST5, NB_THREADS);

  else if(!strcmp(argv[1], "6mt"))
    LAUNCH_THREADS(TEST6, NB_THREADS);

  else if(!strcmp(argv[1], "7mt"))
    LAUNCH_THREADS(TEST7, NB_THREADS);

  else if(!strcmp(argv[1], "8mt"))
    LAUNCH_THREADS(TEST8, NB_THREADS);

  else if(!strcmp(argv[1], "9mt"))
    LAUNCH_THREADS(TEST9, NB_THREADS);

  else if(!strcmp(argv[1], "A"))
    {
      int i;
      /* initialization of the test */
      for(i = 0; i < NB_ITEMA; i++)
        tab_alloc_testA[i] = NULL;

      LAUNCH_THREADS(TESTA, NB_THREADS);
    }

  else if(!strcmp(argv[1], "Bmt"))
    LAUNCH_THREADS(TESTB, NB_THREADS);

  else
    {
      LogTest("***** Unknown test: \"%s\" ******", argv[1]);
      LogTest("%s", usage);
      exit(1);
    }

  exit(0);

}
