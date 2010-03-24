#ifndef GANESHA_FUSE_WRAP_H_
#define GANESHA_FUSE_WRAP_H_

#include <fcntl.h>
#include <time.h>
#include <utime.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <stdint.h>

/* Major version of FUSE library interface */
#define FUSE_MAJOR_VERSION 2

/* Minor version of FUSE library interface */
#define FUSE_MINOR_VERSION 6

#define FUSE_MAKE_VERSION(maj, min)  ((maj) * 10 + (min))
#define FUSE_VERSION FUSE_MAKE_VERSION(FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION)

struct ganefuse;
struct ganefuse_cmd;
typedef int (*ganefuse_fill_dir_t) (void *buf, const char *name,
                                    const struct stat * stbuf, off_t off);

struct ganefuse_file_info
{
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

struct ganefuse_conn_info
{
  unsigned proto_major;
  unsigned proto_minor;
  unsigned async_read;
  unsigned max_write;
  unsigned max_readahead;
  unsigned reserved[27];

};

/* Used by deprecated getdir() method */
typedef struct ganefuse_dirhandle *ganefuse_dirh_t;
typedef int (*ganefuse_dirfil_t) (ganefuse_dirh_t h, const char *name, int type,
                                  ino_t ino);

struct ganefuse_operations
{
  int (*getattr) (const char *, struct stat *);
  int (*readlink) (const char *, char *, size_t);

  /* getdir (deprecated, use readdir() instead)
   * Supported for backward compatibility.
   */
  int (*getdir) (const char *, ganefuse_dirh_t, ganefuse_dirfil_t);

  int (*mknod) (const char *, mode_t, dev_t);
  int (*mkdir) (const char *, mode_t);
  int (*unlink) (const char *);
  int (*rmdir) (const char *);
  int (*symlink) (const char *, const char *);
  int (*rename) (const char *, const char *);
  int (*link) (const char *, const char *);
  int (*chmod) (const char *, mode_t);
  int (*chown) (const char *, uid_t, gid_t);
  int (*truncate) (const char *, off_t);

    /**
     * Deprecated, use utimens() instead.
     * However, we support it in GANESHA if utimens is not provided.
     */
  int (*utime) (const char *, struct utimbuf *);

  int (*open) (const char *, struct ganefuse_file_info *);
  int (*read) (const char *, char *, size_t, off_t, struct ganefuse_file_info *);
  int (*write) (const char *, const char *, size_t, off_t, struct ganefuse_file_info *);
  int (*statfs) (const char *, struct statvfs *);
  int (*flush) (const char *, struct ganefuse_file_info *);
  int (*release) (const char *, struct ganefuse_file_info *);
  int (*fsync) (const char *, int, struct ganefuse_file_info *);
  int (*setxattr) (const char *, const char *, const char *, size_t, int);
  int (*getxattr) (const char *, const char *, char *, size_t);
  int (*listxattr) (const char *, char *, size_t);
  int (*removexattr) (const char *, const char *);
  int (*opendir) (const char *, struct ganefuse_file_info *);
  int (*readdir) (const char *, void *, ganefuse_fill_dir_t, off_t,
                  struct ganefuse_file_info *);
  int (*releasedir) (const char *, struct ganefuse_file_info *);
  int (*fsyncdir) (const char *, int, struct ganefuse_file_info *);
  void *(*init) (struct ganefuse_conn_info * conn);
  void (*destroy) (void *);
  int (*access) (const char *, int);
  int (*create) (const char *, mode_t, struct ganefuse_file_info *);
  int (*ftruncate) (const char *, off_t, struct ganefuse_file_info *);
  int (*fgetattr) (const char *, struct stat *, struct ganefuse_file_info *);
  int (*lock) (const char *, struct ganefuse_file_info *, int cmd, struct flock *);
  int (*utimens) (const char *, const struct timespec tv[2]);
  int (*bmap) (const char *, size_t blocksize, uint64_t * idx);
};

struct ganefuse_context
{
  struct ganefuse *ganefuse;
  uid_t uid;
  gid_t gid;
  pid_t pid;
  void *private_data;
};

struct ganefuse_context *ganefuse_get_context(void);

#ifdef  __cplusplus
extern "C"
{
#endif

  int ganefuse_main(int argc, char *argv[],
                    const struct ganefuse_operations *op, void *user_data);

#ifdef  __cplusplus
}
#endif
/* Binding for FUSE binded programs, so they don't need to
 * change their code.
 */
#define fuse            ganefuse
#define fuse_cmd        ganefuse_cmd
#define fuse_fill_dir_t ganefuse_fill_dir_t
#define fuse_file_info  ganefuse_file_info
#define fuse_conn_info  ganefuse_conn_info
#define fuse_operations ganefuse_operations
#define fuse_context    ganefuse_context
#define fuse_main       ganefuse_main
#define fuse_get_context ganefuse_get_context
#define fuse_dirh_t     ganefuse_dirh_t
#define fuse_dirfil_t   ganefuse_dirfil_t
#endif
