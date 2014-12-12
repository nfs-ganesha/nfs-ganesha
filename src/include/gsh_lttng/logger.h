
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER ganesha_logger

#if !defined(GANESHA_LTTNG_LOG_TP_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_LOG_TP_H

#include <lttng/tracepoint.h>

TRACEPOINT_EVENT(
	ganesha_logger,
	log,
	TP_ARGS(unsigned char, component,
		unsigned char, level,
		const char *, file,
		unsigned int, line,
		const char *, function,
		char *, message),
	TP_FIELDS(
		ctf_integer(unsigned char, component, component)
		ctf_integer(unsigned char, level, level)
		ctf_string(file, file)
		ctf_integer(unsigned int, line, line)
		ctf_string(fnc, function)
		ctf_string(msg, message)
	)
)

TRACEPOINT_LOGLEVEL(
	ganesha_logger,
	log,
	TRACE_INFO)

#endif /* GANESHA_LTTNG_LOG_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/logger.h"

#include <lttng/tracepoint-event.h>
