
#include <utime.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------*/

/*
 * First define FUSE-like types. Their definition is
 * compliant with FUSE use (include FUSE usual fields).
 */

typedef unsigned long ganefuse_ino_t;
typedef struct ganefuse_req *ganefuse_req_t;
struct ganefuse_session;
struct ganefuse_chan;

struct ganefuse_entry_param {
  ganefuse_ino_t ino;
  unsigned long generation;
  struct stat attr;
  double attr_timeout;
  double entry_timeout;

};

struct ganefuse_ctx {
  uid_t uid;
  gid_t gid;
  pid_t pid;

};

/* 'to_set' flags in setattr */
#define GANEFUSE_SET_ATTR_MODE      (1 << 0)
#define GANEFUSE_SET_ATTR_UID       (1 << 1)
#define GANEFUSE_SET_ATTR_GID       (1 << 2)
#define GANEFUSE_SET_ATTR_SIZE      (1 << 3)
#define GANEFUSE_SET_ATTR_ATIME     (1 << 4)
#define GANEFUSE_SET_ATTR_MTIME     (1 << 5)

struct ganefuse_file_info {
  int flags;
  unsigned long fh_old;
  int writepage;
  unsigned int direct_io:1;
  unsigned int keep_cache:1;
  unsigned int flush:1;
  unsigned int padding:29;
  uint64_t fh;
  uint64_t lock_owner;

};

struct ganefuse_conn_info {
  unsigned proto_major;
  unsigned proto_minor;
  unsigned async_read;
  unsigned max_write;
  unsigned max_readahead;
  unsigned reserved[27];

};

struct ganefuse_lowlevel_ops {
  void (*init) (void *userdata, struct ganefuse_conn_info * conn);
  void (*destroy) (void *userdata);
  void (*lookup) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name);
  void (*forget) (ganefuse_req_t req, ganefuse_ino_t ino, unsigned long nlookup);
  void (*getattr) (ganefuse_req_t req, ganefuse_ino_t ino,
                   struct ganefuse_file_info * fi);
  void (*setattr) (ganefuse_req_t req, ganefuse_ino_t ino, struct stat * attr, int to_set,
                   struct ganefuse_file_info * fi);
  void (*readlink) (ganefuse_req_t req, ganefuse_ino_t ino);
  void (*mknod) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name,
                 mode_t mode, dev_t rdev);
  void (*mkdir) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name,
                 mode_t mode);
  void (*unlink) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name);
  void (*rmdir) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name);
  void (*symlink) (ganefuse_req_t req, const char *link, ganefuse_ino_t parent,
                   const char *name);
  void (*rename) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name,
                  ganefuse_ino_t newparent, const char *newname);
  void (*link) (ganefuse_req_t req, ganefuse_ino_t ino, ganefuse_ino_t newparent,
                const char *newname);
  void (*open) (ganefuse_req_t req, ganefuse_ino_t ino, struct ganefuse_file_info * fi);
  void (*read) (ganefuse_req_t req, ganefuse_ino_t ino, size_t size, off_t off,
                struct ganefuse_file_info * fi);
  void (*write) (ganefuse_req_t req, ganefuse_ino_t ino, const char *buf,
                 size_t size, off_t off, struct ganefuse_file_info * fi);
  void (*flush) (ganefuse_req_t req, ganefuse_ino_t ino, struct ganefuse_file_info * fi);
  void (*release) (ganefuse_req_t req, ganefuse_ino_t ino,
                   struct ganefuse_file_info * fi);
  void (*fsync) (ganefuse_req_t req, ganefuse_ino_t ino, int datasync,
                 struct ganefuse_file_info * fi);
  void (*opendir) (ganefuse_req_t req, ganefuse_ino_t ino,
                   struct ganefuse_file_info * fi);
  void (*readdir) (ganefuse_req_t req, ganefuse_ino_t ino, size_t size, off_t off,
                   struct ganefuse_file_info * fi);
  void (*releasedir) (ganefuse_req_t req, ganefuse_ino_t ino,
                      struct ganefuse_file_info * fi);

  void (*fsyncdir) (ganefuse_req_t req, ganefuse_ino_t ino, int datasync,
                    struct ganefuse_file_info * fi);
  void (*statfs) (ganefuse_req_t req, ganefuse_ino_t ino);
  void (*setxattr) (ganefuse_req_t req, ganefuse_ino_t ino, const char *name,
                    const char *value, size_t size, int flags);
  void (*getxattr) (ganefuse_req_t req, ganefuse_ino_t ino, const char *name,
                    size_t size);
  void (*listxattr) (ganefuse_req_t req, ganefuse_ino_t ino, size_t size);
  void (*removexattr) (ganefuse_req_t req, ganefuse_ino_t ino, const char *name);
  void (*access) (ganefuse_req_t req, ganefuse_ino_t ino, int mask);
  void (*create) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name,
                  mode_t mode, struct ganefuse_file_info * fi);
  void (*getlk) (ganefuse_req_t req, ganefuse_ino_t ino, struct ganefuse_file_info * fi,
                 struct flock * lock);
  void (*setlk) (ganefuse_req_t req, ganefuse_ino_t ino, struct ganefuse_file_info * fi,
                 struct flock * lock, int sleep);
  void (*bmap) (ganefuse_req_t req, ganefuse_ino_t ino, size_t blocksize, uint64_t idx);
};

struct ganefuse_lowlevel_ops25 {
  void (*init) (void *userdata, struct ganefuse_conn_info * conn);
  void (*destroy) (void *userdata);
  void (*lookup) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name);
  void (*forget) (ganefuse_req_t req, ganefuse_ino_t ino, unsigned long nlookup);
  void (*getattr) (ganefuse_req_t req, ganefuse_ino_t ino,
                   struct ganefuse_file_info * fi);
  void (*setattr) (ganefuse_req_t req, ganefuse_ino_t ino, struct stat * attr, int to_set,
                   struct ganefuse_file_info * fi);
  void (*readlink) (ganefuse_req_t req, ganefuse_ino_t ino);
  void (*mknod) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name,
                 mode_t mode, dev_t rdev);
  void (*mkdir) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name,
                 mode_t mode);
  void (*unlink) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name);
  void (*rmdir) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name);
  void (*symlink) (ganefuse_req_t req, const char *link, ganefuse_ino_t parent,
                   const char *name);
  void (*rename) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name,
                  ganefuse_ino_t newparent, const char *newname);
  void (*link) (ganefuse_req_t req, ganefuse_ino_t ino, ganefuse_ino_t newparent,
                const char *newname);
  void (*open) (ganefuse_req_t req, ganefuse_ino_t ino, struct ganefuse_file_info * fi);
  void (*read) (ganefuse_req_t req, ganefuse_ino_t ino, size_t size, off_t off,
                struct ganefuse_file_info * fi);
  void (*write) (ganefuse_req_t req, ganefuse_ino_t ino, const char *buf,
                 size_t size, off_t off, struct ganefuse_file_info * fi);
  void (*flush) (ganefuse_req_t req, ganefuse_ino_t ino, struct ganefuse_file_info * fi);
  void (*release) (ganefuse_req_t req, ganefuse_ino_t ino,
                   struct ganefuse_file_info * fi);
  void (*fsync) (ganefuse_req_t req, ganefuse_ino_t ino, int datasync,
                 struct ganefuse_file_info * fi);
  void (*opendir) (ganefuse_req_t req, ganefuse_ino_t ino,
                   struct ganefuse_file_info * fi);
  void (*readdir) (ganefuse_req_t req, ganefuse_ino_t ino, size_t size, off_t off,
                   struct ganefuse_file_info * fi);
  void (*releasedir) (ganefuse_req_t req, ganefuse_ino_t ino,
                      struct ganefuse_file_info * fi);

  void (*fsyncdir) (ganefuse_req_t req, ganefuse_ino_t ino, int datasync,
                    struct ganefuse_file_info * fi);
  void (*statfs) (ganefuse_req_t req);
  void (*setxattr) (ganefuse_req_t req, ganefuse_ino_t ino, const char *name,
                    const char *value, size_t size, int flags);
  void (*getxattr) (ganefuse_req_t req, ganefuse_ino_t ino, const char *name,
                    size_t size);
  void (*listxattr) (ganefuse_req_t req, ganefuse_ino_t ino, size_t size);
  void (*removexattr) (ganefuse_req_t req, ganefuse_ino_t ino, const char *name);
  void (*access) (ganefuse_req_t req, ganefuse_ino_t ino, int mask);
  void (*create) (ganefuse_req_t req, ganefuse_ino_t parent, const char *name,
                  mode_t mode, struct ganefuse_file_info * fi);
};

struct ganefuse_args {
  int argc;
  char **argv;
  int allocated;
};

struct ganefuse_opt {
  const char *templ;
  unsigned long offset;
  int value;
};

/* function/macro definitions */

#define GANEFUSE_ARGS_INIT(_argc_, _argv_) { _argc_, _argv_, 0 }

int ganefuse_parse_cmdline(struct ganefuse_args *args, char **mountpoint,
                           int *multithreaded, int *foreground);

typedef int (*ganefuse_opt_proc_t) (void *data, const char *arg, int key,
                                    struct ganefuse_args * outargs);
int ganefuse_opt_parse(struct ganefuse_args *args, void *data,
                       const struct ganefuse_opt opts[], ganefuse_opt_proc_t proc);
int ganefuse_opt_add_opt(char **opts, const char *opt);
int ganefuse_opt_add_arg(struct ganefuse_args *args, const char *arg);
int ganefuse_opt_insert_arg(struct ganefuse_args *args, int pos, const char *arg);
void ganefuse_opt_free_args(struct ganefuse_args *args);
int ganefuse_opt_match(const struct ganefuse_opt opts[], const char *opt);

/* reply functions */

int ganefuse_reply_err(ganefuse_req_t req, int err);
void ganefuse_reply_none(ganefuse_req_t req);
int ganefuse_reply_entry(ganefuse_req_t req, const struct ganefuse_entry_param *e);
int ganefuse_reply_create(ganefuse_req_t req, const struct ganefuse_entry_param *e,
                          const struct ganefuse_file_info *fi);
int ganefuse_reply_attr(ganefuse_req_t req, const struct stat *attr, double attr_timeout);
int ganefuse_reply_readlink(ganefuse_req_t req, const char *link);
int ganefuse_reply_open(ganefuse_req_t req, const struct ganefuse_file_info *fi);
int ganefuse_reply_write(ganefuse_req_t req, size_t count);
int ganefuse_reply_buf(ganefuse_req_t req, const char *buf, size_t size);
int ganefuse_reply_iov(ganefuse_req_t req, const struct iovec *iov, int count);
int ganefuse_reply_statfs(ganefuse_req_t req, const struct statvfs *stbuf);
int ganefuse_reply_xattr(ganefuse_req_t req, size_t count);
int ganefuse_reply_lock(ganefuse_req_t req, struct flock *lock);
int ganefuse_reply_bmap(ganefuse_req_t req, uint64_t idx);
size_t ganefuse_add_direntry(ganefuse_req_t req, char *buf, size_t bufsize,
                             const char *name, const struct stat *stbuf, off_t off);

/* req functions */

void *ganefuse_req_userdata(ganefuse_req_t req);
const struct ganefuse_ctx *ganefuse_req_ctx(ganefuse_req_t req);

typedef void (*ganefuse_interrupt_func_t) (ganefuse_req_t req, void *data);

void ganefuse_req_interrupt_func(ganefuse_req_t req, ganefuse_interrupt_func_t func,
                                 void *data);
int ganefuse_req_interrupted(ganefuse_req_t req);

struct ganefuse_session *ganefuse_lowlevel_new(struct ganefuse_args *args,
                                               const struct ganefuse_lowlevel_ops *op,
                                               size_t op_size, void *userdata);

struct ganefuse_session *ganefuse_lowlevel_new25(struct ganefuse_args *args,
                                                 const struct ganefuse_lowlevel_ops25 *op,
                                                 size_t op_size, void *userdata);

/* session type and calls */

struct ganefuse_session_ops {
  void (*process) (void *data, const char *buf, size_t len, struct ganefuse_chan * ch);
  void (*exit) (void *data, int val);
  int (*exited) (void *data);
  void (*destroy) (void *data);
};

struct ganefuse_session *ganefuse_session_new(struct ganefuse_session_ops *op,
                                              void *data);
void ganefuse_session_add_chan(struct ganefuse_session *se, struct ganefuse_chan *ch);
void ganefuse_session_remove_chan(struct ganefuse_chan *ch);
struct ganefuse_chan *ganefuse_session_next_chan(struct ganefuse_session *se,
                                                 struct ganefuse_chan *ch);
void ganefuse_session_process(struct ganefuse_session *se, const char *buf, size_t len,
                              struct ganefuse_chan *ch);
void ganefuse_session_destroy(struct ganefuse_session *se);
void ganefuse_session_exit(struct ganefuse_session *se);
void ganefuse_session_reset(struct ganefuse_session *se);
int ganefuse_session_exited(struct ganefuse_session *se);
int ganefuse_session_loop(struct ganefuse_session *se);
int ganefuse_session_loop_mt(struct ganefuse_session *se);

/* chan type and calls */

struct ganefuse_chan_ops {
  int (*receive) (struct ganefuse_chan ** chp, char *buf, size_t size);
  int (*send) (struct ganefuse_chan * ch, const struct iovec iov[], size_t count);
  void (*destroy) (struct ganefuse_chan * ch);
};

struct ganefuse_chan *ganefuse_chan_new(struct ganefuse_chan_ops *op, int fd,
                                        size_t bufsize, void *data);
int ganefuse_chan_fd(struct ganefuse_chan *ch);
size_t ganefuse_chan_bufsize(struct ganefuse_chan *ch);
void *ganefuse_chan_data(struct ganefuse_chan *ch);
struct ganefuse_session *ganefuse_chan_session(struct ganefuse_chan *ch);
int ganefuse_chan_recv(struct ganefuse_chan **ch, char *buf, size_t size);
int ganefuse_chan_send(struct ganefuse_chan *ch, const struct iovec iov[], size_t count);
void ganefuse_chan_destroy(struct ganefuse_chan *ch);

struct ganefuse_chan *ganefuse_mount(const char *mountpoint, struct ganefuse_args *args);
int ganefuse_mount25(const char *mountpoint, struct ganefuse_args *args);

void ganefuse_unmount25(const char *mountpoint);
void ganefuse_unmount(const char *mountpoint, struct ganefuse_chan *ch);

/* for backward compatibility */
int ganefuse_chan_receive(struct ganefuse_chan *ch, char *buf, size_t size);
struct ganefuse_chan *ganefuse_kern_chan_new(int fd);
size_t ganefuse_dirent_size(size_t namelen);
char *ganefuse_add_dirent(char *buf, const char *name, const struct stat *stbuf,
                          off_t off);

/* ---------------------------------------------------------------------------*/

/*
 * Associate them to fuse types with the same name
 * so no change is needed for FUSE-binded filesystems.
 */
#define fuse_ino_t             ganefuse_ino_t
#define fuse_req_t             ganefuse_req_t
#define fuse_session	       ganefuse_session
#define fuse_chan	       ganefuse_chan
#define fuse_entry_param       ganefuse_entry_param
#define fuse_ctx               ganefuse_ctx
#define fuse_conn_info         ganefuse_conn_info
#define fuse_file_info         ganefuse_file_info
#define fuse_args              ganefuse_args
#define fuse_opt               ganefuse_opt
#define fuse_opt_proc_t        ganefuse_opt_proc_t
#define fuse_interrupt_func_t  ganefuse_interrupt_func_t
#define fuse_session_ops       ganefuse_session_ops
#define fuse_chan_ops          ganefuse_chan_ops

/* call bindings */
#define fuse_parse_cmdline     ganefuse_parse_cmdline

#define fuse_reply_err      ganefuse_reply_err
#define fuse_reply_none     ganefuse_reply_none
#define fuse_reply_entry    ganefuse_reply_entry
#define fuse_reply_create   ganefuse_reply_create
#define fuse_reply_attr     ganefuse_reply_attr
#define fuse_reply_readlink ganefuse_reply_readlink
#define fuse_reply_open     ganefuse_reply_open
#define fuse_reply_write    ganefuse_reply_write
#define fuse_reply_buf      ganefuse_reply_buf
#define fuse_reply_iov      ganefuse_reply_iov
#define fuse_reply_statfs   ganefuse_reply_statfs
#define fuse_reply_xattr    ganefuse_reply_xattr
#define fuse_reply_lock     ganefuse_reply_lock
#define fuse_reply_bmap     ganefuse_reply_bmap
#define fuse_add_direntry   ganefuse_add_direntry

#define fuse_req_userdata   ganefuse_req_userdata
#define fuse_req_ctx        ganefuse_req_ctx
#define fuse_req_interrupt_func ganefuse_req_interrupt_func
#define fuse_req_interrupted    ganefuse_req_interrupted

#define fuse_session_new 	ganefuse_session_new
#define fuse_session_add_chan 	ganefuse_session_add_chan
#define fuse_session_remove_chan ganefuse_session_remove_chan
#define fuse_session_next_chan	ganefuse_session_next_chan
#define fuse_session_process	ganefuse_session_process
#define fuse_session_destroy	ganefuse_session_destroy
#define fuse_session_exit	ganefuse_session_exit
#define fuse_session_reset	ganefuse_session_reset
#define fuse_session_exited	ganefuse_session_exited
#define fuse_session_loop	ganefuse_session_loop
#define fuse_session_loop_m	ganefuse_session_loop_mt

#define fuse_chan_new           ganefuse_chan_new
#define fuse_chan_fd            ganefuse_chan_fd
#define fuse_chan_bufsize       ganefuse_chan_bufsize
#define fuse_chan_data          ganefuse_chan_data
#define fuse_chan_session       ganefuse_chan_session
#define fuse_chan_recv          ganefuse_chan_recv
#define fuse_chan_send          ganefuse_chan_send
#define fuse_chan_destroy       ganefuse_chan_destroy
#define fuse_chan_receive       ganefuse_chan_receive
#define fuse_kern_chan_new 	ganefuse_kern_chan_new

#define fuse_opt_parse          ganefuse_opt_parse
#define fuse_opt_add_opt        ganefuse_opt_add_opt
#define fuse_opt_add_arg        ganefuse_opt_add_arg
#define fuse_opt_insert_arg     ganefuse_opt_insert_arg
#define fuse_opt_free_args      ganefuse_opt_free_args
#define fuse_opt_match          ganefuse_opt_match

#if FUSE_USE_VERSION < 25
#error "FUSE bindings before version 25 are not supported"
#elif FUSE_USE_VERSION == 25
#   define fuse_mount 		ganefuse_mount25
#   define fuse_lowlevel_ops    ganefuse_lowlevel_ops25
#   define fuse_lowlevel_new    ganefuse_lowlevel_new25
#else
#   define fuse_mount 		ganefuse_mount
#   define fuse_lowlevel_ops    ganefuse_lowlevel_ops
#   define fuse_lowlevel_new    ganefuse_lowlevel_new
#endif

#if FUSE_USE_VERSION < 26
#   define fuse_unmount 		ganefuse_unmount25
#else
#   define fuse_unmount 		ganefuse_unmount
#endif

#define fuse_dirent_size 	ganefuse_dirent_size
#define fuse_add_dirent		ganefuse_add_dirent

/* macro binding */
#define FUSE_ARGS_INIT          GANEFUSE_ARGS_INIT

#define FUSE_SET_ATTR_MODE      GANEFUSE_SET_ATTR_MODE
#define FUSE_SET_ATTR_UID       GANEFUSE_SET_ATTR_UID
#define FUSE_SET_ATTR_GID       GANEFUSE_SET_ATTR_GID
#define FUSE_SET_ATTR_SIZE      GANEFUSE_SET_ATTR_SIZE
#define FUSE_SET_ATTR_ATIME     GANEFUSE_SET_ATTR_ATIME
#define FUSE_SET_ATTR_MTIME     GANEFUSE_SET_ATTR_MTIME
