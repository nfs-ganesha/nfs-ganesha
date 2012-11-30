/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * -------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "nlm_list.h"
#include "abstract_mem.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "nfs_rpc_callback.h"
#include "nfs_rpc_callback_simulator.h"
#include "sal_functions.h"
#include "ganesha_dbus.h"

/**
 *
 * \file nfs_rpc_callback_simulator.c
 * \author Matt Benjamin and Lee Dobryden
 * \brief RPC callback dispatch package
 *
 * \section DESCRIPTION
 *
 * This module implements a stocastic dispatcher for callbacks, which works
 * by traversing the list of connected clients and, dispatching a callback
 * at random in consideration of state.
 *
 * This concept is inspired by the upcall simulator, though necessarily less
 * fully satisfactory until delegation and layout state are available.
 *
 */

/* XML data to answer org.freedesktop.DBus.Introspectable.Introspect requests */
static const char* introspection_xml =
"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
"\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
"<node>\n"
"  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
"    <method name=\"Introspect\">\n"
"      <arg name=\"data\" direction=\"out\" type=\"s\"/>\n"
"    </method>\n"
"  </interface>\n"
"  <interface name=\"org.ganesha.nfsd.cbsim\">\n"
"    <method name=\"get_client_ids\">\n"
"      <arg name=\"clientids\" direction=\"out\" type=\"at\"/>\n"
"    </method>\n"
"    <method name=\"fake_recall\">\n"
"      <arg name=\"clientid\" direction=\"in\" type=\"t\"/>\n"
"    </method>\n"
"  </interface>\n"
"</node>\n"
;

extern hash_table_t *ht_confirmed_client_id;

static DBusHandlerResult
nfs_rpc_cbsim_get_client_ids(DBusConnection *conn, DBusMessage *msg,
                      void *user_data)
{
  DBusMessage* reply;
  static uint32_t i, serial = 1;
  hash_table_t *ht = ht_confirmed_client_id;
  struct rbt_head *head_rbt;
  hash_data_t *pdata = NULL;
  struct rbt_node *pn;
  nfs_client_id_t *pclientid;
  uint64_t clientid;
  DBusMessageIter iter, sub_iter;

  /* create a reply from the message */
  reply = dbus_message_new_method_return(msg);
  dbus_message_iter_init_append(reply, &iter);

  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                                   DBUS_TYPE_UINT64_AS_STRING, &sub_iter);
  /* For each bucket of the hashtable */
  for(i = 0; i < ht->parameter.index_size; i++) {
    head_rbt = &(ht->partitions[i].rbt);
    
    /* acquire mutex */
    PTHREAD_RWLOCK_WRLOCK(&(ht->partitions[i].lock));
    
    /* go through all entries in the red-black-tree*/
    RBT_LOOP(head_rbt, pn) {
      pdata = RBT_OPAQ(pn);
      pclientid =
	(nfs_client_id_t *)pdata->buffval.pdata;
      clientid = pclientid->cid_clientid;
      dbus_message_iter_append_basic(&sub_iter, DBUS_TYPE_UINT64, &clientid);
      RBT_INCREMENT(pn);      
    }
    PTHREAD_RWLOCK_UNLOCK(&(ht->partitions[i].lock));
  }
  dbus_message_iter_close_container(&iter, &sub_iter);
  /* send the reply && flush the connection */
  if (!dbus_connection_send(conn, reply, &serial)) {
      LogCrit(COMPONENT_DBUS, "reply failed");
  }
  dbus_connection_flush(conn);
  dbus_message_unref(reply);
  serial++;
  return (DBUS_HANDLER_RESULT_HANDLED);
}

static int32_t
cbsim_test_bchan(clientid4 clientid)
{
    int32_t tries, code = 0;
    nfs_client_id_t *pclientid = NULL;
    struct timeval CB_TIMEOUT = {15, 0};
    rpc_call_channel_t *chan;
    enum clnt_stat stat;

    code = nfs_client_id_get_confirmed(clientid, &pclientid);
    if (code != CLIENT_ID_SUCCESS) {
        LogCrit(COMPONENT_NFS_CB,
                "No clid record for %"PRIx64" (%d) code %d", clientid,
                (int32_t) clientid, code);
        code = EINVAL;
        goto out;
    }

    assert(pclientid);

    /* create (fix?) channel */
    for (tries = 0; tries < 2; ++tries) {

        chan = nfs_rpc_get_chan(pclientid, NFS_RPC_FLAG_NONE);
        if (! chan) {
            LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed");
            goto out;
        }

        if (! chan->clnt) {
            LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed (no clnt)");
            goto out;
        }

        /* try the CB_NULL proc -- inline here, should be ok-ish */
        stat = rpc_cb_null(chan, CB_TIMEOUT);
        LogDebug(COMPONENT_NFS_CB,
                 "rpc_cb_null on client %"PRIx64" returns %d",
                 clientid, stat);


        /* RPC_INTR indicates that we should refresh the channel
         * and retry */
        if (stat != RPC_INTR)
            break;
    }

out:
    return (code);
}

/*
 * Demonstration callback invocation.
 */
static void cbsim_free_compound(nfs4_compound_t *cbt)
{
    int ix;
    nfs_cb_argop4 *argop = NULL;

    for (ix = 0; ix < cbt->v_u.v4.args.argarray.argarray_len; ++ix) {
        argop = cbt->v_u.v4.args.argarray.argarray_val + ix;
        if (argop) {
            switch (argop->argop) {
            case  NFS4_OP_CB_RECALL:
                gsh_free(argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val);
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

static int32_t cbsim_completion_func(rpc_call_t* call, rpc_call_hook hook,
                                     void* arg, uint32_t flags)
{
    LogDebug(COMPONENT_NFS_CB, "%p %s", call,
             (hook == RPC_CALL_ABORT) ?
             "RPC_CALL_ABORT" :
             "RPC_CALL_COMPLETE");
    switch (hook) {
    case RPC_CALL_COMPLETE:
        /* potentially, do something more interesting here */
        LogDebug(COMPONENT_NFS_CB, "call result: %d", call->stat);
        free_rpc_call(call);
        break;
    default:
        LogDebug(COMPONENT_NFS_CB, "%p unknown hook %d", call, hook);
        break;
    }

    return (0);
}

static int32_t
cbsim_fake_cbrecall(clientid4 clientid)
{
    int32_t code = 0;
    nfs_client_id_t *pclientid = NULL;
    rpc_call_channel_t *chan = NULL;
    nfs_cb_argop4 argop[1];
    rpc_call_t *call;

    LogDebug(COMPONENT_NFS_CB,
             "called with clientid %"PRIx64, clientid);

    code  = nfs_client_id_get_confirmed(clientid, &pclientid);
    if (code != CLIENT_ID_SUCCESS) {
        LogCrit(COMPONENT_NFS_CB,
                "No clid record for %"PRIx64" (%d) code %d", clientid,
                (int32_t) clientid, code);
        code = EINVAL;
        goto out;
    }

    assert(pclientid);

    chan = nfs_rpc_get_chan(pclientid, NFS_RPC_FLAG_NONE);
    if (! chan) {
        LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed");
        goto out;
    }

    if (! chan->clnt) {
        LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed (no clnt)");
        goto out;
    }

    /* allocate a new call--freed in completion hook */
    call = alloc_rpc_call();
    call->chan = chan;

    /* setup a compound */
    cb_compound_init_v4(&call->cbt, 6,
			pclientid->cid_cb.cb_u.v40.cb_callback_ident,
                        "brrring!!!", 10);

    /* TODO: api-ify */
    memset(argop, 0, sizeof(nfs_cb_argop4));
    argop->argop = NFS4_OP_CB_RECALL;
    argop->nfs_cb_argop4_u.opcbrecall.stateid.seqid = 0xdeadbeef;
    strcpy(argop->nfs_cb_argop4_u.opcbrecall.stateid.other, "0xdeadbeef");
    argop->nfs_cb_argop4_u.opcbrecall.truncate = TRUE;
    argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_len = 11;
    /* leaks, sorry */
    argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val = gsh_strdup("0xabadcafe");

    /* add ops, till finished (dont exceed count) */
    cb_compound_add_op(&call->cbt, argop);

    /* set completion hook */
    call->call_hook = cbsim_completion_func;

    /* call it (here, in current thread context) */
    code = nfs_rpc_submit_call(call,
                               NFS_RPC_FLAG_NONE /* NFS_RPC_CALL_INLINE */);

out:
    return (code);
}

static DBusHandlerResult
nfs_rpc_cbsim_fake_recall(DBusConnection *conn, DBusMessage *msg,
                         void *user_data)
{
   DBusMessage* reply;
   DBusMessageIter args;
   static uint32_t serial = 1;
   clientid4 clientid = 9315; /* XXX ew! */

   LogDebug(COMPONENT_NFS_CB, "called!");

   /* read the arguments */
   if (!dbus_message_iter_init(msg, &args))
       LogDebug(COMPONENT_DBUS, "message has no arguments"); 
   else if (DBUS_TYPE_UINT64 != 
            dbus_message_iter_get_arg_type(&args)) 
       LogDebug(COMPONENT_DBUS, "arg not uint64"); 
   else {
       dbus_message_iter_get_basic(&args, &clientid);
       LogDebug(COMPONENT_DBUS, "param: %"PRIx64, clientid);
   }

   (void) cbsim_test_bchan(clientid);
   (void) cbsim_fake_cbrecall(clientid);

   reply = dbus_message_new_method_return(msg);
   if (!dbus_connection_send(conn, reply, &serial)) {
       LogCrit(COMPONENT_DBUS, "reply failed"); 
   }

   dbus_connection_flush(conn);
   dbus_message_unref(reply);
   serial++;

   return (DBUS_HANDLER_RESULT_HANDLED);
}

static DBusHandlerResult
nfs_rpc_cbsim_introspection(DBusConnection *conn, DBusMessage *msg,
			 void *user_data)
{
  static uint32_t serial = 1;
  DBusMessage* reply;
  DBusMessageIter iter;
  
  /* create a reply from the message */ 
  reply = dbus_message_new_method_return(msg);
  dbus_message_iter_init_append(reply, &iter);
  dbus_message_iter_append_basic( &iter, DBUS_TYPE_STRING,
                                  &introspection_xml );

  /* send the reply && flush the connection */
  if (! dbus_connection_send(conn, reply, &serial)) {
      LogCrit(COMPONENT_DBUS, "reply failed");
  }

  dbus_connection_flush(conn);
  dbus_message_unref(reply);
  serial++;

  return (DBUS_HANDLER_RESULT_HANDLED);
}

static DBusHandlerResult
nfs_rpc_cbsim_entrypoint(DBusConnection *conn, DBusMessage *msg,
			     void *user_data)
{
    const char *interface = dbus_message_get_interface(msg);
    const char *method = dbus_message_get_member(msg);

    if ((interface && (! strcmp(interface, DBUS_INTERFACE_INTROSPECTABLE))) ||
        (method && (! strcmp(method, "Introspect")))) {
        return(nfs_rpc_cbsim_introspection(conn, msg, user_data));
    }

    if (method) {
        if (! strcmp(method, "get_client_ids"))
            return(nfs_rpc_cbsim_get_client_ids(conn, msg, user_data));

        if (! strcmp(method, "fake_recall"))
            return(nfs_rpc_cbsim_fake_recall(conn, msg, user_data));
    }

    return (DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
}

/*
 * Initialize subsystem
 */
void nfs_rpc_cbsim_pkginit(void)
{
    (void) gsh_dbus_register_path("CBSIM", nfs_rpc_cbsim_entrypoint);
    LogEvent(COMPONENT_NFS_CB, "Callback Simulator Initialized");
}

/*
 * Shutdown subsystem
 */
void nfs_rpc_cbsim_pkgshutdown(void)
{

}
