/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Copyright (c) 2010-2017 Red Hat, Inc. and/or its affiliates.
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * -------------
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "gsh_list.h"
#include "abstract_mem.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "nfs_rpc_callback.h"
#include "nfs_rpc_callback_simulator.h"
#include "sal_functions.h"
#include "gsh_dbus.h"

/**
 * @file nfs_rpc_callback_simulator.c
 * @author Matt Benjamin <matt@linuxbox.com>
 * @author Lee Dobryden <lee@linuxbox.com>
 * @brief RPC callback dispatch package
 *
 * This module implements a stocastic dispatcher for callbacks, which
 * works by traversing the list of connected clients and, dispatching
 * a callback at random in consideration of state.
 *
 * This concept is inspired by the upcall simulator, though
 * necessarily less fully satisfactory until delegation and layout
 * state are available.
 */

/**
 * @brief Return a timestamped list of NFSv4 client ids.
 *
 * For all NFSv4 clients, a clientid reliably indicates a callback
 * channel
 *
 * @param args    (not used)
 * @param reply   the message reply
 */

static bool nfs_rpc_cbsim_get_v40_client_ids(DBusMessageIter *args,
					     DBusMessage *reply,
					     DBusError *error)
{
	hash_table_t *ht = ht_confirmed_client_id;
	struct rbt_head *head_rbt;
	struct hash_data *pdata = NULL;
	struct rbt_node *pn;
	nfs_client_id_t *pclientid;
	uint64_t clientid;
	DBusMessageIter iter, sub_iter;
	struct timespec ts;
	uint32_t i;

	/* create a reply from the message */
	now(&ts);
	dbus_message_iter_init_append(reply, &iter);
	dbus_append_timestamp(&iter, &ts);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					 DBUS_TYPE_UINT64_AS_STRING, &sub_iter);
	/* For each bucket of the hashtable */
	for (i = 0; i < ht->parameter.index_size; i++) {
		head_rbt = &(ht->partitions[i].rbt);

		/* acquire mutex */
		PTHREAD_RWLOCK_wrlock(&(ht->partitions[i].lock));

		/* go through all entries in the red-black-tree */
		RBT_LOOP(head_rbt, pn) {
			pdata = RBT_OPAQ(pn);
			pclientid = pdata->val.addr;
			clientid = pclientid->cid_clientid;
			dbus_message_iter_append_basic(&sub_iter,
						       DBUS_TYPE_UINT64,
						       &clientid);
			RBT_INCREMENT(pn);
		}
		PTHREAD_RWLOCK_unlock(&(ht->partitions[i].lock));
	}
	dbus_message_iter_close_container(&iter, &sub_iter);
	return true;
}

/* DBUS get_client_ids method descriptor
 */

static struct gsh_dbus_method cbsim_get_client_ids = {
	.name = "get_client_ids",
	.method = nfs_rpc_cbsim_get_v40_client_ids,
	.args = {
		 {
		  .name = "time",
		  .type = "(tt)",
		  .direction = "out"},
		 {
		  .name = "clientids",
		  .type = "at",
		  .direction = "out"},
		 {NULL, NULL, NULL}
		 }
};

/**
 * @brief Return a timestamped list of session ids.
 *
 * @param args    (not used)
 * @param reply   the message reply
 */

static bool nfs_rpc_cbsim_get_session_ids(DBusMessageIter *args,
					  DBusMessage *reply,
					  DBusError *error)
{
	uint32_t i;
	hash_table_t *ht = ht_session_id;
	struct rbt_head *head_rbt;
	struct hash_data *pdata = NULL;
	struct rbt_node *pn;
	char session_id[2 * NFS4_SESSIONID_SIZE];	/* guaranteed to fit */
	nfs41_session_t *session_data;
	DBusMessageIter iter, sub_iter;
	struct timespec ts;

	/* create a reply from the message */
	now(&ts);
	dbus_message_iter_init_append(reply, &iter);
	dbus_append_timestamp(&iter, &ts);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					 DBUS_TYPE_UINT64_AS_STRING, &sub_iter);
	/* For each bucket of the hashtable */
	for (i = 0; i < ht->parameter.index_size; i++) {
		head_rbt = &(ht->partitions[i].rbt);

		/* acquire mutex */
		PTHREAD_RWLOCK_wrlock(&(ht->partitions[i].lock));

		/* go through all entries in the red-black-tree */
		RBT_LOOP(head_rbt, pn) {
			pdata = RBT_OPAQ(pn);
			session_data = pdata->val.addr;
			/* format */
			b64_ntop((unsigned char *)session_data->session_id,
				 NFS4_SESSIONID_SIZE, session_id,
				 (2 * NFS4_SESSIONID_SIZE));
			dbus_message_iter_append_basic(&sub_iter,
						       DBUS_TYPE_STRING,
						       &session_id);
			RBT_INCREMENT(pn);
		}
		PTHREAD_RWLOCK_unlock(&(ht->partitions[i].lock));
	}
	dbus_message_iter_close_container(&iter, &sub_iter);
	return true;
}

/* DBUS get_session_ids method descriptor
 */

static struct gsh_dbus_method cbsim_get_session_ids = {
	.name = "get_session_ids",
	.method = nfs_rpc_cbsim_get_session_ids,
	.args = {
		 {
		  .name = "time",
		  .type = "(tt)",
		  .direction = "out"},
		 {
		  .name = "sessionids",
		  .type = "at",
		  .direction = "out"},
		 {NULL, NULL, NULL}
		 }
};

static int cbsim_test_bchan(clientid4 clientid)
{
	nfs_client_id_t *pclientid = NULL;
	int code;

	code = nfs_client_id_get_confirmed(clientid, &pclientid);
	if (code != CLIENT_ID_SUCCESS) {
		LogCrit(COMPONENT_NFS_CB,
			"No clid record for %" PRIx64 " (%d) code %d", clientid,
			(int32_t) clientid, code);
		return EINVAL;
	}

	nfs_test_cb_chan(pclientid);

	return code;
}

/**
 * Demonstration callback invocation.
 */
static void cbsim_free_compound(nfs4_compound_t *cbt) __attribute__ ((unused));

static void cbsim_free_compound(nfs4_compound_t *cbt)
{
	int ix;
	nfs_cb_argop4 *argop = NULL;

	for (ix = 0; ix < cbt->v_u.v4.args.argarray.argarray_len; ++ix) {
		argop = cbt->v_u.v4.args.argarray.argarray_val + ix;
		if (argop) {
			CB_RECALL4args *opcbrecall =
				&argop->nfs_cb_argop4_u.opcbrecall;

			switch (argop->argop) {
			case NFS4_OP_CB_RECALL:
				gsh_free(opcbrecall->fh.nfs_fh4_val);
				break;
			default:
				/* TODO:  ahem */
				break;
			}
		}
	}

	/* XXX general free (move ?) */
	cb_compound_free(cbt);
}

static void cbsim_completion_func(rpc_call_t *call)
{
	LogDebug(COMPONENT_NFS_CB, "%p %s", call,
		 !(call->states & NFS_CB_CALL_ABORTED) ? "Success" : "Failed");
	if (!(call->states & NFS_CB_CALL_ABORTED)) {
		/* potentially, do something more interesting here */
		LogMidDebug(COMPONENT_NFS_CB, "call result: %d",
			    call->call_req.cc_error.re_status);
	} else {
		LogDebug(COMPONENT_NFS_CB,
			 "Aborted: %d",
			 call->call_req.cc_error.re_status);
	}
}

static int cbsim_fake_cbrecall(clientid4 clientid)
{
	nfs_client_id_t *pclientid = NULL;
	rpc_call_channel_t *chan = NULL;
	nfs_cb_argop4 argop[1];
	rpc_call_t *call;
	int code;

	LogDebug(COMPONENT_NFS_CB, "called with clientid %" PRIx64, clientid);

	code = nfs_client_id_get_confirmed(clientid, &pclientid);
	if (code != CLIENT_ID_SUCCESS) {
		LogCrit(COMPONENT_NFS_CB,
			"No clid record for %" PRIx64 " (%d) code %d", clientid,
			(int32_t) clientid, code);
		code = EINVAL;
		goto out;
	}

	assert(pclientid);

	chan = nfs_rpc_get_chan(pclientid, NFS_RPC_FLAG_NONE);
	if (!chan) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed");
		goto out;
	}

	if (!chan->clnt) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed (no clnt)");
		goto out;
	}

	if (!chan->auth) {
		LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed (no auth)");
		goto out;
	}

	/* allocate a new call--freed in completion hook */
	call = alloc_rpc_call();

	call->chan = chan;

	/* setup a compound */
	cb_compound_init_v4(&call->cbt, 6, 0,
			    pclientid->cid_cb.v40.cb_callback_ident,
			    "brrring!!!", 10);

	/* TODO: api-ify */
	memset(argop, 0, sizeof(nfs_cb_argop4));
	argop->argop = NFS4_OP_CB_RECALL;
	argop->nfs_cb_argop4_u.opcbrecall.stateid.seqid = 0xdeadbeef;
	strlcpy(argop->nfs_cb_argop4_u.opcbrecall.stateid.other, "0xdeadbeef",
		12);
	argop->nfs_cb_argop4_u.opcbrecall.truncate = TRUE;
	argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_len = 11;
	/* leaks, sorry */
	argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val =
	    gsh_strdup("0xabadcafe");

	/* add ops, till finished (dont exceed count) */
	cb_compound_add_op(&call->cbt, argop);

	/* set completion hook */
	call->call_hook = cbsim_completion_func;
	call->call_arg = NULL;

	/* call it (here, in current thread context) */
	code = nfs_rpc_call(call, NFS_RPC_CALL_NONE);
	if (code)
		free_rpc_call(call);

 out:
	return code;
}

/**
 * @brief Fake/force a recall of a client id.
 *
 * For all NFSv4 clients, a clientid reliably indicates a callback
 * channel
 *
 * @param args    the client id to be recalled
 * @param reply   the message reply (empty)
 */

static bool nfs_rpc_cbsim_fake_recall(DBusMessageIter *args,
				      DBusMessage *reply,
				      DBusError *error)
{
	clientid4 clientid = 9315;	/* XXX ew! */

	LogDebug(COMPONENT_NFS_CB, "called!");

	/* read the arguments */
	if (args == NULL) {
		LogDebug(COMPONENT_DBUS, "message has no arguments");
	} else if (dbus_message_iter_get_arg_type(args) != DBUS_TYPE_UINT64) {
		LogDebug(COMPONENT_DBUS, "arg not uint64");
	} else {
		dbus_message_iter_get_basic(args, &clientid);
		LogDebug(COMPONENT_DBUS, "param: %" PRIx64, clientid);
	}

	cbsim_test_bchan(clientid);
	cbsim_fake_cbrecall(clientid);

	return true;
}

/* DBUS fake_recall method descriptor
 */

static struct gsh_dbus_method cbsim_fake_recall = {
	.name = "fake_recall",
	.method = nfs_rpc_cbsim_fake_recall,
	.args = {
		 {
		  .name = "clientid",
		  .type = "t",
		  .direction = "in"},
		 {NULL, NULL, NULL}
		 }
};

/* DBUS org.ganesha.nfsd.cbsim methods list
 */

static struct gsh_dbus_method *cbsim_methods[] = {
	&cbsim_get_client_ids,
	&cbsim_get_session_ids,
	&cbsim_fake_recall,
	NULL
};

static struct gsh_dbus_interface cbsim_interface = {
	.name = "org.ganesha.nfsd.cbsim",
	.props = NULL,
	.methods = cbsim_methods,
	.signals = NULL
};

/* DBUS list of interfaces on /org/ganesha/nfsd/CBSIM
 */

static struct gsh_dbus_interface *cbsim_interfaces[] = {
	&cbsim_interface,
	NULL
};

/**
 * @brief Initialize subsystem
 */
void nfs_rpc_cbsim_pkginit(void)
{
	gsh_dbus_register_path("CBSIM", cbsim_interfaces);
	LogEvent(COMPONENT_NFS_CB, "Callback Simulator Initialized");
}

/**
 * @brief Shutdown subsystem
 */
void nfs_rpc_cbsim_pkgshutdown(void)
{
	/* return */
}
