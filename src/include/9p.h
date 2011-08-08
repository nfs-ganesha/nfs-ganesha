/*
 * include/net/9p/9p.h
 *
 * 9P protocol definitions.
 *
 *  Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

/* 
 * Copied from 2.6.38-rc2 kernel, taken from diod sources (code.google.com/p/diod/) then adapted to ganesha
 */

#ifndef NET_9P_H
#define NET_9P_H

typedef uint8_t   u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define _9P_MSG_SIZE 65560 
#define _9P_HDR_SIZE  4
#define _9P_TYPE_SIZE 1
#define _9P_TAG_SIZE  2

int _9p_version( u32 * plenin, char *pmsg, u32 * plenout, char * preply) ;

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
enum _9p_qid_t {
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

/* ample room for _9P_TWRITE/_9P_RREAD header */
#define _9P_IOHDRSZ	24

/* Room for readdir header */
#define _9P_READDIRHDRSZ	24

/**
 * struct _9p_str - length prefixed string type
 * @len: length of the string
 * @str: the string
 *
 * The protocol uses length prefixed strings for all
 * string data, so we replicate that for our internal
 * string members.
 */

struct _9p_str {
	u16 len;
	char *str;
};

/**
 * struct _9p_qid - file system entity information
 * @type: 8-bit type &p9_qid_t
 * @version: 16-bit monotonically incrementing version number
 * @path: 64-bit per-server-unique ID for a file system element
 *
 * qids are identifiers used by 9P servers to track file system
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
	u8 type;
	u32 version;
	u64 path;
};

/* Bit values for getattr valid field.
 */
#define _9P_GETATTR_MODE		0x00000001ULL
#define _9P_GETATTR_NLINK	0x00000002ULL
#define _9P_GETATTR_UID		0x00000004ULL
#define _9P_GETATTR_GID		0x00000008ULL
#define _9P_GETATTR_RDEV		0x00000010ULL
#define _9P_GETATTR_ATIME	0x00000020ULL
#define _9P_GETATTR_MTIME	0x00000040ULL
#define _9P_GETATTR_CTIME	0x00000080ULL
#define _9P_GETATTR_INO		0x00000100ULL
#define _9P_GETATTR_SIZE		0x00000200ULL
#define _9P_GETATTR_BLOCKS	0x00000400ULL

#define _9P_GETATTR_BTIME	0x00000800ULL
#define _9P_GETATTR_GEN		0x00001000ULL
#define _9P_GETATTR_DATA_VERSION	0x00002000ULL

#define _9P_GETATTR_BASIC	0x000007ffULL /* Mask for fields up to BLOCKS */
#define _9P_GETATTR_ALL		0x00003fffULL /* Mask for All fields above */

/* Bit values for setattr valid field from <linux/fs.h>.
 */
#define _9P_SETATTR_MODE		0x00000001UL
#define _9P_SETATTR_UID		0x00000002UL
#define _9P_SETATTR_GID		0x00000004UL
#define _9P_SETATTR_SIZE		0x00000008UL
#define _9P_SETATTR_ATIME	0x00000010UL
#define _9P_SETATTR_MTIME	0x00000020UL
#define _9P_SETATTR_CTIME	0x00000040UL
#define _9P_SETATTR_ATIME_SET	0x00000080UL
#define _9P_SETATTR_MTIME_SET	0x00000100UL

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

/* Structures for Protocol Operations */
struct _9p_rlerror {
	u32 ecode;
};
struct _9p_tstatfs {
	u32 fid;
};
struct _9p_rstatfs {
	u32 type;
	u32 bsize;
	u64 blocks;
	u64 bfree;
	u64 bavail;
	u64 files;
	u64 ffree;
	u64 fsid;
	u32 namelen;
};
struct _9p_tlopen {
	u32 fid;
	u32 flags;
};
struct _9p_rlopen {
	struct _9p_qid qid;
	u32 iounit;
};
struct _9p_tlcreate {
	u32 fid;
	struct _9p_str name;
	u32 flags;
	u32 mode;
	u32 gid;
};
struct _9p_rlcreate {
	struct _9p_qid qid;
	u32 iounit;
};
struct _9p_tsymlink {
	u32 fid;
	struct _9p_str name;
	struct _9p_str symtgt;
	u32 gid;
};
struct _9p_rsymlink {
	struct _9p_qid qid;
};
struct _9p_tmknod {
	u32 fid;
	struct _9p_str name;
	u32 mode;
	u32 major;
	u32 minor;
	u32 gid;
};
struct _9p_rmknod {
	struct _9p_qid qid;
};
struct _9p_trename {
	u32 fid;
	u32 dfid;
	struct _9p_str name;
};
struct _9p_rrename {
};
struct _9p_treadlink {
	u32 fid;
};
struct _9p_rreadlink {
	struct _9p_str target;
};
struct _9p_tgetattr {
	u32 fid;
	u64 request_mask;
};
struct _9p_rgetattr {
	u64 valid;
	struct _9p_qid qid;
	u32 mode;
	u32 uid;
	u32 gid;
	u64 nlink;
	u64 rdev;
	u64 size;
	u64 blksize;
	u64 blocks;
	u64 atime_sec;
	u64 atime_nsec;
	u64 mtime_sec;
	u64 mtime_nsec;
	u64 ctime_sec;
	u64 ctime_nsec;
	u64 btime_sec;
	u64 btime_nsec;
	u64 gen;
	u64 data_version;
};
struct _9p_tsetattr {
	u32 fid;
	u32 valid;
	u32 mode;
	u32 uid;
	u32 gid;
	u64 size;
	u64 atime_sec;
	u64 atime_nsec;
	u64 mtime_sec;
	u64 mtime_nsec;
};
struct _9p_rsetattr {
};
struct _9p_txattrwalk {
	u32 fid;
	u32 attrfid;
	struct _9p_str name;
};
struct _9p_rxattrwalk {
	u64 size;
};
struct _9p_txattrcreate {
	u32 fid;
	struct _9p_str name;
	u64 size;
	u32 flag;
};
struct _9p_rxattrcreate {
};
struct _9p_treaddir {
	u32 fid;
	u64 offset;
	u32 count;
};
struct _9p_rreaddir {
	u32 count;
	u8 *data;
};
struct _9p_tfsync {
	u32 fid;
};
struct _9p_rfsync {
};
struct _9p_tlock {
	u32 fid;
	u8 type;
	u32 flags;
	u64 start;
	u64 length;
	u32 proc_id;
	struct _9p_str client_id;
};
struct _9p_rlock {
	u8 status;
};
struct _9p_tgetlock {
	u32 fid;
	u8 type;
	u64 start;
	u64 length;
	u32 proc_id;
	struct _9p_str client_id;
};
struct _9p_rgetlock {
	u8 type;
	u64 start;
	u64 length;
	u32 proc_id;
	struct _9p_str client_id;
};
struct _9p_tlink {
	u32 dfid;
	u32 fid;
	struct _9p_str name;
};
struct _9p_rlink {
};
struct _9p_tmkdir {
	u32 fid;
	struct _9p_str name;
	u32 mode;
	u32 gid;
};
struct _9p_rmkdir {
	struct _9p_qid qid;
};
struct _9p_trenameat {
	u32 olddirfid;
	struct _9p_str oldname;
	u32 newdirfid;
	struct _9p_str newname;
};
struct _9p_rrenameat {
};
struct _9p_tunlinkat {
	u32 dirfid;
	struct _9p_str name;
	u32 flags;
};
struct _9p_runlinkat {
};
struct _9p_tawrite {
	u32 fid;
	u8 datacheck;
	u64 offset;
	u32 count;
	u32 rsize;
	u8 *data;
	u32 check;
};
struct _9p_rawrite {
	u32 count;
};
struct _9p_tversion {
	u32 msize;
	struct _9p_str version;
};
struct _9p_rversion {
	u32 msize;
	struct _9p_str version;
};
struct _9p_tauth {
	u32 afid;
	struct _9p_str uname;
	struct _9p_str aname;
	u32 n_uname;		/* 9P2000.u extensions */
};
struct _9p_rauth {
	struct _9p_qid qid;
};
struct _9p_rerror {
	struct _9p_str error;
	u32 errnum;		/* 9p2000.u extension */
};
struct _9p_tflush {
	u16 oldtag;
};
struct _9p_rflush {
};
struct _9p_tattach {
	u32 fid;
	u32 afid;
	struct _9p_str uname;
	struct _9p_str aname;
	u32 n_uname;		/* 9P2000.u extensions */
};
struct _9p_rattach {
	struct _9p_qid qid;
};
struct _9p_twalk {
	u32 fid;
	u32 newfid;
	u16 nwname;
	struct _9p_str wnames[_9P_MAXWELEM];
};
struct _9p_rwalk {
	u16 nwqid;
	struct _9p_qid wqids[_9P_MAXWELEM];
};
struct _9p_topen {
	u32 fid;
	u8 mode;
};
struct _9p_ropen {
	struct _9p_qid qid;
	u32 iounit;
};
struct _9p_tcreate {
	u32 fid;
	struct _9p_str name;
	u32 perm;
	u8 mode;
	struct _9p_str extension;
};
struct _9p_rcreate {
	struct _9p_qid qid;
	u32 iounit;
};
struct _9p_tread {
	u32 fid;
	u64 offset;
	u32 count;
};
struct _9p_rread {
	u32 count;
	u8 *data;
};
struct _9p_twrite {
	u32 fid;
	u64 offset;
	u32 count;
	u8 *data;
};
struct _9p_rwrite {
	u32 count;
};
struct _9p_tclunk {
	u32 fid;
};
struct _9p_rclunk {
};
struct _9p_tremove {
	u32 fid;
};
struct _9p_rremove {
};

union _9p_tmsg {
} ;
#endif /* NET_9P_H */
