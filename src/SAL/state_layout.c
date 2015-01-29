/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2011, Linux Box Corporation
 * contributor: Adam C. Emerson
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @file  state_layout.c
 * @brief Layout state management.
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#include "log.h"
#include "hashtable.h"
#include "fsal.h"
#include "sal_functions.h"
#include "nfs_core.h"

/**
 * @brief Add a segment to an existing layout state
 *
 * This function is intended to be used in nfs41_op_layoutget to add
 * each segment returned by FSAL_layoutget to an existing state of
 * type STATE_TYPE_LAYOUT.
 *
 * @param[in] state           The layout state.
 * @param[in] segment         Layout segment itself granted by the FSAL
 * @param[in] fsal_data       Pointer to FSAL-specific data for this segment.
 * @param[in] return_on_close True for automatic return on last close
 *
 * @return STATE_SUCCESS on completion, other values of state_status_t
 *         on failure.
 */
state_status_t state_add_segment(state_t *state, struct pnfs_segment *segment,
				 void *fsal_data, bool return_on_close)
{
	/* Pointer to the new segment being added to the state */
	state_layout_segment_t *new_segment = NULL;
	pthread_mutexattr_t mattr;

	if (state->state_type != STATE_TYPE_LAYOUT) {
		LogCrit(COMPONENT_PNFS,
			"Attempt to add layout segment to "
			"non-layout state: %p", state);
		return STATE_BAD_TYPE;
	}

	new_segment = gsh_calloc(1, sizeof(*new_segment));
	if (!new_segment)
		return STATE_MALLOC_ERROR;

	if (pthread_mutexattr_init(&mattr) != 0) {
		gsh_free(new_segment);
		return STATE_INIT_ENTRY_FAILED;
	}
#if defined(__linux__)
	if (pthread_mutexattr_settype(&mattr,
				      PTHREAD_MUTEX_RECURSIVE_NP) != 0) {
		gsh_free(new_segment);
		return STATE_INIT_ENTRY_FAILED;
	}
#else
	if (pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE) != 0) {
		gsh_free(new_segment);
		return STATE_INIT_ENTRY_FAILED;
	}
#endif

	if (pthread_mutex_init(&new_segment->sls_mutex, &mattr) != 0) {
		gsh_free(new_segment);
		return STATE_INIT_ENTRY_FAILED;
	}

	pthread_mutexattr_destroy(&mattr);

	new_segment->sls_fsal_data = fsal_data;
	new_segment->sls_state = state;
	new_segment->sls_segment = *segment;

	glist_add_tail(&state->state_data.layout.state_segments,
		       &new_segment->sls_state_segments);

	/* Based on comments by Benny Halevy, if any segment is marked
	   return_on_close, all segments should be treated as
	   return_on_close. */
	if (return_on_close)
		state->state_data.layout.state_return_on_close = true;

	return STATE_SUCCESS;
}

/**
 * @brief Delete a layout segment
 *
 * This function must be called with the mutex lock held.
 *
 * @param[in] segment Segment to delete
 *
 * @return State status.
 */
state_status_t state_delete_segment(state_layout_segment_t *segment)
{
	glist_del(&segment->sls_state_segments);
	pthread_mutex_unlock(&segment->sls_mutex);
	pthread_mutex_destroy(&segment->sls_mutex);
	gsh_free(segment);
	return STATE_SUCCESS;
}

/**
 * @brief Find pre-existing layouts
 *
 * This function finds a state corresponding to a given file,
 * clientid, and layout type if one exists.
 *
 * @param[in]  entry Cache inode entry for the file
 * @param[in]  owner The state owner.  This must be a clientid owner.
 * @param[in]  type  Layout type specified by the client.
 * @param[out] state The found state, NULL if not found.
 *
 * @return STATE_SUCCESS if the layout is found, STATE_NOT_FOUND if it
 *         isn't, and an appropriate code if other bad things happen.
 */

state_status_t state_lookup_layout_state(cache_entry_t *entry,
					 state_owner_t *owner,
					 layouttype4 type, state_t **state)
{
	/* Pointer for iterating over the list of states on the file */
	struct glist_head *glist_iter = NULL;
	/* The state under inspection in the loop */
	state_t *state_iter = NULL;
	/* The state found, if one exists */
	state_t *state_found = NULL;

	glist_for_each(glist_iter, &entry->list_of_states) {
		state_iter = glist_entry(glist_iter, state_t, state_list);
		if ((state_iter->state_type == STATE_TYPE_LAYOUT)
		    && (state_iter->state_owner == owner)
		    && (state_iter->state_data.layout.state_layout_type ==
			type)) {
			state_found = state_iter;
			break;
		}
	}

	if (!state_found) {
		return STATE_NOT_FOUND;
	} else if (state_found->state_entry != entry) {
		return STATE_INCONSISTENT_ENTRY;
	} else {
		*state = state_found;
		return STATE_SUCCESS;
	}
}

/** @} */
