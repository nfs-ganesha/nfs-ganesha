/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file nfs_reaper_thread.c
 * @brief check for expired clients and whack them.
 */

#include "config.h"
#include <pthread.h>
#include <unistd.h>
#include "log.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_core.h"
#include "log.h"
#include "fridgethr.h"

#define REAPER_DELAY 10

unsigned int reaper_delay = REAPER_DELAY;

static struct fridgethr *reaper_fridge;

static int reap_expired_open_owners(hash_table_t *ht_reap)
{
	struct rbt_head *head_rbt;
	struct hash_data *addr = NULL;
	uint32_t i;
	int rc;
	struct rbt_node *pn;
	int count = 0;
	char str[LOG_BUFF_LEN];
	struct display_buffer dspbuf = {
			sizeof(str), str, str};
	state_owner_t *powner;
	time_t tnow, tclose, texpire;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_value;
	struct gsh_buffdesc old_key;

	/* For each bucket of the requested hashtable */
	for (i = 0; i < ht_reap->parameter.index_size; i++) {
		head_rbt = &ht_reap->partitions[i].rbt;

 restart:
		/* Use the time at the start or restart of a bucket to
		 * check for validity (don't call time() too many times).
		 */
		tnow = time(NULL);

		/* acquire mutex */
		PTHREAD_RWLOCK_wrlock(&ht_reap->partitions[i].lock);

		/* go through all entries in the red-black-tree */
		RBT_LOOP(head_rbt, pn) {
			addr = RBT_OPAQ(pn);


			powner = addr->val.addr;
			count++;

			if (powner->so_type != STATE_OPEN_OWNER_NFSV4) {
				RBT_INCREMENT(pn);
				continue;
			}

			display_owner(&dspbuf, powner);

			/* Cleanup the open owner only if its refcount is zero
			 * and its last_close_time exceeds the lease_lifetime
			 */
			tclose = atomic_fetch_time_t(&powner->so_owner.
						     so_nfs4_owner.
						     last_close_time);
			texpire = tclose + nfs_param.nfsv4_param.lease_lifetime;

			if ((tclose == 0) || (texpire > tnow)) {
				if (tclose != 0 &&
					isFullDebug(COMPONENT_STATE)) {
					LogFullDebug(COMPONENT_STATE,
							"Did not release CLOSE_PENDING %s, %d seconds left",
							str,
							(int) (texpire - tnow));
				}
				RBT_INCREMENT(pn);
				continue;
			}
			LogFullDebug(COMPONENT_STATE, "Free {%s}", str);
			buffkey.addr = powner;
			buffkey.len = sizeof(*powner);

			atomic_inc_int32_t(&powner->so_refcount);
			PTHREAD_RWLOCK_unlock(&ht_reap->partitions[i].
					      lock);
			rc = HashTable_Del(ht_reap, &buffkey,
					   &old_key, &old_value);

			if (rc != HASHTABLE_SUCCESS) {
				LogCrit(COMPONENT_CLIENTID,
					"Could not remove expired owner %s error=%s",
					str, hash_table_err_to_str(rc));
			}

			atomic_dec_int32_t(&powner->so_refcount);
			free_state_owner(powner);
			goto restart;

		}
		PTHREAD_RWLOCK_unlock(&ht_reap->partitions[i].lock);
	}

	return count;
}

static int reap_hash_table(hash_table_t *ht_reap)
{
	struct rbt_head *head_rbt;
	struct hash_data *addr = NULL;
	uint32_t i;
	struct rbt_node *pn;
	nfs_client_id_t *client_id;
	nfs_client_record_t *client_rec;
	int count = 0;

	/* For each bucket of the requested hashtable */
	for (i = 0; i < ht_reap->parameter.index_size; i++) {
		head_rbt = &ht_reap->partitions[i].rbt;

restart:
		/* acquire mutex */
		PTHREAD_RWLOCK_wrlock(&ht_reap->partitions[i].lock);

		/* go through all entries in the red-black-tree */
		RBT_LOOP(head_rbt, pn) {
			char str[LOG_BUFF_LEN];
			struct display_buffer dspbuf = {sizeof(str), str, str};
			bool str_valid = false;

			addr = RBT_OPAQ(pn);
			client_id = addr->val.addr;

			count++;

			PTHREAD_MUTEX_lock(&client_id->cid_mutex);

			if (valid_lease(client_id)) {
				PTHREAD_MUTEX_unlock(&client_id->cid_mutex);
				RBT_INCREMENT(pn);
				continue;
			}

			PTHREAD_MUTEX_unlock(&client_id->cid_mutex);

			if (isDebug(COMPONENT_CLIENTID)) {
				display_client_id_rec(&dspbuf, client_id);
				LogFullDebug(COMPONENT_CLIENTID,
					     "Expire index %d %s", i, str);
				str_valid = true;
			}

			/* Get the client record */
			client_rec = client_id->cid_client_record;

			/* get a ref to client_id as we might drop the
			 * last reference with expiring.
			 */
			inc_client_id_ref(client_id);

			/* if record is STALE, the linkage to client_record is
			 * removed already.
			 */
			if (client_rec != NULL) {
				inc_client_record_ref(client_rec);
				PTHREAD_MUTEX_lock(&client_rec->cr_mutex);
			}

			PTHREAD_RWLOCK_unlock(&ht_reap->partitions[i].lock);
			nfs_client_id_expire(client_id, false);

			if (client_rec != NULL) {
				PTHREAD_MUTEX_unlock(&client_rec->cr_mutex);
				dec_client_record_ref(client_rec);
			}

			if (isFullDebug(COMPONENT_CLIENTID)) {
				if (!str_valid)
					display_printf(&dspbuf, "clientid %p",
						       client_id);

				LogFullDebug(COMPONENT_CLIENTID,
					     "Reaper done, expired {%s}", str);
			}

			/* drop our reference to the client_id */
			dec_client_id_ref(client_id);

			goto restart;
		}
		PTHREAD_RWLOCK_unlock(&ht_reap->partitions[i].lock);
	}
	return count;
}

struct reaper_state {
	bool old_state_cleaned;
	size_t count;
	bool logged;
	bool in_grace;
};

static struct reaper_state reaper_state = {
	.old_state_cleaned = false,
	.count = 0,
	.logged = false,
	.in_grace = false
};

static void reaper_run(struct fridgethr_context *ctx)
{
	struct reaper_state *rst = ctx->arg;

	SetNameFunction("reaper");
	rst->in_grace = nfs_in_grace();

	if (!rst->old_state_cleaned) {
		/* if not in grace period, clean up the old state */
		if (!rst->in_grace) {
			nfs4_clean_old_recov_dir(v4_old_dir);
			rst->old_state_cleaned = true;
		}
	}

	if (isDebug(COMPONENT_CLIENTID) && ((rst->count > 0) || !rst->logged)) {
		LogDebug(COMPONENT_CLIENTID,
			 "Now checking NFS4 clients for expiration");

		rst->logged = (rst->count == 0);

#ifdef DEBUG_SAL
		if (rst->count == 0) {
			dump_all_states();
			dump_all_owners();
		}
#endif
	}

	rst->count =
	    (reap_hash_table(ht_confirmed_client_id) +
	     reap_hash_table(ht_unconfirmed_client_id));

	rst->count += reap_expired_open_owners(ht_nfs4_owner);
}

int reaper_init(void)
{
	struct fridgethr_params frp;
	int rc = 0;

	if (nfs_param.nfsv4_param.lease_lifetime < (2 * REAPER_DELAY))
		reaper_delay = nfs_param.nfsv4_param.lease_lifetime / 2;

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 1;
	frp.thr_min = 1;
	frp.thread_delay = reaper_delay;
	frp.flavor = fridgethr_flavor_looper;

	rc = fridgethr_init(&reaper_fridge, "reaper", &frp);
	if (rc != 0) {
		LogMajor(COMPONENT_CLIENTID,
			 "Unable to initialize reaper fridge, error code %d.",
			 rc);
		return rc;
	}

	rc = fridgethr_submit(reaper_fridge, reaper_run, &reaper_state);
	if (rc != 0) {
		LogMajor(COMPONENT_CLIENTID,
			 "Unable to start reaper thread, error code %d.", rc);
		return rc;
	}

	return 0;
}

int reaper_shutdown(void)
{
	int rc = fridgethr_sync_command(reaper_fridge,
					fridgethr_comm_stop,
					120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_CLIENTID,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(reaper_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_CLIENTID,
			 "Failed shutting down reaper thread: %d", rc);
	}
	return rc;
}
