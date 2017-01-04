
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER state

#if !defined(GANESHA_LTTNG_STATE_TP_H) || \
	defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_STATE_TP_H

#include <lttng/tracepoint.h>

/**
 * @brief Trace a state add event
 *
 * @param[in] function	Name of function adding state
 * @param[in] line	Line number of call
 * @param[in] obj	obj state is added to
 * @param[in] state	state being added
 */
TRACEPOINT_EVENT(
	state,
	add,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		void *, state),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_integer_hex(void *, state, state)
	)
)

TRACEPOINT_LOGLEVEL(
	state,
	add,
	TRACE_INFO)

/**
 * @brief Trace a state delete event
 *
 * @param[in] function	Name of function deleting state
 * @param[in] line	Line number of call
 * @param[in] obj	obj state is deleted from
 * @param[in] state	state being deleted
 */
TRACEPOINT_EVENT(
	state,
	delete,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		void *, state),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_integer_hex(void *, state, state)
	)
)

TRACEPOINT_LOGLEVEL(
	state,
	delete,
	TRACE_INFO)

#endif /* GANESHA_LTTNG_STATE_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/state.h"

#include <lttng/tracepoint-event.h>
