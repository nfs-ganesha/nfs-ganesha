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
 * @param[in] name	File name
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_alloc,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		const char *, name,
		uint32_t, refcount),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_string(name, name)
		ctf_integer(uint32_t, refcount, refcount)
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
 * @param[in] name	File name
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_free,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		const char *, name),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_string(name, name)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_free,
	TRACE_INFO)

/**
 * @brief Trace a lookup of an obj
 *
 * @param[in] function	Name of function releasing ref
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 * @param[in] name	File name
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_lookup,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		const char *, name),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_string(name, name)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_lookup,
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
		int, numlinks,
		int, is_export),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_integer(int, numlinks, numlinks)
		ctf_integer(int, is_export, is_export)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_inuse,
	TRACE_INFO)

/**
 * @brief Trace a getattrs call
 *
 * @param[in] function	Name of function
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


/**
 * @brief Trace a setattrs call
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 * @param[in] name	File name
 * @param[in] size	Size of file
 * @param[in] numlinks	Number of links
 * @param[in] chg	Change counter
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_setattrs,
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
	mem_setattrs,
	TRACE_INFO)


/**
 * @brief Trace a write call
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 * @param[in] name	File name
 * @param[in] size	Size of file
 * @param[in] dsize	Data size of file
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_write,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		const char *, name,
		void *, state,
		uint64_t, size,
		uint64_t, dsize),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_string(name, name)
		ctf_integer_hex(void *, state, state)
		ctf_integer(uint64_t, size, size)
		ctf_integer(uint64_t, dsize, dsize)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_write,
	TRACE_INFO)


/**
 * @brief Trace a read call
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 * @param[in] name	File name
 * @param[in] size	Size of file
 * @param[in] dsize	Data size of file
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_read,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		const char *, name,
		void *, state,
		uint64_t, size,
		uint64_t, dsize),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_string(name, name)
		ctf_integer_hex(void *, state, state)
		ctf_integer(uint64_t, size, size)
		ctf_integer(uint64_t, dsize, dsize)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_read,
	TRACE_INFO)


/**
 * @brief Trace an open call
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 * @param[in] name	File name
 * @param[in] state	Address of state
 * @param[in] truncated	True if truncated
 * @param[in] setattrs	Pointer of setattrs
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_open,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		const char *, name,
		void *, state,
		uint32_t, truncated,
		uint32_t, setattrs),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_string(name, name)
		ctf_integer_hex(void *, state, state)
		ctf_integer(uint32_t, truncated, truncated)
		ctf_integer(uint32_t, setattrs, setattrs)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_open,
	TRACE_INFO)

/**
 * @brief Trace a close call
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 * @param[in] name	File name
 * @param[in] state	Address of state
 * @param[in] truncated	True if truncated
 * @param[in] setattrs	Pointer of setattrs
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_close,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		const char *, name,
		void *, state),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_string(name, name)
		ctf_integer_hex(void *, state, state)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_close,
	TRACE_INFO)

/**
 * @brief Trace an creat_handle
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] obj	Address of obj
 * @param[in] name	File name
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_create_handle,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		const char *, name),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_string(name, name)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_create_handle,
	TRACE_INFO)


/**
 * @brief Trace an alloc_state
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] state	Address of state
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_alloc_state,
	TP_ARGS(const char *, function,
		int, line,
		void *, state),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, state, state)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_alloc_state,
	TRACE_INFO)



/**
 * @brief Trace a rename
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] olddir	Name of old directory
 * @param[in] oldname	Name of old file
 * @param[in] newdir	Name of new directory
 * @param[in] newname	Name of new file
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_rename,
	TP_ARGS(const char *, function,
		int, line,
		void *, obj,
		const char *, olddir,
		const char *, oldname,
		const char *, newdir,
		const char *, newname),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, obj, obj)
		ctf_string(olddir, olddir)
		ctf_string(oldname, oldname)
		ctf_string(newdir, newdir)
		ctf_string(newname, newname)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_rename,
	TRACE_INFO)

/**
 * @brief Trace a link
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] olddir	Name of old directory
 * @param[in] oldname	Name of old file
 * @param[in] newdir	Name of new directory
 * @param[in] newname	Name of new file
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_link,
	TP_ARGS(const char *, function,
		int, line,
		void *, dir,
		const char *, dirname,
		void *, file,
		const char *, filename,
		const char *, newname,
		uint32_t, numlinks),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, dir, dir)
		ctf_string(dirname, dirname)
		ctf_integer_hex(void *, file, file)
		ctf_string(filename, filename)
		ctf_string(newname, newname)
		ctf_integer(uint32_t, numlinks, numlinks)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_link,
	TRACE_INFO)


/**
 * @brief Trace a unlink
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] olddir	Name of old directory
 * @param[in] oldname	Name of old file
 * @param[in] newdir	Name of new directory
 * @param[in] newname	Name of new file
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_unlink,
	TP_ARGS(const char *, function,
		int, line,
		void *, dir,
		const char *, dirname,
		void *, file,
		const char *, filename,
		uint32_t, numlinks),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, dir, dir)
		ctf_string(dirname, dirname)
		ctf_integer_hex(void *, file, file)
		ctf_string(filename, filename)
		ctf_integer(uint32_t, numlinks, numlinks)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_unlink,
	TRACE_INFO)


/**
 * @brief Trace a get_ref
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] olddir	Name of old directory
 * @param[in] oldname	Name of old file
 * @param[in] newdir	Name of new directory
 * @param[in] newname	Name of new file
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_get_ref,
	TP_ARGS(const char *, function,
		int, line,
		void *, file,
		const char *, filename,
		uint32_t, refcount),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, file, file)
		ctf_string(filename, filename)
		ctf_integer(uint32_t, refcount, refcount)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_get_ref,
	TRACE_INFO)

/**
 * @brief Trace a put_ref
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] olddir	Name of old directory
 * @param[in] oldname	Name of old file
 * @param[in] newdir	Name of new directory
 * @param[in] newname	Name of new file
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_put_ref,
	TP_ARGS(const char *, function,
		int, line,
		void *, file,
		const char *, filename,
		uint32_t, refcount),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, file, file)
		ctf_string(filename, filename)
		ctf_integer(uint32_t, refcount, refcount)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_put_ref,
	TRACE_INFO)


/**
 * @brief Trace a readdir
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] olddir	Name of old directory
 * @param[in] oldname	Name of old file
 * @param[in] newdir	Name of new directory
 * @param[in] newname	Name of new file
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_readdir,
	TP_ARGS(const char *, function,
		int, line,
		void *, dir,
		const char *, dirname,
		uint64_t, whence),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, dir, dir)
		ctf_string(dirname, dirname)
		ctf_integer(uint64_t, whence, whence)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_readdir,
	TRACE_INFO)

/**
 * @brief Trace a mkdir
 *
 * @param[in] function	Name of function
 * @param[in] line	Line number of call
 * @param[in] dir	Directory handle
 * @param[in] dirname	Name of directory
 * @param[in] newname	Name of new directory
 */
TRACEPOINT_EVENT(
	fsalmem,
	mem_mkdir,
	TP_ARGS(const char *, function,
		int, line,
		void *, dir,
		const char *, dirname,
		const char *, newname),
	TP_FIELDS(
		ctf_string(function, function)
		ctf_integer(int, line, line)
		ctf_integer_hex(void *, dir, dir)
		ctf_string(dirname, dirname)
		ctf_string(newname, newname)
	)
)

TRACEPOINT_LOGLEVEL(
	fsalmem,
	mem_mkdir,
	TRACE_INFO)


#endif /* GANESHA_LTTNG_FSALMEM_TP_H */

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gsh_lttng/fsal_mem.h"

#include <lttng/tracepoint-event.h>
