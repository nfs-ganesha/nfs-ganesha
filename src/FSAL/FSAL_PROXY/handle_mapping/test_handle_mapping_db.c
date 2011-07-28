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
  time_t now;

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

  /* Init logging */
  SetNamePgm("test_handle_mapping");
  SetNameFileLog("/dev/tty");
  SetNameFunction("main");
  SetNameHost("localhost");

  /* count databases */

  rc = handlemap_db_count(dir);

  LogTest("handlemap_db_count(%s)=%d", dir, rc);

  if(rc != 0 && count != rc)
    {
      LogTest("Warning: incompatible thread count %d <> database count %d", count, rc);
    }

  rc = handlemap_db_init(dir, "/tmp", count, 1024, FALSE);

  LogTest("handlemap_db_init() = %d", rc);
  if(rc)
    exit(rc);

  rc = handlemap_db_reaload_all(NULL);

  LogTest("handlemap_db_reaload_all() = %d", rc);
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

      rc = handlemap_db_insert(&nfs23_digest, &handle);
      if(rc)
        exit(rc);
    }

  gettimeofday(&tv2, NULL);

  timersub(&tv2, &tv1, &tvdiff);

  LogTest("%u threads inserted 10000 handles in %d.%06ds", count, (int)tvdiff.tv_sec,
          (int)tvdiff.tv_usec);

  rc = handlemap_db_flush();

  gettimeofday(&tv3, NULL);

  timersub(&tv3, &tv1, &tvdiff);
  LogTest("Total time with %u threads (including flush): %d.%06ds", count,
          (int)tvdiff.tv_sec, (int)tvdiff.tv_usec);

  LogTest("Now, delete operations");

  for(i = 0; i < 10000; i++)
    {
      nfs23_map_handle_t nfs23_digest;

      nfs23_digest.object_id = 12345 + i;
      nfs23_digest.handle_hash = (1999 * i + now) % 479001599;

      rc = handlemap_db_delete(&nfs23_digest);
      if(rc)
        exit(rc);
    }

  gettimeofday(&tv2, NULL);
  timersub(&tv2, &tv3, &tvdiff);

  LogTest("%u threads deleted 10000 handles in %d.%06ds", count, (int)tvdiff.tv_sec,
          (int)tvdiff.tv_usec);

  rc = handlemap_db_flush();

  gettimeofday(&tv1, NULL);
  timersub(&tv1, &tv3, &tvdiff);
  LogTest("Delete time with %u threads (including flush): %d.%06ds", count,
          (int)tvdiff.tv_sec, (int)tvdiff.tv_usec);

  exit(0);

}
