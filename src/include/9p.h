/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/* 
 * Copied from 2.6.38-rc2 kernel, taken from diod sources (code.google.com/p/diod/) then adapted to ganesha
 */

#ifndef _9P_H
#define _9P_H
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/select.h>
#include "fsal.h"
#include "cache_inode.h"

#ifdef _USE_9P_RDMA
#include <infiniband/arch.h>
#include <rdma/rdma_cma.h>
#include "mooshika.h"
#endif

typedef uint8_t   u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define NB_PREALLOC_HASH_9P 100
#define NB_PREALLOC_FID_9P  100
#define PRIME_9P 17 

#define _9P_LOCK_CLIENT_LEN 64

#define _9P_FID_PER_CONN        1024

/* _9P_MSG_SIZE : maximum message size for 9P/TCP */
#define _9P_MSG_SIZE 70000 

#define _9P_HDR_SIZE  4
#define _9P_TYPE_SIZE 1
#define _9P_TAG_SIZE  2
#define _9P_STD_HDR_SIZE (_9P_HDR_SIZE + _9P_TYPE_SIZE + _9P_TAG_SIZE)

/* _9P_BLK_SIZE : (fake) filesystem block size that we return in getattr() */
#define _9P_BLK_SIZE 4096
#define _9P_IOUNIT   0

/* _9P_RDMA_CHUNK_SIZE : buffer size, and maximum message size for 9P/RDMA */
#define _9P_RDMA_CHUNK_SIZE (1024*1024)
#define _9P_RDMA_BUFF_NUM 64
/* Half the buffers are for recv, half for send */
#define _9P_RDMA_OUT (_9P_RDMA_BUFF_NUM/2)
#define _9P_RDMA_BACKLOG 10 

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


/* Various header lengths to check message sizes : */

/* size[4] Rread tag[2] count[4] data[count] */
#define _9P_ROOM_RREAD (_9P_STD_HDR_SIZE + 4 )

/* size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count] */
#define _9P_ROOM_TWRITE (_9P_STD_HDR_SIZE + 4 + 8 + 4)

/* size[4] Rreaddir tag[2] count[4] data[count] */
#define _9P_ROOM_RREADDIR (_9P_STD_HDR_SIZE + 4 )

/**
 * @brief Length prefixed string type
 *
 * The protocol uses length prefixed strings for all
 * string data, so we replicate that for our internal
 * string members.
 */

struct _9p_str {
	u16  len; /*< Length of the string */
	char *str; /*< The string */
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

typedef struct _9p_qid {
	u8 type; /*< Type */
	u32 version; /*< Monotonically incrementing version number */
	u64 path; /*< Per-server-unique ID for a file system element */
} _9p_qid_t;

typedef struct _9p_fid__
{
  u32                     fid ;
  struct req_op_context   op_context ;
  struct user_cred        ucred ;
  exportlist_t          * pexport ;
  cache_entry_t         * pentry ;
  _9p_qid_t               qid ;
  cache_entry_t         * ppentry ;
  char                    name[MAXNAMLEN] ;
  u32                     opens ;
  bool                    from_attach ;
  union 
    { 
       u32      iounit ;
       struct _9p_xattr_desc 
        {  
          unsigned int xattr_id ;
          caddr_t      xattr_content ;
          u64          xattr_size ;
          u64          xattr_offset ;
          bool         xattr_write ;
        } xattr ;

    } specdata ;
} _9p_fid_t ;

typedef enum _9p_trans_type__
{
  _9P_TCP,
  _9P_RDMA
} _9p_trans_type_t ;

struct flush_condition;

/* flush hook : 
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
typedef struct _9p_flush_hook__
{
  int tag;
  struct flush_condition *condition;
  unsigned long sequence;
  struct glist_head list;
} _9p_flush_hook_t;

typedef struct _9p_flush_bucket__
{
  pthread_mutex_t lock;
  struct glist_head list;
} _9p_flush_bucket_t;

#define FLUSH_BUCKETS 64

typedef struct _9p_conn__
{
  union  trans_data
   {
     long int        sockfd ;
#ifdef _USE_9P_RDMA
      msk_trans_t   *rdma_trans ;
#endif 
   } trans_data ;
  _9p_trans_type_t trans_type ;
  uint32_t        refcount;
  struct sockaddr_storage addrpeer ;
  struct timeval  birth;  /* This is useful if same sockfd is reused on socket's close/open  */
  _9p_fid_t       * fids[_9P_FID_PER_CONN] ;
  _9p_flush_bucket_t flush_buckets[FLUSH_BUCKETS];
  unsigned long sequence ;
  pthread_mutex_t sock_lock;
  unsigned int msize;
} _9p_conn_t ;

#ifdef _USE_9P_RDMA
typedef struct _9p_datalock
{
  msk_data_t *data;
  struct _9p_datalock * sender ;
  pthread_mutex_t lock;
} _9p_datalock_t ;

typedef struct _9p_rdma_priv
{
  _9p_conn_t    * pconn ;
  uint8_t       * rdmabuf ;
  msk_data_t    * rdata ;
  _9p_datalock_t  * datalock ;
} _9p_rdma_priv ;
#define _9p_rdma_priv_of(x) ((_9p_rdma_priv*)x->private_data)
#endif

typedef struct _9p_request_data__
{
  char        * _9pmsg ;
  _9p_conn_t  *  pconn ;
#ifdef _USE_9P_RDMA
  _9p_datalock_t * datalock ;
#endif
  _9p_flush_hook_t flush_hook;
} _9p_request_data_t ;



typedef int (*_9p_function_t) (_9p_request_data_t * preq9p, 
                               void * pworker_data,
                               u32 * plenout, char * preply) ;

typedef struct _9p_function_desc__
{
  _9p_function_t service_function;
  char *funcname;
} _9p_function_desc_t;


#define _9p_getptr( __cursor, __pvar, __type ) \
do                                             \
{                                              \
  __pvar=(__type *)__cursor ;                  \
  __cursor += sizeof( __type ) ;               \
} while( 0 ) 

#define _9p_getstr( __cursor, __len, __str ) \
do                                           \
{                                            \
  __len = (u16 *)__cursor ;                  \
  __cursor += sizeof( u16 ) ;                \
  __str = __cursor ;                         \
  __cursor += *__len ;                       \
} while( 0 )                           

#define _9p_setptr( __cursor, __pvar, __type ) \
do                                             \
{                                              \
  *((__type *)__cursor) = *__pvar ;              \
  __cursor += sizeof( __type ) ;               \
} while( 0 ) 

#define _9p_setvalue( __cursor, __var, __type ) \
do                                             \
{                                              \
  *((__type *)__cursor) = __var ;              \
  __cursor += sizeof( __type ) ;               \
} while( 0 ) 

#define _9p_savepos( __cursor, __savedpos, __type ) \
do                                                  \
{                                                   \
  __savedpos = __cursor ;                           \
  __cursor += sizeof( __type ) ;                    \
} while ( 0 ) 

/* Insert a qid */
#define _9p_setqid( __cursor, __qid )  \
do                                     \
{                                      \
  *((u8 *)__cursor) = __qid.type ;     \
  __cursor += sizeof( u8 ) ;           \
  *((u32 *)__cursor) = __qid.version ; \
  __cursor += sizeof( u32 ) ;          \
  *((u64 *)__cursor) = __qid.path ;    \
  __cursor += sizeof( u64 ) ;          \
} while( 0 ) 

/* Insert a non-null terminated string */
#define _9p_setstr( __cursor, __len, __str ) \
do                                           \
{                                            \
  *((u16 *)__cursor) = __len ;               \
  __cursor += sizeof( u16 ) ;                \
  memcpy( __cursor, __str, __len ) ;         \
  __cursor += __len ;                        \
} while( 0 )

/* _9p_setbuffer : 
 * Copy data from __buffer into the reply,
 * with a length u32 header.
 */
#define _9p_setbuffer( __cursor, __len, __buffer ) \
do                                           \
{                                            \
  *((u32 *)__cursor) = __len ;               \
  __cursor += sizeof( u32 ) ;                \
  memcpy( __cursor, __buffer, __len ) ;         \
  __cursor += __len ;                        \
} while( 0 )

/* _9p_setfilledbuffer : 
 * Data has already been copied into the reply.
 * Only move the cursor and set the length.
 */
#define _9p_setfilledbuffer( __cursor, __len ) \
do                                           \
{                                            \
  *((u32 *)__cursor) = __len ;               \
  __cursor += sizeof( u32 ) + __len ;        \
} while( 0 )

/* _9p_getbuffertofill :
 * Get a pointer where to copy data in the reply.
 * This leaves room in the reply for a u32 len header
 */
#define _9p_getbuffertofill( __cursor ) (((char*) (cursor)) + sizeof(u32))

#define _9p_setinitptr( __cursor, __start, __reqtype ) \
do                                                     \
{                                                      \
  __cursor = __start + _9P_HDR_SIZE;                   \
  *((u8 *)__cursor) = __reqtype ;                      \
  __cursor += sizeof( u8 ) ;                           \
} while( 0 ) 

/* _9p_setendptr :
 * Calculate message size, and write this value in the
 * header of the 9p message.
 */
#define _9p_setendptr( __cursor, __start )         \
do                                                 \
{                                                  \
  *((u32 *)__start) =  (u32)(__cursor - __start) ; \
} while( 0 ) 


/* _9p_checkbound : 
 * Check that the message size is less than *__maxlen,
 * AND set *__maxlen to actual message size.
 */
#define _9p_checkbound( __cursor, __start, __maxlen ) \
do                                                    \
{                                                     \
if( (u32)( __cursor - __start ) > *__maxlen )         \
  return -1 ;                                         \
else                                                  \
   *__maxlen = (u32)( __cursor - __start )  ;         \
} while( 0 ) 

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

#define _9P_GETATTR_BASIC	0x000007ffULL /* Mask for fields up to BLOCKS */
#define _9P_GETATTR_ALL		0x00003fffULL /* Mask for All fields above */

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

/* service functions */
int _9p_read_conf( config_file_t   in_config,
                   _9p_parameter_t *pparam ) ;
int _9p_init( _9p_parameter_t * pparam ) ;

/* Tools functions */
int _9p_tools_get_req_context_by_uid( u32 uid, _9p_fid_t * pfid ) ;
int _9p_tools_get_req_context_by_name( int uname_len, char * uname_str, _9p_fid_t * pfid ) ;
int _9p_tools_errno( cache_inode_status_t cache_status ) ;
void _9p_tools_fsal_attr2stat( struct attrlist * pfsalattr, struct stat * pstat ) ;
void _9p_tools_acess2fsal( u32 * paccessin, fsal_accessflags_t * pfsalaccess ) ;
void _9p_openflags2FSAL( u32 * inflags, fsal_openflags_t * outflags ) ;
void _9p_chomp_attr_value(char *str, size_t size) ;
void _9p_cleanup_fids(_9p_conn_t *conn ) ;

#ifdef _USE_9P_RDMA
/* 9P/RDMA callbacks */
void* _9p_rdma_handle_trans(void *arg) ;
void _9p_rdma_callback_recv(msk_trans_t *trans, msk_data_t *pdata, void *arg) ;
void _9p_rdma_callback_disconnect(msk_trans_t *trans) ;
void _9p_rdma_callback_send(msk_trans_t *trans, msk_data_t *pdata, void *arg) ;
void _9p_rdma_callback_recv_err(msk_trans_t *trans, msk_data_t *pdata, void *arg) ;
void _9p_rdma_callback_send_err(msk_trans_t *trans, msk_data_t *pdata, void *arg) ;

#endif
void _9p_AddFlushHook(_9p_request_data_t *req, int tag, unsigned long sequence);
void _9p_FlushFlushHook(_9p_conn_t *conn, int tag, unsigned long sequence);
int _9p_LockAndTestFlushHook(_9p_request_data_t *req);
void _9p_ReleaseFlushHook(_9p_request_data_t *req);
void _9p_DiscardFlushHook(_9p_request_data_t *req);

/* Protocol functions */
int _9p_not_2000L( _9p_request_data_t * preq9p, 
                   void * pworker_data,
                   u32 * plenout, 
                   char * preply) ;

int _9p_clunk( _9p_request_data_t * preq9p, 
               void * pworker_data,
               u32 * plenout,
               char * preply) ;

int _9p_attach( _9p_request_data_t * preq9p, 
                void * pworker_data,
                u32 * plenout, 
                char * preply) ;

int _9p_auth( _9p_request_data_t * preq9p, 
              void * pworker_data,
              u32 * plenout, 
              char * preply) ;

int _9p_lcreate( _9p_request_data_t * preq9p, 
                 void * pworker_data,
                 u32 * plenout, 
                 char * preply) ;

int _9p_flush( _9p_request_data_t * preq9p, 
               void * pworker_data,
               u32 * plenout, 
               char * preply) ;

int _9p_getattr( _9p_request_data_t * preq9p, 
                 void * pworker_data,
                 u32 * plenout, 
                 char * preply) ;

int _9p_getlock( _9p_request_data_t * preq9p, 
                 void * pworker_data,
                 u32 * plenout, 
                 char * preply) ;

int _9p_link( _9p_request_data_t * preq9p, 
              void * pworker_data,
              u32 * plenout, 
              char * preply) ;

int _9p_lock( _9p_request_data_t * preq9p, 
              void * pworker_data,
              u32 * plenout, 
              char * preply) ;

int _9p_lopen( _9p_request_data_t * preq9p, 
               void * pworker_data,
               u32 * plenout, 
               char * preply) ;

int _9p_mkdir( _9p_request_data_t * preq9p, 
               void * pworker_data,
               u32 * plenout, 
               char * preply) ;

int _9p_mknod( _9p_request_data_t * preq9p, 
               void * pworker_data,
               u32 * plenout, 
               char * preply) ;

int _9p_read( _9p_request_data_t * preq9p, 
              void * pworker_data,
              u32 * plenout, 
              char * preply) ;

int _9p_readdir( _9p_request_data_t * preq9p, 
                 void * pworker_data,
                 u32 * plenout, 
                 char * preply) ;

int _9p_readlink( _9p_request_data_t * preq9p, 
                  void * pworker_data,
                  u32 * plenout, 
                  char * preply) ;

int _9p_setattr( _9p_request_data_t * preq9p, 
                 void * pworker_data,
                 u32 * plenout, 
                 char * preply) ;

int _9p_symlink( _9p_request_data_t * preq9p, 
                 void * pworker_data,
                 u32 * plenout, 
                 char * preply) ;

int _9p_remove( _9p_request_data_t * preq9p, 
                void * pworker_data,
                u32 * plenout, 
                char * preply) ;

int _9p_rename( _9p_request_data_t * preq9p, 
                void * pworker_data,
                u32 * plenout, 
                char * preply) ;

int _9p_renameat( _9p_request_data_t * preq9p, 
                  void * pworker_data,
                  u32 * plenout, 
                  char * preply) ;

int _9p_statfs( _9p_request_data_t * preq9p, 
                void * pworker_data,
                u32 * plenout, 
                char * preply) ;

int _9p_fsync( _9p_request_data_t * preq9p, 
               void  * pworker_data,
               u32 * plenout, 
               char * preply) ;

int _9p_unlinkat( _9p_request_data_t * preq9p, 
                  void * pworker_data,
                  u32 * plenout, 
                  char * preply) ;

int _9p_version( _9p_request_data_t * preq9p, 
                 void * pworker_data,
                 u32 * plenout,
                 char * preply) ;

int _9p_walk( _9p_request_data_t * preq9p, 
              void * pworker_data,
              u32 * plenout,
              char * preply) ;

int _9p_write( _9p_request_data_t * preq9p, 
               void * pworker_data,
               u32 * plenout,
               char * preply) ;

int _9p_xattrcreate( _9p_request_data_t * preq9p, 
                     void * pworker_data,
                     u32 * plenout,
                     char * preply) ;

int _9p_xattrwalk( _9p_request_data_t * preq9p, 
                   void * pworker_data,
                   u32 * plenout,
                   char * preply) ;

int _9p_rerror( _9p_request_data_t * preq9p,
                void * pworker_data,
                u16 * msgtag,
                u32   err, 
	        u32 * plenout, 
                char * preply) ;

#endif /* _9P_H */
