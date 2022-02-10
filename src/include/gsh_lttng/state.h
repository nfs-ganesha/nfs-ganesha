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
