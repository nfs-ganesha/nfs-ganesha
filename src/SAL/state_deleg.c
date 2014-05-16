/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright IBM  (2014)
 * contributeur : Jeremy Bongio   jbongio@us.ibm.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file state_deleg.c
 * @brief Delegation management
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

#include "fsal.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "cache_inode_lru.h"
#include "export_mgr.h"

void init_clientfile_deleg(struct clientfile_deleg_heuristics *clfile_entry)
{
}

/**
 * @brief Initialize new delegation state as argument for state_add()
 *
 * Initialize delegation state struct. This is then given as an argument
 * to state_add()
 *
 * @param[in/out] deleg_state Delegation state struct to be init. Can't be NULL.
 * @param[in] open_state Open state for the file this delegation is for.
 * @param[in] sd_type Type of delegation, READ or WRITE.
 * @param[in] client Client that will own this delegation.
 */
void init_new_deleg_state(state_data_t *deleg_state, state_t *open_state,
			  open_delegation_type4 sd_type,
			  nfs_client_id_t *client)
{
	struct clientfile_deleg_heuristics *clfile_entry =
		&deleg_state->deleg.clfile_stats;

	deleg_state->deleg.sd_open_state = open_state;
	deleg_state->deleg.sd_type = sd_type;
	deleg_state->deleg.grant_time = time(NULL);

	clfile_entry->clientid = client;
	clfile_entry->last_delegation = 0;
	clfile_entry->num_recalls = 0;
	clfile_entry->num_recall_badhandles = 0;
	clfile_entry->num_recall_races = 0;
	clfile_entry->num_recall_timeouts = 0;
	clfile_entry->num_recall_aborts = 0;
}

/**
 * @brief Update statistics on successfully granted delegation.
 *
 * Update statistics on successfully granted delegation.
 * Note: This should be called only when a delegation is successfully granted.
 *       So far this should only be called in state_lock().
 *
 * @param[in] entry Inode entry the delegation is for.
 * @param[in] state Delegation state pertaining to new delegation lock.
 */
bool update_delegation_stats(cache_entry_t *entry, state_t *state)
{
	struct clientfile_deleg_heuristics *clfile_entry =
		&state->state_data.deleg.clfile_stats;

	/* Update delegation stats for file. */
	struct file_deleg_heuristics *statistics =
		&entry->object.file.deleg_heuristics;
	statistics->curr_delegations++;
	statistics->disabled = false;
	statistics->delegation_count++;
	statistics->last_delegation = time(NULL);

	/* Update delegation stats for client. */
	clfile_entry->clientid->deleg_heuristics.curr_deleg_grants++;

	/* Update delegation stats for client-file. */
	clfile_entry->last_delegation = statistics->last_delegation;

	return true;
}

/* Add a new delegation length to the average length stat. */
static int advance_avg(time_t prev_avg, time_t new_time,
		       uint32_t prev_tot, uint32_t curr_tot)
{
	return ((prev_tot * prev_avg) + new_time) / curr_tot;
}

/**
 * @brief Update statistics on successfully recalled delegation.
 *
 * Update statistics on successfully recalled delegation.
 * Note: This should be called only when a delegation is successfully recalled.
 *
 * @param[in] entry Inode entry the delegation is based on.
 * @param[in] client Client that owned the delegation
 */
bool deleg_heuristics_recall(cache_entry_t *entry, nfs_client_id_t *client)
{
	/* Update delegation stats for file. */
	struct file_deleg_heuristics *statistics =
		&entry->object.file.deleg_heuristics;
	statistics->curr_delegations--;
	statistics->disabled = false;
	statistics->recall_count++;

	/* Update delegation stats for client. */
	client->deleg_heuristics.curr_deleg_grants--;

	/* Update delegation stats for client-file. */
	statistics->avg_hold = advance_avg(statistics->avg_hold,
					   time(NULL)
					   - statistics->last_delegation,
					   statistics->recall_count - 1,
					   statistics->recall_count);
	return true;
}

/**
 * @brief Initialize the file-specific delegation statistics
 *
 * Initialize the file-specific delegation statistics used later for deciding
 * if a delegation should be granted on this file based on heuristics.
 *
 * @param[in] entry Inode entry the delegation will be on.
 */
bool init_deleg_heuristics(cache_entry_t *entry)
{
	struct file_deleg_heuristics *statistics;

	if (entry->type != REGULAR_FILE) {
		LogCrit(COMPONENT_STATE,
			"Initialization of delegation stats for an entry that is NOT a regular file!");
		return false;
	}

	statistics = &entry->object.file.deleg_heuristics;
	statistics->curr_delegations = 0;
	statistics->deleg_type = OPEN_DELEGATE_NONE;
	statistics->disabled = false;
	statistics->delegation_count = 0;
	statistics->recall_count = 0;
	statistics->last_delegation = 0;
	statistics->last_recall = 0;
	statistics->avg_hold = 0;
	statistics->num_opens = 0;
	statistics->first_open = 0;

	return true;
}

/**
 * @brief Decide if a delegation should be granted based on heuristics.
 *
 * Decide if a delegation should be granted based on heuristics.
 * Note: Whether the export supports delegations should be checked before
 * calling this function.
 * The open_state->state_type will decide whether we attempt to get a READ or
 * WRITE delegation.
 *
 * @param[in] entry Inode entry the delegation will be on.
 * @param[in] client Client that would own the delegation.
 * @param[in] open_state The open state for the inode to be delegated.
 */
bool should_we_grant_deleg(cache_entry_t *entry, nfs_client_id_t *client,
			   state_t *open_state)
{
	/* specific file, all clients, stats */
	struct file_deleg_heuristics *file_stats =
		&entry->object.file.deleg_heuristics;
	/* specific client, all files stats */
	struct client_deleg_heuristics *cl_stats = &client->deleg_heuristics;
	/* specific client, specific file stats */
	float ACCEPTABLE_FAILS = 0.1; /* 10% */
	float ACCEPTABLE_OPEN_FREQUENCY = .01; /* per second */
	time_t spread;

	LogDebug(COMPONENT_STATE, "Checking if we should grant delegation.");

	if (open_state->state_type != STATE_TYPE_SHARE) {
		LogDebug(COMPONENT_STATE,
			 "expects a SHARE open state and no other.");
		return false;
	}

	/* Check if this file is opened too frequently to delegate. */
	spread = time(NULL) - file_stats->first_open;
	if (spread != 0 &&
	     (file_stats->num_opens / spread) > ACCEPTABLE_OPEN_FREQUENCY) {
		LogDebug(COMPONENT_STATE,
			 "This file is opened too frequently to delegate.");
		return false;
	}

	/* Check if open state and requested delegation agree. */
	if (file_stats->curr_delegations > 0) {
		if (file_stats->deleg_type == OPEN_DELEGATE_READ &&
		    open_state->state_data.share.share_access &
		    OPEN4_SHARE_ACCESS_WRITE) {
			LogMidDebug(COMPONENT_STATE,
				    "READ delegate requested, but file is opened for WRITE.");
			return false;
		}
		if (file_stats->deleg_type == OPEN_DELEGATE_WRITE &&
		    !(open_state->state_data.share.share_access &
		      OPEN4_SHARE_ACCESS_WRITE)) {
			LogMidDebug(COMPONENT_STATE,
				    "WRITE delegate requested, but file is not opened for WRITE.");
		}
	}

	/* Check if this is a misbehaving or unreliable client */
	if (cl_stats->tot_recalls > 0 &&
	    ((1.0 - (cl_stats->failed_recalls / cl_stats->tot_recalls))
	     > ACCEPTABLE_FAILS)) {
		LogDebug(COMPONENT_STATE,
			 "Client is %.0f unreliable during recalls. Allowed failure rate is %.0f. Denying delegation.",
			 1.0 - (cl_stats->failed_recalls
				/ cl_stats->tot_recalls),
			 ACCEPTABLE_FAILS);
		return false;
	}
	/* minimum average milliseconds that delegations should be held on a
	   file. if less, then this is not a good file for delegations. */
#define MIN_AVG_HOLD 1500
	if (file_stats->avg_hold < MIN_AVG_HOLD
	    && file_stats->avg_hold != 0) {
		LogDebug(COMPONENT_STATE,
			 "Average length of delegation (%lld) is less than minimum avg (%lld). Denying delegation.",
			 (long long) file_stats->avg_hold,
			 (long long) MIN_AVG_HOLD);
		return false;
	}

	LogDebug(COMPONENT_STATE, "Let's delegate!!");
	return true;
}

/**
 * @brief Form the ACE mask for the delegated file.
 *
 * Form the ACE mask for the delegated file.
 *
 * @param[in] entry Inode entry the delegation will be on.
 * @param[in/out] permissions ACE mask for delegated inode.
 * @param[in] type Type of delegation. Either READ or WRITE.
 */
void get_deleg_perm(cache_entry_t *entry, nfsace4 *permissions,
		    open_delegation_type4 type)
{
	/* We need to create an access_mask that shows who
	 * can OPEN this file. */
	if (type == OPEN_DELEGATE_WRITE)
		;
	else if (type == OPEN_DELEGATE_READ)
		;
	permissions->type = ACE4_ACCESS_ALLOWED_ACE_TYPE;
	permissions->flag = 0;
	permissions->access_mask = 0;
	permissions->who.utf8string_len = 0;
	permissions->who.utf8string_val = NULL;
}
