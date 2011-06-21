/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright IBM  (2011)
 * contributor : Frank Filz  ffilz@us.ibm.com
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
#include "rpc.h"
#include "log_macros.h"
#include "nlm_list.h"
#include "nlm_util.h"
#include "nlm_async.h"
#include "nfs_core.h"

nfs_parameter_t nfs_param;

void *rpc_tcp_socket_manager_thread(void *Arg)
{
  return NULL;
}

netobj *create_int_netobj(netobj *obj, int x)
{
  if(obj == NULL)
    return NULL;
  obj->n_len   = 0;
  obj->n_bytes = (char *)Mem_Alloc(sizeof(int));
  if(obj->n_bytes == NULL)
    return NULL;
  obj->n_len = sizeof(int);
  *(int *)obj->n_bytes = x;
  return obj;
}

netobj *create_arb_netobj(netobj *obj, int len, void *data)
{
  if(obj == NULL)
    return NULL;
  obj->n_len   = 0;
  obj->n_bytes = (char *)Mem_Alloc(len);
  if(obj->n_bytes == NULL)
    return NULL;
  obj->n_len = len;
  memcpy(obj->n_bytes, data, len);
  return obj;
}

static struct nlm4_lockargs *create_lock(struct nlm4_lockargs *lock,
                                         int                   cookie,
                                         bool_t                block,
                                         bool_t                exclusive,
                                         char                 *caller_name,
                                         int                   fh_len,
                                         void                 *fh,
                                         int                   oh,
                                         int32_t               svid,
                                         uint64_t              l_offset,
                                         uint64_t              l_len,
                                         bool_t                reclaim,
                                         int32_t               state)
{
  memset(lock, sizeof(struct nlm4_lockargs), 0);
  lock->block              = block;
  lock->exclusive          = exclusive;
  lock->alock.caller_name  = caller_name;
  lock->alock.svid         = svid;
  lock->alock.l_offset     = l_offset;
  lock->alock.l_len        = l_len;
  lock->reclaim            = reclaim;
  lock->state              = state;
  if(create_int_netobj(&lock->cookie, cookie) == NULL)
    {
      LogTest("create_lock failed to create cookie");
      goto err_out;
    }
  if(create_arb_netobj(&lock->alock.fh, fh_len, fh) == NULL)
    {
      LogTest("create_lock failed to create fh");
      goto err_out;
    }
  if(create_int_netobj(&lock->alock.oh, oh) == NULL)
    {
      LogTest("create_lock failed to create oh");
      goto err_out;
    }
  return lock;

err_out:
  netobj_free(&lock->cookie);
  netobj_free(&lock->alock.fh);
  netobj_free(&lock->alock.oh);
  return NULL;
}

int cookie = 0xcc00001;

int apply_lock(char *id, char *caller_name, char *file, int owner, int svid, uint64_t offset, uint64_t len, int exclusive)
{
  struct nlm4_lockargs lock, *result;
  nlm_lock_entry_t *nlm_entry;

  result = create_lock(&lock, cookie++, 0, exclusive, caller_name, strlen(file), file, owner, svid, offset, len, 0, 0);
  if(result == NULL)
    {
      LogTest("Failed to create %s", id);
      return 1;
    }
  nlm_entry = nlm_add_to_locklist(&lock, NULL, NULL, NULL);
  if(nlm_entry == NULL)
    {
      LogTest("Failed to add %s to locklist", id);
      return 1;
    }
  nlm_lock_entry_dec_ref(nlm_entry);
  return 0;
}

int apply_read_lock(char *id, char *caller_name, char *file, int owner, int svid, uint64_t offset, uint64_t len)
{
  return apply_lock(id, caller_name, file, owner, svid, offset, len, 0);
}

int apply_write_lock(char *id, char *caller_name, char *file, int owner, int svid, uint64_t offset, uint64_t len)
{
  return apply_lock(id, caller_name, file, owner, svid, offset, len, 1);
}

int remove_lock(char *id, char *caller_name, char *file, int owner, int svid, uint64_t offset, uint64_t len, int exclusive)
{
  struct nlm4_lockargs lock, *result;
  nlm_lock_entry_t *nlm_entry;

  result = create_lock(&lock, cookie++, 0, exclusive, caller_name, strlen(file), file, owner, svid, offset, len, 0, 0);
  if(result == NULL)
    {
      LogTest("Failed to create %s", id);
      return 1;
    }
  nlm_entry = nlm_find_lock_entry(&(lock.alock), 0, NLM4_GRANTED);
  if(nlm_entry == NULL)
    {
      LogTest("Failed to find %s", id);
      return 1;
    }
  nlm_lock_entry_dec_ref(nlm_entry);
  nlm_delete_lock_entry(&(lock.alock));
  return 0;
}

int remove_read_lock(char *id, char *caller_name, char *file, int owner, int svid, uint64_t offset, uint64_t len)
{
  return remove_lock(id, caller_name, file, owner, svid, offset, len, 0);
}

int remove_write_lock(char *id, char *caller_name, char *file, int owner, int svid, uint64_t offset, uint64_t len)
{
  return remove_lock(id, caller_name, file, owner, svid, offset, len, 1);
}

/* Allocate and write into memory. */
int TEST_NLM(void)
{
  LogTest("TEST_NLM");
  nlm_init_locklist();

  if(apply_read_lock("first lock", "test1", "file1", 1, 1, 0, 1024))
    return 1;
  if(apply_read_lock("second lock", "test2", "file1", 2, 1, 0, 1024))
    return 1;
  if(apply_read_lock("third lock", "test1", "file1", 1, 1, 0, 1024))
    return 1;
  if(apply_read_lock("fourth lock", "test2", "file1", 2, 1, 0, 1024))
    return 1;
  dump_lock_list();
  if(remove_read_lock("first lock", "test1", "file1", 1, 1, 0, 1024))
    return 1;
  dump_lock_list();
  if(remove_read_lock("second lock", "test2", "file1", 2, 1, 0, 1024))
    return 1;
  dump_lock_list();
  if(apply_read_lock("lock a", "test1", "file1", 1, 1, 0, 256))
    return 1;
  if(apply_read_lock("lock b", "test1", "file1", 1, 1, 512, 256))
    return 1;
  if(apply_read_lock("lock c", "test1", "file1", 1, 1, 1024, 256))
    return 1;
  if(apply_read_lock("lock d", "test1", "file1", 1, 1, 1536, 256))
    return 1;
  if(apply_read_lock("bridging lock", "test1", "file1", 1, 1, 256, 256))
    return 1;
  dump_lock_list();
  if(remove_read_lock("locks a-d", "test1", "file1", 1, 1, 0, 0))
    return 1;
  dump_lock_list();
  if(apply_read_lock("lock to be split", "test1", "file1", 1, 1, 0, 3))
    return 1;
  if(remove_read_lock("middle", "test1", "file1", 1, 1, 1, 1))
    return 1;
  dump_lock_list();
  if(remove_read_lock("both", "test1", "file1", 1, 1, 0, 0))
    return 1;
  dump_lock_list();
  if(apply_read_lock("lock to be split", "test1", "file1", 1, 1, 0, 5))
    return 1;
  if(remove_read_lock("middle", "test1", "file1", 1, 1, 2, 1))
    return 1;
  dump_lock_list();
  if(remove_read_lock("both", "test1", "file1", 1, 1, 0, 0))
    return 1;
  dump_lock_list();

  if(apply_read_lock("lock a", "test1", "file1", 1, 1, 0, 256))
    return 1;
  if(apply_read_lock("lock b", "test1", "file1", 1, 1, 512, 256))
    return 1;
  if(apply_read_lock("lock c", "test1", "file1", 1, 1, 1024, 256))
    return 1;
  if(apply_read_lock("lock d", "test1", "file1", 1, 1, 1536, 256))
    return 1;
  if(apply_read_lock("lock e", "test1", "file1", 2, 1, 1024, 256))
    return 1;
  if(apply_read_lock("lock f", "test1", "file1", 2, 1, 2048, 256))
    return 1;
  dump_lock_list();
  if(remove_read_lock("all owner 1", "test1", "file1", 1, 1, 0, 0))
    return 1;
  dump_lock_list();
  
  if(remove_read_lock("all owner 2", "test1", "file1", 2, 1, 0, 0))
    return 1;

  dump_all_locks();

  return 0;
}

static char usage[] =
    "Usage :\n"
    "\ttest_support <test_name>\n\n"
    "\twhere <test_name> is:\n"
    "\t\tnlm : test nlm_utils\n";

int main(int argc, char **argv)
{
  int rc;

  /* Init the Buddy System allocation */
  if((rc = BuddyInit(NULL)) != BUDDY_SUCCESS)
    {
      LogTest("Error initializing memory allocator");
      exit(1);
    }

  SetDefaultLogging("TEST");
  SetNamePgm("test_support");
  InitLogging();

  if(!strcmp(argv[1], "nlm"))
    return TEST_NLM();

  else
    {
      LogTest("***** Unknown test: \"%s\" ******", argv[1]);
      LogTest("%s", usage);
      exit(1);
    }

  return 0;
}
