#include "config.h"
#include "handle_mapping_db.h"
#include "stuff_alloc.h"
#include <sys/time.h>

log_t fsal_log = LOG_INITIALIZER;
desc_log_stream_t log_stream;

int main(int argc, char **argv)
{
  unsigned int i;
  struct timeval tv1, tv2, tv3, tvdiff;
  int count, rc;
  char *dir;
  time_t now;

  if (argc != 3 || (count = atoi(argv[2])) == 0)
    {
      printf("usage: test_handle_mapping <db_dir> <db_count>\n");
      exit(1);
    }
#ifndef _NO_BUDDY_SYSTEM

  if ((rc = BuddyInit(NULL)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      DisplayLogJdLevel(fsal_log, NIV_CRIT, "ERROR: Could not initialize memory manager");
      exit(rc);
    }
#endif

  dir = argv[1];

  log_stream.flux = stdout;
  AddLogStreamJd(&fsal_log, V_STREAM, log_stream, NIV_DEBUG, SUP);

  /* Init logging */
  SetNamePgm("test_handle_mapping");
  SetNameFileLog("/dev/tty");
  SetNameFunction("main");
  SetNameHost("localhost");

  InitDebug(NIV_FULL_DEBUG);

  /* count databases */

  rc = handlemap_db_count(dir);

  printf("handlemap_db_count(%s)=%d\n", dir, rc);

  if (rc != 0 && count != rc)
    {
      printf("Warning: incompatible thread count %d <> database count %d\n", count, rc);
    }

  rc = handlemap_db_init(dir, "/tmp", count, 1024, FALSE);

  printf("handlemap_db_init() = %d\n", rc);
  if (rc)
    exit(rc);

  rc = handlemap_db_reaload_all(NULL);

  printf("handlemap_db_reaload_all() = %d\n", rc);
  if (rc)
    exit(rc);

  gettimeofday(&tv1, NULL);

  /* Now insert a set of handles */

  now = time(NULL);

  for (i = 0; i < 10000; i++)
    {
      nfs23_map_handle_t nfs23_digest;
      fsal_handle_t handle;

      memset(&handle, i, sizeof(fsal_handle_t));
      nfs23_digest.object_id = 12345 + i;
      nfs23_digest.handle_hash = (1999 * i + now) % 479001599;

      rc = handlemap_db_insert(&nfs23_digest, &handle);
      if (rc)
        exit(rc);
    }

  gettimeofday(&tv2, NULL);

  timersub(&tv2, &tv1, &tvdiff);

  printf("%u threads inserted 10000 handles in %d.%06ds\n", count, (int)tvdiff.tv_sec,
         (int)tvdiff.tv_usec);

  rc = handlemap_db_flush();

  gettimeofday(&tv3, NULL);

  timersub(&tv3, &tv1, &tvdiff);
  printf("Total time with %u threads (including flush): %d.%06ds\n", count,
         (int)tvdiff.tv_sec, (int)tvdiff.tv_usec);

  printf("Now, delete operations\n");

  for (i = 0; i < 10000; i++)
    {
      nfs23_map_handle_t nfs23_digest;

      nfs23_digest.object_id = 12345 + i;
      nfs23_digest.handle_hash = (1999 * i + now) % 479001599;

      rc = handlemap_db_delete(&nfs23_digest);
      if (rc)
        exit(rc);
    }

  gettimeofday(&tv2, NULL);
  timersub(&tv2, &tv3, &tvdiff);

  printf("%u threads deleted 10000 handles in %d.%06ds\n", count, (int)tvdiff.tv_sec,
         (int)tvdiff.tv_usec);

  rc = handlemap_db_flush();

  gettimeofday(&tv1, NULL);
  timersub(&tv1, &tv3, &tvdiff);
  printf("Delete time with %u threads (including flush): %d.%06ds\n", count,
         (int)tvdiff.tv_sec, (int)tvdiff.tv_usec);

  exit(0);

}
