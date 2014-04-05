/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2010, 2012
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    fsi_ipc_ccl.h
 * Description: Common code layer common function definitions
 * Author:      FSI IPC Team
 * ----------------------------------------------------------------------------
 */

#ifndef __FSI_IPC_CCL_H__
#define __FSI_IPC_CCL_H__

/* Linux includes */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/acl.h>
#include <sys/syscall.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ------------------------------------------------------
 * FSI defines -- must match those in fsi_ipc_common.h
 * ------------------------------------------------------
 */
#define FSI_CCL_IPC_OPEN_IP_ADDR_STR_SIZE        128
#define MAX_FSI_CCL_IPC_SHMEM_BUF_PER_STREAM       1
#define FSI_CCL_IPC_SHMEM_WRITEBUF_PER_BUF         4
#define FSI_CCL_IPC_SHMEM_READBUF_PER_BUF          4
#define FSI_CCL_MAX_STREAMS                      800
#define FSI_CCL_IPC_EOK                            0
#define FSI_CCL_IPC_CLOSE_HANDLE_REQ_Q_KEY    0x7656
#define FSI_CCL_IPC_CLOSE_HANDLE_RSP_Q_KEY    0x7657

/*
 * 8208 == SymLinkReqMsg size
 * 14 * 8 == CommonMsgHdr size
 * IP_ADDR == extra data in CommonMsgHdr
 */
#define SYMLINK_REQ_MSG_SIZE    8208
#define COMMON_MSG_HDR_SIZE  (14 * 8)
#define FSI_CCL_IPC_MSG_SIZE (SYMLINK_REQ_MSG_SIZE + COMMON_MSG_HDR_SIZE +     \
				FSI_CCL_IPC_OPEN_IP_ADDR_STR_SIZE)

/* matches "PersistentHandle" */
#define FSI_CCL_PERSISTENT_HANDLE_N_BYTES         32
	struct CCLPersistentHandle {
		char handle[FSI_CCL_PERSISTENT_HANDLE_N_BYTES];
	};

/* matches "msg_t" */
	struct ccl_msg_t {
		long int mtype;
		char mtext[FSI_CCL_IPC_MSG_SIZE];
	};

/* matches ClientOpDynamicFsInfoRspMsg */
	struct CCLClientOpDynamicFsInfoRspMsg {
		uint64_t totalBytes;
		uint64_t freeBytes;
		uint64_t availableBytes;
		uint64_t totalFiles;
		uint64_t freeFiles;
		uint64_t availableFiles;
		struct timespec time;
	};
/*
 * ----------------------------------------------------
 * END CCL matching definitions
 * ----------------------------------------------------
 */

/* needed for use with locking macros */
#define CCL_UP_MUTEX_LOCK   ccl_up_mutex_lock
#define CCL_UP_MUTEX_UNLOCK ccl_up_mutex_unlock
/*
 * CCL Version to ensure that Ganesha FSAL and CCL does not go out of sync
 * We use "PT-Version.Minor" the build number when this file changes. The
 * intent is not to change this for every minor. For instance, the current
 * minor may be 172, but the version may remain "4.1.0.164", indicating that
 * the last minor this file changed is in minor 164.
 */
#define PT_FSI_CCL_VERSION "4.1.0.201"

#define UNUSED_ARG(arg) (void)(arg);

#define FSI_CIFS_RESERVED_STREAMS   4	/* CIFS does not allow handles 0-2 */

#define FSI_BLOCK_ALIGN(x, blocksize) \
(((x) % (blocksize)) ? (((x) / (blocksize)) * (blocksize)) : (x))

/* When polling for results, number of seconds to try before timingout */
#define FSI_COMMAND_TIMEOUT_SEC      900
/* In seconds, if timed responses exceed then make log entry*/
#define FSI_COMMAND_LOG_THRESHOLD_SEC 20
/* Parameter to usleep */
#define USLEEP_INTERVAL        10000

/* Timeout for opened handle to be considered old in polling thread path */
#define CCL_POLLING_THREAD_HANDLE_TIMEOUT_SEC 300
/* Timeout for on-demand hread looking for handles to close */
#define CCL_ON_DEMAND_HANDLE_TIMEOUT_SEC       15

#define PTFSAL_FILESYSTEM_NUMBER              77
#define FSI_IPC_MSGID_BASE               5000000

	typedef enum fsi_ipc_trace_level {
		FSI_NO_LEVEL = 0,
		FSI_FATAL,
		FSI_ERR,
		FSI_WARNING,
		FSI_NOTICE,
		FSI_STAT,
		FSI_INFO,
		FSI_DEBUG,

		/* this one must be last */
		FSI_NUM_TRACE_LEVELS
	} fsi_ipc_trace_level;

#ifndef __GNUC__
#define __attribute__(x)	/*nothing */
#endif				/* __GNUC__ */

/* Log-related functions and declarations */

#define MAX_LOG_LINE_LEN 512

	typedef int (*log_function_t) (int level, const char *message);
	typedef int (*log_level_check_function_t) (int level);
	int ccl_log(const fsi_ipc_trace_level level, const char *func,
		    const char *format, ...);
/*
 * The following functions enable compile-time check with a cost of a function
 * call. Ths function is empty, but due to its  __attribute__ declaration
 * the compiler checks the format string which is passed to it by the
 * FSI_TRACE...() mechanism.
 */
	static void
	 compile_time_check_func(const char *fmt, ...)
	    __attribute__ ((format(printf, 1, 2)));	/* 1=format 2=params */

/*
 * ----------------------------------------------------------------------------
 * This is needed to make FSI_TRACE macro work
 * Part of the magic of __attribute__ is this function needs to be defined,
 * though it's a noop
 */
	static inline void
	 compile_time_check_func(const char *fmt, ...) {
		UNUSED_ARG(fmt);
		/* do nothing */
	}
/*
 * Our own trace macro that adds standard prefix to statements that includes
 * the level and function name (by calling the wrapper ccl_log)
 */
#define FSI_TRACE(level, ...)                                              \
{                                                                          \
	compile_time_check_func(__VA_ARGS__);                              \
	CCL_LOG(level, __func__, __VA_ARGS__);                             \
}
#define FSI_TRACE_COND_RC(rc, errVal, ...)                                 \
{                                                                          \
	FSI_TRACE((errVal) == (rc) ? FSI_INFO : FSI_ERR, ## __VA_ARGS__);  \
}
#define FSI_TRACE_HANDLE(handle)                                           \
{                                                                          \
	uint64_t *handlePtr = (uint64_t *) handle;                         \
	FSI_TRACE(FSI_INFO, "persistent handle: 0x%lx %lx %lx %lx",        \
		handlePtr[0], handlePtr[1], handlePtr[2], handlePtr[3]);   \
}
#define WAIT_SHMEM_ATTACH()                                                \
{                                                                          \
	while (g_shm_at == 0) {                                            \
		FSI_TRACE(FSI_INFO, "waiting for shmem attach");           \
		sleep(1);                                                  \
	}                                                                  \
}
#define CCL_CLOSE_STYLE_NORMAL             0
#define CCL_CLOSE_STYLE_FIRE_AND_FORGET    1
#define CCL_CLOSE_STYLE_NO_INDEX           2
	extern int g_shm_id;	/* SHM ID */
	extern char *g_shm_at;	/* SHM Base Address */
	extern int g_io_req_msgq;
	extern int g_io_rsp_msgq;
	extern int g_non_io_req_msgq;
	extern int g_non_io_rsp_msgq;
	extern int g_shmem_req_msgq;
	extern int g_shmem_rsp_msgq;
	extern char g_chdir_dirpath[PATH_MAX];
	extern uint64_t g_client_pid;
	extern uint64_t g_server_pid;
	/* FSI client handles */
	extern struct file_handles_struct_t g_fsi_handles;
	/* FSI client Dir handles */
	extern struct dir_handles_struct_t g_fsi_dir_handles;
	/* FSI client ACL handles */
	extern struct acl_handles_struct_t g_fsi_acl_handles;

	extern uint64_t g_client_trans_id;	/* FSI global transaction id */
	extern int g_close_trace;   /* FSI global trace of io rates at close */
	extern int g_multithreaded;	/* ganesha = true, samba = false */

	extern char g_client_address[];

#define MAX_FSI_PERF_COUNT            1000	/* for m_perf_xxx counters */

/* enum for client buffer return code state */
	enum e_buf_rc_state {
		BUF_RC_STATE_UNKNOWN = 0,	/* default */
		BUF_RC_STATE_PENDING,	/* waiting on server Rc */
		BUF_RC_STATE_FILLING,	/* filling with write data */
		/* received Rc, not processed by client */
		BUF_RC_STATE_RC_NOT_PROCESSED,
		BUF_RC_STATE_RC_PROCESSED  /* client processed received Rc */
	};

	enum e_ccl_write_mode {
		CCL_WRITE_IMMEDIATE = 0,/* write should be immediately issued */
		CCL_WRITE_BUFFERED  /* pwrite does not need to issue write */
	};
/*
 * ----------------------------------------------------------------------------
 *  @struct io_buf_status_t
 *  @brief  contains I/O buffer status
 * ----------------------------------------------------------------------------
 */
	struct io_buf_status_t {
		char *m_p_shmem;	/* IPC shmem pointer */
		int m_this_io_op;	/* enumerated I/O operation */
		/* (read/write/other I/O) */
		int m_buf_in_use;   /* used to determine available buffers */
		/* a usable buffer is not in use */
		/* and not "not allocated" */
		int m_data_valid;	/* set on read when data received */
		int m_bytes_in_buf;	/* number of bytes of data in buffer */
		int m_buf_use_enum;	/* BufUsexxx enumeration */
		int m_buf_rc_state;	/* enum return code state BufRcXxx */
		uint64_t m_trans_id;	/* transaction id */
	};

	typedef struct fsi_stat_struct__ {
		uint64_t st_dev;	/* Device */
		uint64_t st_ino;	/* File serial number */
		uint64_t st_mode;	/* File mode */
		uint64_t st_nlink;	/* Link count */
		uint64_t st_uid;	/* User ID of the file's owner */
		uint64_t st_gid;	/* Group ID of the file's group */
		uint64_t st_rdev;	/* Device number, if device */
		uint64_t st_size;	/* Size of file, in bytes */
		uint64_t st_atime_sec;	/* Time of last access  sec only */
		uint64_t st_mtime_sec;	/* Time of last modification  sec */
		uint64_t st_ctime_sec;	/* Time of last change  sec */
		uint64_t st_btime_sec;	/* Birthtime */
		uint64_t st_blksize;	/* Optimal block size for I/O */
		uint64_t st_blocks;	/* Number of 512-byte blocks */
		/* allocated */
		struct CCLPersistentHandle st_persistentHandle;
	} fsi_stat_struct;

	enum e_nfs_state {
		NFS_OPEN = 1,
		NFS_CLOSE = 2,
		CCL_CLOSING = 4,
		CCL_CLOSE = 8,

		IGNORE_STATE = 16
	};

/*
 * ----------------------------------------------------------------------------
 * @struct file_handle_t
 * @brief  client file handle
 * ----------------------------------------------------------------------------
 */
	struct file_handle_t {
		/* full filename used with API */
		char m_filename[PATH_MAX];
		int m_hndl_in_use;	/* used to flag available entries */
		int m_prev_io_op;	/* enumerated I/O operation */
		/* (read/write/other I/O) */
		struct io_buf_status_t
		 m_writebuf_state[MAX_FSI_CCL_IPC_SHMEM_BUF_PER_STREAM *
				  FSI_CCL_IPC_SHMEM_WRITEBUF_PER_BUF];
		/* one entry per write data buffer */

		/* how many write buffers this handle actually uses */
		int m_writebuf_cnt;
		/* index of the filling write buffer (-1 if none) */
		int m_write_inuse_index;
		/* number of bytes in the filling write buffer */
		int m_write_inuse_bytes;
		/* offset of first byte in filling buffer*/
		uint64_t m_write_inuse_offset;
		/* one entry per read data buffer */
		struct io_buf_status_t
		 m_readbuf_state[MAX_FSI_CCL_IPC_SHMEM_BUF_PER_STREAM *
				 FSI_CCL_IPC_SHMEM_READBUF_PER_BUF];
		/* how many read buffers this handle actually uses */
		int m_readbuf_cnt;
		/* SHM handle array */
		uint64_t m_shm_handle[MAX_FSI_CCL_IPC_SHMEM_BUF_PER_STREAM];
		/* set if we are writing and first write is complete*/
		int m_first_write_done;
		/* set if we completed first read */
		int m_first_read_done;
		/* IPC close file response received*/
		int m_close_rsp_rcvd;
		/* IPC fsync file response received*/
		int m_fsync_rsp_rcvd;

		int m_read_at_eof;	/* set if at EOF - only for read */
		uint64_t m_file_loc;	/* used for writes and fstat */

		/*
		 * this is the location assuming
		 * last read or write succeeded
		 * this is the location the next
		 * sequential write (not pwrite)
		 * would use as an offset
		 */
		uint64_t m_file_flags;	/* flags */
		fsi_stat_struct m_stat;
		uint64_t m_fs_handle;	/* handle */
		uint64_t m_exportId;	/* export id */
		int m_deferred_io_rc;	/* deferred io return code */

		int m_dir_not_file_flag;
		/*
		 * set if this handle represents a
		 * directory instead of a file
		 * (open must issue opendir
		 * if the entity being opened
		 * is a directory)
		 */
		struct fsi_struct_dir_t *m_dirp;	/* dir pointer */
		/* if m_dir_not_file_flag is set */
		uint64_t m_resourceHandle; /* handle for resource management */
		struct timeval m_perf_pwrite_start[MAX_FSI_PERF_COUNT];
		struct timeval m_perf_pwrite_end[MAX_FSI_PERF_COUNT];
		struct timeval m_perf_aio_start[MAX_FSI_PERF_COUNT];
		struct timeval m_perf_open_end;
		struct timeval m_perf_close_end;
		uint64_t m_perf_pwrite_count; /* num of pwrite while open */
		uint64_t m_perf_pread_count;  /* num of pread while open */
		uint64_t m_perf_aio_count;    /* num of aio_force while open */
		uint64_t m_perf_fstat_count;  /* num of fstat while open */
		enum e_nfs_state m_nfs_state;
		time_t m_last_io_time;	/* Last time I/O was performed. */
		int m_ftrunc_rsp_rcvd;
		uint64_t m_eio_counter;	/* number of EIOs encountered */
		int m_sticky_rc;	/* 'sticky' rc */
		uint64_t m_outstanding_io_count;/* number of unfinished IOs */
		/* on this handle */
	};

/*
 * ----------------------------------------------------------------------------
 * @struct file_handles_struct_t
 * @brief  contains filehandles
 * ----------------------------------------------------------------------------
 */
	struct file_handles_struct_t {
		struct file_handle_t m_handle[FSI_CCL_MAX_STREAMS +
					      FSI_CIFS_RESERVED_STREAMS];
		int m_count;	/* maximum handle used */
	};

/*
 * ----------------------------------------------------------------------------
 * @struct fsi_struct_dir_t
 * @brief  fsi unique directory information
 * ----------------------------------------------------------------------------
 */
	struct fsi_struct_dir_t {
		uint64_t m_dir_handle_index;
		uint64_t m_last_ino;	/* last inode we responded with */
		uint64_t m_exportId;
		char dname[PATH_MAX];
		struct dirent dbuf;	/* generic DIRENT buffer */
	};

/*
 * ----------------------------------------------------------------------------
 *  @struct dir_handle_t
 *  @brief  directory handle
 * ----------------------------------------------------------------------------
 */
	struct dir_handle_t {
		int m_dir_handle_in_use;   /* used to flag available entries*/
		uint64_t m_fs_dir_handle;	/* fsi_facade handle */
		struct fsi_struct_dir_t m_fsi_struct_dir;/* directory struct */
		uint64_t m_resourceHandle;	/* server resource handle */
	};

/*
 * ----------------------------------------------------------------------------
 *  @struct dir_handles_struct_t
 *  @brief  contains directory handles
 * ----------------------------------------------------------------------------
 */
	struct dir_handles_struct_t {
		struct dir_handle_t m_dir_handle[FSI_CCL_MAX_STREAMS];
		int m_count;
	};

/*
 * ----------------------------------------------------------------------------
 *  @struct acl_handle_t
 *  @brief  ACL handle
 * ----------------------------------------------------------------------------
 */
	struct acl_handle_t {
		int m_acl_handle_in_use;/* used to flag available entries */
		uint64_t m_acl_handle;	/* acl handle */
		uint64_t m_resourceHandle;	/* server resource handle */
	};

/*
 * ----------------------------------------------------------------------------
 *  @struct acl_handles_struct_t
 *  @brief  contains ACL handles
 * ----------------------------------------------------------------------------
 */
	struct acl_handles_struct_t {
		struct acl_handle_t m_acl_handle[FSI_CCL_MAX_STREAMS];
		int m_count;
	};

/*
 * ----------------------------------------------------------------------------
 * Structures for CCL abstraction
 * ----------------------------------------------------------------------------
 */
#define NONIO_MSG_TYPE \
((g_multithreaded) ? (unsigned long)(syscall(SYS_gettid)) : g_client_pid)
#define MULTITHREADED 1
#define NON_MULTITHREADED 0

/* The context every call to CCL is made in */
/* THIS IS ALSO OFTEN REFERRED TO AS THE "CONTEXT" */
	typedef struct {
		uint64_t export_id;	/* export id */
		uint64_t uid;	/* user id of the connecting user */
		uint64_t gid;	/* group id of the connecting user */
		char client_address[256];	/* address of client */
		char *export_path;	/* export path name */

		/*
		 * TODO check on if the next fiels are used by fsal or ccl
		 * next 2 fields left over from prototype -
		 * do not use these if not already using
		 */
		const char *param;	/* incoming parameter */
		int handle_index;	/* Samba File descriptor fsp->fh->fd */
		/*  or essentially or index into our */
		/* global g_fsi_handles.m_handle[] array */
	} ccl_context_t;

/*
 * ----------------------------------------------------------------------------
 * End of defines new for CCL abstraction
 * ----------------------------------------------------------------------------
 */
/*
 * FSI IPC Statistics definitions
 * ----------------------------------------------------------------------------
 *  @struct ipc_client_stats_t
 *  @brief  contains client statistics structure
 * ----------------------------------------------------------------------------
 */
/* Statistics Logging interval of 5 minutes */
#ifndef UNIT_TEST
#define FSI_IPC_CLIENT_STATS_LOG_INTERVAL (60 * 5)
#else				/* UNIT_TEST */
#define FSI_IPC_CLIENT_STATS_LOG_INTERVAL 2
#endif				/* UNIT_TEST */

#define FSI_RETURN(result, handle)     \
{                                      \
	ccl_ipc_stats_logger(handle);        \
	return result;                       \
}

	struct ipc_client_stats_t {
		uint64_t count;
		uint64_t sum;
		uint64_t sumsq;
		uint64_t min;
		uint64_t max;
		uint64_t overflow_flag;
	};

#define VARIANCE(pstat)                                                      \
	((pstat)->count > 1  ?                                               \
	(((pstat)->sumsq - (pstat)->sum * ((pstat)->sum / (pstat)->count)) / \
	((pstat)->count - 1)) :                                              \
	0)

/*
 * ----------------------------------------------------------------------------
 * Defines for CCL Internal Statistics
 *
 * Defines for IO Idle time statistic collection,  this is time we are
 * idle waiting for user to send a read or write or doing other operations
 * ----------------------------------------------------------------------------
 */
	extern struct timeval g_begin_io_idle_time;
	extern struct ipc_client_stats_t g_client_io_idle_time;
	extern uint64_t g_num_reads_in_progress;
	extern uint64_t g_num_writes_in_progress;

#define START_IO_IDLE_CLOCK()                                                 \
{                                                                             \
	if (g_begin_io_idle_time.tv_sec != 0) {                               \
		FSI_TRACE(FSI_ERR,                                            \
			"IDLE CLOCK was already started, distrust idle stat");\
	}                                                                     \
	int rc = gettimeofday(&g_begin_io_idle_time, NULL);                   \
	if (rc != 0)                                                          \
		FSI_TRACE(FSI_ERR, "gettimeofday rc = %d", rc);               \
}

#define END_IO_IDLE_CLOCK()                                                   \
{                                                                             \
	struct timeval curr_time;                                              \
	struct timeval diff_time;                                              \
	if (g_begin_io_idle_time.tv_sec == 0) {                                \
		FSI_TRACE(FSI_ERR,                                             \
			"IDLE CLOCK already not running, distrust idle stat"); \
	}                                                                      \
	int rc = gettimeofday(&curr_time, NULL);                               \
	if (rc != 0) {                                                         \
		FSI_TRACE(FSI_ERR, "gettimeofday rc = %d", rc);                \
	} else {                                                               \
		timersub(&curr_time, &g_begin_io_idle_time, &diff_time);       \
		uint64_t delay =                                               \
			diff_time.tv_sec * 1000000 + diff_time.tv_usec;        \
		if (update_stats(&g_client_io_idle_time, delay)) {             \
			FSI_TRACE(FSI_WARNING,                                 \
				"IO Idle time stats sum square overflow");     \
		}                                                              \
	}                                                                      \
memset(&g_begin_io_idle_time, 0, sizeof(g_begin_io_idle_time));                \
}

#define STATS_MUTEX_LOCK()                                                    \
{                                                                             \
	if (g_multithreaded) {                                                \
		CCL_UP_MUTEX_LOCK(&g_statistics_mutex);                       \
	} else {                                                              \
		ccl_up_mutex_lock(&g_statistics_mutex);                       \
	}                                                                     \
}                                                                             \

#define STATS_MUTEX_UNLOCK()                                                  \
{                                                                             \
	if (g_multithreaded) {                                                \
		CCL_UP_MUTEX_UNLOCK(&g_statistics_mutex);                     \
	} else {                                                              \
		ccl_up_mutex_unlock(&g_statistics_mutex);                     \
	}                                                                     \
}                                                                             \

#define IDLE_STAT_READ_START()                                                \
{                                                                             \
	STATS_MUTEX_LOCK();                                                   \
	g_num_reads_in_progress++;					      \
	if (((g_num_reads_in_progress + g_num_writes_in_progress) == 1) &&    \
		(g_begin_io_idle_time.tv_sec != 0)) {			      \
		END_IO_IDLE_CLOCK();					      \
	}		                                                      \
	STATS_MUTEX_UNLOCK();                                                 \
}

#define IDLE_STAT_READ_END()                                                  \
{                                                                             \
	STATS_MUTEX_LOCK();                                                   \
	if (g_num_reads_in_progress == 0) {                                   \
		FSI_TRACE(FSI_ERR,                                            \
			"IO Idle read count off, distrust IDLE stat ");       \
	}                                                                     \
	g_num_reads_in_progress--;                                            \
	if ((g_num_reads_in_progress + g_num_writes_in_progress) == 0) {      \
		START_IO_IDLE_CLOCK();                                        \
	}                                                                     \
	STATS_MUTEX_UNLOCK();                                                 \
}

#define IDLE_STAT_WRITE_START()                                               \
{                                                                             \
	STATS_MUTEX_LOCK();                                                   \
	g_num_writes_in_progress++;                                           \
	if (((g_num_reads_in_progress + g_num_writes_in_progress) == 1) &&    \
		(g_begin_io_idle_time.tv_sec != 0)) {                         \
		END_IO_IDLE_CLOCK();                                          \
	}                                                                     \
	STATS_MUTEX_UNLOCK();                                                 \
}

#define IDLE_STAT_WRITE_END()                                                 \
{                                                                             \
	STATS_MUTEX_LOCK();                                                   \
	if (g_num_writes_in_progress == 0) {                                  \
		FSI_TRACE(FSI_DEBUG,                                          \
			"IO Idle write count off, distrust IDLE stat ");      \
	}                                                                     \
	g_num_writes_in_progress--;                                           \
	if ((g_num_reads_in_progress + g_num_writes_in_progress) == 0) {      \
		START_IO_IDLE_CLOCK();                                        \
	}                                                                     \
	STATS_MUTEX_UNLOCK();                                                 \
}

/*
 * ---------------------------------------------------------------------------
 * End of CCL Internal Statistics defines
 * ---------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------
 * Definitions for ACL structures for interface to ACL functions
 *  - these are not used by NFS
 *  - these are patterened after Sambe versions, Samba Shim layer should be
 *    sure to translate these to Samba versions... for now they match Samba
 *    but if Samba changes we need to detect
 * ---------------------------------------------------------------------------
 */
#define CCL_ACL_FIRST_ENTRY                     0
#define CCL_ACL_NEXT_ENTRY                      1

#define CCL_ACL_TYPE_ACCESS                     0
#define CCL_ACL_TYPE_DEFAULT                    1

/*
 * ---------------------------------------------------------------------------
 * End of ACL definitions
 * ---------------------------------------------------------------------------
 */

/*
 * This determines how many times to poll when an existing opened handle
 * that we are trying to reopen, but we are already closing that handle.
 * Open processing will poll until that handle is completely closed before
 * opening it again.
 */
#define CCL_MAX_CLOSING_TO_CLOSE_POLLING_COUNT 480

/*
 * ---------------------------------------------------------------------------
 * Forward declarations for structs used in CCL function prototypes
 * ---------------------------------------------------------------------------
 */
	struct CommonShmemDataHdr;
	struct CommonMsgHdr;
	struct ClientOpPreadReqMsg;
	struct ClientOpStatRsp;
	struct ClientOpDynamicFsInfoRspMsg;

/*
 * ---------------------------------------------------------------------------
 * Function Prototypes
 * ---------------------------------------------------------------------------
 */
	int ccl_check_version(char *version);
	char *ccl_get_version(void);
	int ccl_init(int multi_threaded, log_function_t log_fn,
		     log_level_check_function_t log_level_check_fn, int
		     ipc_ccl_to_component_trc_level_map[FSI_NUM_TRACE_LEVELS]);
	int add_acl_handle(uint64_t fs_acl_handle);
	int add_dir_handle(uint64_t fs_dir_handle);
	int add_fsi_handle(struct file_handle_t *p_new_handle);
	int convert_fsi_name(ccl_context_t *handle, const char *filename,
			     char *sv_filename, const size_t sv_filename_size);
	int delete_acl_handle(uint64_t aclHandle);
	int delete_dir_handle(int dir_handle_index);
	int delete_fsi_handle(int handle_index);
	int ccl_cache_name_and_handle(char *handle, char *name);
	int ccl_check_handle_index(int handle_index);
	int ccl_find_handle_by_name_and_export(const char *filename,
					       ccl_context_t *handle);
	int ccl_update_cache_stat(const char *filename, uint64_t newMode,
				  uint64_t export_id);
	int ccl_find_dir_handle_by_name_and_export(const char *filename,
						   ccl_context_t *handle);
	int ccl_set_stat_buf(fsi_stat_struct *dest,
			     const struct ClientOpStatRsp *src);
	int ccl_get_name_from_handle(char *handle, char *name);
	int ccl_stat(ccl_context_t *handle, const char *filename,
		     fsi_stat_struct *sbuf);
	int ccl_fstat(int handle_index, fsi_stat_struct *sbuf);
	int ccl_stat_by_handle(ccl_context_t *context,
			       struct CCLPersistentHandle *handle,
			       fsi_stat_struct *sbuf);
	uint64_t get_acl_resource_handle(uint64_t aclHandle);
	int have_pending_io_response(int handle_index);
	int io_msgid_from_index(int index);
	void ld_common_msghdr(struct CommonMsgHdr *p_msg_hdr,
			      uint64_t transaction_type, uint64_t data_length,
			      uint64_t export_id, int handle_index,
			      int fs_handle, int use_crc, int is_IO_q,
			      const char *client_ip_addr);
	void ld_uid_gid(uint64_t *uid, uint64_t *gid, ccl_context_t *handle);
	void load_shmem_hdr(struct CommonShmemDataHdr *p_shmem_hdr,
			    uint64_t transaction_type, uint64_t data_length,
			    uint64_t offset, int handle_index,
			    uint64_t transaction_id, int use_crc);
	void perform_msg_delay(struct timeval *pdiff_time);
	int rcv_msg_nowait(int msg_id, void *p_msg_buf, size_t msg_size,
			   long msg_type);
	int rcv_msg_wait(int msg_id, void *p_msg_buf, size_t msg_size,
			 long msg_type);
	int rcv_msg_wait_block(int msg_id, void *p_msg_buf, size_t msg_size,
			       long msg_type);
	int wait_for_response(const int msg_id, void *p_msg_buf,
			      const size_t msg_size, const long msg_type,
			      const struct CommonMsgHdr *p_hdr,
			      const uint64_t transaction_id,
			      const uint64_t transaction_type,
			      const int min_rsp_msg_bytes);
	int send_msg(int msg_id, const void *p_msg_buf, size_t msg_size);
	int ccl_chmod(ccl_context_t *handle, const char *path, mode_t mode);
	int ccl_chown(ccl_context_t *handle, const char *path, uid_t uid,
		      gid_t gid);
	int ccl_ntimes(ccl_context_t *handle, const char *filename,
		       uint64_t atime, uint64_t mtime, uint64_t btime);
	int ccl_mkdir(ccl_context_t *handle, const char *path, mode_t mode);
	int ccl_rmdir(ccl_context_t *handle, const char *path);
	int ccl_get_real_filename(ccl_context_t *handle, const char *path,
				  const char *name, char *found_name,
				  const size_t found_name_max_size);
	uint64_t ccl_disk_free(ccl_context_t *handle, const char *path,
			       uint64_t *bsize, uint64_t *dfree,
			       uint64_t *dsize);
	int ccl_unlink(ccl_context_t *handle, char *path);
	int ccl_rename(ccl_context_t *handle, const char *old_name,
		       const char *new_name);
	int ccl_opendir(ccl_context_t *handle, const char *filename,
			const char *mask, uint32_t attr);
	int ccl_fdopendir(ccl_context_t *handle, int handle_index,
			  const char *mask, uint32_t attributes);
	int ccl_closedir(ccl_context_t *handle, struct fsi_struct_dir_t *dirp);
	int ccl_readdir(ccl_context_t *handle, struct fsi_struct_dir_t *dirp,
			fsi_stat_struct *sbuf);
	void ccl_seekdir(ccl_context_t *handle, struct fsi_struct_dir_t *dirp,
			 long offset);
	long ccl_telldir(ccl_context_t *handle, struct fsi_struct_dir_t *dirp);
	int ccl_chdir(ccl_context_t *handle, const char *path);
	int ccl_fsync(ccl_context_t *handle, int handle_index);
	int ccl_ftruncate(ccl_context_t *handle, int handle_index,
			  uint64_t offset);
	ssize_t ccl_pread(ccl_context_t *handle, void *data, size_t n,
			  uint64_t offset, uint64_t max_readahead_offset);
	ssize_t ccl_pwrite(ccl_context_t *handle, int handle_index,
			   const void *data, size_t n, uint64_t offset);
	int ccl_open(ccl_context_t *handle, char *path, int flags,
		     mode_t mode);
	int ccl_close_internal(ccl_context_t *handle, int handle_index,
			       int close_style,
			       struct file_handle_t *file_handle);
	int ccl_close(ccl_context_t *handle, int handle_index,
		      int close_style);
	int merge_errno_rc(int rc_a, int rc_b);
	int get_any_io_responses(int handle_index, int *p_combined_rc,
				 struct ccl_msg_t *p_msg);
	void issue_read_ahead(struct file_handle_t *p_pread_hndl,
			      int handle_index, uint64_t offset,
			      struct ccl_msg_t *p_msg,
			      struct CommonMsgHdr *p_pread_hdr,
			      struct ClientOpPreadReqMsg *p_pread_req,
			      const ccl_context_t *handle,
			      uint64_t max_readahead_offset);
	void load_deferred_io_rc(int handle_index, int cur_error);
	int merge_errno_rc(int rc_a, int rc_b);
	int parse_io_response(int handle_index, struct ccl_msg_t *p_msg);
	int read_existing_data(struct file_handle_t *p_pread_hndl, char *p_data,
			       uint64_t *p_cur_offset, uint64_t *p_cur_length,
			       int *p_pread_rc, int *p_pread_incomplete,
			       int handle_index);
	int update_read_status(struct file_handle_t *p_pread_hndl,
			       int handle_index, uint64_t cur_offset,
			       struct ccl_msg_t *p_msg,
			       struct CommonMsgHdr *p_pread_hdr,
			       struct ClientOpPreadReqMsg *p_pread_req,
			       const ccl_context_t *handle,
			       uint64_t max_readahead_offset,
			       int *p_combined_rc);
	int verify_io_response(int transaction_type, int cur_index,
			       struct CommonMsgHdr *p_msg_hdr,
			       struct CommonShmemDataHdr *p_shmem_hdr,
			       struct io_buf_status_t *p_io_buf_status);
	int wait_free_write_buf(int handle_index, int *p_combined_rc,
				struct ccl_msg_t *p_msg);
	int flush_write_buffer(int handle_index, ccl_context_t *handle);
	int wait_for_io_results(int handle_index);
	int synchronous_flush_write_buffer(int handle_index,
					   ccl_context_t *handle);
	int ccl_ipc_stats_init();
	void ccl_ipc_stats_set_log_interval(uint64_t interval);
	void ccl_ipc_stats_logger(ccl_context_t *handle);
	void ccl_ipc_stats_on_io_complete(struct timeval *done_time);
	void ccl_ipc_stats_on_io_start(uint64_t delay);
	void ccl_ipc_stats_on_read(uint64_t bytes);
	void ccl_ipc_stats_on_write(uint64_t bytes);
	uint64_t update_stats(struct ipc_client_stats_t *stat, uint64_t value);

/* ACL function prototypes */
	int ccl_sys_acl_get_entry(ccl_context_t *handle, acl_t theacl,
				  int entry_id, acl_entry_t *entry_p);
	int ccl_sys_acl_get_tag_type(ccl_context_t *handle,
				     acl_entry_t entry_d,
				     acl_tag_t *tag_type_p);
	int ccl_sys_acl_get_permset(ccl_context_t *handle, acl_entry_t entry_d,
				    acl_permset_t *permset_p);
	void *ccl_sys_acl_get_qualifier(ccl_context_t *handle,
					acl_entry_t entry_d);
	acl_t ccl_sys_acl_get_file(ccl_context_t *handle, const char *path_p,
				   acl_type_t type);
	int ccl_sys_acl_clear_perms(ccl_context_t *handle,
				    acl_permset_t permset);
	int ccl_sys_acl_add_perm(ccl_context_t *handle, acl_permset_t permset,
				 acl_perm_t perm);
	acl_t ccl_sys_acl_init(ccl_context_t *handle, int count);
	int ccl_sys_acl_create_entry(ccl_context_t *handle, acl_t *pacl,
				     acl_entry_t *pentry);
	int ccl_sys_acl_set_tag_type(ccl_context_t *handle, acl_entry_t entry,
				     acl_tag_t tagtype);
	int ccl_sys_acl_set_qualifier(ccl_context_t *handle, acl_entry_t entry,
				      void *qual);
	int ccl_sys_acl_set_permset(ccl_context_t *handle, acl_entry_t entry,
				    acl_permset_t permset);
	int ccl_sys_acl_set_file(ccl_context_t *handle, const char *name,
				 acl_type_t acltype, acl_t theacl);
	int ccl_sys_acl_delete_def_file(ccl_context_t *handle,
					const char *path);
	int ccl_sys_acl_get_perm(ccl_context_t *handle, acl_permset_t permset,
				 acl_perm_t perm);
	int ccl_sys_acl_free_acl(ccl_context_t *handle, acl_t posix_acl);

/* Prototypes - new for Ganesha */
	int ccl_name_to_handle(ccl_context_t *pvfs_handle, char *path,
			       struct CCLPersistentHandle *phandle);
	int ccl_handle_to_name(ccl_context_t *pvfs_handle,
			       struct CCLPersistentHandle *phandle, char *path);
	int ccl_dynamic_fsinfo(ccl_context_t *pvfs_handle, char *path,
			       struct ClientOpDynamicFsInfoRspMsg *pfs_info);
	int ccl_readlink(ccl_context_t *pvfs_handle, const char *path,
			 char *link_content);
	int ccl_symlink(ccl_context_t *pvfs_handle, const char *path,
			const char *link_content);
	void ccl_update_handle_last_io_timestamp(int handle_index);
	int ccl_update_handle_nfs_state(int handle_index,
					enum e_nfs_state state,
					int expected_state);
	int ccl_safe_update_handle_nfs_state(int handle_index,
					     enum e_nfs_state state,
					     int expected_state);
	int ccl_fsal_try_stat_by_index(ccl_context_t *handle, int handle_index,
				       char *fsal_name, fsi_stat_struct *sbuf);
	int ccl_fsal_try_fastopen_by_index(ccl_context_t *handle,
					   int handle_index, char *fsal_name);
	int ccl_find_oldest_handle();
	bool ccl_can_close_handle(int handle_index, int timeout);
	int ccl_implicit_close_for_nfs(int handle_index_to_close,
				       int close_style);
	int ccl_up_mutex_lock(pthread_mutex_t *mutex);
	int ccl_up_mutex_unlock(pthread_mutex_t *mutex);
	unsigned long ccl_up_self();
	void ccl_close_listener(int closeHandle_req_msgq,
				int closeHandle_rsp_msgq);
/* Added the following functions for lock mutex from FSAL PT side */
	int ccl_lock_io_operation_mutex(int handle_index);
	int ccl_unlock_io_operation_mutex(int handle_index);
	int ccl_lock_io_handle_mutex(int handle_index);
	int ccl_unlock_io_handle_mutex(int handle_index);
	int ccl_lock_file_mutex();
	int ccl_unlock_file_mutex();

/* dir handle mutex */
	extern pthread_mutex_t g_dir_mutex;
/* acl handle mutex */
	extern pthread_mutex_t g_acl_mutex;
/* file handle processing mutex */
	extern pthread_mutex_t g_file_mutex;
	extern pthread_mutex_t g_statistics_mutex;
/* only one thread can parse an io at a time */
	extern pthread_mutex_t g_parseio_mutex;
	extern pthread_mutex_t g_io_handle_mutex[];
	extern pthread_mutex_t g_dir_handle_mutex[];
	extern pthread_mutex_t g_io_operation_mutex[];
	extern pthread_mutex_t g_statistics_mutex;

#endif				/* ifndef __FSI_IPC_CCL_H__ */

#ifdef __cplusplus
}				/* extern "C" */
#endif
