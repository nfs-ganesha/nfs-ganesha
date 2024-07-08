// SPDX-License-Identifier: LGPL-3.0-or-later
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

/* This file creates LTTNG tracepoints weak symbols. These are empty weak
 * functions that are overloaded by libganesha_trace.so. This allows us
 * to compile with LTTNG, but only enable it when we load libganesha_trace.so.
 *
 * Build targets that call tracepoints need to link with this file, by linking
 * with ganesha_trace_symbols. This will allow them to compile.
 */
#ifdef USE_LTTNG

#define TRACEPOINT_DEFINE
#define TRACEPOINT_PROBE_DYNAMIC_LINKAGE

#include "gsh_lttng/fsal_ceph.h"
#include "gsh_lttng/fsal_gluster.h"
#include "gsh_lttng/fsal_mem.h"
#include "gsh_lttng/logger.h"
#include "gsh_lttng/mdcache.h"
#include "gsh_lttng/nfs4.h"
#include "gsh_lttng/nfs_rpc.h"
#include "gsh_lttng/state.h"

#endif /* USE_LTTNG */
