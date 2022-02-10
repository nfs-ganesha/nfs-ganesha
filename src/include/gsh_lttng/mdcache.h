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
#define TRACEPOINT_PROVIDER mdcache

#if !defined(GANESHA_LTTNG_MDCACHE_TP_H) || \
	defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_MDCACHE_TP_H

#include <lttng/tracepoint.h>
#include <stdint.h>

/**
 * @brief Trace an increase in refcount of an entry
 *
 * @param[in] function	Name of function taking ref
 * @param[in] line	Line number of call
 * @param[in] obj_handle	Address of obj_handle
 * @param[in] refcnt	Refcount after increase
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_lru_ref,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj_handle,
		void *, sub_handle,
		int32_t, refcnt),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj_handle, obj_handle)
		ctf_integer_hex(void *, sub_handle, sub_handle)
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
 * @param[in] obj_handle	Address of obj_handle
 * @param[in] refcnt	Refcount after decrease
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_lru_unref,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj_handle,
		void *, sub_handle,
		int32_t, refcnt),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj_handle, obj_handle)
		ctf_integer_hex(void *, sub_handle, sub_handle)
		ctf_integer(int32_t, refcnt, refcnt)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_lru_unref,
	TRACE_INFO)

/**
 * @brief Trace a QLOCK event
 *
 * @param[in] function	Name of function taking ref
 * @param[in] line	Line number of call
 */
TRACEPOINT_EVENT(
	mdcache,
	qlock,
	TP_ARGS(const char *, function,
		int, line,
		void *, qlane),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, qlane, qlane)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	qlock,
	TRACE_INFO)

/**
 * @brief Trace a QUNLOCK event
 *
 * @param[in] function	Name of function taking ref
 * @param[in] line	Line number of call
 */
TRACEPOINT_EVENT(
	mdcache,
	qunlock,
	TP_ARGS(const char *, function,
		int, line,
		void *, qlane),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, qlane, qlane)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	qunlock,
	TRACE_INFO)

/**
 * @brief Trace a reap (reuse) of an entry
 *
 * @param[in] obj_handle	Address of obj_handle
 * @param[in] refcnt	Reference count of entry
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_lru_reap,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj_handle,
		int32_t, refcnt),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj_handle, obj_handle)
		ctf_integer(int32_t, refcnt, refcnt)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_lru_reap,
	TRACE_INFO)

/**
 * @brief Trace a alloc of a new entry
 *
 * @param[in] obj_handle	Address of obj_handle
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_lru_get,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj_handle,
		void *, sub_handle,
		int32_t, refcnt),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj_handle, obj_handle)
		ctf_integer_hex(void *, sub_handle, sub_handle)
		ctf_integer(int32_t, refcnt, refcnt)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_lru_get,
	TRACE_INFO)

/**
 * @brief Trace a reap (reuse) of a chunk
 *
 * @param[in] parent	Address of parent
 * @param[in] chunk	Address of chunk
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_lru_reap_chunk,
	TP_ARGS(const char *, function,
		int, line,
		void *, parent,
		void *, chunk),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, parent, parent)
		ctf_integer_hex(void *, chunk, chunk)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_lru_reap_chunk,
	TRACE_INFO)

/**
 * @brief Trace insertion of an entry in the LRU
 *
 * @param[in] obj_handle	Address of obj_handle
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_lru_insert,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj_handle,
		int32_t, refcnt),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj_handle, obj_handle)
		ctf_integer(int32_t, refcnt, refcnt)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_lru_insert,
	TRACE_INFO)

/**
 * @brief Trace removal of an entry from the LRU
 *
 * @param[in] obj_handle	Address of obj_handle
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_lru_remove,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj_handle,
		int32_t, refcnt),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj_handle, obj_handle)
		ctf_integer(int32_t, refcnt, refcnt)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_lru_remove,
	TRACE_INFO)

/**
 * @brief Trace killing of entry
 *
 * @param[in] obj_handle	Address of obj_handle
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_kill_entry,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj_handle,
		int32_t, refcnt,
		int32_t, freed),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj_handle, obj_handle)
		ctf_integer(int32_t, refcnt, refcnt)
		ctf_integer(int32_t, freed, freed)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_kill_entry,
	TRACE_INFO)

/**
 * @brief Trace readdir cache populate
 *
 * @param[in] obj_handle	Address of obj_handle
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_readdir_populate,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj_handle,
		void *, sub_handle,
		uint64_t, whence),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj_handle, obj_handle)
		ctf_integer_hex(void *, sub_handle, sub_handle)
		ctf_integer(uint64_t, whence, whence)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_readdir_populate,
	TRACE_INFO)

/**
 * @brief Trace readdir begin
 *
 * @param[in] obj_handle	Address of obj_handle
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_readdir,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj_handle),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj_handle, obj_handle)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_readdir,
	TRACE_INFO)

/**
 * @brief Trace readdir callback
 *
 * @param[in] obj_handle	Address of obj_handle
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_readdir_cb,
	TP_ARGS(const char *, function,
		int, line,
		const char *, dirent_name,
		void *, obj_handle,
		void *, sub_handle,
		int32_t, refcnt),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_string(dirent_name, dirent_name)
		ctf_integer_hex(void *, obj_handle, obj_handle)
		ctf_integer_hex(void *, sub_handle, sub_handle)
		ctf_integer(int32_t, refcnt, refcnt)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_readdir_cb,
	TRACE_INFO)

/**
 * @brief Trace lookup
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] parent	Address of parent obj_handle
 * @param[in] name	Name to lookup
 * @param[in] obj_handle	Address of obj_handle (if found)
 */
TRACEPOINT_EVENT(
	mdcache,
	mdc_lookup,
	TP_ARGS(const char *, function,
		int, line,
		void *, parent,
		const char *, name,
		void *, obj_handle),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, parent, parent)
		ctf_string(name, name)
		ctf_integer_hex(void *, obj_handle, obj_handle)
	)
)

TRACEPOINT_LOGLEVEL(
	mdcache,
	mdc_lookup,
	TRACE_INFO)

#endif /* GANESHA_LTTNG_MDCACHE_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/mdcache.h"

#include <lttng/tracepoint-event.h>
