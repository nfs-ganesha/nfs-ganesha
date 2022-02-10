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
#define TRACEPOINT_PROVIDER fsalceph

#if !defined(GANESHA_LTTNG_FSALCEPH_TP_H) || \
	defined(TRACEPOINT_HEADER_MULTI_READ)
#define GANESHA_LTTNG_FSALCEPH_TP_H

#include <lttng/tracepoint.h>
#include <stdint.h>

/**
 * @brief Trace Ceph handle
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] handle	Ceph handle
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_create_handle,
	TP_ARGS(const char *, function,
		int, line,
		void *, handle),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, handle, handle)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_create_handle,
	TRACE_INFO)

/**
 * @brief Trace Ceph lookup
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] path	lookup path
 * @param[in] handle	obj handle
 * @param[in] ino	inode number of target
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_lookup,
	TP_ARGS(const char *, function,
		int, line,
		const char *, path,
		void *, handle,
		uint64_t, ino),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_string(path, path)
		ctf_integer_hex(void *, handle, handle)
		ctf_integer_hex(uint64_t, ino, ino)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_lookup,
	TRACE_INFO)

/**
 * @brief Trace Ceph mkdir
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] name	folder name
 * @param[in] handle	folder obj handle
 * @param[in] ino	folder inode number
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_mkdir,
	TP_ARGS(const char *, function,
		int, line,
		const char *, name,
		void *, handle,
		uint64_t, ino),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_string(name, name)
		ctf_integer_hex(void *, handle, handle)
		ctf_integer_hex(uint64_t, ino, ino)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_mkdir,
	TRACE_INFO)

/**
 * @brief Trace Ceph mknode
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] name	node name
 * @param[in] type	node type
 * @param[in] handle	node obj handle
 * @param[in] ino	node inode number
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_mknod,
	TP_ARGS(const char *, function,
		int, line,
		const char *, name,
		int, type,
		void *, handle,
		uint64_t, ino),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_string(name, name)
		ctf_integer(int, type, type)
		ctf_integer_hex(void *, handle, handle)
		ctf_integer_hex(uint64_t, ino, ino)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_mknod,
	TRACE_INFO)

/**
 * @brief Trace Ceph open2
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] name	to be opend name
 * @param[in] handle	node's obj handle
 * @param[in] ino	node inode number
 * @param[in] type	stage (opend/created)
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_open,
	TP_ARGS(const char *, function,
		int, line,
		const char *, name,
		void *, handle,
		uint64_t, ino,
		const char *, stage),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_string(name, name)
		ctf_integer_hex(void *, handle, handle)
		ctf_integer_hex(uint64_t, ino, ino)
		ctf_string(stage, stage)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_open,
	TRACE_INFO)

/**
 * @brief Trace Ceph close
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] ino	node inode number
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_close,
	TP_ARGS(const char *, function,
		int, line,
		uint64_t, ino),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(uint64_t, ino, ino)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_close,
	TRACE_INFO)

/**
 * @brief Trace Ceph write
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] fileid	target fileid (means inode)
 * @param[in] size	written size
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_write,
	TP_ARGS(const char *, function,
		int, line,
		uint64_t, fileid,
		uint64_t, size),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(uint64_t, ino, fileid)
		ctf_integer(uint64_t, size, size)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_write,
	TRACE_INFO)

/**
 * @brief Trace Ceph read
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] fileid	target fileid (means inode)
 * @param[in] size	written size
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_read,
	TP_ARGS(const char *, function,
		int, line,
		uint64_t, fileid,
		uint64_t, size),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(uint64_t, ino, fileid)
		ctf_integer(uint64_t, size, size)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_read,
	TRACE_INFO)

/**
 * @brief Trace Ceph readdir
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] fileid	target dir ino number
 * @param[in] rfiles	target dir's rfiles
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_readdir,
	TP_ARGS(const char *, function,
		int, line,
		uint64_t, fileid,
		uint64_t, rfiles),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(uint64_t, ino, fileid)
		ctf_integer(uint64_t, rfiles, rfiles)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_readdir,
	TRACE_INFO)

/**
 * @brief Trace Ceph getattrs
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] fileid	target ino
 * @param[in] size	target size
 * @param[in] mode	target mode
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_getattrs,
	TP_ARGS(const char *, function,
		int, line,
		uint64_t, fileid,
		uint64_t, size,
		uint16_t, mode),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(uint64_t, ino, fileid)
		ctf_integer(uint64_t, size, size)
		ctf_integer_hex(uint16_t, mode, mode)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_getattrs,
	TRACE_INFO)

/*
 * @brief Trace Ceph setattrs
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] fileid	target ino
 * @param[in] size	target size
 * @param[in] mode	target mode
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_setattrs,
	TP_ARGS(const char *, function,
		int, line,
		uint64_t, fileid,
		uint64_t, size,
		uint16_t, mode),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(uint64_t, ino, fileid)
		ctf_integer(uint64_t, size, size)
		ctf_integer_hex(uint16_t, mode, mode)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_setattrs,
	TRACE_INFO)

/**
 * @brief Trace Ceph unlink
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] name	unlink target name
 * @param[in] type	unlink target type
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_unlink,
	TP_ARGS(const char *, function,
		int, line,
		const char *, name,
		const char *, type),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_string(name, name)
		ctf_string(type, type)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_unlink,
	TRACE_INFO)

/**
 * @brief Trace Ceph commit
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] ino	node inode number
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_commit,
	TP_ARGS(const char *, function,
		int, line,
		uint64_t, ino),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(uint64_t, ino, ino)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_commit,
	TRACE_INFO)

/**
 * @brief Trace Ceph lock
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] ino	node inode number
 * @param[in] op	lock op
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_lock,
	TP_ARGS(const char *, function,
		int, line,
		uint64_t, ino,
		int, op),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(uint64_t, ino, ino)
		ctf_integer(int, lock_op, op)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_lock,
	TRACE_INFO)

/**
 * @brief Trace Ceph lease
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] ino	node inode number
 * @param[in] cmd	delegations command
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_lease,
	TP_ARGS(const char *, function,
		int, line,
		uint64_t, ino,
		int, cmd),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(uint64_t, ino, ino)
		ctf_integer(int, deleg_cmd, cmd)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_lease,
	TRACE_INFO)

/**
 * @brief Trace Ceph fallocate
 *
 * @param[in] function	Function name
 * @param[in] line	Line number of call
 * @param[in] ino	node inode number
 * @param[in] mode	falloc mode
 * @param[in] offset	falloc offset
 * @param[in] length	falloc length
 */

TRACEPOINT_EVENT(
	fsalceph,
	ceph_falloc,
	TP_ARGS(const char *, function,
		int, line,
		uint64_t, ino,
		int, mode,
		uint64_t, offset,
		uint64_t, length),
	TP_FIELDS(
		ctf_string(func, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(uint64_t, ino, ino)
		ctf_integer(int, falloc_mode, mode)
		ctf_integer(uint64_t, offset, offset)
		ctf_integer(uint64_t, length, length)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalceph,
	ceph_falloc,
	TRACE_INFO)

#endif /* GANESHA_LTTNG_FSALCEPH_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/fsal_ceph.h"

#include <lttng/tracepoint-event.h>
