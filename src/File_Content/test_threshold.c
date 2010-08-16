#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "nfs_exports.h"
#include "fsal.h"
#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <sys/vfs.h>

int main(int argc, char **argv)
{
  char *path;
  int rc;

  int is_over;
  unsigned long to_purge;

  if(argc != 2)
    printf("Usage: %s <fs_path>\n", argv[0]);
  path = argv[1];

  /* init logging */
  SetNamePgm("test_threshold");
  SetNameFunction("main");
  SetNameHost("localhost");
  InitDebug(NIV_FULL_DEBUG);

  /* Set the log */
  SetNameFileLog("/dev/tty");

  printf("cache_content_check_threshold( %s, %u, %u, %p, %p)\n",
         path, 70, 80, &is_over, &to_purge);

  rc = cache_content_check_threshold(path, 70, 80, &is_over, &to_purge);

  printf("rc=%d\n", rc);

  exit(rc);
  return rc;

}
