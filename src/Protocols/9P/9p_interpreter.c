/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
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
 * ---------------------------------------
 */

/**
 * @file  9p_interpreter.c
 * @brief 9P interpretor
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "nfs_core.h"
#include "9p.h"
#include "cache_inode.h"
#include "fsal.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "server_stats.h"

/* opcode to function array */
const struct _9p_function_desc _9pfuncdesc[] = {
	[0] = {_9p_not_2000L, "no function"},	/* out of bounds */
	[_9P_TSTATFS] = {_9p_statfs, "_9P_TSTATFS"},
	[_9P_TLOPEN] = {_9p_lopen, "_9P_TLOPEN"},
	[_9P_TLCREATE] = {_9p_lcreate, "_9P_TLCREATE"},
	[_9P_TSYMLINK] = {_9p_symlink, "_9P_TSYMLINK"},
	[_9P_TMKNOD] = {_9p_mknod, "_9P_TMKNOD"},
	[_9P_TRENAME] = {_9p_rename, "_9P_TRENAME"},
	[_9P_TREADLINK] = {_9p_readlink, "_9P_TREADLINK"},
	[_9P_TGETATTR] = {_9p_getattr, "_9P_TGETATTR"},
	[_9P_TSETATTR] = {_9p_setattr, "_9P_TSETATTR"},
	[_9P_TXATTRWALK] = {_9p_xattrwalk, "_9P_TXATTRWALK"},
	[_9P_TXATTRCREATE] = {_9p_xattrcreate, "_9P_TXATTRCREATE"},
	[_9P_TREADDIR] = {_9p_readdir, "_9P_TREADDIR"},
	[_9P_TFSYNC] = {_9p_fsync, "_9P_TFSYNC"},
	[_9P_TLOCK] = {_9p_lock, "_9P_TLOCK"},
	[_9P_TGETLOCK] = {_9p_getlock, "_9P_TGETLOCK"},
	[_9P_TLINK] = {_9p_link, "_9P_TLINK"},
	[_9P_TMKDIR] = {_9p_mkdir, "_9P_TMKDIR"},
	[_9P_TRENAMEAT] = {_9p_renameat, "_9P_TRENAMEAT"},
	[_9P_TUNLINKAT] = {_9p_unlinkat, "_9P_TUNLINKAT"},
	[_9P_TVERSION] = {_9p_version, "_9P_TVERSION"},
	[_9P_TAUTH] = {_9p_auth, "_9P_TAUTH"},
	[_9P_TATTACH] = {_9p_attach, "_9P_TATTACH"},
	[_9P_TFLUSH] = {_9p_flush, "_9P_TFLUSH"},
	[_9P_TWALK] = {_9p_walk, "_9P_TWALK"},
	[_9P_TOPEN] = {_9p_not_2000L, "_9P_TOPEN"},
	[_9P_TCREATE] = {_9p_not_2000L, "_9P_TCREATE"},
	[_9P_TREAD] = {_9p_read, "_9P_TREAD"},
	[_9P_TWRITE] = {_9p_write, "_9P_TWRITE"},
	[_9P_TCLUNK] = {_9p_clunk, "_9P_TCLUNK"},
	[_9P_TREMOVE] = {_9p_remove, "_9P_TREMOVE"},
	[_9P_TSTAT] = {_9p_not_2000L, "_9P_TSTAT"},
	[_9P_TWSTAT] = {_9p_not_2000L, "_9P_TWSTAT"}
};

int _9p_not_2000L(struct _9p_request_data *req9p, void *worker_data,
		  u32 *plenout, char *preply)
{
	char *msgdata = req9p->_9pmsg + _9P_HDR_SIZE;
	u8 *pmsgtype = NULL;
	u16 msgtag = 0;

	/* Get message's type */
	pmsgtype = (u8 *) msgdata;
	LogEvent(COMPONENT_9P,
		 "(%u|%s) is not a 9P2000.L message, returning ENOTSUP",
		 *pmsgtype, _9pfuncdesc[*pmsgtype].funcname);

	_9p_rerror(req9p, worker_data, &msgtag, ENOTSUP, plenout, preply);

	return -1;
}				/* _9p_not_2000L */

static ssize_t tcp_conn_send(struct _9p_conn *conn, const void *buf, size_t len,
			     int flags)
{
	ssize_t ret;

	pthread_mutex_lock(&conn->sock_lock);
	ret = send(conn->trans_data.sockfd, buf, len, flags);
	pthread_mutex_unlock(&conn->sock_lock);

	if (ret < 0)
		server_stats_transport_done(conn->client,
					    0, 0, 0,
					    0, 0, 1);
	else
		server_stats_transport_done(conn->client,
					    0, 0, 0,
					    ret, 1, 0);
	return ret;
}

void _9p_tcp_process_request(struct _9p_request_data *req9p,
			     nfs_worker_data_t *worker_data)
{
	u32 outdatalen = 0;
	int rc = 0;
	char replydata[_9P_MSG_SIZE];

	rc = _9p_process_buffer(req9p, worker_data, replydata, &outdatalen);
	if (rc != 1) {
		LogMajor(COMPONENT_9P,
			 "Could not process 9P buffer on socket #%lu",
			 req9p->pconn->trans_data.sockfd);
	} else {
		if (tcp_conn_send(req9p->pconn, replydata, outdatalen, 0) !=
		    outdatalen)
			LogMajor(COMPONENT_9P,
				 "Could not send 9P/TCP reply correclty on socket #%lu",
				 req9p->pconn->trans_data.sockfd);
	}
	_9p_DiscardFlushHook(req9p);
	return;
}				/* _9p_process_request */

int _9p_process_buffer(struct _9p_request_data *req9p,
		       nfs_worker_data_t *worker_data, char *replydata,
		       u32 *poutlen)
{
	char *msgdata;
	u32 msglen;
	u8 msgtype;
	int rc = 0;

	msgdata = req9p->_9pmsg;

	/* Get message's length */
	msglen = *(u32 *) msgdata;
	msgdata += _9P_HDR_SIZE;

	/* Get message's type */
	msgtype = *(u8 *) msgdata;
	msgdata += _9P_TYPE_SIZE;

	/* Check boundaries. 0 is no_function fallback */
	if (msgtype < _9P_TSTATFS || msgtype > _9P_TWSTAT
	    || _9pfuncdesc[msgtype].service_function == NULL)
		msgtype = 0;

	LogFullDebug(COMPONENT_9P, "9P msg: length=%u type (%u|%s)", msglen,
		     (u32) msgtype, _9pfuncdesc[msgtype].funcname);

	/* Temporarily set outlen to maximum message size. This value will be
	 * used inside the protocol functions for additional bound checking,
	 * and then replaced by the actual message size, (see _9p_checkbound())
	 */
	*poutlen = req9p->pconn->msize;

	/* Call the 9P service function */
	rc = _9pfuncdesc[msgtype].service_function(req9p,
						   (void *)worker_data,
						   poutlen, replydata);
	op_ctx = NULL; /* poison the op context to disgard it */
	if (rc < 0)
		LogDebug(COMPONENT_9P, "%s: Error",
			 _9pfuncdesc[msgtype].funcname);

/**
 * @todo ops stats accounting goes here.
 * service function return codes need to be reworked to return error code
 * properly so that internal error code (currently -1) is distinguished
 * from protocol op error, currently partially handled in rerror, and
 * success return here so we can count errors and totals properly.
 * I/O stats handled in read and write as in nfs.
 */
	return rc;
}
