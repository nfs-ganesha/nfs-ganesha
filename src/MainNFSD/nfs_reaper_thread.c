// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef __APPLE__
#include <malloc.h>
#endif

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
		/* Before starting with traversal of RBT in the bucket,
		 * if the expired client list gets filled, need reap it first
		 */
		count += reap_expired_client_list(NULL);
		head_rbt = &ht_reap->partitions[i].rbt;
restart:
		/* acquire mutex */
		PTHREAD_RWLOCK_wrlock(&ht_reap->partitions[i].ht_lock);

		/* go through all entries in the red-black-tree */
		RBT_LOOP(head_rbt, pn)
		{
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer dspbuf = { sizeof(str), str,
							 str };
			bool str_valid = false;

			addr = RBT_OPAQ(pn);
			client_id = addr->val.addr;

			count++;

			PTHREAD_MUTEX_lock(&client_id->cid_mutex);

			if (client_id->marked_for_delayed_cleanup ||
			    valid_lease(client_id, false)) {
				PTHREAD_MUTEX_unlock(&client_id->cid_mutex);
				RBT_INCREMENT(pn);
				continue;
			}

			if (isDebug(COMPONENT_CLIENTID)) {
				display_client_id_rec(&dspbuf, client_id);
				LogFullDebug(COMPONENT_CLIENTID,
					     "Expired index %d %s", i, str);
				str_valid = true;
			}

			/* Get the client record. It will not be NULL. */
			client_rec = client_id->cid_client_record;

			/* get a ref to client_id as we might drop the
			 * last reference while expiring. This also protects
			 * the reference to the client_record.
			 */
			inc_client_id_ref(client_id);

			PTHREAD_MUTEX_unlock(&client_id->cid_mutex);
			PTHREAD_RWLOCK_unlock(&ht_reap->partitions[i].ht_lock);

			/* Before expiring the current client in RBT node,
			 * if the expired client list gets filled, need reap it
			 */
			count += reap_expired_client_list(NULL);

			PTHREAD_MUTEX_lock(&client_rec->cr_mutex);

			nfs_client_id_expire(client_id, false, false);

			PTHREAD_MUTEX_unlock(&client_rec->cr_mutex);

			if (isFullDebug(COMPONENT_CLIENTID)) {
				if (!str_valid)
					display_printf(&dspbuf, "clientid %p",
						       client_id);
				if (client_id->marked_for_delayed_cleanup) {
					LogFullDebug(
						COMPONENT_CLIENTID,
						"Reaper, Parked for later cleanup {%s}",
						str);
				} else {
					LogFullDebug(
						COMPONENT_CLIENTID,
						"Reaper done, expired {%s}",
						str);
				}
			}

			/* drop our reference to the client_id */
			dec_client_id_ref(client_id);

			goto restart;
		}
		PTHREAD_RWLOCK_unlock(&ht_reap->partitions[i].ht_lock);
	}
	return count;
}

static int reap_expired_open_owners(void)
{
	int count = 0;
	time_t tnow = time(NULL);
	time_t texpire;
	state_owner_t *owner;
	struct state_nfs4_owner_t *nfs4_owner;

	PTHREAD_MUTEX_lock(&cached_open_owners_lock);

	/* Walk the list of cached NFS 4 open owners. Because we hold
	 * the mutex while walking this list, it is impossible for another
	 * thread to get a primary reference to these owners while we
	 * process, and thus prevent them from expiring.
	 */
	while (true) {
		owner = glist_first_entry(
			&cached_open_owners, state_owner_t,
			so_owner.so_nfs4_owner.so_cache_entry);

		if (owner == NULL)
			break;

		nfs4_owner = &owner->so_owner.so_nfs4_owner;
		texpire = atomic_fetch_time_t(&nfs4_owner->so_cache_expire);
		if (texpire > tnow) {
			/* This owner has not yet expired. */
			if (isFullDebug(COMPONENT_STATE)) {
				char str[LOG_BUFF_LEN] = "\0";
				struct display_buffer dspbuf = { sizeof(str),
								 str, str };

				display_owner(&dspbuf, owner);

				LogFullDebug(
					COMPONENT_STATE,
					"Did not release CLOSE_PENDING %d seconds left for {%s}",
					(int)(texpire - tnow), str);
			}

			/* Because entries are not moved on this list, and
			 * they are added when they first become eligible,
			 * the entries are in order of expiration time, and
			 * thus once we hit one that is not expired yet, the
			 * rest are also not expired.
			 */
			break;
		}

		/* This cached owner has expired, uncache it. */
		uncache_nfs4_owner(nfs4_owner);
		count++;
	}

	PTHREAD_MUTEX_unlock(&cached_open_owners_lock);

	return count;
}

#ifndef __APPLE__
/* Return resident memory of the process in MB */
static size_t get_current_rss(void)
{
	static long page_size;
	int rc, fd;
	char buf[1024];
	long vsize = 0, rss = 0;

	if (page_size == 0) {
		page_size = sysconf(_SC_PAGESIZE);
		if (page_size <= 0) {
			LogEvent(COMPONENT_MEMLEAKS, "_SC_PAGESIZE failed, %d",
				 errno);
			return 0;
		}
	}

	fd = open("/proc/self/statm", O_RDONLY);
	if (fd < 0)
		return 0;

	rc = read(fd, buf, sizeof(buf) - 1);
	if (rc < 0)
		goto out;

	buf[rc] = '\0';
	rc = sscanf(buf, "%ld %ld", &vsize, &rss);
	if (rc != 2) {
		LogEvent(COMPONENT_MEMLEAKS,
			 "Failed to read data from /proc/self/statm");
	}
out:
	close(fd);
	return ((uint64_t)rss * page_size) / (1024 * 1024);
}

static void reap_malloc_frag(void)
{
	static size_t trim_threshold;
	size_t min_threshold = nfs_param.core_param.malloc_trim_minthreshold;
	size_t rss;

	if (trim_threshold == 0)
		trim_threshold = min_threshold;

	rss = get_current_rss();
	LogDebug(COMPONENT_MEMLEAKS, "current rss: %zu MB, threshold: %zu MB",
		 rss, trim_threshold);

	if (rss < trim_threshold) {
		/* If the threshold is too big, drop it in relation to
		 * the current RSS
		 */
		if (trim_threshold > rss + rss / 2)
			trim_threshold = MAX(rss + rss / 2, min_threshold);
		return;
	}

	LogEvent(COMPONENT_MEMLEAKS,
		 "calling malloc_trim, current rss: %zu MB, threshold: %zu MB",
		 rss, trim_threshold);

	malloc_trim(0);
	rss = get_current_rss();
	/* Set trim threshold to one and one half times of the current RSS */
	trim_threshold = MAX(rss + rss / 2, min_threshold);

	LogEvent(COMPONENT_MEMLEAKS,
		 "called malloc_trim, current rss: %zu MB, threshold: %zu MB",
		 rss, trim_threshold);
}
#endif

struct reaper_state {
	size_t count;
	bool logged;
};

static struct reaper_state reaper_state;

static void reaper_run(struct fridgethr_context *ctx)
{
	struct reaper_state *rst = ctx->arg;

	SetNameFunction("reaper");

	/* see if we need to start a grace period */
	nfs_maybe_start_grace();

	/*
	 * Try to lift the grace period, unless we're shutting down.
	 * Ordinarily, we'd take the mutex to check this, but this is just a
	 * best-effort sort of thing.
	 */
	if (!admin_shutdown)
		nfs_try_lift_grace();

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

	rst->count = reap_expired_client_list(NULL);
	rst->count += (reap_hash_table(ht_confirmed_client_id) +
		       reap_hash_table(ht_unconfirmed_client_id));

	rst->count += reap_expired_open_owners();

#ifndef __APPLE__
	if (nfs_param.core_param.malloc_trim)
		reap_malloc_frag();
#endif
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

void reaper_wake(void)
{
	struct fridgethr *frt = reaper_fridge;

	if (frt)
		fridgethr_wake(frt);
}

int reaper_shutdown(void)
{
	int rc =
		fridgethr_sync_command(reaper_fridge, fridgethr_comm_stop, 120);

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
