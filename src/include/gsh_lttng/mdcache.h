
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER mdcache

#if !defined(GANESHA_LTTNG_MDCACHE_TP_H) || \
	defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_MDCACHE_TP_H

#include <lttng/tracepoint.h>

/**
 * @brief Trace an increase in refcount of an entry
 *
 * @param[in] function	Name of function taking ref
 * @param[in] line	Line number of call
 * @param[in] entry	Address of entry
 * @param[in] refcnt	Refcount after increase
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_lru_ref,
	TP_ARGS(const char *, function,
		int, line,
		void *, entry,
		int32_t, refcnt),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, entry, entry)
		ctf_integer(int32_t, refcnt, refcnt)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_lru_ref,
	TRACE_INFO)

/**
 * @brief Trace a decrease in refcount of an entry
 *
 * @param[in] function	Name of function releasing ref
 * @param[in] line	Line number of call
 * @param[in] entry	Address of entry
 * @param[in] refcnt	Refcount after decrease
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_lru_unref,
	TP_ARGS(const char *, function,
		int, line,
		void *, entry,
		int32_t, refcnt),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, entry, entry)
		ctf_integer(int32_t, refcnt, refcnt)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_lru_unref,
	TRACE_INFO)

#endif /* GANESHA_LTTNG_MDCACHE_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/mdcache.h"

#include <lttng/tracepoint-event.h>
