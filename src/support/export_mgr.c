/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2013
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @defgroup Filesystem export management
 * @{
 */

/**
 * @file export_mgr.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief export manager
 */

#include "config.h"

#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/param.h>
#include <pthread.h>
#include <assert.h>
#include <arpa/inet.h>
#include "nlm_list.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "avltree.h"
#include "ganesha_types.h"
#ifdef USE_DBUS_STATS
#include "ganesha_dbus.h"
#endif
#include "export_mgr.h"
#include "client_mgr.h"
#include "server_stats_private.h"
#include "server_stats.h"
#include "abstract_atomic.h"
#include "gsh_intrinsic.h"
#include "nfs_tools.h"


/**
 * @brief Exports are stored in an AVL tree with front-end cache.
 *
 */
struct export_by_id
{
	pthread_rwlock_t lock;
        struct avltree t;
        struct avltree_node **cache;
	uint32_t cache_sz;
};


static struct export_by_id export_by_id;

static struct glist_head exportlist;

/**
 * @brief Compute cache slot for an entry
 *
 * This function computes a hash slot, taking an address modulo the
 * number of cache slotes (which should be prime).
 *
 * @param wt [in] The table
 * @param ptr [in] Entry address
 *
 * @return The computed offset.
 */
static inline uint32_t
eid_cache_offsetof(struct export_by_id *eid, uint64_t k)
{
    return (k % eid->cache_sz);
}

/* XXX GCC header include issue (abstract_atomic.h IS included, but
 * atomic_fetch_voidptr fails when used from cache_inode_remove.c. */

/**
 * @brief Atomically fetch a void *
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void *
atomic_fetch_voidptr(void **var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void *
atomic_fetch_voidptr(void **var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store a void *
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_voidptr(void **var, void *val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_voidptr(void **var, void *val)
{
     (void)__sync_lock_test_and_set(var, 0);
}
#endif

/**
 * @brief Export id comparator for AVL tree walk
 *
 */
static int
export_id_cmpf(const struct avltree_node *lhs,
	       const struct avltree_node *rhs)
{
	struct gsh_export *lk, *rk;

	lk = avltree_container_of(lhs, struct gsh_export, node_k);
	rk = avltree_container_of(rhs, struct gsh_export, node_k);
	if(lk->export.id != rk->export.id)
		return (lk->export.id < rk->export.id) ? -1 : 1;
	else
		return 0;
}

/**
 * @brief Lookup the export manager struct for this export id
 *
 * Lookup the export manager struct by export id.
 * Export ids are assigned by the config file and carried about
 * by file handles.
 *
 * @param export_id   [IN] the export id extracted from the handle
 * @param lookup_only [IN] if true, don't create a new entry
 *
 * @return pointer to ref locked export
 */
struct gsh_export *
get_gsh_export(int export_id, bool lookup_only)
{
	struct avltree_node *node = NULL;
	struct gsh_export *exp;
	struct export_stats *export_st;
	struct gsh_export v;
        void **cache_slot;

	v.export.id = export_id;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);

        /* check cache */
        cache_slot = (void **)
            &(export_by_id.cache[eid_cache_offsetof(&export_by_id, export_id)]);
        node = (struct avltree_node *) atomic_fetch_voidptr(cache_slot);
        if (node) {
            if (export_id_cmpf(&v.node_k, node) == 0) {
                /* got it in 1 */
		LogDebug(COMPONENT_HASHTABLE_CACHE,
                         "export_mgr cache hit slot %d\n",
                         eid_cache_offsetof(&export_by_id, export_id));
		exp =  avltree_container_of(node, struct gsh_export, node_k);
		if(exp->state == EXPORT_READY)
			goto out;
            }
        }

	/* fall back to AVL */
	node = avltree_lookup(&v.node_k, &export_by_id.t);
	if(node) {
		exp = avltree_container_of(node, struct gsh_export, node_k);
		if(exp->state != EXPORT_READY) {
			PTHREAD_RWLOCK_unlock(&export_by_id.lock);
			return NULL;
		}
                /* update cache */
		atomic_store_voidptr(cache_slot, node);
		goto out;
	} else if(lookup_only) {
		PTHREAD_RWLOCK_unlock(&export_by_id.lock);
		return NULL;
	}
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);

	export_st = gsh_calloc(sizeof(struct export_stats), 1);
	if(export_st == NULL) {
		return NULL;
	}
	exp = &export_st->export;
	exp->export.id = export_id;
	exp->refcnt = 0;  /* we will hold a ref starting out... */

	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);
	node = avltree_insert(&exp->node_k, &export_by_id.t);
	if(node) {
		gsh_free(export_st); /* somebody beat us to it */
		exp = avltree_container_of(node, struct gsh_export, node_k);
	} else {
		pthread_mutex_init(&exp->lock, NULL);
                /* update cache */
		atomic_store_voidptr(cache_slot, &exp->node_k);
		glist_add_tail(&exportlist, &exp->export.exp_list);
	}

out:
	atomic_inc_int64_t(&exp->refcnt);
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return exp;
}

/**
 * @brief Set export entry's state
 *
 * Set the state under the global write lock to keep it safe
 * from scan/lookup races.
 * We assert state transitions because errors here are BAD.
 *
 * @param export [IN] The export to change state
 * @param state  [IN} the state to set
 */

void set_gsh_export_state(struct gsh_export *export,
			  export_state_t state)
{
	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);
	if(state == EXPORT_READY) {
		assert(export->state == EXPORT_INIT ||
		       export->state == EXPORT_BLOCKED);
	} else if(state == EXPORT_BLOCKED) {
		assert(export->state == EXPORT_READY);
	} else if(state == EXPORT_RELEASE) {
		assert(export->state == EXPORT_BLOCKED &&
		       export->refcnt == 0);
	} else {
		assert(0);
	}
	export->state = state;
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
}

/**
 * @brief Lookup the export manager struct by export path
 *
 * Gets an export entry from its path using a substring match and
 * linear search of the export list. 
 * If path has a trailing '/', ignor it.
 *
 * @param path       [IN] the path for the entry to be found.
 *
 * @return pointer to ref locked stats block
 */

struct gsh_export *
get_gsh_export_by_path(char *path)
{
	struct gsh_export *exp;
	exportlist_t *export = NULL;
	struct glist_head * glist;
	int len_path = strlen(path);
	int len_export;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);
	if(path[len_path - 1] == '/')
		len_path--;
	glist_for_each(glist, &exportlist) {
		export = glist_entry(glist, exportlist_t, exp_list);
		exp = container_of(export, struct gsh_export, export);
		if(exp->state != EXPORT_READY)
			continue;
		len_export = strlen(export->fullpath);
		/* a path shorter than the full path cannot match
		 */
		if(len_path < len_export)
			continue;
		/* if the char in fullpath just after the end of path is not '/'
		 * it is a name token longer, i.e. /mnt/foo != /mnt/foob/
		 */
		if(export->fullpath[len_path] != '/' &&
		   export->fullpath[len_path] != '\0')
			continue;
		/* we agree on size, now compare the leading substring
		 */
		if( !strncmp(export->fullpath, path, len_path))
			goto out;
	}
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return NULL;

out:
	atomic_inc_int64_t(&exp->refcnt);
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return exp;
}

/**
 * @brief Lookup the export manager struct by export pseudo path
 *
 * Gets an export entry from its pseudo (if it exists)
 *
 * @param path       [IN] the path for the entry to be found.
 *
 * @return pointer to ref locked export
 */

struct gsh_export *
get_gsh_export_by_pseudo(char *path)
{
	struct gsh_export *exp;
	exportlist_t *export = NULL;
	struct glist_head * glist;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);
	glist_for_each(glist, &exportlist) {
		export = glist_entry(glist, exportlist_t, exp_list);
		exp = container_of(export, struct gsh_export, export);
		if(exp->state != EXPORT_READY)
			continue;
		if(export->pseudopath != NULL &&
		   !strcmp(export->pseudopath, path))
			goto out;
	}
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return NULL;

out:
	atomic_inc_int64_t(&exp->refcnt);
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return exp;
}

/**
 * @brief Lookup the export manager struct by export tag
 *
 * Gets an export entry from its pseudo (if it exists)
 *
 * @param path       [IN] the path for the entry to be found.
 *
 * @return pointer to ref locked export
 */

struct gsh_export *
get_gsh_export_by_tag(char *tag)
{
	struct gsh_export *exp;
	exportlist_t *export = NULL;
	struct glist_head * glist;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);
	glist_for_each(glist, &exportlist) {
		export = glist_entry(glist, exportlist_t, exp_list);
		exp = container_of(export, struct gsh_export, export);
		if(exp->state != EXPORT_READY)
			continue;
		if(export->FS_tag != NULL &&
		   !strcmp(export->FS_tag, tag))
			goto out;
	}
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return NULL;

out:
	exp = container_of(export, struct gsh_export, export);
	atomic_inc_int64_t(&exp->refcnt);
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return exp;
}

/**
 * @brief Release the export management struct
 *
 * We are done with it, let it go.
 */

void put_gsh_export(struct gsh_export *export)
{
	assert(export->refcnt > 0);
	atomic_dec_int64_t(&export->refcnt);
}

/**
 * @brief Remove the export management struct
 *
 * Remove it from the AVL tree.
 */

bool remove_gsh_export(int export_id)
{
	struct avltree_node *node = NULL;
	struct avltree_node *cnode = NULL;
	struct gsh_export *exp = NULL;
	exportlist_t *export = NULL;
	struct export_stats *export_st;
	struct gsh_export v;
        void **cache_slot;
	bool removed = true;

	v.export.id = export_id;

	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);
	node = avltree_lookup(&v.node_k, &export_by_id.t);
	if(node) {
		exp = avltree_container_of(node, struct gsh_export, node_k);
		if(exp->state != EXPORT_RELEASE || exp->refcnt > 0) {
			removed = false;
			goto out;
		}
		cache_slot = (void **)
			&(export_by_id.cache[eid_cache_offsetof(&export_by_id, export_id)]);
		cnode = (struct avltree_node *) atomic_fetch_voidptr(cache_slot);
		if(node == cnode)
			atomic_store_voidptr(cache_slot, NULL);
		avltree_remove(node, &export_by_id.t);
		export = &exp->export;
		glist_del(&export->exp_list);
	}
out:
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	if(removed && node) {
		free_export_resources(export);
		export_st = container_of(exp, struct export_stats, export);
		server_stats_free(&export_st->st);
		gsh_free(export_st);
	}
	return removed;
}

/**
 * @ Walk the tree and do the callback on each node
 *
 * @param cb    [IN] Callback function
 * @param state [IN] param block to pass
 */

int foreach_gsh_export(bool (*cb)(struct gsh_export *exp,
				  void *state),
		       void *state)
{
	struct glist_head * glist;
	struct gsh_export *exp;
	exportlist_t *export;
	int cnt = 0;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);
	glist_for_each(glist, &exportlist) {
		export = glist_entry(glist, exportlist_t, exp_list);
		exp = container_of(export, struct gsh_export, export);
		if( !cb(exp, state))
			break;
		cnt++;
	}
	PTHREAD_RWLOCK_unlock(&export_by_id.lock);
	return cnt;
}

#ifdef USE_DBUS_STATS

/* DBUS interfaces
 */

struct showexports_state {
	DBusMessageIter export_iter;
};

static bool export_to_dbus(struct gsh_export *exp_node,
			   void *state)
{
	struct showexports_state *iter_state
		= (struct showexports_state *)state;
	struct export_stats *exp;
	DBusMessageIter struct_iter;
	struct timespec last_as_ts = ServerBootTime;
	const char *path;

	exp = container_of(exp_node, struct export_stats, export);
	path = (exp_node->export.pseudopath != NULL) ?
		exp_node->export.pseudopath :
		exp_node->export.fullpath;
	timespec_add_nsecs(exp_node->last_update, &last_as_ts);
	dbus_message_iter_open_container(&iter_state->export_iter,
					 DBUS_TYPE_STRUCT,
					 NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter,
				       DBUS_TYPE_INT32,
				       &exp_node->export.id);
	dbus_message_iter_append_basic(&struct_iter,
				       DBUS_TYPE_STRING,
				       &path);
	server_stats_summary(&struct_iter, &exp->st);
	dbus_append_timestamp(&struct_iter, &last_as_ts);
	dbus_message_iter_close_container(&iter_state->export_iter,
					  &struct_iter);
	return true;
}

static bool
gsh_export_showexports(DBusMessageIter *args,
			      DBusMessage *reply)
{
	DBusMessageIter iter;
	struct showexports_state iter_state;
	struct timespec timestamp;

	now(&timestamp);
	/* create a reply from the message */
	dbus_message_iter_init_append(reply, &iter);
	dbus_append_timestamp(&iter, &timestamp);
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					 "(isbbbbbbb(tt))",
					 &iter_state.export_iter);
	
	(void) foreach_gsh_export(export_to_dbus, (void *)&iter_state);

	dbus_message_iter_close_container(&iter, &iter_state.export_iter);
	return true;
}

static struct gsh_dbus_method export_show_exports = {
	.name = "ShowExports",
	.method = gsh_export_showexports,
	.args = { TIMESTAMP_REPLY,
		{
			.name = "exports",
			.type = "a(isbbbbbbb(tt))",
			.direction = "out"
		},
		  END_ARG_LIST
	}
};

static struct gsh_dbus_method *export_mgr_methods[] = {
	&export_show_exports,
	NULL
};
	
/* org.ganesha.nfsd.exportmgr interface
 */
static struct gsh_dbus_interface export_mgr_table = {
	.name = "org.ganesha.nfsd.exportmgr",
	.props = NULL,
	.methods = export_mgr_methods,
	.signals = NULL
};


/* org.ganesha.nfsd.exportstats interface
 */

/* parse the ipaddr string in args
 */

static bool arg_export_id(DBusMessageIter *args,
		       int32_t *export_id,
		       char **errormsg
	)
{
	bool success = true;

	if (args == NULL) {
		success = false;
		*errormsg = "message has no arguments";
	} else if (DBUS_TYPE_INT32 !=
		   dbus_message_iter_get_arg_type(args)) {
		success = false;
		*errormsg = "arg not a 32 bit integer";
	} else {
		dbus_message_iter_get_basic(args, export_id);
	}
	return success;
}

/* DBUS client manager stats helpers
 */

static struct gsh_export *lookup_export(DBusMessageIter *args,
					char **errormsg)
{
	int32_t export_id;
	struct gsh_export *export = NULL;
	bool success = true;

	success = arg_export_id(args, &export_id, errormsg);
	if(success) {
		export = get_gsh_export(export_id, true);
		if(export == NULL)
			*errormsg = "Export id not found";
	}
	return export;
}
		   
/**
 * DBUS method to report NFSv3 I/O statistics
 *
 */

static bool
get_nfsv3_export_io(DBusMessageIter *args,
		   DBusMessage *reply)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if(export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if(export_st->st.nfsv3 == NULL) {
			success = false;
			errormsg = "Export does not have any NFSv3 activity";
		}
	}
	dbus_status_reply(&iter, success, errormsg);
	if(success)
		server_dbus_v3_iostats(export_st->st.nfsv3, &iter);

	if(export != NULL)
		put_gsh_export(export);
	return true;
}

static struct gsh_dbus_method export_show_v3_io = {
	.name = "GetNFSv3IO",
	.method = get_nfsv3_export_io,
	.args = { EXPORT_ID_ARG,
		  STATUS_REPLY,
		  TIMESTAMP_REPLY,
		  IOSTATS_REPLY,
		  END_ARG_LIST
	}
};


/**
 * DBUS method to report 9p I/O statistics
 *
 */
static bool
get_9p_export_io(DBusMessageIter *args,
		 DBusMessage *reply)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if(export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if(export_st->st._9p == NULL) {
			success = false;
			errormsg = "Export does not have any 9p activity";
		}
	}
	dbus_status_reply(&iter, success, errormsg);
	if(success)
		server_dbus_9p_iostats(export_st->st._9p, &iter);

	if(export != NULL)
		put_gsh_export(export);
	return true;
}

static struct gsh_dbus_method export_show_9p_io = {
	.name = "Get9pIO",
	.method = get_9p_export_io,
	.args = { EXPORT_ID_ARG,
		  STATUS_REPLY,
		  TIMESTAMP_REPLY,
		  IOSTATS_REPLY,
		  END_ARG_LIST
	}
};

/**
 * DBUS method to report NFSv40 I/O statistics
 *
 */

static bool
get_nfsv40_export_io(DBusMessageIter *args,
		    DBusMessage *reply)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if(export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if(export_st->st.nfsv40 == NULL) {
			success = false;
			errormsg = "Export does not have any NFSv4.0 activity";
		}
	}
	dbus_status_reply(&iter, success, errormsg);
	if(success)
		server_dbus_v40_iostats(export_st->st.nfsv40, &iter);

	if(export != NULL)
		put_gsh_export(export);
	return true;
}

static struct gsh_dbus_method export_show_v40_io = {
	.name = "GetNFSv40IO",
	.method = get_nfsv40_export_io,
	.args = { EXPORT_ID_ARG,
		  STATUS_REPLY,
		  TIMESTAMP_REPLY,
		  IOSTATS_REPLY,
		  END_ARG_LIST
	}
};

/**
 * DBUS method to report NFSv41 I/O statistics
 *
 */

static bool
get_nfsv41_export_io(DBusMessageIter *args,
		    DBusMessage *reply)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if(export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if(export_st->st.nfsv41 == NULL) {
			success = false;
			errormsg = "Export does not have any NFSv4.1 activity";
		}
	}
	dbus_status_reply(&iter, success, errormsg);
	if(success)
		server_dbus_v41_iostats(export_st->st.nfsv41, &iter);

	if(export != NULL)
		put_gsh_export(export);
	return true;
}

static struct gsh_dbus_method export_show_v41_io = {
	.name = "GetNFSv41IO",
	.method = get_nfsv41_export_io,
	.args = { EXPORT_ID_ARG,
		  STATUS_REPLY,
		  TIMESTAMP_REPLY,
		  IOSTATS_REPLY,
		  END_ARG_LIST
	}
};

/**
 * DBUS method to report NFSv41 layout statistics
 *
 */

static bool
get_nfsv41_export_layouts(DBusMessageIter *args,
			  DBusMessage *reply)
{
	struct gsh_export *export = NULL;
	struct export_stats *export_st = NULL;
	bool success = true;
	char *errormsg = "OK";
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	export = lookup_export(args, &errormsg);
	if(export == NULL) {
		success = false;
	} else {
		export_st = container_of(export, struct export_stats, export);
		if(export_st->st.nfsv41 == NULL) {
			success = false;
			errormsg = "Export does not have any NFSv4.1 activity";
		}
	}
	dbus_status_reply(&iter, success, errormsg);
	if(success)
		server_dbus_v41_layouts(export_st->st.nfsv41, &iter);

	if(export != NULL)
		put_gsh_export(export);
	return true;
}

static struct gsh_dbus_method export_show_v41_layouts = {
	.name = "GetNFSv41Layouts",
	.method = get_nfsv41_export_layouts,
	.args = { EXPORT_ID_ARG,
		  STATUS_REPLY,
		  TIMESTAMP_REPLY,
		  LAYOUTS_REPLY,
		  END_ARG_LIST
	}
};

static struct gsh_dbus_method *export_stats_methods[] ={
	&export_show_v3_io,
	&export_show_v40_io,
	&export_show_v41_io,
	&export_show_v41_layouts,
        &export_show_9p_io,
	NULL
};

static struct gsh_dbus_interface export_stats_table = {
	.name = "org.ganesha.nfsd.exportstats",
	.methods = export_stats_methods
};

/* DBUS list of interfaces on /org/ganesha/nfsd/ExportMgr
 */

static struct gsh_dbus_interface *export_interfaces[] = {
	&export_mgr_table,
	&export_stats_table,
	NULL
};

void dbus_export_init(void)
{
	gsh_dbus_register_path("ExportMgr", export_interfaces);
}
#endif /* USE_DBUS_STATS */

/**
 * @brief Initialize export manager
 */

void export_pkginit(void)
{
	pthread_rwlockattr_t rwlock_attr;

	pthread_rwlockattr_init(&rwlock_attr);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(
		&rwlock_attr,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	pthread_rwlock_init(&export_by_id.lock, &rwlock_attr);
	avltree_init(&export_by_id.t, export_id_cmpf, 0);
	export_by_id.cache_sz = 255;
	export_by_id.cache = gsh_calloc(export_by_id.cache_sz,
					sizeof(struct avltree_node *));
	glist_init(&exportlist);
}

/** @} */
