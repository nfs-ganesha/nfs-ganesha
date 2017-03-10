#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER fsalmem

#if !defined(GANESHA_LTTNG_FSALMEM_TP_H) || \
	defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_FSALMEM_TP_H

#include <stdint.h>
#include <lttng/tracepoint.h>

/**
 * @brief Trace an allocation of an obj
 *
 * @param[in] function	Name of function taking ref
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_alloc,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_alloc,
	TRACE_INFO)

/**
 * @brief Trace a free of an obj
 *
 * @param[in] function	Name of function releasing ref
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_free,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_free,
	TRACE_INFO)

/**
 * @brief Trace a failed free of an obj
 *
 * @param[in] function	Name of function releasing ref
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 * @param[in] inavl	True if inavl
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_inuse,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		int, inavl),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_integer(int, inavl, inavl)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_inuse,
	TRACE_INFO)

/**
 * @brief Trace a getattrs call
 *
 * @param[in] function	Name of function releasing ref
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 * @param[in] name	File name
 * @param[in] size	Size of file
 * @param[in] numlinks	Number of links
 * @param[in] chg	Change counter
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_getattrs,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		const char *, name,
		uint64_t, size,
		uint32_t, numlinks,
		uint64_t, chg),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_string(name, name)
		ctf_integer(uint64_t, size, size)
		ctf_integer(uint32_t, numlinks, numlinks)
		ctf_integer(uint64_t, chg, chg)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_getattrs,
	TRACE_INFO)


#endif /* GANESHA_LTTNG_FSALMEM_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/fsal_mem.h"

#include <lttng/tracepoint-event.h>
