#include "config.h"
#include "handle_mapping_db.h"
#include "stuff_alloc.h"
#include <sys/time.h>

int main(int argc, char **argv)
{
  unsigned int i;
  struct timeval tv1, tv2, tv3, tvdiff;
  int count, rc;
  char *dir;
  handle_map_param_t param;
  time_t now;

  /* Init logging */
  SetNamePgm("test_handle_mapping");
  SetDefaultLogging("TEST");
  SetNameFunction("main");
  SetNameHost("localhost");
  InitLogging();

  if(argc != 3 || (count = atoi(argv[2])) == 0)
    {
      LogTest("usage: test_handle_mapping <db_dir> <db_count>");
      exit(1);
    }

#ifndef _NO_BUDDY_SYSTEM

  if((rc = BuddyInit(NULL)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogCrit(COMPONENT_FSAL, "ERROR: Could not initialize memory manager");
      exit(rc);
    }
#endif

  dir = argv[1];

  strcpy(param.databases_directory, dir);
  strcpy(param.temp_directory, "/tmp");
  param.database_count = count;
  param.hashtable_size = 27;
  param.nb_handles_prealloc = 1024;
  param.nb_db_op_prealloc = 1024;
  param.synchronous_insert = FALSE;

  rc = HandleMap_Init(&param);

  LogTest("HandleMap_Init() = %d", rc);
  if(rc)
    exit(rc);

  gettimeofday(&tv1, NULL);

  /* Now insert a set of handles */

  now = time(NULL);

  for(i = 0; i < 10000; i++)
    {
      nfs23_map_handle_t nfs23_digest;
      fsal_handle_t handle;

      memset(&handle, i, sizeof(fsal_handle_t));
      nfs23_digest.object_id = 12345 + i;
      nfs23_digest.handle_hash = (1999 * i + now) % 479001599;

      rc = HandleMap_SetFH(&nfs23_digest, &handle);
      if(rc && (rc != HANDLEMAP_EXISTS))
        exit(rc);
    }

  gettimeofday(&tv2, NULL);

  timersub(&tv2, &tv1, &tvdiff);

  LogTest("%u threads inserted 10000 handles in %d.%06ds",
          count, (int)tvdiff.tv_sec, (int)tvdiff.tv_usec);

  /* Now get them ! */

  for(i = 0; i < 10000; i++)
    {
      nfs23_map_handle_t nfs23_digest;
      fsal_handle_t handle;

      nfs23_digest.object_id = 12345 + i;
      nfs23_digest.handle_hash = (1999 * i + now) % 479001599;

      rc = HandleMap_GetFH(&nfs23_digest, &handle);
      if(rc)
        {
          LogTest("Error %d retrieving handle !", rc);
          exit(rc);
        }

      rc = HandleMap_DelFH(&nfs23_digest);
      if(rc)
        {
          LogTest("Error %d deleting handle !", rc);
          exit(rc);
        }

    }

  gettimeofday(&tv3, NULL);

  timersub(&tv3, &tv2, &tvdiff);

  LogTest("Retrieved and deleted 10000 handles in %d.%06ds", (int)tvdiff.tv_sec,
          (int)tvdiff.tv_usec);

  rc = HandleMap_Flush();

  gettimeofday(&tv3, NULL);

  timersub(&tv3, &tv1, &tvdiff);
  LogTest("Total time with %u threads (including flush): %d.%06ds", count,
          (int)tvdiff.tv_sec, (int)tvdiff.tv_usec);

  exit(0);

}
