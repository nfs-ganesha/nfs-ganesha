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
