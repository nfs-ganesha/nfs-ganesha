/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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

/**
 * nfs_reaper_thread.c : check for expired clients and whack them.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <pthread.h>
#include <unistd.h>
#include "log.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_core.h"
#include "log.h"

#define REAPER_DELAY 10

unsigned int reaper_delay = REAPER_DELAY;
extern char v4_old_dir[PATH_MAX];

static int reap_hash_table(hash_table_t * ht_reap)
{
  struct rbt_head     * head_rbt;
  hash_data_t         * pdata = NULL;
  uint32_t              i;
  int                   v4, rc;
  struct rbt_node     * pn;
  nfs_client_id_t     * pclientid;
  nfs_client_record_t * precord;
  int                   count = 0;

  /* For each bucket of the requested hashtable */
  for(i = 0; i < ht_reap->parameter.index_size; i++)
    {
      head_rbt = &ht_reap->partitions[i].rbt;

 restart:
      /* acquire mutex */
      PTHREAD_RWLOCK_WRLOCK(&ht_reap->partitions[i].lock);

      /* go through all entries in the red-black-tree*/
      RBT_LOOP(head_rbt, pn)
        {
          pdata = RBT_OPAQ(pn);

          pclientid = (nfs_client_id_t *)pdata->buffval.pdata;
          count++;

          /*
           * little hack: only want to reap v4 clients
           * 4.1 initializess this field to '1'
           */
#ifdef _USE_NFS4_1
          v4 = (pclientid->cid_create_session_sequence == 0);
#else
          v4 = TRUE;
#endif
          P(pclientid->cid_mutex);

          if(!valid_lease(pclientid) && v4)
            {
              inc_client_id_ref(pclientid);

              /* Take a reference to the client record */
              precord = pclientid->cid_client_record;
              inc_client_record_ref(precord);

              V(pclientid->cid_mutex);

              PTHREAD_RWLOCK_UNLOCK(&ht_reap->partitions[i].lock);

              if(isDebug(COMPONENT_CLIENTID))
                {
                  char                  str[LOG_BUFF_LEN];
                  struct display_buffer dspbuf = {sizeof(str), str, str};

                  (void) display_client_id_rec(&dspbuf, pclientid);

                  LogFullDebug(COMPONENT_CLIENTID,
                               "Expire index %d %s",
                               i, str);
                }

              /* Take cr_mutex and expire clientid */
              P(precord->cr_mutex);

              rc = nfs_client_id_expire(pclientid, 0);

              V(precord->cr_mutex);

              dec_client_id_ref(pclientid);
              dec_client_record_ref(precord);
              if(rc)
                goto restart;
            }
          else
            {
              V(pclientid->cid_mutex);
            }

          RBT_INCREMENT(pn);
        }

      PTHREAD_RWLOCK_UNLOCK(&ht_reap->partitions[i].lock);
    }

  return count;
}

void *reaper_thread(void *UnusedArg)
{
  int    old_state_cleaned = 0;
  int    count = 0;
  bool_t logged = TRUE;
  bool_t in_grace;

  SetNameFunction("reaper");

  if(nfs_param.nfsv4_param.lease_lifetime < (2 * REAPER_DELAY))
    reaper_delay = nfs_param.nfsv4_param.lease_lifetime / 2;

  while(1)
    {
      /* Initial wait */
      /** @todo: should this be configurable? */
      /* sleep(nfs_param.core_param.reaper_delay); */
      sleep(reaper_delay);

      in_grace = nfs_in_grace();

      if (old_state_cleaned == 0)
        {
          /* if not in grace period, clean up the old state */
          if(!in_grace)
            {
              nfs4_clean_old_recov_dir(v4_old_dir);
              old_state_cleaned = 1;
            }
        }

      if(isDebug(COMPONENT_CLIENTID) && ((count > 0) || !logged))
        {
          LogDebug(COMPONENT_CLIENTID,
                   "Now checking NFS4 clients for expiration");

          logged = count == 0;

#ifdef _DEBUG_MEMLEAKS
          if(count == 0)
            {
              dump_all_states();
              dump_all_owners();
            }
#endif
        }

      count = reap_hash_table(ht_confirmed_client_id) +
              reap_hash_table(ht_unconfirmed_client_id);
    }                           /* while ( 1 ) */

  return NULL;
}
