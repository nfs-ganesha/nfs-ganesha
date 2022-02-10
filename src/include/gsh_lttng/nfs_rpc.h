/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER nfs_rpc

#if !defined(GANESHA_LTTNG_NFS_RPC_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_NFS_RPC_H

#include <lttng/tracepoint.h>

/**
 * @brief Trace the start of the rpc_execute function
 *
 * @param req  - the address of request we are handling
 */

TRACEPOINT_EVENT(
	nfs_rpc,
	start,
	TP_ARGS(nfs_request_t *, req),
	TP_FIELDS(
		ctf_integer_hex(nfs_request_t *, req, req)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	start,
	TRACE_INFO)

/**
 * @brief Trace the exit of the rpc_execute function
 *
 * The timestamp difference is the latency of the request
 *
 * @param req - the address of the request we just handled
 */

TRACEPOINT_EVENT(
	nfs_rpc,
	end,
	TP_ARGS(nfs_request_t *, req),
	TP_FIELDS(
		ctf_integer_hex(nfs_request_t *, req, req)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	end,
	TRACE_INFO)

/**
 * @brief Trace the start of the rpc_execute function
 *
 * @param req  - the address of request we are handling
 */

TRACEPOINT_EVENT(
	nfs_rpc,
	op_start,
	TP_ARGS(nfs_request_t *, req,
		const char *, op_name,
		int, export_id),
	TP_FIELDS(
		ctf_integer_hex(nfs_request_t *, req, req)
		ctf_string(op_name, op_name)
		ctf_integer(int, export_id, export_id)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	op_start,
	TRACE_INFO)

/**
 * @brief Trace the exit of the rpc_execute function
 *
 * The timestamp difference is the latency of the request
 *
 * @param req - the address of the request we just handled
 */

TRACEPOINT_EVENT(
	nfs_rpc,
	op_end,
	TP_ARGS(nfs_request_t *, req),
	TP_FIELDS(
		ctf_integer_hex(nfs_request_t *, req, req)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	op_end,
	TRACE_INFO)

/**
 * @brief Trace the start of the NFSv4 op function
 *
 * @param op_num  - Op number within compound
 * @param op_code  - Numerical opcode of op
 * @param op_name  - Text name of op
 */

TRACEPOINT_EVENT(
	nfs_rpc,
	v4op_start,
	TP_ARGS(int, op_num,
		int, op_code,
		const char *, op_name),
	TP_FIELDS(
		ctf_integer(int, op_num, op_num)
		ctf_integer(int, op_code, op_code)
		ctf_string(op_name, op_name)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	v4op_start,
	TRACE_INFO)

/**
 * @brief Trace the exit of the NFSv4 op function
 *
 * The timestamp difference is the latency of the request
 *
 * @param op_num  - Op number within compound
 * @param op_code  - Numerical opcode of op
 * @param op_name  - Text name of op
 * @param status  - Result of op
 */

TRACEPOINT_EVENT(
	nfs_rpc,
	v4op_end,
	TP_ARGS(int, op_num,
		int, op_code,
		const char *, op_name,
		const char *, status),
	TP_FIELDS(
		ctf_integer(int, op_num, op_num)
		ctf_integer(int, op_code, op_code)
		ctf_string(op_name, op_name)
		ctf_string(status, status)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	v4op_end,
	TRACE_INFO)

TRACEPOINT_EVENT(
	nfs_rpc,
	before_reply,
	TP_ARGS(const char *, function,
		unsigned int, line,
		void *, xprt),
	TP_FIELDS(
		ctf_string(fnc, function)
		ctf_integer(unsigned int, line, line)
		ctf_integer_hex(void *, xprt, xprt)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	before_reply,
	TRACE_INFO)

TRACEPOINT_EVENT(
	nfs_rpc,
	before_recv,
	TP_ARGS(const char *, function,
		unsigned int, line,
		void *, xprt),
	TP_FIELDS(
		ctf_string(fnc, function)
		ctf_integer(unsigned int, line, line)
		ctf_integer_hex(void *, xprt, xprt)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	before_recv,
	TRACE_INFO)

TRACEPOINT_EVENT(
	nfs_rpc,
	valid,
	TP_ARGS(const char *, function,
		unsigned int, line,
		void *, xprt,
		unsigned int, prog,
		unsigned int, vers,
		unsigned int, proc),
	TP_FIELDS(
		ctf_string(fnc, function)
		ctf_integer(unsigned int, line, line)
		ctf_integer_hex(void *, xprt, xprt)
		ctf_integer(unsigned int, prog, prog)
		ctf_integer(unsigned int, vers, vers)
		ctf_integer(unsigned int, proc, proc)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	valid,
	TRACE_INFO)

#endif /* GANESHA_LTTNG_NFS_RPC_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/nfs_rpc.h"

#include <lttng/tracepoint-event.h>
