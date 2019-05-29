#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER fsalgl

#if !defined(GANESHA_LTTNG_FSALGLUSTER_TP_H) || \
	defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_FSALGLUSTER_TP_H

#include <stdint.h>
#include <lttng/tracepoint.h>

/**
 * @brief Trace glfd open call
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] flag	Flag value
 * @param[in] glfd	Gluster fd
 */

TRACEPOINT_EVENT(
	fsalgl,
	open_fd,
	TP_ARGS(const char *, function,
		int, line,
		int, flag,
		void *, glfd),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer(int, flag, flag)
		ctf_integer(void *, glfd, glfd)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalgl,
	open_fd,
	TRACE_INFO)
/**
 * @brief Trace close call for glfd
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] glfd	Gluster fd
 */

TRACEPOINT_EVENT(
	fsalgl,
	close_fd,
	TP_ARGS(const char *, function,
		int, line,
		void *, glfd),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer(void *, glfd, glfd)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalgl,
	close_fd,
	TRACE_INFO)

/**
 * @brief Trace Glusterfs handle
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] glfd	Glusterfs handle
 */

TRACEPOINT_EVENT(
	fsalgl,
	gl_handle,
	TP_ARGS(const char *, function,
		int, line,
		void *, handle),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer(void *, handle, handle)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalgl,
	gl_handle,
	TRACE_INFO)

#endif /* GANESHA_LTTNG_FSALGLUSTER_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/fsal_gluster.h"

#include <lttng/tracepoint-event.h>
