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

/**
 * enum p9_msg_t - 9P message types
 * @P9_TLERROR: not used
 * @P9_RLERROR: response for any failed request for 9P2000.L
 * @P9_TSTATFS: file system status request
 * @P9_RSTATFS: file system status response
 * @P9_TSYMLINK: make symlink request
 * @P9_RSYMLINK: make symlink response
 * @P9_TMKNOD: create a special file object request
 * @P9_RMKNOD: create a special file object response
 * @P9_TLCREATE: prepare a handle for I/O on an new file for 9P2000.L
 * @P9_RLCREATE: response with file access information for 9P2000.L
 * @P9_TRENAME: rename request
 * @P9_RRENAME: rename response
 * @P9_TMKDIR: create a directory request
 * @P9_RMKDIR: create a directory response
 * @P9_TVERSION: version handshake request
 * @P9_RVERSION: version handshake response
 * @P9_TAUTH: request to establish authentication channel
 * @P9_RAUTH: response with authentication information
 * @P9_TATTACH: establish user access to file service
 * @P9_RATTACH: response with top level handle to file hierarchy
 * @P9_TERROR: not used
 * @P9_RERROR: response for any failed request
 * @P9_TFLUSH: request to abort a previous request
 * @P9_RFLUSH: response when previous request has been cancelled
 * @P9_TWALK: descend a directory hierarchy
 * @P9_RWALK: response with new handle for position within hierarchy
 * @P9_TOPEN: prepare a handle for I/O on an existing file
 * @P9_ROPEN: response with file access information
 * @P9_TCREATE: prepare a handle for I/O on a new file
 * @P9_RCREATE: response with file access information
 * @P9_TREAD: request to transfer data from a file or directory
 * @P9_RREAD: response with data requested
 * @P9_TWRITE: reuqest to transfer data to a file
 * @P9_RWRITE: response with out much data was transfered to file
 * @P9_TCLUNK: forget about a handle to an entity within the file system
 * @P9_RCLUNK: response when server has forgotten about the handle
 * @P9_TREMOVE: request to remove an entity from the hierarchy
 * @P9_RREMOVE: response when server has removed the entity
 * @P9_TSTAT: request file entity attributes
 * @P9_RSTAT: response with file entity attributes
 * @P9_TWSTAT: request to update file entity attributes
 * @P9_RWSTAT: response when file entity attributes are updated
 *
 * There are 14 basic operations in 9P2000, paired as
 * requests and responses.  The one special case is ERROR
 * as there is no @P9_TERROR request for clients to transmit to
 * the server, but the server may respond to any other request
 * with an @P9_RERROR.
 *
 * See Also: http://plan9.bell-labs.com/sys/man/5/INDEX.html
 */

enum p9_msg_t {
	P9_TLERROR = 6,
	P9_RLERROR,
	P9_TSTATFS = 8,
	P9_RSTATFS,
	P9_TLOPEN = 12,
	P9_RLOPEN,
	P9_TLCREATE = 14,
	P9_RLCREATE,
	P9_TSYMLINK = 16,
	P9_RSYMLINK,
	P9_TMKNOD = 18,
	P9_RMKNOD,
	P9_TRENAME = 20,
	P9_RRENAME,
	P9_TREADLINK = 22,
	P9_RREADLINK,
	P9_TGETATTR = 24,
	P9_RGETATTR,
	P9_TSETATTR = 26,
	P9_RSETATTR,
	P9_TXATTRWALK = 30,
	P9_RXATTRWALK,
	P9_TXATTRCREATE = 32,
	P9_RXATTRCREATE,
	P9_TREADDIR = 40,
	P9_RREADDIR,
	P9_TFSYNC = 50,
	P9_RFSYNC,
	P9_TLOCK = 52,
	P9_RLOCK,
	P9_TGETLOCK = 54,
	P9_RGETLOCK,
	P9_TLINK = 70,
	P9_RLINK,
	P9_TMKDIR = 72,
	P9_RMKDIR,
	P9_TRENAMEAT = 74,
	P9_RRENAMEAT,
	P9_TUNLINKAT = 76,
	P9_RUNLINKAT,
	P9_TVERSION = 100,
	P9_RVERSION,
	P9_TAUTH = 102,
	P9_RAUTH,
	P9_TATTACH = 104,
	P9_RATTACH,
	P9_TERROR = 106,
	P9_RERROR,
	P9_TFLUSH = 108,
	P9_RFLUSH,
	P9_TWALK = 110,
	P9_RWALK,
	P9_TOPEN = 112,
	P9_ROPEN,
	P9_TCREATE = 114,
	P9_RCREATE,
	P9_TREAD = 116,
	P9_RREAD,
	P9_TWRITE = 118,
	P9_RWRITE,
	P9_TCLUNK = 120,
	P9_RCLUNK,
	P9_TREMOVE = 122,
	P9_RREMOVE,
	P9_TSTAT = 124,
	P9_RSTAT,
	P9_TWSTAT = 126,
	P9_RWSTAT,
};

/**
 * enum p9_qid_t - QID types
 * @P9_QTDIR: directory
 * @P9_QTAPPEND: append-only
 * @P9_QTEXCL: excluse use (only one open handle allowed)
 * @P9_QTMOUNT: mount points
 * @P9_QTAUTH: authentication file
 * @P9_QTTMP: non-backed-up files
 * @P9_QTSYMLINK: symbolic links (9P2000.u)
 * @P9_QTLINK: hard-link (9P2000.u)
 * @P9_QTFILE: normal files
 *
 * QID types are a subset of permissions - they are primarily
 * used to differentiate semantics for a file system entity via
 * a jump-table.  Their value is also the most signifigant 16 bits
 * of the permission_t
 *
 * See Also: http://plan9.bell-labs.com/magic/man2html/2/stat
 */
enum p9_qid_t {
	P9_QTDIR = 0x80,
	P9_QTAPPEND = 0x40,
	P9_QTEXCL = 0x20,
	P9_QTMOUNT = 0x10,
	P9_QTAUTH = 0x08,
	P9_QTTMP = 0x04,
	P9_QTSYMLINK = 0x02,
	P9_QTLINK = 0x01,
	P9_QTFILE = 0x00,
};

/* 9P Magic Numbers */
#define P9_NOTAG	(u16)(~0)
#define P9_NOFID	(u32)(~0)
#define P9_NONUNAME	(u32)(~0)
#define P9_MAXWELEM	16

/* ample room for P9_TWRITE/P9_RREAD header */
#define P9_IOHDRSZ	24

/* Room for readdir header */
#define P9_READDIRHDRSZ	24

/**
 * struct p9_str - length prefixed string type
 * @len: length of the string
 * @str: the string
 *
 * The protocol uses length prefixed strings for all
 * string data, so we replicate that for our internal
 * string members.
 */

struct p9_str {
	u16 len;
	char *str;
};

/**
 * struct p9_qid - file system entity information
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

struct p9_qid {
	u8 type;
	u32 version;
	u64 path;
};

/* Bit values for getattr valid field.
 */
#define P9_GETATTR_MODE		0x00000001ULL
#define P9_GETATTR_NLINK	0x00000002ULL
#define P9_GETATTR_UID		0x00000004ULL
#define P9_GETATTR_GID		0x00000008ULL
#define P9_GETATTR_RDEV		0x00000010ULL
#define P9_GETATTR_ATIME	0x00000020ULL
#define P9_GETATTR_MTIME	0x00000040ULL
#define P9_GETATTR_CTIME	0x00000080ULL
#define P9_GETATTR_INO		0x00000100ULL
#define P9_GETATTR_SIZE		0x00000200ULL
#define P9_GETATTR_BLOCKS	0x00000400ULL

#define P9_GETATTR_BTIME	0x00000800ULL
#define P9_GETATTR_GEN		0x00001000ULL
#define P9_GETATTR_DATA_VERSION	0x00002000ULL

#define P9_GETATTR_BASIC	0x000007ffULL /* Mask for fields up to BLOCKS */
#define P9_GETATTR_ALL		0x00003fffULL /* Mask for All fields above */

/* Bit values for setattr valid field from <linux/fs.h>.
 */
#define P9_SETATTR_MODE		0x00000001UL
#define P9_SETATTR_UID		0x00000002UL
#define P9_SETATTR_GID		0x00000004UL
#define P9_SETATTR_SIZE		0x00000008UL
#define P9_SETATTR_ATIME	0x00000010UL
#define P9_SETATTR_MTIME	0x00000020UL
#define P9_SETATTR_CTIME	0x00000040UL
#define P9_SETATTR_ATIME_SET	0x00000080UL
#define P9_SETATTR_MTIME_SET	0x00000100UL

/* Bit values for lock status.
 */
#define P9_LOCK_SUCCESS 0
#define P9_LOCK_BLOCKED 1
#define P9_LOCK_ERROR 2
#define P9_LOCK_GRACE 3

/* Bit values for lock flags.
 */
#define P9_LOCK_FLAGS_BLOCK 1
#define P9_LOCK_FLAGS_RECLAIM 2

/* Structures for Protocol Operations */
struct p9_rlerror {
	u32 ecode;
};
struct p9_tstatfs {
	u32 fid;
};
struct p9_rstatfs {
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
struct p9_tlopen {
	u32 fid;
	u32 flags;
};
struct p9_rlopen {
	struct p9_qid qid;
	u32 iounit;
};
struct p9_tlcreate {
	u32 fid;
	struct p9_str name;
	u32 flags;
	u32 mode;
	u32 gid;
};
struct p9_rlcreate {
	struct p9_qid qid;
	u32 iounit;
};
struct p9_tsymlink {
	u32 fid;
	struct p9_str name;
	struct p9_str symtgt;
	u32 gid;
};
struct p9_rsymlink {
	struct p9_qid qid;
};
struct p9_tmknod {
	u32 fid;
	struct p9_str name;
	u32 mode;
	u32 major;
	u32 minor;
	u32 gid;
};
struct p9_rmknod {
	struct p9_qid qid;
};
struct p9_trename {
	u32 fid;
	u32 dfid;
	struct p9_str name;
};
struct p9_rrename {
};
struct p9_treadlink {
	u32 fid;
};
struct p9_rreadlink {
	struct p9_str target;
};
struct p9_tgetattr {
	u32 fid;
	u64 request_mask;
};
struct p9_rgetattr {
	u64 valid;
	struct p9_qid qid;
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
struct p9_tsetattr {
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
struct p9_rsetattr {
};
struct p9_txattrwalk {
	u32 fid;
	u32 attrfid;
	struct p9_str name;
};
struct p9_rxattrwalk {
	u64 size;
};
struct p9_txattrcreate {
	u32 fid;
	struct p9_str name;
	u64 size;
	u32 flag;
};
struct p9_rxattrcreate {
};
struct p9_treaddir {
	u32 fid;
	u64 offset;
	u32 count;
};
struct p9_rreaddir {
	u32 count;
	u8 *data;
};
struct p9_tfsync {
	u32 fid;
};
struct p9_rfsync {
};
struct p9_tlock {
	u32 fid;
	u8 type;
	u32 flags;
	u64 start;
	u64 length;
	u32 proc_id;
	struct p9_str client_id;
};
struct p9_rlock {
	u8 status;
};
struct p9_tgetlock {
	u32 fid;
	u8 type;
	u64 start;
	u64 length;
	u32 proc_id;
	struct p9_str client_id;
};
struct p9_rgetlock {
	u8 type;
	u64 start;
	u64 length;
	u32 proc_id;
	struct p9_str client_id;
};
struct p9_tlink {
	u32 dfid;
	u32 fid;
	struct p9_str name;
};
struct p9_rlink {
};
struct p9_tmkdir {
	u32 fid;
	struct p9_str name;
	u32 mode;
	u32 gid;
};
struct p9_rmkdir {
	struct p9_qid qid;
};
struct p9_trenameat {
	u32 olddirfid;
	struct p9_str oldname;
	u32 newdirfid;
	struct p9_str newname;
};
struct p9_rrenameat {
};
struct p9_tunlinkat {
	u32 dirfid;
	struct p9_str name;
	u32 flags;
};
struct p9_runlinkat {
};
struct p9_tawrite {
	u32 fid;
	u8 datacheck;
	u64 offset;
	u32 count;
	u32 rsize;
	u8 *data;
	u32 check;
};
struct p9_rawrite {
	u32 count;
};
struct p9_tversion {
	u32 msize;
	struct p9_str version;
};
struct p9_rversion {
	u32 msize;
	struct p9_str version;
};
struct p9_tauth {
	u32 afid;
	struct p9_str uname;
	struct p9_str aname;
	u32 n_uname;		/* 9P2000.u extensions */
};
struct p9_rauth {
	struct p9_qid qid;
};
struct p9_rerror {
	struct p9_str error;
	u32 errnum;		/* 9p2000.u extension */
};
struct p9_tflush {
	u16 oldtag;
};
struct p9_rflush {
};
struct p9_tattach {
	u32 fid;
	u32 afid;
	struct p9_str uname;
	struct p9_str aname;
	u32 n_uname;		/* 9P2000.u extensions */
};
struct p9_rattach {
	struct p9_qid qid;
};
struct p9_twalk {
	u32 fid;
	u32 newfid;
	u16 nwname;
	struct p9_str wnames[P9_MAXWELEM];
};
struct p9_rwalk {
	u16 nwqid;
	struct p9_qid wqids[P9_MAXWELEM];
};
struct p9_topen {
	u32 fid;
	u8 mode;
};
struct p9_ropen {
	struct p9_qid qid;
	u32 iounit;
};
struct p9_tcreate {
	u32 fid;
	struct p9_str name;
	u32 perm;
	u8 mode;
	struct p9_str extension;
};
struct p9_rcreate {
	struct p9_qid qid;
	u32 iounit;
};
struct p9_tread {
	u32 fid;
	u64 offset;
	u32 count;
};
struct p9_rread {
	u32 count;
	u8 *data;
};
struct p9_twrite {
	u32 fid;
	u64 offset;
	u32 count;
	u8 *data;
};
struct p9_rwrite {
	u32 count;
};
struct p9_tclunk {
	u32 fid;
};
struct p9_rclunk {
};
struct p9_tremove {
	u32 fid;
};
struct p9_rremove {
};

#endif /* NET_9P_H */
