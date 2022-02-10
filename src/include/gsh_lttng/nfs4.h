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
#define TRACEPOINT_PROVIDER nfs4

#if !defined(GANESHA_LTTNG_NFS4_TP_H) || \
	defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_NFS4_TP_H

#include <lttng/tracepoint.h>

/**
 * @brief Trace an increase in refcount of a session
 *
 * @param[in] function	Name of function taking ref
 * @param[in] line	Line number of call
 * @param[in] entry	Address of entry
 * @param[in] refcnt	Refcount after increase
 */
TRACEPOINT_EVENT(
	nfs4,
	session_ref,
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
	nfs4,
	session_ref,
	TRACE_INFO)

/**
 * @brief Trace a decrease in refcount a session
 *
 * @param[in] function	Name of function releasing ref
 * @param[in] line	Line number of call
 * @param[in] entry	Address of entry
 * @param[in] refcnt	Refcount after decrease
 */
TRACEPOINT_EVENT(
	nfs4,
	session_unref,
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
	nfs4,
	session_unref,
	TRACE_INFO)


#endif /* GANESHA_LTTNG_NFS4_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/nfs4.h"

#include <lttng/tracepoint-event.h>
