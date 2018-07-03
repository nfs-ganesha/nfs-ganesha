/*                                                                            */
/* Copyright (C) 2017 International Business Machines                         */
/* All rights reserved.                                                       */
/*                                                                            */
/* Redistribution and use in source and binary forms, with or without         */
/* modification, are permitted provided that the following conditions         */
/* are met:                                                                   */
/*                                                                            */
/*  1. Redistributions of source code must retain the above copyright notice, */
/*     this list of conditions and the following disclaimer.                  */
/*  2. Redistributions in binary form must reproduce the above copyright      */
/*     notice, this list of conditions and the following disclaimer in the    */
/*     documentation and/or other materials provided with the distribution.   */
/*  3. The name of the author may not be used to endorse or promote products  */
/*     derived from this software without specific prior written              */
/*     permission.                                                            */
/*                                                                            */
/* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR       */
/* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES  */
/* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.    */
/* IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL*/
/* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO*/
/* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;*/
/* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,   */
/* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR    */
/* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF     */
/* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                 */
/*                                                                            */
/* %Z%%M%       %I%  %W% %G% %U% */

#include "config.h"
#include "gsh_list.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include <abstract_atomic.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "nfs_proto_functions.h"
#include "include/gpfs_nfs.h"

struct fsal_op_stats gpfs_op_stats[GPFS_STAT_MAX_OPS];
struct fsal_stats gpfs_stats;

#ifdef USE_DBUS
/**
 *  * @brief Return string for gpfs opcode
 *   *
 *    * @param[in] gpfs opcode
 *     */
static char *gpfs_opcode_to_name(int opcode)
{
	switch (opcode) {
	case OPENHANDLE_GET_VERSION:
		return "GET_VERSION";
	case OPENHANDLE_NAME_TO_HANDLE:
		return "NAME_TO_HANDLE";
	case OPENHANDLE_OPEN_BY_HANDLE:
		return "OPEN_BY_HANDLE";
	case OPENHANDLE_LAYOUT_TYPE:
		return "LAYOUT_TYPE";
	case OPENHANDLE_GET_DEVICEINFO:
		return "GET_DEVICEINFO";
	case OPENHANDLE_GET_DEVICELIST:
		return "GET_DEVICELIST";
	case OPENHANDLE_LAYOUT_GET:
		return "LAYOUT_GET";
	case OPENHANDLE_LAYOUT_RETURN:
		return "LAYOUT_RETURN";
	case OPENHANDLE_INODE_UPDATE:
		return "INODE_UPDATE";
	case OPENHANDLE_GET_XSTAT:
		return "GET_XSTAT";
	case OPENHANDLE_SET_XSTAT:
		return "SET_XSTAT";
	case OPENHANDLE_CHECK_ACCESS:
		return "CHECK_ACCESS";
	case OPENHANDLE_OPEN_SHARE_BY_HANDLE:
		return "OPEN_SHARE_BY_HANDLE";
	case OPENHANDLE_GET_LOCK:
		return "GET_LOCK";
	case OPENHANDLE_SET_LOCK:
		return "SET_LOCK";
	case OPENHANDLE_THREAD_UPDATE:
		return "THREAD_UPDATE";
	case OPENHANDLE_LAYOUT_COMMIT:
		return "LAYOUT_COMMIT";
	case OPENHANDLE_DS_READ:
		return "DS_READ";
	case OPENHANDLE_DS_WRITE:
		return "DS_WRITE";
	case OPENHANDLE_GET_VERIFIER:
		return "GET_VERIFIER";
	case OPENHANDLE_FSYNC:
		return "FSYNC";
	case OPENHANDLE_SHARE_RESERVE:
		return "SHARE_RESERVE";
	case OPENHANDLE_GET_NODEID:
		return "GET_NODEID";
	case OPENHANDLE_SET_DELEGATION:
		return "SET_DELEGATION";
	case OPENHANDLE_CLOSE_FILE:
		return "CLOSE_FILE";
	case OPENHANDLE_LINK_BY_FH:
		return "LINK_BY_FH";
	case OPENHANDLE_RENAME_BY_FH:
		return "RENAME_BY_FH";
	case OPENHANDLE_STAT_BY_NAME:
		return "STAT_BY_NAME";
	case OPENHANDLE_GET_HANDLE:
		return "GET_HANDLE";
	case OPENHANDLE_READLINK_BY_FH:
		return "READLINK_BY_FH";
	case OPENHANDLE_UNLINK_BY_NAME:
		return "UNLINK_BY_NAME";
	case OPENHANDLE_CREATE_BY_NAME:
		return "CREATE_BY_NAME";
	case OPENHANDLE_READ_BY_FD:
		return "READ_BY_FD";
	case OPENHANDLE_WRITE_BY_FD:
		return "WRITE_BY_FD";
	case OPENHANDLE_CREATE_BY_NAME_ATTR:
		return "CREATE_BY_NAME_ATTR";
	case OPENHANDLE_GRACE_PERIOD:
		return "GRACE_PERIOD";
	case OPENHANDLE_ALLOCATE_BY_FD:
		return "ALLOCATE_BY_FD";
	case OPENHANDLE_REOPEN_BY_FD:
		return "REOPEN_BY_FD";
	case OPENHANDLE_FADVISE_BY_FD:
		return "FADVISE_BY_FD";
	case OPENHANDLE_SEEK_BY_FD:
		return "SEEK_BY_FD";
	case OPENHANDLE_STATFS_BY_FH:
		return "STATFS_BY_FH";
	case OPENHANDLE_GETXATTRS:
		return "GETXATTRS";
	case OPENHANDLE_SETXATTRS:
		return "SETXATTRS";
	case OPENHANDLE_REMOVEXATTRS:
		return "REMOVEXATTRS";
	case OPENHANDLE_LISTXATTRS:
		return "LISTXATTRS";
	case OPENHANDLE_MKNODE_BY_NAME:
		return "MKNODE_BY_NAME";
	case OPENHANDLE_reserved:
		return "reserved";
	case OPENHANDLE_TRACE_ME:
		return "TRACE_ME";
	case OPENHANDLE_QUOTA:
		return "QUOTA";
	case OPENHANDLE_FS_LOCATIONS:
		return "FS_LOCATIONS";
	default:
		return "UNMONITORED";
	}
}
#endif  /* USE_DBUS */

/** @fn prepare_for_stats(struct fsal_module *fsal_hdl)
 *  *  @brief prepare the structure which will hold the stats
 *   */
void prepare_for_stats(struct fsal_module *fsal_hdl)
{
	int idx, op;

	gpfs_stats.total_ops = GPFS_TOTAL_OPS;
	gpfs_stats.op_stats = gpfs_op_stats;
	fsal_hdl->stats = &gpfs_stats;
	for (op = GPFS_MIN_OP; op <= GPFS_MAX_OP; op++) {
		idx = gpfs_op2index(op);
		fsal_hdl->stats->op_stats[idx].op_code = op;
	}
}

#ifdef USE_DBUS
/** @fn fsal_gpfs_extract_stats(struct fsal_module *fsal_hdl, void *iter)
 *  *  @brief Extract the FSAL specific performance counters
 *   */
void fsal_gpfs_extract_stats(struct fsal_module *fsal_hdl, void *iter)
{
	struct timespec timestamp;
	DBusMessageIter struct_iter;
	DBusMessageIter *iter1 = (DBusMessageIter *)iter;
	char *message;
	uint64_t total_ops, total_resp, min_resp, max_resp, op_counter = 0;
	double res = 0.0;
	int i;
	struct fsal_stats *gpfs_stats;

	now(&timestamp);
	dbus_append_timestamp(iter, &timestamp);
	gpfs_stats = fsal_hdl->stats;
	message = "GPFS";
	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &message);

	dbus_message_iter_open_container(iter1, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	for (i = 0; i < GPFS_STAT_PH_INDEX; i++) {
		if (i == GPFS_STAT_NO_OP_1 || i == GPFS_STAT_NO_OP_2
		     || i == GPFS_STAT_NO_OP_3)
			continue;

		total_ops = atomic_fetch_uint64_t(
				&gpfs_stats->op_stats[i].num_ops);
		if (total_ops == 0)
			continue;

		total_resp = atomic_fetch_uint64_t(
				&gpfs_stats->op_stats[i].resp_time);
		min_resp = atomic_fetch_uint64_t(
				&gpfs_stats->op_stats[i].resp_time_min);
		max_resp = atomic_fetch_uint64_t(
				&gpfs_stats->op_stats[i].resp_time_max);
		/* We have valid stats, send it across */
		message = gpfs_opcode_to_name(gpfs_stats->op_stats[i].op_code);
		dbus_message_iter_append_basic(&struct_iter,
				DBUS_TYPE_STRING, &message);
		dbus_message_iter_append_basic(&struct_iter,
			DBUS_TYPE_UINT64, &total_ops);
		res = (double) total_resp * 0.000001 / total_ops;
		dbus_message_iter_append_basic(&struct_iter,
			DBUS_TYPE_DOUBLE, &res);
		res = (double) min_resp * 0.000001;
		dbus_message_iter_append_basic(&struct_iter,
			DBUS_TYPE_DOUBLE, &res);
		res = (double) max_resp * 0.000001;
		dbus_message_iter_append_basic(&struct_iter,
			DBUS_TYPE_DOUBLE, &res);
		op_counter += total_ops;
	}
	if (op_counter == 0) {
		message = "None";
		/* insert dummy stats to avoid dbus crash */
		dbus_message_iter_append_basic(&struct_iter,
				DBUS_TYPE_STRING, &message);
		dbus_message_iter_append_basic(&struct_iter,
			DBUS_TYPE_UINT64, &total_ops);
		dbus_message_iter_append_basic(&struct_iter,
			DBUS_TYPE_DOUBLE, &res);
		dbus_message_iter_append_basic(&struct_iter,
			DBUS_TYPE_DOUBLE, &res);
		dbus_message_iter_append_basic(&struct_iter,
			DBUS_TYPE_DOUBLE, &res);
	} else {
		message = "OK";
	}
	dbus_message_iter_close_container(iter1, &struct_iter);
	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &message);
}
#endif   /* USE_DBUS */

void fsal_gpfs_reset_stats(struct fsal_module *fsal_hdl)
{
	int i;
	struct fsal_stats *gpfs_stats;

	gpfs_stats = fsal_hdl->stats;

	/* reset all the counters */
	for (i = 0; i < GPFS_STAT_PH_INDEX; i++) {
		atomic_store_uint64_t(
				&gpfs_stats->op_stats[i].num_ops, 0);
		atomic_store_uint64_t(
				&gpfs_stats->op_stats[i].resp_time, 0);
		atomic_store_uint64_t(
				&gpfs_stats->op_stats[i].resp_time_min, 0);
		atomic_store_uint64_t(
				&gpfs_stats->op_stats[i].resp_time_max, 0);
	}
}


