/* SPDX-License-Identifier: unknown license... */
/* NFS V2 definitions */

const NFS2_MAXDATA = 8192;
const NFS2_MAXPATHLEN = 1024;
const NFS2_MAXNAMLEN = 255;
const NFS2_COOKIESIZE  = 4;
const NFS2_FHSIZE = 32;
const NFS2_MNTPATHLEN = 1024;
const NFS2_MNTNAMLEN = 255;

typedef string nfspath2<NFS2_MAXPATHLEN>;
typedef string filename2<NFS2_MAXNAMLEN>;
typedef opaque fhandle2[NFS2_FHSIZE];
typedef opaque nfsdata2<NFS2_MAXDATA>;
typedef opaque nfscookie2[NFS2_COOKIESIZE];

enum nfsstat2 {
    NFS_OK = 0,
    NFSERR_PERM=1,
    NFSERR_NOENT=2,
    NFSERR_IO=5,
    NFSERR_NXIO=6,
    NFSERR_ACCES=13,
    NFSERR_EXIST=17,
    NFSERR_NODEV=19,
    NFSERR_NOTDIR=20,
    NFSERR_ISDIR=21,
    NFSERR_FBIG=27,
    NFSERR_NOSPC=28,
    NFSERR_ROFS=30,
    NFSERR_NAMETOOLONG=63,
    NFSERR_NOTEMPTY=66,
    NFSERR_DQUOT=69,
    NFSERR_STALE=70,
    NFSERR_WFLUSH=99
};

enum ftype2 {
    NFNON = 0,
    NFREG = 1,
    NFDIR = 2,
    NFBLK = 3,
    NFCHR = 4,
    NFLNK = 5,
    NFSOCK = 6,
    NFBAD = 7,
    NFFIFO = 8
};

struct nfstime2 {
      unsigned int seconds;
      unsigned int useconds;
};


struct fattr2 {
  ftype2       type;
  unsigned int mode;
  unsigned int nlink;
  unsigned int uid;
  unsigned int gid;
  unsigned int size;
  unsigned int blocksize;
  unsigned int rdev;
  unsigned int blocks;
  unsigned int fsid;
  unsigned int fileid;
  nfstime2     atime;
  nfstime2     mtime;
  nfstime2     ctime;
};
 
union fhstatus2 switch (unsigned status) {
case 0:
    fhandle2 directory;
default:
    void;
} ;

struct diropargs2 {
        fhandle2 dir;
        filename2 name;
};

struct DIROP2resok 
{ 
 fhandle2 file;
 fattr2   attributes;
} ;

union DIROP2res switch (nfsstat2 status)
{
 case NFS_OK:
   DIROP2resok diropok ;
 default:
   void;
};



union ATTR2res switch (nfsstat2 status) {
  case NFS_OK:
      fattr2 attributes;
  default:
      void;
};



struct sattr2 {
      unsigned int mode;
      unsigned int uid;
      unsigned int gid;
      unsigned int size;
      nfstime2      atime;
      nfstime2      mtime;
};
 



struct statinfo2 {
 unsigned tsize;
 unsigned bsize;
 unsigned blocks;
 unsigned bfree;
 unsigned bavail;
} ;

union STATFS2res switch (nfsstat2 status) {
case NFS_OK:
    statinfo2 info;
default:
    void;
};

struct READDIR2args {
  fhandle2 dir;
  nfscookie2 cookie;
  unsigned count;
};

struct entry2 {
  unsigned fileid;
  filename2 name;
  nfscookie2 cookie;
  entry2 *nextentry;
};

struct READDIR2resok {
  entry2 *entries;
  bool eof;
} ;

union READDIR2res switch (nfsstat2 status) {
case NFS_OK:
  READDIR2resok readdirok;
default:
  void;
};



struct SYMLINK2args {
 diropargs2 from;
 nfspath2 to;
 sattr2 attributes;
};


struct LINK2args {
 fhandle2 from;
 diropargs2 to;
};

struct RENAME2args {
 diropargs2 from;
 diropargs2 to;
};


struct CREATE2args {
 diropargs2 where;
 sattr2 attributes;
};


struct WRITE2args {
 fhandle2 file;
 unsigned beginoffset;
 unsigned offset;
 unsigned totalcount;
 nfsdata2 data;
};

struct READ2resok {
 fattr2 attributes ;
 nfsdata2 data ;
};
 
union READ2res switch (nfsstat2 status) {
 case NFS_OK: 
   READ2resok readok;
 default:
   void;
};

struct READ2args {
 fhandle2 file;
 unsigned offset;
 unsigned count;
 unsigned totalcount;
};



union READLINK2res switch (nfsstat2 status) {
 case NFS_OK:
     nfspath2 data;
 default:
     void;
};


struct SETATTR2args {
         fhandle2 file;
         sattr2 attributes;
};
 
/* NFS V3 definitions */


#define NFS3_FHSIZE 64
#define NFS3_COOKIEVERFSIZE 8
#define NFS3_CREATEVERFSIZE 8
#define NFS3_WRITEVERFSIZE 8

typedef unsigned hyper nfs3_uint64;
typedef hyper nfs3_int64;
typedef unsigned int nfs3_uint32;
typedef int nfs3_int32;
typedef string filename3<>;
typedef string nfspath3<>;
typedef nfs3_uint64 fileid3;
typedef nfs3_uint64 cookie3;
typedef opaque cookieverf3[NFS3_COOKIEVERFSIZE];
typedef opaque createverf3[NFS3_CREATEVERFSIZE];
typedef opaque writeverf3[NFS3_WRITEVERFSIZE];
typedef nfs3_uint32 uid3;
typedef nfs3_uint32 gid3;
typedef nfs3_uint64 size3;
typedef nfs3_uint64 offset3;
typedef nfs3_uint32 mode3;
typedef nfs3_uint32 count3;


enum nfsstat3
{
 NFS3_OK             = 0,
 NFS3ERR_PERM        = 1,
 NFS3ERR_NOENT       = 2,
 NFS3ERR_IO          = 5,
 NFS3ERR_NXIO        = 6,
 NFS3ERR_ACCES       = 13,
 NFS3ERR_EXIST       = 17,
 NFS3ERR_XDEV        = 18,
 NFS3ERR_NODEV       = 19,
 NFS3ERR_NOTDIR      = 20,
 NFS3ERR_ISDIR       = 21,
 NFS3ERR_INVAL       = 22,
 NFS3ERR_FBIG        = 27,
 NFS3ERR_NOSPC       = 28,
 NFS3ERR_ROFS        = 30,
 NFS3ERR_MLINK       = 31,
 NFS3ERR_NAMETOOLONG = 63,
 NFS3ERR_NOTEMPTY    = 66,
 NFS3ERR_DQUOT       = 69,
 NFS3ERR_STALE       = 70,
 NFS3ERR_REMOTE      = 71,
 NFS3ERR_BADHANDLE   = 10001,
 NFS3ERR_NOT_SYNC    = 10002,
 NFS3ERR_BAD_COOKIE  = 10003,
 NFS3ERR_NOTSUPP     = 10004,
 NFS3ERR_TOOSMALL    = 10005,
 NFS3ERR_SERVERFAULT = 10006,
 NFS3ERR_BADTYPE     = 10007,
 NFS3ERR_JUKEBOX     = 10008
};

enum ftype3
{
 NF3REG    = 1,
 NF3DIR    = 2,
 NF3BLK    = 3,
 NF3CHR    = 4,
 NF3LNK    = 5,
 NF3SOCK   = 6,
 NF3FIFO   = 7
};

struct specdata3
{
 nfs3_uint32     specdata1;
 nfs3_uint32     specdata2;
};

struct nfs_fh3
{
 opaque       data<NFS3_FHSIZE>;
};

struct nfstime3
{
 nfs3_uint32   seconds;
 nfs3_uint32   nseconds;
};

struct fattr3
{
 ftype3     type;
 mode3      mode;
 nfs3_uint32     nlink;
 uid3       uid;
 gid3       gid;
 size3      size;
 size3      used;
 specdata3  rdev;
 nfs3_uint64     fsid;
 fileid3    fileid;
 nfstime3   atime;
 nfstime3   mtime;
 nfstime3   ctime;
};

union post_op_attr switch (bool attributes_follow)
{
 case TRUE:
 fattr3   attributes;
 case FALSE:
 void;
};

struct wcc_attr
{
 size3       size;
 nfstime3    mtime;
 nfstime3    ctime;
};

union pre_op_attr switch (bool attributes_follow)
{
 case TRUE:
  wcc_attr  attributes;
 case FALSE:
  void;
};

struct wcc_data
{
 pre_op_attr    before;
 post_op_attr   after;
};

union post_op_fh3 switch (bool handle_follows)
{
 case TRUE:
  nfs_fh3  handle;
 case FALSE:
  void;
};

enum time_how
{
 DONT_CHANGE        = 0,
 SET_TO_SERVER_TIME = 1,
 SET_TO_CLIENT_TIME = 2
};

union set_mode3 switch (bool set_it)
{
 case TRUE:
  mode3    mode;
 default:
  void;
};

union set_uid3 switch (bool set_it)
{
 case TRUE:
  uid3     uid;
 default:
  void;
};

union set_gid3 switch (bool set_it)
{
 case TRUE:
  gid3     gid;
 default:
  void;
};

union set_size3 switch (bool set_it)
{
 case TRUE:
  size3    size;
 default:
  void;
};

union set_atime switch (time_how set_it)
{
 case SET_TO_CLIENT_TIME:
  nfstime3  atime;
 default:
  void;
};

union set_mtime switch (time_how set_it)
{
 case SET_TO_CLIENT_TIME:
  nfstime3  mtime;
 default:
  void;
};

struct sattr3
{
 set_mode3   mode;
 set_uid3    uid;
 set_gid3    gid;
 set_size3   size;
 set_atime   atime;
 set_mtime   mtime;
};

struct diropargs3
{
 nfs_fh3     dir;
 filename3   name;
};


     
struct GETATTR3args
{
 nfs_fh3  object;
};

struct GETATTR3resok
{
 fattr3   obj_attributes;
};

union GETATTR3res switch (nfsstat3 status)
{
 case NFS3_OK:
  GETATTR3resok  resok;
 default:
  void;
};

     
union sattrguard3 switch (bool check)
{
 case TRUE:
  nfstime3  obj_ctime;
 case FALSE:
  void;
};

struct SETATTR3args
{
 nfs_fh3      object;
 sattr3       new_attributes;
 sattrguard3  guard;
};

struct SETATTR3resok
{
 wcc_data  obj_wcc;
};

struct SETATTR3resfail
{
 wcc_data  obj_wcc;
};

union SETATTR3res switch (nfsstat3 status)
{
 case NFS3_OK:
  SETATTR3resok   resok;
 default:
  SETATTR3resfail resfail;
};

      
struct LOOKUP3args
{
 diropargs3  what;
};

struct LOOKUP3resok
{
 nfs_fh3      object;
 post_op_attr obj_attributes;
 post_op_attr dir_attributes;
};

struct LOOKUP3resfail
{
 post_op_attr dir_attributes;
};

union LOOKUP3res switch (nfsstat3 status)
{
 case NFS3_OK:
  LOOKUP3resok    resok;
 default:
  LOOKUP3resfail  resfail;
};

     
const ACCESS3_READ    = 0x0001;
const ACCESS3_LOOKUP  = 0x0002;
const ACCESS3_MODIFY  = 0x0004;
const ACCESS3_EXTEND  = 0x0008;
const ACCESS3_DELETE  = 0x0010;
const ACCESS3_EXECUTE = 0x0020;

struct ACCESS3args
{
 nfs_fh3  object;
 nfs3_uint32   access;
};

struct ACCESS3resok
{
 post_op_attr   obj_attributes;
 nfs3_uint32         access;
};

struct ACCESS3resfail
{
 post_op_attr   obj_attributes;
};

union ACCESS3res switch (nfsstat3 status)
{
 case NFS3_OK:
  ACCESS3resok   resok;
 default:
  ACCESS3resfail resfail;
};

      
struct READLINK3args
{
 nfs_fh3  symlink;
};

struct READLINK3resok
{
 post_op_attr   symlink_attributes;
 nfspath3       data;
};

struct READLINK3resfail
{
 post_op_attr   symlink_attributes;
};

union READLINK3res switch (nfsstat3 status)
{
 case NFS3_OK:
  READLINK3resok   resok;
 default:
  READLINK3resfail resfail;
};

      
struct READ3args
{
 nfs_fh3  file;
 offset3  offset;
 count3   count;
};

struct READ3resok
{
 post_op_attr   file_attributes;
 count3         count;
 bool           eof;
 opaque         data<>;
};

struct READ3resfail
{
 post_op_attr   file_attributes;
};

union READ3res switch (nfsstat3 status)
{
 case NFS3_OK:
  READ3resok   resok;
 default:
  READ3resfail resfail;
};

      
enum stable_how
{
 UNSTABLE  = 0,
 DATA_SYNC = 1,
 FILE_SYNC = 2
};

struct WRITE3args
{
 nfs_fh3     file;
 offset3     offset;
 count3      count;
 stable_how  stable;
 opaque      data<>;
};

struct WRITE3resok
{
 wcc_data    file_wcc;
 count3      count;
 stable_how  committed;
 writeverf3  verf;
};

struct WRITE3resfail
{
 wcc_data    file_wcc;
};

union WRITE3res switch (nfsstat3 status)
{
 case NFS3_OK:
  WRITE3resok    resok;
 default:
  WRITE3resfail  resfail;
};


enum createmode3
{
 UNCHECKED = 0,
 GUARDED   = 1,
 EXCLUSIVE = 2
};

union createhow3 switch (createmode3 mode)
{
 case UNCHECKED:
 case GUARDED:
  sattr3 obj_attributes;
 case EXCLUSIVE:
  createverf3  verf;
};

struct CREATE3args
{
 diropargs3   where;
 createhow3   how;
};

struct CREATE3resok
{
 post_op_fh3   obj;
 post_op_attr  obj_attributes;
 wcc_data      dir_wcc;
};

struct CREATE3resfail
{
 wcc_data      dir_wcc;
};

union CREATE3res switch (nfsstat3 status)
{
 case NFS3_OK:
  CREATE3resok    resok;
 default:
  CREATE3resfail  resfail;
};

      
struct MKDIR3args
{
 diropargs3   where;
 sattr3       attributes;
};

struct MKDIR3resok
{
 post_op_fh3   obj;
 post_op_attr  obj_attributes;
 wcc_data      dir_wcc;
};

struct MKDIR3resfail
{
 wcc_data      dir_wcc;
};

union MKDIR3res switch (nfsstat3 status)
{
 case NFS3_OK:
  MKDIR3resok   resok;
 default:
  MKDIR3resfail resfail;
};

      
struct symlinkdata3
{
 sattr3    symlink_attributes;
 nfspath3  symlink_data;
};

struct SYMLINK3args
{
 diropargs3    where;
 symlinkdata3  symlink;
};

struct SYMLINK3resok
{
 post_op_fh3   obj;
 post_op_attr  obj_attributes;
 wcc_data      dir_wcc;
};

struct SYMLINK3resfail
{
 wcc_data      dir_wcc;
};

union SYMLINK3res switch (nfsstat3 status)
{
 case NFS3_OK:
  SYMLINK3resok   resok;
 default:
  SYMLINK3resfail resfail;
};

     
struct devicedata3
{
 sattr3     dev_attributes;
 specdata3  spec;
};

union mknoddata3 switch (ftype3 type)
{
 case NF3CHR:
 case NF3BLK:
  devicedata3  device;
 case NF3SOCK:
 case NF3FIFO:
  sattr3       pipe_attributes;
 default:
  void;
};

struct MKNOD3args
{
 diropargs3   where;
 mknoddata3   what;
};


struct MKNOD3resok
{
 post_op_fh3   obj;
 post_op_attr  obj_attributes;
 wcc_data      dir_wcc;
};

struct MKNOD3resfail
{
 wcc_data      dir_wcc;
};

union MKNOD3res switch (nfsstat3 status)
{
 case NFS3_OK:
  MKNOD3resok   resok;
 default:
  MKNOD3resfail resfail;
};


struct REMOVE3args
{
 diropargs3  object;
};

struct REMOVE3resok
{
 wcc_data    dir_wcc;
};

struct REMOVE3resfail
{
 wcc_data    dir_wcc;
};

union REMOVE3res switch (nfsstat3 status)
{
 case NFS3_OK:
  REMOVE3resok   resok;
 default:
  REMOVE3resfail resfail;
};

      
struct RMDIR3args
{
 diropargs3  object;
};

struct RMDIR3resok
{
 wcc_data    dir_wcc;
};

struct RMDIR3resfail
{
 wcc_data    dir_wcc;
};

union RMDIR3res switch (nfsstat3 status)
{
 case NFS3_OK:
  RMDIR3resok   resok;
 default:
  RMDIR3resfail resfail;
};

     
struct RENAME3args
{
 diropargs3   from;
 diropargs3   to;
};

struct RENAME3resok
{
 wcc_data     fromdir_wcc;
 wcc_data     todir_wcc;
};

struct RENAME3resfail
{
 wcc_data     fromdir_wcc;
 wcc_data     todir_wcc;
};

union RENAME3res switch (nfsstat3 status)
{
 case NFS3_OK:
  RENAME3resok   resok;
 default:
  RENAME3resfail resfail;
};

      
struct LINK3args
{
 nfs_fh3     file;
 diropargs3  link;
};

struct LINK3resok
{
 post_op_attr   file_attributes;
 wcc_data       linkdir_wcc;
};

struct LINK3resfail
{
 post_op_attr   file_attributes;
 wcc_data       linkdir_wcc;
};

union LINK3res switch (nfsstat3 status) {
 case NFS3_OK:
  LINK3resok    resok;
 default:
  LINK3resfail  resfail;
};

      
struct READDIR3args
{
 nfs_fh3      dir;
 cookie3      cookie;
 cookieverf3  cookieverf;
 count3       count;
};

struct entry3
{
 fileid3      fileid;
 filename3    name;
 cookie3      cookie;
 entry3       *nextentry;
};

struct dirlist3
{
 entry3       *entries;
 bool         eof;
};

struct READDIR3resok
{
 post_op_attr dir_attributes;
 cookieverf3  cookieverf;
 dirlist3     reply;
};

struct READDIR3resfail
{
 post_op_attr dir_attributes;
};

union READDIR3res switch (nfsstat3 status)
{
 case NFS3_OK:
  READDIR3resok   resok;
 default:
  READDIR3resfail resfail;
};

struct READDIRPLUS3args
{
 nfs_fh3      dir;
 cookie3      cookie;
 cookieverf3  cookieverf;
 count3       dircount;
 count3       maxcount;
};

struct entryplus3
{
 fileid3      fileid;
 filename3    name;
 cookie3      cookie;
 post_op_attr name_attributes;
 post_op_fh3  name_handle;
 entryplus3   *nextentry;
};

struct dirlistplus3
{
 entryplus3   *entries;
 bool         eof;
};

struct READDIRPLUS3resok
{
 post_op_attr dir_attributes;
 cookieverf3  cookieverf;
 dirlistplus3 reply;
};


struct READDIRPLUS3resfail
{
 post_op_attr dir_attributes;
};

union READDIRPLUS3res switch (nfsstat3 status)
{
 case NFS3_OK:
  READDIRPLUS3resok   resok;
 default:
  READDIRPLUS3resfail resfail;
};

struct FSSTAT3args
{
 nfs_fh3   fsroot;
};

struct FSSTAT3resok
{
 post_op_attr obj_attributes;
 size3        tbytes;
 size3        fbytes;
 size3        abytes;
 size3        tfiles;
 size3        ffiles;
 size3        afiles;
 nfs3_uint32       invarsec;
};

struct FSSTAT3resfail
{
 post_op_attr obj_attributes;
};

union FSSTAT3res switch (nfsstat3 status)
{
 case NFS3_OK:
  FSSTAT3resok   resok;
 default:
 FSSTAT3resfail resfail;
};

const FSF3_LINK        = 0x0001;
const FSF3_SYMLINK     = 0x0002;
const FSF3_HOMOGENEOUS = 0x0008;
const FSF3_CANSETTIME  = 0x0010;

struct FSINFO3args
{
 nfs_fh3   fsroot;
};

struct FSINFO3resok
{
 post_op_attr obj_attributes;
 nfs3_uint32       rtmax;
 nfs3_uint32       rtpref;
 nfs3_uint32       rtmult;
 nfs3_uint32       wtmax;
 nfs3_uint32       wtpref;
 nfs3_uint32       wtmult;
 nfs3_uint32       dtpref;
 size3        maxfilesize;
 nfstime3     time_delta;
 nfs3_uint32       properties;
};

struct FSINFO3resfail
{
 post_op_attr obj_attributes;
};

union FSINFO3res switch (nfsstat3 status)
{
 case NFS3_OK:
  FSINFO3resok   resok;
 default:
  FSINFO3resfail resfail;
};

struct PATHCONF3args
{
 nfs_fh3   object;
};

struct PATHCONF3resok
{
 post_op_attr obj_attributes;
 nfs3_uint32       linkmax;
 nfs3_uint32       name_max;
 bool         no_trunc;
 bool         chown_restricted;
 bool         case_insensitive;
 bool         case_preserving;
};

struct PATHCONF3resfail
{
 post_op_attr obj_attributes;
};

union PATHCONF3res switch (nfsstat3 status)
{
 case NFS3_OK:
  PATHCONF3resok   resok;
 default:
  PATHCONF3resfail resfail;
};

struct COMMIT3args
{
 nfs_fh3    file;
 offset3    offset;
 count3     count;
};

struct COMMIT3resok
{
 wcc_data   file_wcc;
 writeverf3 verf;
};

struct COMMIT3resfail
{
 wcc_data   file_wcc;
};

union COMMIT3res switch (nfsstat3 status)
{
 case NFS3_OK:
  COMMIT3resok   resok;
 default:
  COMMIT3resfail resfail;
};


program NFS_PROGRAM
{
 version NFS_V2
 {
  void            NFSPROC_NULL(void)                     = 0;
  ATTR2res        NFSPROC_GETATTR(fhandle2)              = 1;
  ATTR2res        NFSPROC_SETATTR(SETATTR2args)          = 2;
  void            NFSPROC_ROOT(void)                     = 3;
  DIROP2res       NFSPROC_LOOKUP(diropargs2)             = 4;
  READLINK2res    NFSPROC_READLINK(fhandle2)             = 5;
  READ2res        NFSPROC_READ(READ2args)                = 6;
  void            NFSPROC_WRITECACHE(void)               = 7;
  ATTR2res        NFSPROC_WRITE(WRITE2args)              = 8;
  DIROP2res       NFSPROC_CREATE(CREATE2args)            = 9;
  nfsstat2        NFSPROC_REMOVE(diropargs2)             = 10;
  nfsstat2        NFSPROC_RENAME(RENAME2args)            = 11;
  nfsstat2        NFSPROC_LINK(LINK2args)                = 12;
  nfsstat2        NFSPROC_SYMLINK(SYMLINK2args)          = 13;
  DIROP2res       NFSPROC_MKDIR(CREATE2args)             = 14;
  nfsstat2        NFSPROC_RMDIR(diropargs2)              = 15;
  READDIR2res     NFSPROC_READDIR(READDIR2args)          = 16;                         
  STATFS2res      NFSPROC_STATFS(fhandle2)               = 17;
 } = 2;
 version NFS_V3
 {
  void            NFSPROC3_NULL(void)                    = 0;
  GETATTR3res     NFSPROC3_GETATTR(GETATTR3args)         = 1;
  SETATTR3res     NFSPROC3_SETATTR(SETATTR3args)         = 2;
  LOOKUP3res      NFSPROC3_LOOKUP(LOOKUP3args)           = 3;
  ACCESS3res      NFSPROC3_ACCESS(ACCESS3args)           = 4;
  READLINK3res    NFSPROC3_READLINK(READLINK3args)       = 5;
  READ3res        NFSPROC3_READ(READ3args)               = 6;
  WRITE3res       NFSPROC3_WRITE(WRITE3args)             = 7;
  CREATE3res      NFSPROC3_CREATE(CREATE3args)           = 8;
  MKDIR3res       NFSPROC3_MKDIR(MKDIR3args)             = 9;
  SYMLINK3res     NFSPROC3_SYMLINK(SYMLINK3args)         = 10;
  MKNOD3res       NFSPROC3_MKNOD(MKNOD3args)             = 11;
  REMOVE3res      NFSPROC3_REMOVE(REMOVE3args)           = 12;
  RMDIR3res       NFSPROC3_RMDIR(RMDIR3args)             = 13;
  RENAME3res      NFSPROC3_RENAME(RENAME3args)           = 14;
  LINK3res        NFSPROC3_LINK(LINK3args)               = 15;
  READDIR3res     NFSPROC3_READDIR(READDIR3args)         = 16;
  READDIRPLUS3res NFSPROC3_READDIRPLUS(READDIRPLUS3args) = 17;
  FSSTAT3res      NFSPROC3_FSSTAT(FSSTAT3args)           = 18;
  FSINFO3res      NFSPROC3_FSINFO(FSINFO3args)           = 19;
  PATHCONF3res    NFSPROC3_PATHCONF(PATHCONF3args)       = 20;
  COMMIT3res      NFSPROC3_COMMIT(COMMIT3args)           = 21;
 } = 3;
} = 100003;
 


