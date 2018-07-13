/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Max Matveev, 2012
 * Copyright CEA/DAM/DIF  (2008)
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 */

/* Proxy handle methods */

#ifndef _PXY_FSAL_METHODS_H
#define _PXY_FSAL_METHODS_H

#ifdef PROXY_HANDLE_MAPPING
#include "handle_mapping/handle_mapping.h"
#endif

/*512 bytes to store header*/
#define SEND_RECV_HEADER_SPACE 512
/*1MB of default maxsize*/
#define DEFAULT_MAX_WRITE_READ 1048576

#include <pthread.h>
#include <dirent.h>
#include <stdbool.h>

struct pxy_fsal_module {
	struct fsal_module module;
	struct fsal_obj_ops handle_ops;
};

extern struct pxy_fsal_module PROXY;

struct pxy_client_params {
	uint32_t retry_sleeptime;
	sockaddr_t srv_addr;
	uint32_t srv_prognum;
	uint64_t srv_sendsize;
	uint64_t srv_recvsize;
	uint32_t srv_timeout;
	uint16_t srv_port;
	bool use_privileged_client_port;
	char *remote_principal;
	char *keytab;
	unsigned int cred_lifetime;
	unsigned int sec_type;
	bool active_krb5;

	/* initialization info for handle mapping */
	bool enable_handle_mapping;

#ifdef PROXY_HANDLE_MAPPING
	handle_map_param_t hdlmap;
#endif
};

struct pxy_export_rpc {
/**
 * pxy_clientid_mutex protects pxy_clientid, pxy_client_seqid,
 * pxy_client_sessionid, no_sessionid and cond_sessionid.
 */
	clientid4 pxy_clientid;
	sequenceid4 pxy_client_seqid;
	sessionid4 pxy_client_sessionid;
	bool no_sessionid;
	pthread_cond_t cond_sessionid;
	pthread_mutex_t pxy_clientid_mutex;

	char pxy_hostname[MAXNAMLEN + 1];
	pthread_t pxy_recv_thread;
	pthread_t pxy_renewer_thread;

	/**
	 * listlock protects rpc_sock and rpc_xid values, sockless condition and
	 * rpc_calls list.
	 */
	struct glist_head rpc_calls;
	int rpc_sock;
	uint32_t rpc_xid;
	pthread_mutex_t listlock;
	pthread_cond_t sockless;
	bool close_thread;

	/*
	 * context_lock protects free_contexts list and need_context condition.
	 */
	struct glist_head free_contexts;
	pthread_cond_t need_context;
	pthread_mutex_t context_lock;
};

struct pxy_export {
	struct fsal_export exp;
	struct pxy_client_params info;
	struct pxy_export_rpc rpc;
};

static inline void pxy_export_init(struct pxy_export *pxy_exp)
{
	pxy_exp->rpc.no_sessionid = true;
	pthread_mutex_init(&pxy_exp->rpc.pxy_clientid_mutex, NULL);
	pthread_cond_init(&pxy_exp->rpc.cond_sessionid, NULL);
	pxy_exp->rpc.rpc_sock = -1;
	pthread_mutex_init(&pxy_exp->rpc.listlock, NULL);
	pthread_cond_init(&pxy_exp->rpc.sockless, NULL);
	pthread_cond_init(&pxy_exp->rpc.need_context, NULL);
	pthread_mutex_init(&pxy_exp->rpc.context_lock, NULL);
}

void pxy_handle_ops_init(struct fsal_obj_ops *ops);

int pxy_init_rpc(struct pxy_export *);

fsal_status_t pxy_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				 const struct req_op_context *opctx,
				 unsigned int cookie,
				 fsal_xattrent_t *xattrs_tab,
				 unsigned int xattrs_tabsize,
				 unsigned int *p_nb_returned, int *end_of_list);

fsal_status_t pxy_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					const struct req_op_context *opctx,
					const char *xattr_name,
					unsigned int *pxattr_id);

fsal_status_t pxy_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					   const struct req_op_context *opctx,
					   const char *xattr_name,
					   void *buffer_addr,
					   size_t buffer_size, size_t *len);

fsal_status_t pxy_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 const struct req_op_context *opctx,
					 unsigned int xattr_id,
					 void *buf,
					 size_t sz,
					 size_t *len);

fsal_status_t pxy_setextattr_value(struct fsal_obj_handle *obj_hdl,
				   const struct req_op_context *opctx,
				   const char *xattr_name,
				   void *buf,
				   size_t sz,
				   int create);

fsal_status_t pxy_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					 const struct req_op_context *opctx,
					 unsigned int xattr_id,
					 void *buf,
					 size_t sz);

fsal_status_t pxy_getextattr_attrs(struct fsal_obj_handle *obj_hdl,
				   const struct req_op_context *opctx,
				   unsigned int xattr_id,
				   struct attrlist *attrs);

fsal_status_t pxy_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
				       const struct req_op_context *opctx,
				       unsigned int xattr_id);

fsal_status_t pxy_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					 const struct req_op_context *opctx,
					 const char *xattr_name);

fsal_status_t pxy_lookup_path(struct fsal_export *exp_hdl,
			      const char *path,
			      struct fsal_obj_handle **handle,
			      struct attrlist *attrs_out);

fsal_status_t pxy_create_handle(struct fsal_export *exp_hdl,
				struct gsh_buffdesc *hdl_desc,
				struct fsal_obj_handle **handle,
				struct attrlist *attrs_out);

fsal_status_t pxy_create_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				const struct fsal_up_vector *up_ops);

fsal_status_t pxy_get_dynamic_info(struct fsal_export *,
				   struct fsal_obj_handle *,
				   fsal_dynamicfsinfo_t *);

fsal_status_t pxy_wire_to_host(struct fsal_export *, fsal_digesttype_t,
				 struct gsh_buffdesc *, int);

struct state_t *pxy_alloc_state(struct fsal_export *exp_hdl,
				enum state_type state_type,
				struct state_t *related_state);

void pxy_free_state(struct fsal_export *exp_hdl, struct state_t *state);

int pxy_close_thread(struct pxy_export *pxy_exp);

#endif
