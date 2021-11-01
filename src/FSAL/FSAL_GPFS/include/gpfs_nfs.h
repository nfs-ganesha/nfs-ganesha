/* SPDX-License-Identifier: BSD-3-Clause */
/*                                                                              */
/* Copyright (C) 2001 International Business Machines                           */
/* All rights reserved.                                                         */
/*                                                                              */
/* This file is part of the GPFS user library.                                  */
/*                                                                              */
/* Redistribution and use in source and binary forms, with or without           */
/* modification, are permitted provided that the following conditions           */
/* are met:                                                                     */
/*                                                                              */
/*  1. Redistributions of source code must retain the above copyright notice,   */
/*     this list of conditions and the following disclaimer.                    */
/*  2. Redistributions in binary form must reproduce the above copyright        */
/*     notice, this list of conditions and the following disclaimer in the      */
/*     documentation and/or other materials provided with the distribution.     */
/*  3. The name of the author may not be used to endorse or promote products    */
/*     derived from this software without specific prior written                */
/*     permission.                                                              */
/*                                                                              */
/* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR         */
/* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES    */
/* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.      */
/* IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, */
/* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, */
/* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  */
/* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     */
/* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      */
/* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       */
/* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   */
/*                                                                              */
/* %Z%%M%       %I%  %W% %G% %U% */
/*
 *  Library calls for GPFS interfaces
 */
#ifndef H_GPFS_NFS
#define H_GPFS_NFS

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WIN32
struct flock
{};
#endif

/* GANESHA common information */

#define GPFS_DEVNAMEX "/dev/ss0"  /* Must be the same as GPFS_DEVNAME */
#define kGanesha 140             /* Must be the same as Ganesha in enum kxOps */

#define OPENHANDLE_GET_VERSION    100
#define OPENHANDLE_GET_VERSION2   1002
#define OPENHANDLE_GET_VERSION3   1003
#define OPENHANDLE_GET_VERSION4   1004
#define OPENHANDLE_NAME_TO_HANDLE 101
#define OPENHANDLE_OPEN_BY_HANDLE 102
#define OPENHANDLE_LAYOUT_TYPE    106
#define OPENHANDLE_GET_DEVICEINFO 107
#define OPENHANDLE_GET_DEVICELIST 108
#define OPENHANDLE_LAYOUT_GET     109
#define OPENHANDLE_LAYOUT_RETURN  110
#define OPENHANDLE_INODE_UPDATE   111
#define OPENHANDLE_GET_XSTAT      112
#define OPENHANDLE_SET_XSTAT      113
#define OPENHANDLE_CHECK_ACCESS   114
#define OPENHANDLE_OPEN_SHARE_BY_HANDLE 115
#define OPENHANDLE_GET_LOCK       116
#define OPENHANDLE_SET_LOCK       117
#define OPENHANDLE_THREAD_UPDATE  118
#define OPENHANDLE_LAYOUT_COMMIT  119
#define OPENHANDLE_DS_READ        120
#define OPENHANDLE_DS_WRITE       121
#define OPENHANDLE_GET_VERIFIER   122
#define OPENHANDLE_FSYNC          123
#define OPENHANDLE_SHARE_RESERVE  124
#define OPENHANDLE_GET_NODEID     125
#define OPENHANDLE_SET_DELEGATION 126
#define OPENHANDLE_CLOSE_FILE     127
#define OPENHANDLE_LINK_BY_FH     128
#define OPENHANDLE_RENAME_BY_FH   129
#define OPENHANDLE_STAT_BY_NAME   130
#define OPENHANDLE_GET_HANDLE     131
#define OPENHANDLE_READLINK_BY_FH 132
#define OPENHANDLE_UNLINK_BY_NAME 133
#define OPENHANDLE_CREATE_BY_NAME 134
#define OPENHANDLE_READ_BY_FD     135
#define OPENHANDLE_WRITE_BY_FD    136
#define OPENHANDLE_CREATE_BY_NAME_ATTR 137
#define OPENHANDLE_GRACE_PERIOD   138
#define OPENHANDLE_ALLOCATE_BY_FD 139
#define OPENHANDLE_REOPEN_BY_FD   140
#define OPENHANDLE_FADVISE_BY_FD  141
#define OPENHANDLE_SEEK_BY_FD     142
#define OPENHANDLE_STATFS_BY_FH   143
#define OPENHANDLE_GETXATTRS      144
#define OPENHANDLE_SETXATTRS      145
#define OPENHANDLE_REMOVEXATTRS   146
#define OPENHANDLE_LISTXATTRS     147
#define OPENHANDLE_MKNODE_BY_NAME 148
#define OPENHANDLE_reserved       149
#define OPENHANDLE_TRACE_ME       150
#define OPENHANDLE_QUOTA          151
#define OPENHANDLE_FS_LOCATIONS   152

#define OPENHANDLE_TRACE_UTIL     155
#define OPENHANDLE_TRACE_COS      156

/* If there is any change in above constants, then update below values.
 * Currently ignoring opcode 1002 */
#define GPFS_MIN_OP		 OPENHANDLE_GET_VERSION
#define GPFS_MAX_OP		 OPENHANDLE_FS_LOCATIONS
#define GPFS_STAT_NO_OP_1           3
#define GPFS_STAT_NO_OP_2           4
#define GPFS_STAT_NO_OP_3           5
/* max stat ops including placeholder for phantom ops  */
#define GPFS_STAT_MAX_OPS (GPFS_MAX_OP-GPFS_MIN_OP+2)
/* placeholder index is the last index in the array */
#define GPFS_STAT_PH_INDEX (GPFS_STAT_MAX_OPS-1)
/* total ops excluding the missing ops 103, 104 and 105 and the placeholder
 * for phantom ops */
#define GPFS_TOTAL_OPS     (GPFS_STAT_MAX_OPS-4)

struct trace_arg
{
  uint32_t level;
  uint32_t len;
  char     *str;
};

#define ganesha_v1 1
#define ganesha_v2 2
#define ganesha_v3 3
#define ganesha_v4 4

int gpfs_ganesha(int op, void *oarg);

#define OPENHANDLE_HANDLE_LEN 40
#define OPENHANDLE_KEY_LEN 28
#define OPENHANDLE_VERSION 2

struct xstat_cred_t
{
  uint32_t principal;          /* user id */
  uint32_t group;              /* primary group id */
  uint16_t num_groups;         /* number secondary groups for this user */
#define XSTAT_CRED_NGROUPS 32
  uint32_t eGroups[XSTAT_CRED_NGROUPS];/* array of secondary groups */
};

struct gpfs_time_t
{
  uint32_t t_sec;
  uint32_t t_nsec;
};

struct gpfs_file_handle
{
  uint16_t handle_size;
  uint16_t handle_type;
  uint16_t handle_version;
  uint16_t handle_key_size;
  uint32_t handle_fsid[2];
  /* file identifier */
  unsigned char f_handle[OPENHANDLE_HANDLE_LEN];
};

struct name_handle_arg
{
  int dfd;
  int flag;
  const char *name;
  struct gpfs_file_handle *handle;
  int expfd;
};

struct get_handle_arg
{
  int mountdirfd;
  int len;
  const char *name;
  struct gpfs_file_handle *dir_fh;
  struct gpfs_file_handle *out_fh;
};

struct open_arg
{
  int mountdirfd;
  int flags;
  int openfd;
  struct gpfs_file_handle *handle;
  const char *cli_ip;
};

struct link_fh_arg
{
  int mountdirfd;
  int len;
  const char *name;
  struct gpfs_file_handle *dir_fh;
  struct gpfs_file_handle *dst_fh;
  const char *cli_ip;
};

struct rename_fh_arg
{
  int mountdirfd;
  int old_len;
  const char *old_name;
  int new_len;
  const char *new_name;
  struct gpfs_file_handle *old_fh;
  struct gpfs_file_handle *new_fh;
  const char *cli_ip;
};

struct glock
{
  int cmd;
  int lfd;
  void *lock_owner;
  struct flock flock;
};
#define GPFS_F_CANCELLK (1024 + 5)   /* Maps to Linux F_CANCELLK */
#define FL_RECLAIM 4
#define EGRACE 140

struct set_get_lock_arg
{
  int mountdirfd;
  struct glock *lock;
  int reclaim;
  const char *cli_ip;
};

struct open_share_arg
{
  int mountdirfd;
  int flags;
  int openfd;
  struct gpfs_file_handle *handle;
  int share_access;
  int share_deny;
  int reclaim;
  const char *cli_ip;
};

struct share_reserve_arg
{
  int mountdirfd;
  int openfd;
  int share_access;
  int share_deny;
  const char *cli_ip;
};

struct fadvise_arg
{
  int mountdirfd;
  int openfd;
  uint64_t offset;
  uint64_t length;
  uint32_t *hints;
};

struct gpfs_io_info {
  uint32_t io_what;
  uint64_t io_offset;
  uint64_t io_len;
  uint32_t io_eof;
  uint32_t io_alloc;
};

struct fseek_arg
{
  int mountdirfd;
  int openfd;
  struct gpfs_io_info *info;
};

struct close_file_arg
{
  int mountdirfd;
  int close_fd;
  int close_flags;
  void *close_owner;
  const char *cli_ip;
};

struct link_arg
{
  int file_fd;
  int dir_fd;
  const char *name;
  const char *cli_ip;
};

struct readlink_arg
{
  int fd;
  char *buffer;
  int size;
};

struct readlink_fh_arg
{
  int mountdirfd;
  struct gpfs_file_handle *handle;
  char *buffer;
  int size;
};

struct nfsd4_pnfs_deviceid {
	/** FSAL_ID - to dispatch getdeviceinfo based on */
	uint8_t fsal_id;
	/** Break up the remainder into useful chunks */
	uint8_t device_id1;
	uint16_t device_id2;
	uint32_t device_id4;
	uint64_t devid;
};

struct gpfs_exp_xdr_stream {
  int *p;
  int *end;
};

enum x_nfsd_fsid {
	x_FSID_DEV = 0,
	x_FSID_NUM,
	x_FSID_MAJOR_MINOR,
	x_FSID_ENCODE_DEV,
	x_FSID_UUID4_INUM,
	x_FSID_UUID8,
	x_FSID_UUID16,
	x_FSID_UUID16_INUM,
	x_FSID_MAX
};

enum x_pnfs_layouttype {
	x_LAYOUT_NFSV4_1_FILES  = 1,
	x_LAYOUT_OSD2_OBJECTS = 2,
	x_LAYOUT_BLOCK_VOLUME = 3,

	x_NFS4_PNFS_PRIVATE_LAYOUT = 0x80000000
};

/* used for both layout return and recall */
enum x_pnfs_layoutreturn_type {
	x_RETURN_FILE = 1,
	x_RETURN_FSID = 2,
	x_RETURN_ALL  = 3
};

enum x_pnfs_iomode {
	x_IOMODE_READ = 1,
	x_IOMODE_RW = 2,
	x_IOMODE_ANY = 3,
};

enum stable_nfs
{
  x_UNSTABLE4 = 0,
  x_DATA_SYNC4 = 1,
  x_FILE_SYNC4 = 2
};

struct pnfstime4 {
	uint64_t	seconds;
	uint32_t	nseconds;
};

struct nfsd4_pnfs_dev_iter_res {
	uint64_t		gd_cookie;	/* request/response */
	uint64_t		gd_verf;	/* request/response */
	uint64_t		gd_devid;	/* response */
	uint32_t		gd_eof;		/* response */
};

/* Arguments for set_device_notify */
struct pnfs_devnotify_arg {
	struct nfsd4_pnfs_deviceid dn_devid;	/* request */
	uint32_t dn_layout_type;		/* request */
	uint32_t dn_notify_types;		/* request/response */
};

struct nfsd4_layout_seg {
	uint64_t	clientid;
	uint32_t	layout_type;
	uint32_t	iomode;
	uint64_t	offset;
	uint64_t	length;
};

struct nfsd4_pnfs_layoutget_arg {
	uint64_t		lg_minlength;
	uint64_t		lg_sbid;
	struct gpfs_file_handle	*lg_fh;
	uint32_t		lg_iomode;
};

struct nfsd4_pnfs_layoutget_res {
	struct nfsd4_layout_seg	lg_seg;	/* request/response */
	uint32_t		lg_return_on_close;
};

struct nfsd4_pnfs_layoutcommit_arg {
	struct nfsd4_layout_seg	lc_seg;		/* request */
	uint32_t		lc_reclaim;	/* request */
	uint32_t		lc_newoffset;	/* request */
	uint64_t		lc_last_wr;	/* request */
	struct pnfstime4		lc_mtime;	/* request */
	uint32_t		lc_up_len;	/* layout length */
	void			*lc_up_layout;	/* decoded by callback */
};

struct nfsd4_pnfs_layoutcommit_res {
	uint32_t		lc_size_chg;	/* boolean for response */
	uint64_t		lc_newsize;	/* response */
};

struct nfsd4_pnfs_layoutreturn_arg {
	uint32_t		lr_return_type;	/* request */
	struct nfsd4_layout_seg	lr_seg;		/* request */
	uint32_t		lr_reclaim;	/* request */
	uint32_t		lrf_body_len;	/* request */
	void			*lrf_body;	/* request */
	void			*lr_cookie;	/* fs private */
};

struct x_xdr_netobj {
	unsigned int	len;
	unsigned char	*data;
};
struct pnfs_filelayout_devaddr {
	struct x_xdr_netobj	r_netid;
	struct x_xdr_netobj	r_addr;
};

/* list of multipath servers */
struct pnfs_filelayout_multipath {
	uint32_t			fl_multipath_length;
	struct pnfs_filelayout_devaddr 	*fl_multipath_list;
};

struct pnfs_filelayout_device {
	uint32_t			fl_stripeindices_length;
	uint32_t			*fl_stripeindices_list;
	uint32_t			fl_device_length;
	struct pnfs_filelayout_multipath *fl_device_list;
};

struct pnfs_filelayout_layout {
	uint32_t                        lg_layout_type; /* response */
	uint32_t                        lg_stripe_type; /* response */
	uint32_t                        lg_commit_through_mds; /* response */
	uint64_t                        lg_stripe_unit; /* response */
	uint64_t                        lg_pattern_offset; /* response */
	uint32_t                        lg_first_stripe_index;	/* response */
	struct nfsd4_pnfs_deviceid	device_id;		/* response */
	uint32_t                        lg_fh_length;		/* response */
	struct gpfs_file_handle          *lg_fh_list;		/* response */
};

enum stripetype4 {
	STRIPE_SPARSE = 1,
	STRIPE_DENSE = 2
};

struct deviceinfo_arg
{
  int mountdirfd;
  int type;
  struct nfsd4_pnfs_deviceid devid;
  struct gpfs_exp_xdr_stream xdr;
};

struct layoutget_arg
{
  int fd;
  struct gpfs_file_handle *handle;
  struct nfsd4_pnfs_layoutget_arg args;
  struct pnfs_filelayout_layout *file_layout;
  struct gpfs_exp_xdr_stream *xdr;
};

struct layoutreturn_arg
{
  int mountdirfd;
  struct gpfs_file_handle *handle;
  struct nfsd4_pnfs_layoutreturn_arg args;
};

struct dsread_arg
{
  int mountdirfd;
  struct gpfs_file_handle *handle;
  char *bufP;
  uint64_t offset;
  uint64_t length;
  uint64_t *filesize;
  int options;
  const char *cli_ip;
};

/* define flags for options */
#define IO_SKIP_HOLE      (1 << 0) /* 01 */
#define IO_SKIP_DATA      (1 << 1) /* 02 */
#define IO_ALLOCATE       (1 << 2) /* 04 */
#define IO_DEALLOCATE     (1 << 3) /* 08 */

struct dswrite_arg
{
  int mountdirfd;
  struct gpfs_file_handle *handle;
  char *bufP;
  uint64_t offset;
  uint64_t length;
  uint32_t stability_wanted;
  uint32_t *stability_got;
  uint32_t *verifier4;
  int options;
  const char *cli_ip;
};

struct read_arg
{
  int mountdirfd;
  int fd;
  char *bufP;
  uint64_t offset;
  uint64_t length;
  uint32_t stability_wanted;
  uint32_t *stability_got;
  uint32_t *verifier4;
  int options;
  const char *cli_ip;
};

struct write_arg
{
  int mountdirfd;
  int fd;
  char *bufP;
  uint64_t offset;
  uint64_t length;
  uint32_t stability_wanted;
  uint32_t *stability_got;
  uint32_t *verifier4;
  int options;
  const char *cli_ip;
};

struct alloc_arg
{
  int fd;
  uint64_t offset;
  uint64_t length;
  int options;
};

struct layoutcommit_arg
{
  int mountdirfd;
  struct gpfs_file_handle *handle;
  uint64_t offset;
  uint64_t length;
  uint32_t reclaim;      /* True if this is a reclaim commit */
  uint32_t new_offset;   /* True if the client has suggested a new offset */
  uint64_t last_write;   /* The offset of the last byte written, if
                               new_offset if set, otherwise undefined. */
  uint32_t time_changed; /* True if the client provided a new value for mtime */
  struct gpfs_time_t new_time;  /* If time_changed is true, the client-supplied
				   modification time for the file.  otherwise,
				   undefined. */
  struct gpfs_exp_xdr_stream *xdr;
};

struct fsync_arg
{
  int mountdirfd;
  struct gpfs_file_handle *handle;
  uint64_t offset;
  uint64_t length;
  uint32_t *verifier4;
};

struct statfs_arg
{
  int mountdirfd;
  struct gpfs_file_handle *handle;
  struct statfs *buf;
};

struct stat_arg
{
    int mountdirfd;
    struct gpfs_file_handle *handle;
    struct stat *buf;
};

struct grace_period_arg
{
    int mountdirfd;
    int grace_sec;
};

struct create_name_arg
{
    int mountdirfd;                 /* in     */
    struct gpfs_file_handle *dir_fh;/* in     */
    uint32_t dev;                   /* in dev or posix flags */
    int mode;                       /* in     */
    int len;                        /* in     */
    const char *name;               /* in     */
    struct gpfs_file_handle *new_fh;/* out    */
    struct stat *buf;               /* in/out */
    int attr_valid;                 /* in     */
    int attr_changed;               /* in     */
    struct gpfs_acl *acl;           /* in/out  */
    const char *cli_ip;
};

struct stat_name_arg
{
    int mountdirfd;
    int len;
    const char *name;
    struct gpfs_file_handle *handle;
    struct stat *buf;
    const char *cli_ip;
};

struct callback_arg
{
    int interface_version;
    int mountdirfd;
    int *reason;
    struct gpfs_file_handle *handle;
    struct glock *fl;
    int *flags;
    struct stat *buf;
    struct pnfs_deviceid *dev_id;
    uint32_t *expire_attr;
};
#define GPFS_INTERFACE_VERSION 10000
#define GPFS_INTERFACE_SUB_VER     1

/* Defines for the flags in callback_arg, keep up to date with CXIUP_xxx */
#define UP_NLINK        0x00000001   /* update nlink */
#define UP_MODE         0x00000002   /* update mode and ctime */
#define UP_OWN          0x00000004   /* update mode,uid,gid and ctime */
#define UP_SIZE         0x00000008   /* update fsize */
#define UP_SIZE_BIG     0x00000010   /* update fsize if bigger */
#define UP_TIMES        0x00000020   /* update all times */
#define UP_ATIME        0x00000040   /* update atime only */
#define UP_PERM         0x00000080   /* update fields needed for permission checking*/
#define UP_RENAME       0x00000100   /* this is a rename op */
#define UP_DESTROY_FLAG 0x00000200   /* clear destroyIfDelInode flag */
#define UP_GANESHA      0x00000400   /* this is a ganesha op */

/* reason list for reason in callback_arg */
#define INODE_INVALIDATE        1
#define INODE_UPDATE            2
#define INODE_LOCK_GRANTED      3
#define INODE_LOCK_AGAIN        4
#define THREAD_STOP             5
#define THREAD_PAUSE            6
#define BREAK_DELEGATION        7
#define LAYOUT_FILE_RECALL      8
#define LAYOUT_RECALL_ANY       9
#define LAYOUT_NOTIFY_DEVICEID 10

/* define flags for attr_valid */
#define XATTR_STAT      (1 << 0)
#define XATTR_ACL       (1 << 1)
#define XATTR_NO_CACHE  (1 << 2)
#define XATTR_EXPIRE    (1 << 3)
#define XATTR_FSID      (1 << 4)

/* define flags for attr_chaged */
#define XATTR_MODE           (1 << 0) //  01
#define XATTR_UID            (1 << 1) //  02
#define XATTR_GID            (1 << 2) //  04
#define XATTR_SIZE           (1 << 3) //  08
#define XATTR_ATIME          (1 << 4) //  10
#define XATTR_MTIME          (1 << 5) //  20
#define XATTR_CTIME          (1 << 6) //  40
#define XATTR_ATIME_SET      (1 << 7) //  80
#define XATTR_MTIME_SET      (1 << 8) // 100
#define XATTR_ATIME_NOW      (1 << 9) // 200
#define XATTR_MTIME_NOW      (1 << 10)// 400
#define XATTR_SPACE_RESERVED (1 << 11)// 800

struct fsal_fsid {
	uint64_t major;
	uint64_t minor;
};

struct xstat_arg
{
    int attr_valid;
    int mountdirfd;
    struct gpfs_file_handle *handle;
    struct gpfs_acl *acl;
    int attr_changed;
    struct stat *buf;
    struct fsal_fsid *fsid;
    uint32_t *expire_attr;
    const char *cli_ip;
};

struct getxattr_arg {
	int mountdirfd;
	struct gpfs_file_handle *handle;
	uint32_t name_len;
	char *name;
	uint32_t value_len;
	void *value;
};

struct setxattr_arg {
	int mountdirfd;
	struct gpfs_file_handle *handle;
	int type;
	uint32_t name_len;
	char *name;
	uint32_t value_len;
	void *value;
	const char *cli_ip;
};

struct removexattr_arg {
	int mountdirfd;
	struct gpfs_file_handle *handle;
	uint32_t name_len;
	char *name;
	const char *cli_ip;
};

struct listxattr_arg {
	int mountdirfd;
	struct gpfs_file_handle *handle;
	uint64_t cookie;
	uint64_t verifier;
	uint32_t eof;
	uint32_t name_len;
	void *names;
	const char *cli_ip;
};

struct fs_loc_arg {
    int mountdirfd;
    struct gpfs_file_handle *handle;
    int fs_root_len;
    char *fs_root;
    int fs_path_len;
    char *fs_path;
    int fs_server_len;
    char *fs_server;
};

struct xstat_access_arg
{
    int mountdirfd;
    struct gpfs_file_handle *handle;
    struct gpfs_acl *acl;
    struct xstat_cred_t *cred;
    unsigned int posix_mode;
    unsigned int access;       /* v4maske */
    unsigned int *supported;	
};

struct quotactl_arg
{
    const char *pathname;
    int cmd;
    int qid;
    void *bufferP;
    const char *cli_ip;
};

#ifdef __cplusplus
}
#endif

extern struct fsal_stats gpfs_stats;

#endif /* H_GPFS_NFS */
