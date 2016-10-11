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


//#define SANITY_CHECK

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

/**
 * @brief Read a slice of the designated file.
 * This function handles whole file boundaries and calls sproxyd_read with
 * offset and size inside the file boundaries.
 * it also handles the read_amount and end_of_file output parameters
 *
 * @param obj_hdl -  object to read
 * @param offset - start position of the read
 * @param buffer_size - size of the destination buffer,
 *                      hence the maximum size to read
 * @param[out] buffer - buffer in which to copy the content read
 * @param[out] read_amount - number of bytes read from storage
 * @param[out] end_of_file - set to true if end of file is reached
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
	scality_content_lock(myself);
	ret = sproxyd_read(export, myself, offset, buffer_size, (char*)buffer);
	scality_content_unlock(myself);
	if ( ret < 0 )
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

struct scality_location *
scality_location_new(const char *key, size_t start, size_t size)
{
	struct scality_location *location;
	location = gsh_malloc(sizeof(*location));
	if ( NULL == location )
		return NULL;
	memset(location, 0, sizeof(*location));
	location->key = key ? gsh_strdup(key) : NULL;
	if ( NULL == location->key && NULL != key ) {
		gsh_free(location);
		return NULL;
	}
	location->start = start;
	location->size = size;
	location->content = NULL;
	location->stencil = NULL;
	location->buffer_size = 0;
	return location;
}

static void
scality_location_alloc_content(struct scality_fsal_obj_handle *myself,
			       struct scality_location *loc)
{
	size_t size_to_allocate = loc->size;
	if ( size_to_allocate < myself->part_size )
		size_to_allocate = myself->part_size;
	loc->content = gsh_malloc(size_to_allocate);
	loc->stencil = gsh_malloc(size_to_allocate);
	assert(NULL != loc->content);
	assert(NULL != loc->stencil);
	memset(loc->content, 0, size_to_allocate);
	memset(loc->stencil, STENCIL_READ, size_to_allocate);
	myself->memory_used += 2 * size_to_allocate;
	loc->buffer_size = size_to_allocate;
}

void
scality_location_free(struct scality_location *location)
{
	if (location) {
		gsh_free(location->key);
		gsh_free(location->content);
		gsh_free(location->stencil);
		//poison it
		memset(location, 0, sizeof *location);
	}
	gsh_free(location);
}

struct scality_location *
scality_location_lookup(struct scality_fsal_obj_handle *myself,
			uint64_t offset,
			size_t size)
{
	struct scality_location needle = {
		.start = offset,
		.size = size,
	};
	struct avltree_node *node;
	node = avltree_lookup(&needle.avltree_node, &myself->locations);
	if (NULL == node)
		return NULL;
	return avltree_container_of(node,
				    struct scality_location,
				    avltree_node);
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
flush_content(struct scality_fsal_export *export,
	      struct scality_fsal_obj_handle *myself)
{
	LogDebug(COMPONENT_FSAL, "flush begin");
	int ret;
	struct avltree_node *node;
	for ( node = avltree_first(&myself->locations) ;
	      node != NULL ;
	      node = avltree_next(node) ) {
		struct scality_location *location;
		location = avltree_container_of(node,
						struct scality_location,
						avltree_node);
		/*
		if ( 0 == location->size ) {
			LogDebug(COMPONENT_FSAL,
				"Empty part encountered: %s",
				location->key);
			continue;
		}
		*/
		if ( NULL != location->key &&
		     NULL == location->content ) {
			LogDebug(COMPONENT_FSAL,
				"skipped clean part: %s",
				location->key);
			continue;
		}
		char *part = gsh_malloc(location->size);
		ret = sproxyd_read(export, myself,
				   location->start, location->size, part);
		if (ret < 0 ) {
			LogCrit(COMPONENT_FSAL,
				"invalid read on %s flush",
				myself->object);
			gsh_free(part);
			return -1;
		}

		char *new_key = sproxyd_new_key();
		LogDebug(COMPONENT_FSAL,
			 "sproxyd put(%s,...,%zu): begin",
			 new_key, location->size);
		ret = sproxyd_put(export, new_key,
				  part, location->size);
		gsh_free(part);
		LogDebug(COMPONENT_FSAL,
			 "sproxyd put(%s,...,%zu): end",
			 new_key, location->size);
		if ( ret < 0 ) {
			LogCrit(COMPONENT_FSAL, "Failed to put in sproxyd");
			gsh_free(new_key);
			return -1;
		}

		scality_add_to_free_list(&myself->delete_on_rollback,
					 gsh_strdup(new_key));
		if ( NULL != location->key ) {
			scality_add_to_free_list(&myself->delete_on_commit,
						 location->key);
		}
		location->key = new_key;

		//if ( node != avltree_last(&myself->locations)) {
		if (location->content) {
			gsh_free(location->content);
			gsh_free(location->stencil);
			location->content = NULL;
			location->stencil = NULL;
			myself->memory_used -= 2 * location->buffer_size;
			location->buffer_size = 0;
		}
		//}
	}
	LogDebug(COMPONENT_FSAL, "flush end");
	return 0;
}

static struct scality_location *
provision_part(struct scality_fsal_obj_handle *myself,
	       uint64_t offset, size_t buffer_size);

fsal_status_t
scality_truncate(struct scality_fsal_obj_handle *myself,
		 size_t filesize)
{
	struct avltree_node *node;
	if ( filesize > myself->attributes.filesize ) {
		struct scality_location *loc;
		loc = provision_part(myself, filesize, 0);
		if ( NULL == loc )
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	else if ( filesize < myself->attributes.filesize ) {
		for ( node = avltree_last(&myself->locations) ;
		      node != NULL ;
		      node = avltree_last(&myself->locations) ) {
			struct scality_location *loc =
				avltree_container_of(node,
						     struct scality_location,
						     avltree_node);
			if ( filesize < loc->start ) {
				avltree_remove(node, &myself->locations);
				myself->memory_used -= 2 * loc->buffer_size;
				if ( NULL != loc->key ) {
					scality_add_to_free_list(&myself->delete_on_commit,
								 gsh_strdup(loc->key));
				}
				scality_location_free(loc);
				--myself->n_locations;
			}
			else if ( filesize < loc->start  + loc->size ) {
				loc->size = filesize - loc->start;
				break;
			}
			else {
				break;
			}
			myself->attributes.spaceused = filesize;
			myself->attributes.filesize = filesize;
		}
	}
	myself->state = SCALITY_FSAL_OBJ_STATE_DIRTY;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static void
delete_parts(struct scality_fsal_export *export,
	     struct scality_fsal_obj_handle *myself)
{
	int ret;
	struct avltree_node *node;
	for ( node = avltree_first(&myself->locations) ;
	      node != NULL ;
	      node = avltree_next(node) ) {
		struct scality_location *location;
		location = avltree_container_of(node,
						struct scality_location,
						avltree_node);
		ret = sproxyd_delete(export,
				     location->key);
		if ( ret < 0 ) {
			LogWarn(COMPONENT_FSAL,
				"Unable to delete key %s from %s",
				location->key, myself->object);
		}
	}
}

void
scality_sanity_check_parts(struct scality_fsal_export *export,
			   struct scality_fsal_obj_handle *myself)
{
#if defined(SANITY_CHECK)
	struct avltree_node *node;
	for ( node = avltree_first(&myself->locations) ;
	      node != NULL ;
	      node = avltree_next(node) ) {
		struct scality_location *loc;
		loc = avltree_container_of(node,
					   struct scality_location,
					   avltree_node);
		assert(NULL != loc->key);
		size_t len;
		int ret;
		ret = sproxyd_head(export, loc->key, &len);
		if ( ret < 0 ) {
			LogCrit(COMPONENT_FSAL,
				"Saving corrupted data: %s, missing part: %s",
				myself->object, loc->key);
			assert(!"Saving corrupted data");
		}
	}
#endif
}

static void
sanity_check_not_ref(struct scality_fsal_obj_handle *myself,
		     const char *id)
{
#if defined(SANITY_CHECK)
	struct avltree_node *node;
	for ( node = avltree_first(&myself->locations) ;
	      node != NULL ;
	      node = avltree_next(node) ) {
		struct scality_location *loc;
		loc = avltree_container_of(node,
					   struct scality_location,
					   avltree_node);
		assert(NULL != loc->key);
		assert(0 != strcmp(id, loc->key));
	}
#endif
}

void
scality_cleanup(struct scality_fsal_export *export,
		struct scality_fsal_obj_handle *myself,
		enum scality_fsal_cleanup_flag cleanup_flag)
{
	struct scality_part *part, *next;
	if (cleanup_flag & SCALITY_FSAL_CLEANUP_PARTS) {
		delete_parts(export, myself);
	}
	for ( part = myself->delete_on_commit ;
	      part ;
	      part = next ) {
		next = part->next;
		if ( cleanup_flag & SCALITY_FSAL_CLEANUP_COMMIT ) {
			int ret;
			sanity_check_not_ref(myself, part->key);
			ret = sproxyd_delete(export, part->key);
			if ( ret < 0 ) {
				LogCrit(COMPONENT_FSAL,
					"Unable to delete %s", part->key);
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
			sanity_check_not_ref(myself, part->key);
			ret = sproxyd_delete(export, part->key);
			if ( ret < 0 ) {
				LogCrit(COMPONENT_FSAL,
					"Unable to delete %s", part->key);
			}
		}
		gsh_free(part->key);
		gsh_free(part);
	}
	myself->delete_on_rollback = NULL;
}

static void
parts_sanity_check(struct scality_fsal_obj_handle *myself)
{
	struct avltree_node *ln, *fn;
	fn = avltree_first(&myself->locations);
	ln = avltree_last(&myself->locations);
	if ( ln && fn && ln != fn ) {
		struct scality_location *ll, *fl;
		fl = avltree_container_of(fn,
					  struct
					  scality_location,
					  avltree_node);
		ll = avltree_container_of(ln,
					  struct
					  scality_location,
					  avltree_node);
		assert(ll->start > fl->start);
		struct avltree_node *prev_node;
		prev_node = avltree_prev(ln);
		if ( prev_node ) {
			struct scality_location *prev;
			prev = avltree_container_of(prev_node,
						    struct scality_location,
						    avltree_node);
			assert(prev->start + prev->size == ll->start );
		}
	}
}

/**
 * @brief provision parts from the end of the file up to the
 *        requested offset and size limited to the current part_size
 *
 * @param myself - object on which the provisioning apply
 * @param offset - offset in the file of the intended write
 * @param buffer_size - amount of data intended to be written
 * @return a part with valid starting point and size in order
 *        to be able to write at least one byte
 */
static struct scality_location *
provision_part(struct scality_fsal_obj_handle *myself,
	       uint64_t offset, size_t buffer_size)
{
	LogDebug(COMPONENT_FSAL, "size: %"PRIu64", offset: %"PRIu64,
		buffer_size, offset);
	/* Can we do something with the last node? */
	while (1) {
		uint64_t new_start = 0;
		struct avltree_node *last_node;
		struct scality_location *loc;
		parts_sanity_check(myself);
		last_node = avltree_last(&myself->locations);
		if ( NULL != last_node ) {
			loc = avltree_container_of(last_node,
						   struct scality_location,
						   avltree_node);

			LogDebug(COMPONENT_FSAL,
				 "found a part: start: %"PRIu64
				 " size: %"PRIu64,
				 loc->start, loc->size);
			/* this function was called or continue
			   for this reason */
			assert( offset >= loc->start + loc->size );

			/* is the offset outside this part and part growable */
			if ( offset >= loc->start + loc->size &&
			     myself->part_size > loc->size &&
			     ( offset + buffer_size > loc->start + loc->size ) ) {
				size_t bytes_added;
				size_t old_size = loc->size;
				bytes_added = myself->part_size - loc->size;
				if ( loc->start + loc->size + bytes_added >
				     offset + buffer_size ) {
					bytes_added -=  loc->start + loc->size
						+ bytes_added
						- offset - buffer_size;
				}
				if ( NULL == loc->stencil &&
				     0 != old_size ) {
					scality_location_alloc_content(myself,
									loc);
				}
				if ( NULL != loc->stencil ) {
					memset(loc->stencil + loc->size,
					       STENCIL_ZERO,
					       bytes_added);
				}
				loc->size += bytes_added;
				myself->attributes.filesize += bytes_added;
				myself->attributes.spaceused += bytes_added;
				LogDebug(COMPONENT_FSAL,
					"growing part to %"PRIu64,
					loc->size);
			}
			else {
				LogDebug(COMPONENT_FSAL,
					"Offset was not inside part or part not growable");
			}

			/* at this point part is at its max size regarding
			   both offset + buffer_size and default part_size */
			if ( offset >= loc->start &&
			     ( offset < loc->start + loc->size ||
			       offset + buffer_size <= loc->start + loc->size ) ) {
				LogDebug(COMPONENT_FSAL,
					 "This part has enough room,"
					 " return it");
				return loc;
			}
			/* not returned? fallthrough to add a new location */
			new_start = loc->start + loc->size;
		}
		else {
			LogDebug(COMPONENT_FSAL, "Will add first location");
		}
		LogDebug(COMPONENT_FSAL,
			 "New location at %"PRIu64, new_start);
		loc = scality_location_new(NULL,
					   new_start,
					   0);
		avltree_insert(&loc->avltree_node,
			       &myself->locations);
		++myself->n_locations;
		parts_sanity_check(myself);
	}
}

/**
 * @brief write in a regular file, taking care of the part boundaries
 *
 * @note short write is the norm
 *
 * @param myself - object to write to
 * @param buffer - points to the data to be written
 * @param offset - position in the file where the write happens
 * @param buffer_size - requested amount to write to the file
 *
 * @return the amount of data written
 */
static ssize_t
write_slice(struct scality_fsal_obj_handle *myself,
	    void *buffer,
	    uint64_t offset,
	    size_t buffer_size)
{
	LogDebug(COMPONENT_FSAL, "size: %"PRIu64", offset: %"PRIu64,
		 buffer_size, offset);
	ssize_t bytes_written;
	struct scality_location *loc;
	loc = scality_location_lookup(myself,
				      offset,
				      buffer_size);
	if (NULL == loc) {
		//outside of current file limits
		loc = provision_part(myself, offset, buffer_size);
		if ( NULL == loc ) {
			return -1;
		}
	}

	if ( NULL == loc->content ) {
		scality_location_alloc_content(myself, loc);
	}

	uint64_t offset_in_part = offset - loc->start;
	ssize_t overflow = (offset + buffer_size) - (loc->start + loc->size);
	bytes_written = buffer_size;
	if ( overflow > 0 )
		bytes_written -= overflow;
	memcpy(loc->content + offset_in_part, buffer, bytes_written);
	memset(loc->stencil + offset_in_part, STENCIL_COPY, bytes_written);

	return bytes_written;
}

/**
 * @brief write in a regular file
 *
 * @note this function tries to perform the whole write operation
 *
 * @param obj_hdl - object to write to
 * @param offset - position in the file where the write happens
 * @param buffer_size - requested amount to write to the file
 * @param buffer - points to the data to be written
 * @param[out] write_amount - amount of data written to the file
 * @param[out] fsal_stable - is the written data synchronized with storage
 *
 */
fsal_status_t scality_write(struct fsal_obj_handle *obj_hdl,
			   uint64_t offset,
			   size_t buffer_size, void *buffer,
			   size_t *write_amount, bool *fsal_stable)
{
	LogDebug(COMPONENT_FSAL, "write begin");
	fsal_errors_t error = ERR_FSAL_NO_ERROR;
	struct scality_fsal_obj_handle *myself;
	myself = container_of(obj_hdl,
			      struct scality_fsal_obj_handle,
			      obj_handle);
	scality_content_lock(myself);

	myself->state = SCALITY_FSAL_OBJ_STATE_DIRTY;
	LogDebug(COMPONENT_FSAL, "size: %"PRIu64", offset: %"PRIu64,
		 buffer_size, offset);
	*write_amount = 0;

	if ( 0 == buffer_size ) {
		goto out;
	}

	while ( buffer_size > 0 ) {
		ssize_t bytes_written;
		bytes_written = write_slice(myself,
					    buffer,
					    offset,
					    buffer_size);
		if ( bytes_written < 0 ) {
			LogCrit(COMPONENT_FSAL,
				"Write slice failed offset: %"PRIu64","
				" buffer_size: %"PRIu64,
				offset, buffer_size);
			error = ERR_FSAL_SERVERFAULT;
			goto out;
		}
		offset += bytes_written;
		buffer += bytes_written;
		buffer_size -= bytes_written;
		*write_amount += bytes_written;
	}
	if ( myself->memory_used > FLUSH_THRESHOLD &&
	     myself->memory_used > myself->part_size * 4 ) {
		/* must at least permit 2 parts on hold in memory.
		 * the '4' multiplier takes the stencil buffer
		 * into account */
		struct scality_fsal_export *export;
		export = container_of(op_ctx->fsal_export,
				      struct scality_fsal_export,
				      export);
		int ret = flush_content(export, myself);
		if ( ret < 0 ) {
			LogCrit(COMPONENT_FSAL,
				"Failed to flush content");
			error = ERR_FSAL_SERVERFAULT;
			goto out;
		}
		if ( NULL != fsal_stable )
			*fsal_stable = true;
	}
	else if ( NULL != fsal_stable ) {
		*fsal_stable = false;
	}


	error = ERR_FSAL_NO_ERROR;
 out:
	scality_content_unlock(myself);
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
	scality_content_lock(myself);

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
		myself->state = SCALITY_FSAL_OBJ_STATE_CLEAN;
		ret = dbd_post(export, myself);
		if ( ret < 0 ) {
			LogCrit(COMPONENT_FSAL,
				 "Unable to setattr(%s)", myself->object);
			error = ERR_FSAL_SERVERFAULT;
			goto out;
		}
		scality_cleanup(export, myself, SCALITY_FSAL_CLEANUP_COMMIT);
	}

	error = ERR_FSAL_NO_ERROR;
 out:
	scality_content_unlock(myself);
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

/* scality_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t scality_close(struct fsal_obj_handle *obj_hdl)
{
	struct scality_fsal_obj_handle *myself = container_of(obj_hdl,
							      struct scality_fsal_obj_handle,
							      obj_handle);
	myself->openflags = FSAL_O_CLOSED;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
