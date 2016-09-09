/* -*- mode: c; c-tab-always-indent: t; c-basic-offset: 8 -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/* file.c
 * File I/O methods for PSEUDO module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "scality_methods.h"
#include "sproxyd_client.h"
#include "dbd_rest_client.h"

/** scality_open
 * called with appropriate locks taken at the cache inode level
 */

fsal_status_t scality_open(struct fsal_obj_handle *obj_hdl,
			  fsal_openflags_t openflags)
{
	struct scality_fsal_obj_handle *myself = container_of(obj_hdl,
							     struct scality_fsal_obj_handle,
							     obj_handle);
	LogDebug(COMPONENT_FSAL, "scality_open(%s)",myself->object);
	myself->openflags = openflags;
	
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* scality_status
 * Let the caller peek into the file's open/close state.
 */

fsal_openflags_t scality_status(struct fsal_obj_handle *obj_hdl)
{
	struct scality_fsal_obj_handle *myself = container_of(obj_hdl,
							     struct scality_fsal_obj_handle,
							     obj_handle);
	LogDebug(COMPONENT_FSAL, "scality_status(%s)",myself->object);
	return myself->openflags;
}

/* scality_read
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t scality_read(struct fsal_obj_handle *obj_hdl,
			  uint64_t offset,
			  size_t buffer_size, void *buffer,
			  size_t *read_amount, bool *end_of_file)
{
	
	struct scality_fsal_obj_handle *myself = container_of(obj_hdl,
							     struct scality_fsal_obj_handle,
							     obj_handle);
	struct scality_fsal_export *export =
		container_of(op_ctx->fsal_export,
			     struct scality_fsal_export, export);
	if ( offset > myself->attributes.filesize ) {
		*read_amount = 0;
		*end_of_file = true;
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	} else if ( offset+buffer_size >= myself->attributes.filesize ) {
		buffer_size = myself->attributes.filesize - offset;
		*read_amount = buffer_size;
		*end_of_file = true;
	} else {
		*read_amount = buffer_size;
		*end_of_file = false;
	}
	int ret;
	ret = sproxyd_read(export, myself, offset, buffer_size, (char*)buffer);
	if ( ret < 0 )
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


static int
add_location(struct scality_fsal_obj_handle *myself)
{
	struct scality_location *tmp;
	tmp = gsh_realloc(myself->locations, sizeof(struct scality_location) *
			  (myself->n_locations+1));	
	if ( NULL == tmp ) {
		//BTW it aborts
		return -1;
	}
	myself->locations = tmp;
	myself->locations[myself->n_locations].key=NULL;
	myself->locations[myself->n_locations].content=NULL;
	myself->locations[myself->n_locations].size=0;
	if ( myself->n_locations > 0 )
		myself->locations[myself->n_locations].start=
			myself->locations[myself->n_locations-1].start+
			myself->locations[myself->n_locations-1].size;
	else
		myself->locations[myself->n_locations].start=0;
	++myself->n_locations;
	return 0;
}

void
scality_add_to_free_list(struct scality_part **partp, char *key)
{
	struct scality_part *part = gsh_malloc(sizeof(*part));
	part->key = key;
	part->next = *partp;
	*partp = part;
}

static int
prepare_last_location(struct scality_fsal_export *export,
		      struct scality_fsal_obj_handle *myself)
{
	struct scality_location *location = &myself->locations[myself->n_locations-1];
	char * content = location->content;
	if ( NULL == content ) {
		content = gsh_malloc(myself->part_size);
		if ( NULL == content ) {
			//BTW it aborts
			return -1;
		}
		myself->memory_used+=myself->part_size;
		if ( location->key ) {
			size_t read_amount;
			bool  end_of_file;
			LogDebug(COMPONENT_FSAL,
				 "read(start: %zu, size: %zu) begin",
				 location->start, location->size);
			fsal_status_t status = scality_read(&myself->obj_handle,
							   location->start,
							   location->size,
							   content,
							   &read_amount,
							   &end_of_file);
			LogDebug(COMPONENT_FSAL,
				 "read(start: %zu, size: %zu) end",
				 location->start, location->size);
			if ( ERR_FSAL_NO_ERROR != status.major )
				return -1;
			if ( read_amount != location->size )
			return -1;
			if ( !end_of_file )
				return -1;
			
			scality_add_to_free_list(&myself->delete_on_commit, location->key);
			location->key = NULL;
		}
		location->content = content;
	}
	else {
		if ( location->key ) {
			gsh_free(location->key);
			location->key = NULL;
		}
	}
	return 0;
}

static int
flush_content(struct scality_fsal_export *export,
	      struct scality_fsal_obj_handle *myself)
{
	LogDebug(COMPONENT_FSAL, "flush begin");
	size_t i;
	int ret;
	for ( i = 0 ; i < myself->n_locations ; ++i ) {
		struct scality_location *location = &myself->locations[i];
		if ( NULL != location->key )
			continue;
		location->key = sproxyd_new_key();
		const char *key = location->key;
		scality_add_to_free_list(&myself->delete_on_rollback, gsh_strdup(location->key));
		LogDebug(COMPONENT_FSAL,
			 "sproxyd put(%s,...,%zu): begin",
			 key, location->size);
		ret = sproxyd_put(export, key, location->content, location->size);
		LogDebug(COMPONENT_FSAL,
			 "sproxyd put(%s,...,%zu): end",
			 key, location->size);
		if ( ret < 0 ) {
			LogCrit(COMPONENT_FSAL, "Failed to put in sproxyd");
			gsh_free(location->key);
			location->key = NULL;
			return -1;
		}
		if ( i < myself->n_locations-1 ) {
			gsh_free(location->content);
			location->content = NULL;
		}
	}
	LogDebug(COMPONENT_FSAL, "flush end");
	return 0;
}

void
scality_cleanup(struct scality_fsal_export *export,
	struct scality_fsal_obj_handle *myself,
	enum scality_fsal_cleanup_flag cleanup_flag)
{
	struct scality_part *part, *next;
	for ( part = myself->delete_on_commit ;
	      part ;
	      part = next ) {
		next = part->next;
		if ( cleanup_flag & SCALITY_FSAL_CLEANUP_COMMIT ) {
			int ret;
			ret = sproxyd_delete(export, part->key);
			if ( ret < 0 ) {
				LogCrit(COMPONENT_FSAL, "Unable to delete %s", part->key);
			}
		}
		gsh_free(part->key);
		gsh_free(part);
	}
	myself->delete_on_commit = NULL;
	for ( part = myself->delete_on_rollback ;
	      part ;
	      part = next ) {
		next = part->next;
		if ( cleanup_flag & SCALITY_FSAL_CLEANUP_ROLLBACK ) {
			int ret;
			ret = sproxyd_delete(export, part->key);
			if ( ret < 0 ) {
				LogCrit(COMPONENT_FSAL, "Unable to delete %s", part->key);
			}
		}
		gsh_free(part->key);
		gsh_free(part);
	}
	myself->delete_on_rollback = NULL;
}

/* scality_write
 * concurrency (locks) is managed in cache_inode_*
 */

fsal_status_t scality_write(struct fsal_obj_handle *obj_hdl,
			   uint64_t offset,
			   size_t buffer_size, void *buffer,
			   size_t *write_amount, bool *fsal_stable)
{
	LogDebug(COMPONENT_FSAL, "write begin");
	int ret;
	fsal_errors_t error = ERR_FSAL_NO_ERROR;
	struct scality_fsal_obj_handle *myself = container_of(obj_hdl,
							     struct scality_fsal_obj_handle,
							     obj_handle);
	struct scality_fsal_export *export =
		container_of(op_ctx->fsal_export,
			     struct scality_fsal_export, export);

	if ( offset != myself->attributes.filesize ) {
		LogCrit(COMPONENT_FSAL,
			"scality_write(): Invoking unsupported FSAL operation: write must append");
		return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
	}

	*write_amount = 0;

	if ( 0 == buffer_size ) {
		goto out;
	}
	
	do {
		if ( 0 == myself->n_locations ||
		     myself->locations[myself->n_locations-1].size >= myself->part_size ) {
			ret = add_location(myself);
			if ( ret < 0 ) {
				LogCrit(COMPONENT_FSAL,
					"unable to add a location to metadata: %s",
					myself->object);
				error = ERR_FSAL_SERVERFAULT;
				goto out;
			}
		}
		
		struct scality_location *location = &myself->locations[myself->n_locations-1];
		
		if ( location->key || NULL == location->content ) {
			ret = prepare_last_location(export, myself);
			if ( ret < 0 ) {
				LogCrit(COMPONENT_FSAL,
					"unable to prepare last location: %s",
					myself->object);
				error = ERR_FSAL_SERVERFAULT;
				goto out;
			}
		}
		
		size_t to_write = buffer_size;
		if ( to_write > myself->part_size - location->size )
			to_write = myself->part_size - location->size;
		memcpy(location->content + location->size, buffer, to_write);
		location->size += to_write;
		myself->attributes.filesize += to_write;
		myself->state = SCALITY_FSAL_OBJ_STATE_DIRTY;
		
		if ( myself->memory_used >= FLUSH_THRESHOLD ) {
			ret = flush_content(export, myself);
			if ( ret < 0 ) {
				LogCrit(COMPONENT_FSAL,
					"unable to flush file content: %s",
					myself->object);
				error = ERR_FSAL_SERVERFAULT;
				//rollback...
				goto out;
			}
		}
		buffer_size -= to_write;
		buffer += to_write;
		*write_amount += to_write;
	} while ( buffer_size > 0 );

	if ( NULL != fsal_stable )
		*fsal_stable = false;
	
	error = ERR_FSAL_NO_ERROR;
 out:	
	LogDebug(COMPONENT_FSAL, "write end");
	return fsalstat(error, 0);
}

/* scality_commit
 * Commit a file range to storage.
 * for right now, fsync will have to do.
 */

fsal_status_t scality_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			    off_t offset, size_t len)
{
	fsal_errors_t error = ERR_FSAL_NO_ERROR;
	struct scality_fsal_obj_handle *myself = container_of(obj_hdl,
							     struct scality_fsal_obj_handle,
							     obj_handle);
	struct scality_fsal_export *export =
		container_of(op_ctx->fsal_export,
			     struct scality_fsal_export, export);

	assert( SCALITY_FSAL_OBJ_STATE_DELETED != myself->state );
	
	if ( SCALITY_FSAL_OBJ_STATE_DIRTY == myself->state ) {
		int ret;
		ret = flush_content(export, myself);
		if ( ret < 0 ) {
			LogCrit(COMPONENT_FSAL,
				"scality_commit failed to flush content: %s",
				myself->object);
			error = ERR_FSAL_SERVERFAULT;
			//rollback...
			goto out;
		}
		
		ret = dbd_post(export, myself);
		if ( ret < 0 ) {
			LogCrit(COMPONENT_FSAL,
				 "Unable to setattr(%s)", myself->object);
			error = ERR_FSAL_NO_ERROR;
			goto out;
		}
		myself->state = SCALITY_FSAL_OBJ_STATE_CLEAN;
	}

	scality_cleanup(export, myself, SCALITY_FSAL_CLEANUP_COMMIT);
	error = ERR_FSAL_NO_ERROR;
 out:	
	return fsalstat(error, 0);
}

/* scality_lock_op
 * lock a region of the file
 * throw an error if the fd is not open.  The old fsal didn't
 * check this.
 */

fsal_status_t scality_lock_op(struct fsal_obj_handle *obj_hdl,
			     void *p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *request_lock,
			     fsal_lock_param_t *conflicting_lock)
{
	/* SCALITY doesn't support non-directory inodes */
	LogCrit(COMPONENT_FSAL,
		"scality_lock_op(): Invoking unsupported FSAL operation");
	return fsalstat(ERR_FSAL_NOTSUPP, ENOTSUP);
}

enum cleanup_from {
	FROM_CLOSE,
	FROM_LRU_CLEANUP
};
static fsal_status_t
cleanup_handle(struct fsal_obj_handle *obj_hdl,
	       enum cleanup_from from)
{
	struct scality_fsal_obj_handle *myself = container_of(obj_hdl,
							     struct scality_fsal_obj_handle,
							     obj_handle);
	if (FROM_CLOSE == from)
		myself->openflags = FSAL_O_CLOSED;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* scality_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t scality_close(struct fsal_obj_handle *obj_hdl)
{
	return cleanup_handle(obj_hdl, FROM_CLOSE);
}

/* scality_lru_cleanup
 * free non-essential resources at the request of cache inode's
 * LRU processing identifying this handle as stale enough for resource
 * trimming.
 */

fsal_status_t scality_lru_cleanup(struct fsal_obj_handle *obj_hdl,
				 lru_actions_t requests)
{
	return cleanup_handle(obj_hdl, FROM_LRU_CLEANUP);
}
