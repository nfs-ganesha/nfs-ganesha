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


/* Exports are stored in an AVL tree
 */

struct export_by_id {
	struct avltree t;
	pthread_rwlock_t lock;
};

static struct export_by_id export_by_id;

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
	if(lk->export_id != rk->export_id)
		return (lk->export_id < rk->export_id) ? -1 : 1;
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
 * @return pointer to ref locked stats block
 */

struct gsh_export *get_gsh_export(int export_id,
				  bool lookup_only)
{
	struct avltree_node *node = NULL;
	struct gsh_export *exp;
	struct export_stats *export_st;
	struct gsh_export v;

/* NOTE: If we call this in the general case, not from within stats
 * code we have to do the following.  We currently get away with it 
 * because the stats code has already done this and passed the export id
 * it found to the stats harvesting functions.  This table has a 1 - 1
 * relationship to an exportlist entry but no linkage because exportlist
 * is a candidate for rework when the pseudo fs "fsal" is done.
 *  Don't muddy the waters right now.
 */

/* 	pexport = nfs_Get_export_by_id(nfs_param.pexportlist, */
/* 					exportid)) == NULL) */
	v.export_id = export_id;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);
	node = avltree_lookup(&v.node_k, &export_by_id.t);
	if(node) {
		exp = avltree_container_of(node, struct gsh_export, node_k);
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
	exp->export_id = export_id;
	exp->refcnt = 0;  /* we will hold a ref starting out... */

	PTHREAD_RWLOCK_wrlock(&export_by_id.lock);
	node = avltree_insert(&exp->node_k, &export_by_id.t);
	if(node) {
		gsh_free(export_st); /* somebody beat us to it */
		exp = avltree_container_of(node, struct gsh_export, node_k);
	} else {
		pthread_mutex_init(&exp->lock, NULL);
	}

out:
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
 * @ Walk the tree and do the callback on each node
 *
 * @param cb    [IN] Callback function
 * @param state [IN] param block to pass
 */

int foreach_gsh_export(bool (*cb)(struct gsh_export *cl,
				  void *state),
		       void *state)
{
	struct avltree_node *export_node;
	struct gsh_export *exp;
	int cnt = 0;

	PTHREAD_RWLOCK_rdlock(&export_by_id.lock);
	for(export_node = avltree_first(&export_by_id.t);
	    export_node != NULL;
	    export_node = avltree_next(export_node)) {
		exp = avltree_container_of(export_node, struct gsh_export, node_k);
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
	exportlist_t *pexport;
	DBusMessageIter struct_iter;
	struct timespec last_as_ts = ServerBootTime;
	const char *path;

	exp = container_of(exp_node, struct export_stats, export);

/* NOTE: See note above about linkage between avl tree and exports list
 * we assume here that they are 1 - 1 and dont' check errors because of
 * this assumption.
 */
	pexport = nfs_Get_export_by_id(nfs_param.pexportlist,
				       exp_node->export_id);
	if(pexport == NULL)
		return false;
	path = pexport->pseudopath; /* is this the "right" one? */
	timespec_add_nsecs(exp_node->last_update, &last_as_ts);
	dbus_message_iter_open_container(&iter_state->export_iter,
					 DBUS_TYPE_STRUCT,
					 NULL,
					 &struct_iter);
	dbus_message_iter_append_basic(&struct_iter,
				       DBUS_TYPE_INT32,
				       &pexport->id);
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

static struct gsh_dbus_method *export_stats_methods[] ={
	&export_show_v3_io,
	&export_show_v40_io,
	&export_show_v41_io,
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

#endif /* USE_DBUS_STATS */

/**
 * @brief Initialize export manager
 */

void gsh_export_init(void)
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
#ifdef USE_DBUS_STATS
	gsh_dbus_register_path("ExportMgr", export_interfaces);
#endif
}


/** @} */
