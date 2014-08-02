
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER nfs_rpc

#if !defined(GANESHA_LTTNG_NFS_RPC_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_NFS_RPC_H

#include <lttng/tracepoint.h>

TRACEPOINT_EVENT(
	nfs_rpc,
	start,
	TP_ARGS(request_data_t *, req),
	TP_FIELDS(
		ctf_integer_hex(request_data_t *, req, req)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	start,
	TRACE_INFO)

TRACEPOINT_EVENT(
	nfs_rpc,
	end,
	TP_ARGS(request_data_t *, req),
	TP_FIELDS(
		ctf_integer_hex(request_data_t *, req, req)
	)
)

TRACEPOINT_LOGLEVEL(
	nfs_rpc,
	end,
	TRACE_INFO)

#endif /* GANESHA_LTTNG_NFS_RPC_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "ganesha_lttng/nfs_rpc.h"

#include <lttng/tracepoint-event.h>

