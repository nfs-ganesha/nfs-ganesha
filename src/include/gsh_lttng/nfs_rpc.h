
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

#endif /* GANESHA_LTTNG_NFS_RPC_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/nfs_rpc.h"

#include <lttng/tracepoint-event.h>
