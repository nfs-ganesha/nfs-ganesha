/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
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

/*
 * Copied from 2.6.38-rc2 kernel, taken from diod sources
 * ( http://code.google.com/p/diod/ ) then adapted to ganesha
 */

#ifndef _9P_H
#define _9P_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>

#include "9p_types.h"
#include "fsal_types.h"
#include "sal_data.h"

#ifdef _USE_9P_RDMA
#include <mooshika.h>
#endif

#define NB_PREALLOC_HASH_9P 100
#define NB_PREALLOC_FID_9P  100
#define PRIME_9P 17

#define _9P_LOCK_CLIENT_LEN 64

#define _9P_FID_PER_CONN        1024

/* _9P_MSG_SIZE: maximum message size for 9P/TCP */
#define _9P_MSG_SIZE 70000

#define _9P_HDR_SIZE  4
#define _9P_TYPE_SIZE 1
#define _9P_TAG_SIZE  2
#define _9P_STD_HDR_SIZE (_9P_HDR_SIZE + _9P_TYPE_SIZE + _9P_TAG_SIZE)

/* _9P_BLK_SIZE: (fake) filesystem block size that we return in getattr() */
#define _9P_BLK_SIZE 4096
#define _9P_IOUNIT   0

/**
 * enum _9p_msg_t - 9P message types
 * @_9P_TLERROR: not used
 * @_9P_RLERROR: response for any failed request for 9P2000.L
 * @_9P_TSTATFS: file system status request
 * @_9P_RSTATFS: file system status response
 * @_9P_TSYMLINK: make symlink request
 * @_9P_RSYMLINK: make symlink response
 * @_9P_TMKNOD: create a special file object request
 * @_9P_RMKNOD: create a special file object response
 * @_9P_TLCREATE: prepare a handle for I/O on an new file for 9P2000.L
 * @_9P_RLCREATE: response with file access information for 9P2000.L
 * @_9P_TRENAME: rename request
 * @_9P_RRENAME: rename response
 * @_9P_TMKDIR: create a directory request
 * @_9P_RMKDIR: create a directory response
 * @_9P_TVERSION: version handshake request
 * @_9P_RVERSION: version handshake response
 * @_9P_TAUTH: request to establish authentication channel
 * @_9P_RAUTH: response with authentication information
 * @_9P_TATTACH: establish user access to file service
 * @_9P_RATTACH: response with top level handle to file hierarchy
 * @_9P_TERROR: not used
 * @_9P_RERROR: response for any failed request
 * @_9P_TFLUSH: request to abort a previous request
 * @_9P_RFLUSH: response when previous request has been cancelled
 * @_9P_TWALK: descend a directory hierarchy
 * @_9P_RWALK: response with new handle for position within hierarchy
 * @_9P_TOPEN: prepare a handle for I/O on an existing file
 * @_9P_ROPEN: response with file access information
 * @_9P_TCREATE: prepare a handle for I/O on a new file
 * @_9P_RCREATE: response with file access information
 * @_9P_TREAD: request to transfer data from a file or directory
 * @_9P_RREAD: response with data requested
 * @_9P_TWRITE: reuqest to transfer data to a file
 * @_9P_RWRITE: response with out much data was transfered to file
 * @_9P_TCLUNK: forget about a handle to an entity within the file system
 * @_9P_RCLUNK: response when server has forgotten about the handle
 * @_9P_TREMOVE: request to remove an entity from the hierarchy
 * @_9P_RREMOVE: response when server has removed the entity
 * @_9P_TSTAT: request file entity attributes
 * @_9P_RSTAT: response with file entity attributes
 * @_9P_TWSTAT: request to update file entity attributes
 * @_9P_RWSTAT: response when file entity attributes are updated
 *
 * There are 14 basic operations in 9P2000, paired as
 * requests and responses.  The one special case is ERROR
 * as there is no @_9P_TERROR request for clients to transmit to
 * the server, but the server may respond to any other request
 * with an @_9P_RERROR.
 *
 * See Also: http://plan9.bell-labs.com/sys/man/5/INDEX.html
 */

enum _9p_msg_t {
	_9P_TLERROR = 6,
	_9P_RLERROR,
	_9P_TSTATFS = 8,
	_9P_RSTATFS,
	_9P_TLOPEN = 12,
	_9P_RLOPEN,
	_9P_TLCREATE = 14,
	_9P_RLCREATE,
	_9P_TSYMLINK = 16,
	_9P_RSYMLINK,
	_9P_TMKNOD = 18,
	_9P_RMKNOD,
	_9P_TRENAME = 20,
	_9P_RRENAME,
	_9P_TREADLINK = 22,
	_9P_RREADLINK,
	_9P_TGETATTR = 24,
	_9P_RGETATTR,
	_9P_TSETATTR = 26,
	_9P_RSETATTR,
	_9P_TXATTRWALK = 30,
	_9P_RXATTRWALK,
	_9P_TXATTRCREATE = 32,
	_9P_RXATTRCREATE,
	_9P_TREADDIR = 40,
	_9P_RREADDIR,
	_9P_TFSYNC = 50,
	_9P_RFSYNC,
	_9P_TLOCK = 52,
	_9P_RLOCK,
	_9P_TGETLOCK = 54,
	_9P_RGETLOCK,
	_9P_TLINK = 70,
	_9P_RLINK,
	_9P_TMKDIR = 72,
	_9P_RMKDIR,
	_9P_TRENAMEAT = 74,
	_9P_RRENAMEAT,
	_9P_TUNLINKAT = 76,
	_9P_RUNLINKAT,
	_9P_TVERSION = 100,
	_9P_RVERSION,
	_9P_TAUTH = 102,
	_9P_RAUTH,
	_9P_TATTACH = 104,
	_9P_RATTACH,
	_9P_TERROR = 106,
	_9P_RERROR,
	_9P_TFLUSH = 108,
	_9P_RFLUSH,
	_9P_TWALK = 110,
	_9P_RWALK,
	_9P_TOPEN = 112,
	_9P_ROPEN,
	_9P_TCREATE = 114,
	_9P_RCREATE,
	_9P_TREAD = 116,
	_9P_RREAD,
	_9P_TWRITE = 118,
	_9P_RWRITE,
	_9P_TCLUNK = 120,
	_9P_RCLUNK,
	_9P_TREMOVE = 122,
	_9P_RREMOVE,
	_9P_TSTAT = 124,
	_9P_RSTAT,
	_9P_TWSTAT = 126,
	_9P_RWSTAT,
};

/* Arbitrary max xattr size, 64k is the limit for VFS given in man xattr(7) */
#define _9P_XATTR_MAX_SIZE 65535

/**
 * 9p internal flags for xattrs: set as guard for read/write and actual
 * setxattr "flush" call
 */
enum _9p_xattr_write {
	_9P_XATTR_READ_ONLY,
	_9P_XATTR_CAN_WRITE,
	_9P_XATTR_DID_WRITE
};

/**
 * enum _9p_qid_t - QID types
 * @_9P_QTDIR: directory
 * @_9P_QTAPPEND: append-only
 * @_9P_QTEXCL: excluse use (only one open handle allowed)
 * @_9P_QTMOUNT: mount points
 * @_9P_QTAUTH: authentication file
 * @_9P_QTTMP: non-backed-up files
 * @_9P_QTSYMLINK: symbolic links (9P2000.u)
 * @_9P_QTLINK: hard-link (9P2000.u)
 * @_9P_QTFILE: normal files
 *
 * QID types are a subset of permissions - they are primarily
 * used to differentiate semantics for a file system entity via
 * a jump-table.  Their value is also the most signifigant 16 bits
 * of the permission_t
 *
 * See Also: http://plan9.bell-labs.com/magic/man2html/2/stat
 */
enum _9p_qid__ {
	_9P_QTDIR = 0x80,
	_9P_QTAPPEND = 0x40,
	_9P_QTEXCL = 0x20,
	_9P_QTMOUNT = 0x10,
	_9P_QTAUTH = 0x08,
	_9P_QTTMP = 0x04,
	_9P_QTSYMLINK = 0x02,
	_9P_QTLINK = 0x01,
	_9P_QTFILE = 0x00,
};

/* 9P Magic Numbers */
#define _9P_NOTAG	(u16)(~0)
#define _9P_NOFID	(u32)(~0)
#define _9P_NONUNAME	(u32)(~0)
#define _9P_MAXWELEM	16

/* Various header lengths to check message sizes: */

/* size[4] Rread tag[2] count[4] data[count] */
#define _9P_ROOM_RREAD (_9P_STD_HDR_SIZE + 4)

/* size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count] */
#define _9P_ROOM_TWRITE (_9P_STD_HDR_SIZE + 4 + 8 + 4)

/* size[4] Rreaddir tag[2] count[4] data[count] */
#define _9P_ROOM_RREADDIR (_9P_STD_HDR_SIZE + 4)

/**
 * @brief Length prefixed string type
 *
 * The protocol uses length prefixed strings for all
 * string data, so we replicate that for our internal
 * string members.
 */

struct _9p_str {
	u16 len;		/*< Length of the string */
	char *str;		/*< The string */
};

/**
 * @brief file system entity information
 *
 * qids are /identifiers used by 9P servers to track file system
 * entities.  The type is used to differentiate semantics for operations
 * on the entity (ie. read means something different on a directory than
 * on a file).  The path provides a server unique index for an entity
 * (roughly analogous to an inode number), while the version is updated
 * every time a file is modified and can be used to maintain cache
 * coherency between clients and serves.
 * Servers will often differentiate purely synthetic entities by setting
 * their version to 0, signaling that they should never be cached and
 * should be accessed synchronously.
 *
 * See Also://plan9.bell-labs.com/magic/man2html/2/stat
 */

struct _9p_qid {
	u8 type;		/*< Type */
	u32 version;		/*< Monotonically incrementing version number */
	u64 path;		/*< Per-server-unique ID
				 *  for a file system element */
};

/**
 * @brief Internal 9P structure containing client credentials.
 *
 * This structure wraps struct user_cred, adding a refcounter to know when it
 * should be released (it is shared between several op_ctx and fids).
 */
struct _9p_user_cred {
	struct user_cred creds; /*< Credentials. */
	int64_t refcount; /*< Counter of references (to the container or the
			   * creds field). */
};

/**
 *
 * @brief Internal 9P structure for xattr operations, linked in fid
 *
 * This structure is allocated as needed (xattrwalk/create) and freed
 * on clunk
 */
struct _9p_xattr_desc {
	char xattr_name[MAXNAMLEN + 1];
	u64 xattr_size;
	u64 xattr_offset;
	enum _9p_xattr_write xattr_write;
	char xattr_content[];
};

struct _9p_fid {
	u32 fid;
	/** Ganesha export of the file (refcounted). */
	struct gsh_export *fid_export;
	struct _9p_user_cred *ucred; /*< Client credentials (refcounted). */
	struct group_data *gdata;
	struct fsal_obj_handle *pentry;
	struct _9p_qid qid;
	struct state_t *state;
	struct fsal_obj_handle *ppentry;
	char name[MAXNAMLEN+1];
	u32 opens;
	struct _9p_xattr_desc *xattr;
};

enum _9p_trans_type {
	_9P_TCP,
	_9P_RDMA
};

struct flush_condition;

/* flush hook:
 *
 * We use this to insert the request in a list
 * so it can be found later during a TFLUSH.
 * The goal is to wait until a request has been fully
 * processed and the reply sent before we send a RFLUSH.
 *
 * When a TFLUSH arrives, its thread will fill `condition'
 * so we can wake it up later, after we have sent the reply
 * to the original request.
 */
struct _9p_flush_hook {
	int tag;
	struct flush_condition *condition;
	unsigned long sequence;
	struct glist_head list;
};

struct _9p_flush_bucket {
	pthread_mutex_t lock;
	struct glist_head list;
};

#define FLUSH_BUCKETS 32

struct _9p_conn {
	union trans_data {
		long sockfd;
#ifdef _USE_9P_RDMA
		msk_trans_t *rdma_trans;
#endif
	} trans_data;
	enum _9p_trans_type trans_type;
	uint32_t refcount;
	struct gsh_client *client;
	struct timeval birth;	/* This is useful if same sockfd is
				   reused on socket's close/open */
	struct _9p_fid *fids[_9P_FID_PER_CONN];
	struct _9p_flush_bucket flush_buckets[FLUSH_BUCKETS];
	unsigned long sequence;
	pthread_mutex_t sock_lock;
	sockaddr_t addrpeer;
	unsigned int msize;
};

#ifdef _USE_9P_RDMA
struct _9p_outqueue {
	msk_data_t *data;
	pthread_mutex_t lock;
	pthread_cond_t cond;
};

struct _9p_rdma_priv_pernic {
	struct ibv_mr *outmr;
	struct ibv_mr *inmr;
	uint8_t *rdmabuf;
	msk_data_t *rdata;
};

struct _9p_rdma_priv {
	struct _9p_conn *pconn;
	struct _9p_outqueue *outqueue;
	struct _9p_rdma_priv_pernic *pernic;
};
#define _9p_rdma_priv_of(x) ((struct _9p_rdma_priv *)x->private_data)
#endif

struct _9p_request_data {
	struct glist_head req_q;	/* chaining of pending requests */
	char *_9pmsg;
	struct _9p_conn *pconn;
#ifdef _USE_9P_RDMA
	msk_data_t *data;
#endif
	struct _9p_flush_hook flush_hook;
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
};

typedef int (*_9p_function_t) (struct _9p_request_data *req9p,
			       u32 *plenout, char *preply);

struct _9p_function_desc {
	_9p_function_t service_function;
	char *funcname;
};

extern const struct _9p_function_desc _9pfuncdesc[];

#define _9p_getptr(__cursor, __pvar, __type) \
do {                                         \
	__pvar = (__type *)__cursor;         \
	__cursor += sizeof(__type);          \
} while (0)

#define _9p_getstr(__cursor, __len, __str) \
do {                                       \
	__len = (u16 *)__cursor;           \
	__cursor += sizeof(u16);           \
	__str = __cursor;                  \
	__cursor += *__len;                \
} while (0)

#define _9p_setptr(__cursor, __pvar, __type) \
do {                                         \
	*((__type *)__cursor) = *__pvar;     \
	__cursor += sizeof(__type);          \
} while (0)

#define _9p_setvalue(__cursor, __var, __type) \
do {                                          \
	*((__type *)__cursor) = __var;        \
	__cursor += sizeof(__type);           \
} while (0)

#define _9p_savepos(__cursor, __savedpos, __type) \
do {                                              \
	__savedpos = __cursor;                    \
	__cursor += sizeof(__type);               \
} while (0)

/* Insert a qid */
#define _9p_setqid(__cursor, __qid)         \
do {                                        \
	*((u8 *)__cursor) = __qid.type;     \
	__cursor += sizeof(u8);             \
	*((u32 *)__cursor) = __qid.version; \
	__cursor += sizeof(u32);            \
	*((u64 *)__cursor) = __qid.path;    \
	__cursor += sizeof(u64);            \
} while (0)

/* Insert a non-null terminated string */
#define _9p_setstr(__cursor, __len, __str) \
do {                                       \
	*((u16 *)__cursor) = __len;        \
	__cursor += sizeof(u16);           \
	memcpy(__cursor, __str, __len);    \
	__cursor += __len;                 \
} while (0)

/* _9p_setbuffer:
 * Copy data from __buffer into the reply,
 * with a length u32 header.
 */
#define _9p_setbuffer(__cursor, __len, __buffer) \
do {                                             \
	*((u32 *)__cursor) = __len;              \
	__cursor += sizeof(u32);                 \
	memcpy(__cursor, __buffer, __len);       \
	__cursor += __len;                       \
} while (0)

/* _9p_setfilledbuffer:
 * Data has already been copied into the reply.
 * Only move the cursor and set the length.
 */
#define _9p_setfilledbuffer(__cursor, __len) \
do {                                         \
	*((u32 *)__cursor) = __len;          \
	__cursor += sizeof(u32) + __len;     \
} while (0)

/* _9p_getbuffertofill:
 * Get a pointer where to copy data in the reply.
 * This leaves room in the reply for a u32 len header
 */
#define _9p_getbuffertofill(__cursor) (((char *) (__cursor)) + sizeof(u32))

#define _9p_setinitptr(__cursor, __start, __reqtype) \
do {                                                 \
	__cursor = __start + _9P_HDR_SIZE;           \
	*((u8 *)__cursor) = __reqtype;               \
	__cursor += sizeof(u8);                      \
} while (0)

/* _9p_setendptr:
 * Calculate message size, and write this value in the
 * header of the 9p message.
 */
#define _9p_setendptr(__cursor, __start)               \
	(*((u32 *)__start) = (u32)(__cursor - __start))

/* _9p_checkbound:
 * Check that the message size is less than *__maxlen,
 * AND set *__maxlen to actual message size.
 */
#define _9p_checkbound(__cursor, __start, __maxlen)    \
do {                                                   \
	if ((u32)(__cursor - __start) > *__maxlen)     \
		return -1;                             \
	else                                           \
		*__maxlen = (u32)(__cursor - __start); \
} while (0)

/* Bit values for getattr valid field.
 */
#define _9P_GETATTR_MODE	0x00000001ULL
#define _9P_GETATTR_NLINK	0x00000002ULL
#define _9P_GETATTR_UID		0x00000004ULL
#define _9P_GETATTR_GID		0x00000008ULL
#define _9P_GETATTR_RDEV	0x00000010ULL
#define _9P_GETATTR_ATIME	0x00000020ULL
#define _9P_GETATTR_MTIME	0x00000040ULL
#define _9P_GETATTR_CTIME	0x00000080ULL
#define _9P_GETATTR_INO		0x00000100ULL
#define _9P_GETATTR_SIZE	0x00000200ULL
#define _9P_GETATTR_BLOCKS	0x00000400ULL

#define _9P_GETATTR_BTIME	0x00000800ULL
#define _9P_GETATTR_GEN		0x00001000ULL
#define _9P_GETATTR_DATA_VERSION	0x00002000ULL

#define _9P_GETATTR_BASIC	0x000007ffULL	/* Mask for fields
						 * up to BLOCKS */
#define _9P_GETATTR_ALL		0x00003fffULL	/* Mask for all fields above */

/* Bit values for setattr valid field from <linux/fs.h>.
 */
#define _9P_SETATTR_MODE	0x00000001UL
#define _9P_SETATTR_UID		0x00000002UL
#define _9P_SETATTR_GID		0x00000004UL
#define _9P_SETATTR_SIZE	0x00000008UL
#define _9P_SETATTR_ATIME	0x00000010UL
#define _9P_SETATTR_MTIME	0x00000020UL
#define _9P_SETATTR_CTIME	0x00000040UL
#define _9P_SETATTR_ATIME_SET	0x00000080UL
#define _9P_SETATTR_MTIME_SET	0x00000100UL

/* Bit values for lock type.
 */
#define _9P_LOCK_TYPE_RDLCK 0
#define _9P_LOCK_TYPE_WRLCK 1
#define _9P_LOCK_TYPE_UNLCK 2

/* Bit values for lock status.
 */
#define _9P_LOCK_SUCCESS 0
#define _9P_LOCK_BLOCKED 1
#define _9P_LOCK_ERROR 2
#define _9P_LOCK_GRACE 3

/* Bit values for lock flags.
 */
#define _9P_LOCK_FLAGS_BLOCK 1
#define _9P_LOCK_FLAGS_RECLAIM 2


/**
 * @defgroup config_9p Structure and defaults for _9P
 *
 * @{
 */

/**
 * @brief Default value for _9p_tcp_port
 */
#define _9P_TCP_PORT 564

/**
 * @brief Default value for _9p_rdma_port
 */
#define _9P_RDMA_PORT 5640

/**
 * @brief Default value for _9p_tcp_msize
 */
#define _9P_TCP_MSIZE 65536

/**
 * @brief Default value for _9p_rdma_msize
 */
#define _9P_RDMA_MSIZE 1048576

/**
 * @brief Default number of receive buffer per nic
 */
#define _9P_RDMA_INPOOL_SIZE 64

/**
 * @brief Default number of send buffer (total, not per nic)
 *
 * shared pool for sends - optimal when set oh-so-slightly
 * higher than the number of worker threads
 */
#define _9P_RDMA_OUTPOOL_SIZE 32

/**
 * @brief Default rdma connection backlog
 * (number of pending connection requests)
 */
#define _9P_RDMA_BACKLOG 10


/**
 * @brief 9p configuration
 */

struct _9p_param {
	/** Number of worker threads.  Set to NB_WORKER_DEFAULT by
	    default and changed with the Nb_Worker option. */
	uint32_t nb_worker;
	/** TCP port for 9p operations.  Defaults to _9P_TCP_PORT,
	    settable by _9P_TCP_Port */
	uint16_t _9p_tcp_port;
	/** RDMA port for 9p operations.  Defaults to _9P_RDMA_PORT,
	    settable by _9P_RDMA_Port */
	uint16_t _9p_rdma_port;
	/** Msize for 9P operation on tcp.  Defaults to _9P_TCP_MSIZE,
	    settable by _9P_TCP_Msize */
	uint32_t _9p_tcp_msize;
	/** Msize for 9P operation on rdma.  Defaults to _9P_RDMA_MSIZE,
	    settable by _9P_RDMA_Msize */
	uint32_t _9p_rdma_msize;
	/** Backlog for 9P rdma connections.  Defaults to _9P_RDMA_BACKLOG,
	    settable by _9P_RDMA_Backlog */
	uint16_t _9p_rdma_backlog;
	/** Input buffer pool size for 9P rdma connections.
	    Defaults to _9P_RDMA_INPOOL_SIZE,
	    settable by _9P_RDMA_Inpool_Size */
	uint16_t _9p_rdma_inpool_size;
	/** Output buffer pool size for 9P rdma connections.
	    Defaults to _9P_RDMA_OUTPOOL_SIZE,
	    settable by _9P_RDMA_OutPool_Size */
	uint16_t _9p_rdma_outpool_size;

};


/** @} */

/* protocol parameter tables */
extern struct _9p_param _9p_param;
extern struct config_block _9p_param_blk;

/* service functions */
int _9p_init(void);

/* Tools functions */

/**
 * @brief Increment the refcounter of a _9p_user_cred structure.
 *
 * @param creds Reference that is being copied.
 */
void get_9p_user_cred_ref(struct _9p_user_cred *creds);

/**
 * @brief Release a reference to an _9p_user_cred structure.
 *
 * This function decrements the refcounter of the containing _9p_user_cred
 * structure. If this counter reaches 0, the structure is freed.
 *
 * @param creds The reference that is released.
 */
void release_9p_user_cred_ref(struct _9p_user_cred *creds);

/**
 * @brief Initialize op_ctx for the current request.
 *
 * op_ctx must point to an allocated structure.
 *
 * @param pfid fid used to initialize the context.
 * @param req9p request date used to initialize export_perms. It can be NULL,
 * in this case export_perms will be uninitialized.
 */
void _9p_init_opctx(struct _9p_fid *pfid, struct _9p_request_data *req9p);

/**
 * @brief Release resources taken by _9p_init_opctx.
 *
 * op_ctx contains several pointers to refcounted objects. This function
 * decrements these counters and sets the associated fields to NULL.
 */
void _9p_release_opctx(void);

/**
 * @brief Free this fid after releasing its resources.
 *
 * This function can be used to free a partially allocated fid, when an error
 * occurs. To release a valid fid, use _9p_tools_clunk instead.
 *
 * @param[in,out] pfid pointer to fid entry.
 */
void free_fid(struct _9p_fid *pfid);

int _9p_tools_get_req_context_by_uid(u32 uid, struct _9p_fid *pfid);
int _9p_tools_get_req_context_by_name(int uname_len, char *uname_str,
				      struct _9p_fid *pfid);
int _9p_tools_errno(fsal_status_t fsal_status);
void _9p_openflags2FSAL(u32 *inflags, fsal_openflags_t *outflags);
int _9p_tools_clunk(struct _9p_fid *pfid);
void _9p_cleanup_fids(struct _9p_conn *conn);

static inline unsigned int _9p_openflags_to_share_access(u32 *inflags)
{
	switch ((*inflags) & O_ACCMODE) {
	case O_RDONLY:
		return OPEN4_SHARE_ACCESS_READ;
	case O_WRONLY:
		return OPEN4_SHARE_ACCESS_WRITE;
	case O_RDWR:
		return OPEN4_SHARE_ACCESS_READ | OPEN4_SHARE_ACCESS_WRITE;
	default:
		return 0;
	}
}

#ifdef _USE_9P_RDMA
/* 9P/RDMA callbacks */
void *_9p_rdma_handle_trans(void *arg);
void _9p_rdma_callback_recv(msk_trans_t *trans, msk_data_t *pdata, void *arg);
void _9p_rdma_callback_disconnect(msk_trans_t *trans);
void _9p_rdma_callback_send(msk_trans_t *trans, msk_data_t *pdata, void *arg);
void _9p_rdma_callback_recv_err(msk_trans_t *trans, msk_data_t *pdata,
				void *arg);
void _9p_rdma_callback_send_err(msk_trans_t *trans, msk_data_t *pdata,
				void *arg);

#endif
void _9p_AddFlushHook(struct _9p_request_data *req, int tag,
		      unsigned long sequence);
void _9p_FlushFlushHook(struct _9p_conn *conn, int tag, unsigned long sequence);
int _9p_LockAndTestFlushHook(struct _9p_request_data *req);
void _9p_ReleaseFlushHook(struct _9p_request_data *req);
void _9p_DiscardFlushHook(struct _9p_request_data *req);

/* Protocol functions */
int _9p_not_2000L(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_clunk(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_attach(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_auth(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_lcreate(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_flush(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_getattr(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_getlock(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_link(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_lock(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_lopen(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_mkdir(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_mknod(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_read(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_readdir(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_readlink(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_setattr(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_symlink(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_remove(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_rename(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_renameat(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_statfs(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_fsync(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_unlinkat(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_version(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_walk(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_write(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_xattrcreate(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_xattrwalk(struct _9p_request_data *req9p, u32 *plenout, char *preply);

int _9p_rerror(struct _9p_request_data *req9p, u16 *msgtag, u32 err,
	       u32 *plenout, char *preply);

/* Expects to already be size checked */
static inline void _9p_get_fname(char *name, int len, const char *str)
{
	memcpy(name, str, len);
	name[len] = '\0';
}

#endif				/* _9P_H */
