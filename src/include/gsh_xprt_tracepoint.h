/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2024 Google LLC
 * contributeur : Shahar Hochma shaharhoch@google.com
 *
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 */

#include "gsh_lttng/gsh_lttng.h"
#include "rpc/svc.h"

/* We can not directly use the similar macros defined in libntirpc because we
 * can't call NTIRPC_AUTO_TRACEPOINT directly from Ganesha. */

#define GSH_XPRT_AUTO_TRACEPOINT(prov, event, log_level, _xprt, format, ...) \
	GSH_AUTO_TRACEPOINT(prov, event, log_level, XPRT_FMT " | " format, \
		XPRT_VARS(_xprt), ##__VA_ARGS__)

#define GSH_XPRT_UNIQUE_AUTO_TRACEPOINT(prov, event, log_level, _xprt, format, \
	...) \
	GSH_UNIQUE_AUTO_TRACEPOINT(prov, event, log_level, \
		XPRT_FMT " | " format, XPRT_VARS(_xprt), ##__VA_ARGS__)
