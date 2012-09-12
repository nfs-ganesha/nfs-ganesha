// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2010, 2011
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsi_ipc_common.h
// Description: Common FSI IPC Client and Server definitions
// Author:      Greg Kishi, Krishna Harathi
// ----------------------------------------------------------------------------

#ifndef __FSI_IPC_COMMON_H__
#define __FSI_IPC_COMMON_H__

#include <stdint.h>
#include <string.h> // for memcpy

// *****************************************************************************
// * DEFINED CONSTANTS                                                         *
// *****************************************************************************

// define the base key for FSI IPC keys

#define FSI_IPC_NON_IO_REQ_Q_KEY         0x7650
#define FSI_IPC_NON_IO_RSP_Q_KEY         0x7651
#define FSI_IPC_IO_REQ_Q_KEY             0x7652
#define FSI_IPC_IO_RSP_Q_KEY             0x7653

#define FSI_IPC_SHMEM_REQ_Q_KEY          0x7654
#define FSI_IPC_SHMEM_RSP_Q_KEY          0x7655

#define FSI_IPC_CLOSE_HANDLE_REQ_Q_KEY   0x7656
#define FSI_IPC_CLOSE_HANDLE_RSP_Q_KEY   0x7657

#define FSI_IPC_SHMEM_KEY                0x7610

// define the number of read buffers per shared memory buffer
#define FSI_IPC_SHMEM_READBUF_PER_BUF    1

// define the number of write buffers per shared memory buffer
#define FSI_IPC_SHMEM_WRITEBUF_PER_BUF   1

// define the data size of the shared memory read buffer
#define FSI_IPC_SHMEM_READBUF_SIZE       262144

// define the data size of the shared memory write buffer
#define FSI_IPC_SHMEM_WRITEBUF_SIZE      262144

// define the maximum number of shared memory buffers per stream
// some streams may get less than this
// current design is min is 4, max is 4
#define MAX_FSI_IPC_SHMEM_BUF_PER_STREAM 4

#define FSI_IPC_PAD_SIZE                            256 // define shm pad size
#define MAX_FSI_IO_THREADS                          256
#define MAX_FSI_NON_IO_THREADS                      256

// Size of the IP Address string coming from file open request
#define FSI_IPC_OPEN_IP_ADDR_STR_SIZE               128

// the client and server use errno for error types
// this is the constant for EOK
#define FSI_IPC_EOK                      0

// maximum size of a log request
#define FSI_IPC_LOG_TEXT_MAX             240

// Maximum number of I/O streams allowed by the client.  This number
// needs to be bigger than the number of I/O streams allowed by the
// server side. (Currently is 64 streams).  Possibly, 256 streams will be
// supported in the future.  The additional stream
// needed is to allow open calls on directory.  Open on directory
// does not use up server side handle, but it does use up client side
// handle in order to account for opendir()/read/closedir() behavior
// in windows.  QC defect# 14410 has more detail
#define FSI_MAX_STREAMS                  300

// size of NFS handle
#define FSI_PERSISTENT_HANDLE_N_BYTES    32

// *****************************************************************************
// * ENUMERATIONS                                                              *
// *****************************************************************************

// Enumeration of client operation types
// used to identify SAMBA operations
enum {
  ClientOpNoOp = 0,             // not used, start of enum
  ClientOpGetShadowCopyData,    // No
  ClientOpOpen,                 // open_file
  ClientOpCloseFn,              // close_file
  ClientOpVfsRead,              // pread*
  ClientOpPread,                // pread
  ClientOpWrite,                // pwrite*
  ClientOpPwrite,               // pwrite
  ClientOpLseek,                // update internal offset*
  ClientOpFsync,                // sync
  ClientOpFstat,                // get_attributes
  ClientOpGetAllocSize,         // TBD
  ClientOpFchmod,               // TBD
  ClientOpFchown,               // TBD
  ClientOpFtruncate,            // truncate
  ClientOpLock,                 // TBD
  ClientOpKernelFlock,          // No
  ClientOpLinuxSetlease,        // No
  ClientOpGetlock,              // TBD
  ClientOpStreaminfo,           // No
  ClientOpStrictLock,           // No
  ClientOpStrictUnlock,         // No
  ClientOpSendfile,             // No
  ClientOpRecvfile,             // No
  ClientOpFgetNtAcl,            // get_attributes
  ClientOpFsetNtAcl,            // get_attributes
  ClientOpFchmodAcl,            // TBD
  ClientOpSysAclGetFd,          // get_attributes
  ClientOpSysAclSetFd,          // TBD
  ClientOpFgetxattr,            // get_attributes
  ClientOpFlistxattr,           // get_attributes
  ClientOpFremovexattr,         // No
  ClientOpFsetxattr,            // No
  ClientOpAioRead,              // No
  ClientOpAioWrite,             // No
  ClientOpAioReturnFn,          // No
  ClientOpAioCancel,            // No
  ClientOpAioErrorFn,           // No
  ClientOpAioFsync,             // No
  ClientOpAioSuspend,           // No
  ClientOpAioForce,             // No
  ClientOpConnectFn,            // TBD
  ClientOpDisconnect,           // TBD
  ClientOpDiskFree,             // Need some level of support for free disk
                                // space reporting*
  ClientOpGetQuota,             // No
  ClientOpSetQuota,             // No
  ClientOpStatvfs,              // TBD
  ClientOpFsCapabilities,       // TBD
  ClientOpOpendir,              // open_directory
  ClientOpReaddir,              // read_directory
  ClientOpSeekdir,              // TBD
  ClientOpTelldir,              // TBD
  ClientOpRewindDir,            // TBD
  ClientOpMkdir,                // make_directory
  ClientOpRmdir,                // delete_directory
  ClientOpClosedir,             // close_directory
  ClientOpInitSearchOp,         // TBD
  ClientOpCreateFile,           // open_file
  ClientOpRename,               // rename
  ClientOpStat,                 // get_attributes
  ClientOpLstat,                // ?
  ClientOpUnlink,               // delete_file*
  ClientOpChmod,                // set_attributes
  ClientOpChown,                // set_attributes
  ClientOpLchown,               // ?
  ClientOpChdir,                // No
  ClientOpGetwd,                // No
  ClientOpNtimes,               // node times operation
  ClientOpLink,                 // Required - No solution!
  ClientOpMknod,                // Do not expect calls to this - verify w/apps
  ClientOpRealpath,             // Do not expect calls to this - verify w/apps
  ClientOpNotifyWatch,          // Do not expect calls to this - verify w/apps
  ClientOpChflags,              // Do not expect calls to this - verify w/apps
  ClientOpFileIdCreate,         // Do not expect calls to this - verify w/apps
  ClientOpGetRealFilename,      // pt_get_real_filename
  ClientOpConnectpath,          // Do not expect calls to this - verify w/apps
  ClientOpBrlLockWindows,       // Do not expect calls to this - verify w/apps
  ClientOpBrlUnlockWindows,     // Do not expect calls to this - verify w/apps
  ClientOpBrlCancelWindows,     // Do not expect calls to this - verify w/apps
  ClientOpGetNtAcl,             // Do not expect calls to this - verify w/apps
  ClientOpChmodAcl,             // Do not expect calls to this - verify w/apps
  ClientOpSysAclGetEntry,       // Do not expect calls to this - verify w/apps
  ClientOpSysAclGetTagType,     // Do not expect calls to this - verify w/apps
  ClientOpSysAclGetPermset,     // Do not expect calls to this - verify w/apps
  ClientOpSysAclGetQualifier,   // Do not expect calls to this - verify w/apps
  ClientOpSysAclGetFile,        // Do not expect calls to this - verify w/apps
  ClientOpSysAclClearPerms,     // Do not expect calls to this - verify w/apps
  ClientOpSysAclAddPerm,        // Do not expect calls to this - verify w/apps
  ClientOpSysAclToText,         // Do not expect calls to this - verify w/apps
  ClientOpSysAclInit,           // pt_fsi_acl_init
  ClientOpSysAclCreateEntry,    // Do not expect calls to this - verify w/apps
  ClientOpSysAclSetTagType,     // Do not expect calls to this - verify w/apps
  ClientOpSysAclSetQualifier,   // Do not expect calls to this - verify w/apps
  ClientOpSysAclSetPermset,     // Do not expect calls to this - verify w/apps
  ClientOpSysAclValid,          // Do not expect calls to this - verify w/apps
  ClientOpSysAclSetFile,        // Do not expect calls to this - verify w/apps
  ClientOpSysAclDeleteDefFile,  // pt_fsi_acl_delete_def_file
  ClientOpSysAclGetPerm,        // Do not expect calls to this - verify w/apps
  ClientOpSysAclFreeText,       // Do not expect calls to this - verify w/apps
  ClientOpSysAclFreeAcl,        // Do not expect calls to this - verify w/apps
  ClientOpSysAclFreeQualifier,  // Do not expect calls to this - verify w/apps
  ClientOpGetxattr,             // No plan to support xattr - verify w/apps
  ClientOpLgetxattr,            // No plan to support xattr - verify w/apps
  ClientOpListxattr,            // No plan to support xattr - verify w/apps
  ClientOpLlistxattr,           // No plan to support xattr - verify w/apps
  ClientOpRemovexattr,          // No plan to support xattr - verify w/apps
  ClientOpLremovexattr,         // No plan to support xattr - verify w/apps
  ClientOpSetxattr,             // No plan to support xattr - verify w/apps
  ClientOpLsetxattr,            // No plan to support xattr - verify w/apps
  ClientOpIsOffline,            // TBD
  ClientOpSetOffline,           // TBD
  ClientOpHandleToName,
  ClientOpSymLink,              // symlink operations
  ClientOpReadLink,
  ClientOpDynamicFsInfo,
  FsiIpcOpReqShmem,             // request shared mem from server
  FsiIpcOpRelShmem,             // release shared mem from server
  FsiIpcOpShutdown,             // stop messaging
  FsiIpcOpPing,                 // Ping
  FsiIpcOpLog,                  // Log
  ClientOpStatByHandle,         // get attributes by handle
  ClientOpNotUsed               // Not used, end of enum
};

// File IO operation enumeration used to
// determine read/write switching
enum {
  IoOpOther = 0,                // file is neither being read or written
  IoOpWrite,                    // file is being written
  IoOpRead,                     // file is neither being read or written
  IoOpClose,                    // file is being closed
  IoOpFsync                     // file is being synced
};

// FSI Operation return codes
enum {
  FsiRcOk = 0,                  // Ok
  FsiRcPending,                 // Operation Pending
  FsiRcError,                   // Generic error
  FsiRcUnsupported,             // Legal, Not supported
  FsiRcIllegal,                 // Unsupported
  FsiRcMsgError,                // MSG error
  FsiRcNotUsed                  // End of enum
};

enum e_nfs_state {
  NFS_OPEN = 0,
  NFS_CLOSE,
  CCL_CLOSE
};

// *****************************************************************************
// * SHARED MEMORY TYPEDEFS and typedef specific enumerations                  *
// *****************************************************************************

// Shared memory is allocated in approximately 256K buffers
// Each buffer is used for either reads or writes
// Each buffer starts with a buffer header which is
// followed by a series of (data header + data) instances
// If it is used for reads, the buffer structure is
// - CommonShmemBufHdr
// - CommonShmemDataHdr
// - 256K buffer for read data
// If it is use for writes, the buffer structure is
// - CommonShmemBufHdr
// - CommonShmemDataHdr for first write buffer
// - 64K buffer for write data
// - CommonShmemDataHdr for second write buffer
// - 64K buffer for write data
// - CommonShmemDataHdr for third write buffer
// - 64K buffer for write data
// - CommonShmemDataHdr for fourth write buffer
// - 64K buffer for write data

// Shared Memory Buffer Header
struct CommonShmemBufHdr {
  time_t   lastUsedTime;        // updated on all I/O by client
  uint64_t bufferUseEnum;       // how buffer is being used
  uint64_t clientPid;           // loaded by server at allocation
  uint64_t fsHandle;            // file handle
                                // clientPid + fileHandle together
                                // keeps the entry unique
  uint64_t pidIsMissing;        // flag indicating pid is missing
  time_t   pidMissingTime;      // when pid first detected missing
};

// enumeration for bufferUseEnum
enum {
  BUF_USE_NOT_ALLOCATED = 0,       // default state
  BUF_USE_ALLOCATED_NOT_USED,      // set by server when allocated
  BUF_USE_CLIENT_USE_READ,         // set by client when reading
  BUF_USE_CLIENT_USE_WRITE         // set by client when writing
};

// Shared Memory Data Header
struct CommonShmemDataHdr {
  pid_t          clientPid;             // pid of client
  uint64_t       clientFileHandleIndex; // file handle from fuse/samba
  uint64_t       transactionId;         // unique transaction id
  uint64_t       transactionType;       // enumerated transaction type
  uint64_t       requestDataBytes;      // number of bytes of client data
  uint64_t       requestOffset;         // request offset in file
  struct timeval requestTimeval;        // request time
  uint64_t       serverThreadId;        // id of server thread
                                        // (returned by server)
                                        // only used for logging
  uint64_t       transactionResponseId; // should match (returned by server)
  uint64_t       location;              // location in file (returned by server)
  uint64_t       size;                  // file size (returned by server)
  uint64_t       transactionRc;         // return code
  uint64_t       responseDataBytes;     // number of bytes of server data
  struct timeval responseTimeval;       // response time
  uint64_t       dbgCrc;                // debug only - if calculating CRC
                                        // invert 0 CRC. Use != 0 as indicator
                                        // we are using CRC
};

// *****************************************************************************
// * MESSAGING TYPEDEFS                                                        *
// *****************************************************************************

// Common Msg Header
struct CommonMsgHdr {
  uint64_t       msgHeaderLength; // the length of the msg header
  uint64_t       dataLength;      // the length of the msg data
  struct timeval msgTimeval;      // timestamp
  uint64_t       clientPid;       // client pid
  uint64_t       ioMtypeOverride; // io message type override
                                  // FSI always uses 0
  uint64_t       serverThreadId;  // id of server thread
                                  // Client = 0, filled in by server
                                  // used for logging
  uint64_t       transactionId;   // this transaction id
                                  // Client increments and sets
                                  // Server copies from request
  uint64_t       transactionRc;   // return code - filled by server
                                  // client always sends 0
  uint64_t       transactionType; // enumerated type that specifies the
                                  // type of transaction and the message
                                  // structure used in the message
  uint64_t       clientHandle;    // for i/o messages (except open where
                                  // the handle is not yet defined) the
                                  // client handle index.
                                  // set to 0 for non-io messages
  uint64_t       fsHandle;        // for i/o messages (except open where
                                  // the handle is not yet defined) the
                                  // fileHandle
                                  // set to 0 for non-io messages
  uint64_t       exportId;        // File System export ID
  uint64_t       dbgMsgCrc;       // debug only - if calculating CRC
                                  // invert 0 CRC. Use != 0 as indicator
                                  // we are using CRC
  char           client_ip_addr[FSI_IPC_OPEN_IP_ADDR_STR_SIZE];
                                  // client's IP address
};

struct ClientOpLogReqMsg {
  int  logLevel;                   // log severity level
  char text[FSI_IPC_LOG_TEXT_MAX]; // message to log to server
};

struct ClientOpLogReqMtext {
  struct CommonMsgHdr      hdr;   // common msg header
  struct ClientOpLogReqMsg data;  // custome message data
};

// The following typedefs are for specific message content
// Each specific message is defined as a typedef struct Client*Msg
// and followed immediately by the consolidated
// CommonMsgHdr+Client*Msg combined Client*Mtext structure that
// defines the actual message mtext contents

// ClientOpOpen Client Request Message
struct ClientOpOpenReqMsg {
  char     fileName[PATH_MAX]; // filename
  uint64_t fileFlags;          // flags
  uint64_t fileMode;           // mode
  uint64_t uid;                // user ID
  uint64_t gid;                // groupd ID
};

struct ClientOpOpenReqMtext {
  struct CommonMsgHdr       hdr;       // common msg header
  struct ClientOpOpenReqMsg data;      // custom message data
};

// ClientOpOpen Server Response Message
struct ClientOpOpenRspMsg {
  uint64_t fileLocation;        // file location
  uint64_t fileSize;            // file size
  uint64_t resourceHandle;      // resource handle acquired
};

struct ClientOpOpenRspMtext {
  struct CommonMsgHdr       hdr;       // common msg header
  struct ClientOpOpenRspMsg data;      // custom message data
};

// ClientOpClose Client Request Message
struct ClientOpCloseReqMsg {
  uint64_t resourceHandle;      // resource handle to release
  uint64_t responseNotNeeded;   // Specify if we PT server
                                // needs to send the response
                                // 1 = NOT needed, 0 = Needed
                                // A value of 1 is used in close
                                // on terminate in Ganesha
};

struct ClientOpCloseReqMtext {
  struct CommonMsgHdr        hdr;      // common msg header
  struct ClientOpCloseReqMsg data;     // custom message data
};

// ClientOpPread Server Request Message
struct ClientOpPreadReqMsg {
  uint64_t resourceHandle;      // resource handle
  uint64_t preadShmemOffset;    // memory offset for write data
  uint64_t offset;              // offset of write
  uint64_t length;              // number of bytes of data
};

struct ClientOpPreadReqMtext {
  struct CommonMsgHdr        hdr;      // common msg header
  struct ClientOpPreadReqMsg data;     // custom message data
};

// ClientOpPwrite Server Request Message
struct ClientOpPwriteReqMsg {
  uint64_t resourceHandle;      // resource handle
  uint64_t pwriteShmemOffset;   // memory offset for write data
  uint64_t offset;              // offset of write
  uint64_t length;              // number of bytes of data
};

struct ClientOpPwriteReqMtext {
  struct CommonMsgHdr         hdr;     // common msg header
  struct ClientOpPwriteReqMsg data;    // custom message data
};

// ClientOpFtrunc Server Request Message
struct ClientOpFtruncReqMsg {
  uint64_t resourceHandle;      // resource handle
  uint64_t offset;              // offset of ftruncate
};

struct ClientOpFtruncReqMtext {
  struct CommonMsgHdr         hdr;     // common msg header
  struct ClientOpFtruncReqMsg data;    // custom message data
};

// Client Shared Memory Buffer response
// response to FsiIpcOpReqShmem request
struct FsiIpcOpShmemRspMsg {
  uint64_t shmBufferHandles[MAX_FSI_IPC_SHMEM_BUF_PER_STREAM];
  int      numWriteBuf;         // number of write buffers allocated
  uint64_t offsetShmemWrite[MAX_FSI_IPC_SHMEM_BUF_PER_STREAM *
                            FSI_IPC_SHMEM_WRITEBUF_PER_BUF]; // write buf ptrs
  int      numReadBuf;          // number of read buffers allocated
  uint64_t offsetShmemRead[MAX_FSI_IPC_SHMEM_BUF_PER_STREAM *
                           FSI_IPC_SHMEM_READBUF_PER_BUF];   // read buf ptrs
};

struct FsiIpcOpShmemRspMtext {
  struct CommonMsgHdr        hdr;      // common msg header
  struct FsiIpcOpShmemRspMsg data;     // custom message data
};

// Client Shared Memory Buffer response
// response to FsiIpcOpReqShmem request
struct FsiIpcOpShmemRelMsg {
  // buffer handle to release
  uint64_t shmBufferHandles[MAX_FSI_IPC_SHMEM_BUF_PER_STREAM];
};

struct FsiIpcOpShmemRelMtext {
  struct CommonMsgHdr        hdr;      // common msg header
  struct FsiIpcOpShmemRelMsg data;     // custom message data
};

// ClientOpOpenDir Client Request Message
struct ClientOpOpenDirReqMsg {
  char     dirName[PATH_MAX]; // directory name
  uint64_t uid;               // user ID
  uint64_t gid;               // group ID
};

struct ClientOpOpenDirReqMtext {
  struct CommonMsgHdr          hdr;    // common msg header
  struct ClientOpOpenDirReqMsg data;   // custom message data
};

// ClientOpOpenDir Server Response Message
struct ClientOpOpenDirRspMsg {
  uint64_t resourceHandle;      // resource handle acquired
};

struct ClientOpOpenDirRspMtext {
  struct CommonMsgHdr          hdr;    // common msg header
  struct ClientOpOpenDirRspMsg data;   // custom message data
};

// ClientOpCloseDir Client Request Message
struct ClientOpCloseDirReqMsg {
  uint64_t resourceHandle;      // resource handle to release
};

struct ClientOpCloseDirReqMtext {
  struct CommonMsgHdr           hdr;   // common msg header
  struct ClientOpCloseDirReqMsg data;  // custom message data
};

// ClientOpCloseDir Server Response Message
struct ClientOpCloseDirRspMsg {
  uint64_t fsDirHandle;         // handle to close
};

struct ClientOpCloseDirRspMtext {
  struct CommonMsgHdr           hdr;   // common msg header
  struct ClientOpCloseDirRspMsg data;  // custom message data
};

// persistent handle structure
struct PersistentHandle {
  char handle[FSI_PERSISTENT_HANDLE_N_BYTES];
};

// ClientOpStat Server Response Message
struct ClientOpStatRsp {
  uint64_t                device;       // Device
  uint64_t                ino;          // File serial number
  uint64_t                mode;         // File mode
  uint64_t                nlink;        // Link count
  uint64_t                uid;          // User ID of the file's owner
  uint64_t                gid;          // Group ID of the file's group
  uint64_t                rDevice;      // Device number, if device
  uint64_t                size;         // Size of file, in bytes
  struct timespec         atime;        // Time of last access
  struct timespec         mtime;        // Time of last modification
  struct timespec         ctime;        // Time of last change
  struct timespec         btime;        // Birthtime
  uint64_t                blksize;      // Optimal block size for I/O
  uint64_t                blocks;       // Number of 512-byte blocks allocated
  struct PersistentHandle persistentHandle;
};

// ClientOpStat Server Request Message
struct ClientOpStatReqMsg {
  char     path[PATH_MAX];
  uint64_t uid;                 // user ID
  uint64_t gid;                 // group ID
};

struct ClientOpStatReqMtext {
  struct CommonMsgHdr       hdr;       // common msg header
  struct ClientOpStatReqMsg data;      // custom message data
};

// ClientOpStat Server Response Message
struct ClientOpStatRspMsg {
  struct ClientOpStatRsp statInfo;     // Stat info
};

struct ClientOpStatRspMtext {
  struct CommonMsgHdr       hdr;       // common msg header
  struct ClientOpStatRspMsg data;      // custom message data
};

// ClientOpStatByHandle Server Response Message
typedef struct ClientOpStatRsp ClientOpStatByHandleRsp;

// ClientOpStatByHandle Server Request Message
struct ClientOpStatByHandleReqMsg {
  uint64_t                uid;                 // user ID
  uint64_t                gid;                 // group ID
  struct PersistentHandle persistentHandle;
};

struct ClientOpStatByHandleReqMtext {
  struct CommonMsgHdr               hdr;       // common msg header
  struct ClientOpStatByHandleReqMsg data;      // custom message data
};

// ClientOpStatByHandle Server Response Message
struct ClientOpStatByHandleRspMsg {
  ClientOpStatByHandleRsp statInfo;            // Stat info
};

struct ClientOpStatByHandleRspMtext {
  struct CommonMsgHdr               hdr;       // common msg header
  struct ClientOpStatByHandleRspMsg data;      // custom message data
};

// ClientOpReadDir Server Request Message
struct ClientOpReadDirReqMsg {
  uint64_t resourceHandle;      // dir handle
};

struct ClientOpReadDirReqMtext {
  struct CommonMsgHdr          hdr;    // common msg header
  struct ClientOpReadDirReqMsg data;   // custom message data
};

// ClientOpReadDir Server Response Message
struct ClientOpReadDirRspMsg {
  char                   entityName[PATH_MAX];
  uint64_t               entityType;
  uint64_t               entitySize;
  struct ClientOpStatRsp statInfo;
};

struct ClientOpReadDirRspMtext {
  struct CommonMsgHdr          hdr;    // common msg header
  struct ClientOpReadDirRspMsg data;   // custom message data
};

// ClientOpChown Client Request Message
struct ClientOpChownReqMsg {
  char     relPath[PATH_MAX]; // file or dir name
  uint64_t currentUid;        // uid (auth check)
  uint64_t currentGid;        // gid (auth check)
  uint64_t newUid;
  uint64_t newGid;
};

struct ClientOpChownReqMtext {
  struct CommonMsgHdr        hdr;      // common msg header
  struct ClientOpChownReqMsg data;     // custom message data
};

// ClientOpChmod Client Request Message
struct ClientOpChmodReqMsg {
  char     relPath[PATH_MAX]; // file or dir name
  uint64_t mode;              // mode
  uint64_t uid;               // uid (auth chk)
  uint64_t gid;               // gid (auth chk)
};

struct ClientOpChmodReqMtext {
  struct CommonMsgHdr        hdr;      // common msg header
  struct ClientOpChmodReqMsg data;     // custom message data
};

// ClientOpNtimes Client Request Message
struct ClientOpNtimesReqMsg {
  char     relPath[PATH_MAX]; // file name
  uint64_t atime;             // access time
  uint64_t mtime;             // modification time
  uint64_t uid;               // uid (auth check)
  uint64_t gid;               // gid (auth check);
};

struct ClientOpNtimesReqMtext {
  struct CommonMsgHdr         hdr;     // common msg header
  struct ClientOpNtimesReqMsg data;    // custom message data
};

// ClientOpMkdir Client Request Message
struct ClientOpMkdirReqMsg {
  char     relPath[PATH_MAX]; // dir name
  uint64_t newMode;           // mode
  uint64_t newUid;            // uid
  uint64_t newGid;            // gid
};

struct ClientOpMkdirReqMtext {
  struct CommonMsgHdr        hdr;      // common msg header
  struct ClientOpMkdirReqMsg data;     // custom message data
};

// ClientOpUnlink Client Request Message
struct ClientOpUnlinkReqMsg {
  char     relPath[PATH_MAX]; // file name
  uint64_t uid;               // uid (auth chk)
  uint64_t gid;               // gid (auth chk)
};

struct ClientOpUnlinkReqMtext {
  struct CommonMsgHdr         hdr;     // common msg header
  struct ClientOpUnlinkReqMsg data;    // custom message data
};

// ClientOpRmdir Client Request Message
struct ClientOpRmdirReqMsg {
  char     relPath[PATH_MAX]; // dir name
  uint64_t uid;               // uid (auth chk)
  uint64_t gid;               // gid (auth chk)
};

struct ClientOpRmdirReqMtext {
  struct CommonMsgHdr        hdr;      // common msg header
  struct ClientOpRmdirReqMsg data;     // custom message data
};

// ClientOpGetRealFileName Client Request Message
struct ClientOpGetRealFileNameReqMsg {
  char path[PATH_MAX];
  char name[NAME_MAX];
};

struct ClientOpGetRealFileNameReqMtext {
  struct CommonMsgHdr                  hdr;  // common msg header
  struct ClientOpGetRealFileNameReqMsg data; // custom message data
};

// ClientOpGetRealFileName Server Response Message
struct ClientOpGetRealFileNameRspMsg {
  char foundName[NAME_MAX]; // real name found in fsi
};

struct ClientOpGetRealFileNameRspMtext {
  struct CommonMsgHdr                  hdr;  // common msg header
  struct ClientOpGetRealFileNameRspMsg data; // custom message data
};

// ClientOpRename Client Request Message
struct ClientOpRenameReqMsg {
  char     oldRelPath[PATH_MAX]; // old file or dir name
  char     newRelPath[PATH_MAX]; // new file or dir name
  uint64_t uid;                  // uid (auth chk)
  uint64_t gid;                  // gid (auth chk)
};

struct ClientOpRenameReqMtext {
  struct CommonMsgHdr         hdr;     // common msg header
  struct ClientOpRenameReqMsg data;    // custom message data
};

// ClientOpSeekDir Server Request Message
struct ClientOpSeekDirReqMsg {
  uint64_t resourceHandle;      // resource handle
  uint64_t offset;              // offset
};

struct ClientOpSeekDirReqMtext {
  struct CommonMsgHdr          hdr;    // common msg header
  struct ClientOpSeekDirReqMsg data;   // custom message data
};

// ClientOpTellDir Server Request Message
struct ClientOpTellDirReqMsg {
  uint64_t resourceHandle;      // resource handle
};

struct ClientOpTellDirReqMtext {
  struct CommonMsgHdr          hdr;    // common msg header
  struct ClientOpTellDirReqMsg data;   // custom message data
};

// ClientOpTellDir Server Response Message
struct ClientOpTellDirRspMsg {
  uint64_t offset;              // offset
};

struct ClientOpTellDirRspMtext {
  struct CommonMsgHdr          hdr;    // common msg header
  struct ClientOpTellDirRspMsg data;   // custom message data
};

// ClientOpDiskFree Server Request Message
struct ClientOpDiskFreeReqMsg {
  char relPath[PATH_MAX]; // file or dir name
};

struct ClientOpDiskFreeReqMtext {
  struct CommonMsgHdr           hdr;   // common msg header
  struct ClientOpDiskFreeReqMsg data;  // custom message data
};

// ClientOpDiskFree Server Response Message
struct ClientOpDiskFreeRspMsg {
  uint64_t block_size;          // block size
  uint64_t disk_free;           // disk free blocks
  uint64_t disk_size;           // disk size in blocks
};

struct ClientOpDiskFreeRspMtext {
  struct CommonMsgHdr           hdr;   // common msg header
  struct ClientOpDiskFreeRspMsg data;  // custom message data
};

// ClientOpSysAclGetFile Server Request Message

struct ClientOpSysAclGetFileReqMsg {
  uint64_t aclType; // SMB_ACL_TYPE_ACCESS or SMB_ACL_TYPE_DEFAULT
  uint64_t uid;     // User ID for checking permission
  uint64_t gid;     // Group ID for checking permission
  char     relPath[PATH_MAX]; // file or dir name
};

struct ClientOpSysAclGetFileReqMtext {
  struct CommonMsgHdr                hdr;  // common msg header
  struct ClientOpSysAclGetFileReqMsg data; // custom message data
};

// ClientOpSysAclGetFile Server Response Message
struct ClientOpSysAclGetFileRspMsg {
  uint64_t resourceHandle;     // resource handle acquired
};

struct ClientOpSysAclGetFileRspMtext {
  struct CommonMsgHdr                hdr;  // common msg header
  struct ClientOpSysAclGetFileRspMsg data; // custom message data
};

// ClientOpSysAclFreeAcl Server Request Message
struct ClientOpSysAclFreeAclReqMsg {
  uint64_t resourceHandle;          // resource handle to release
};

struct ClientOpSysAclFreeAclReqMtext {
  struct CommonMsgHdr                 hdr; // common msg header
  struct ClientOpSysAclFreeAclReqMsg data; // custom message data
};

// ClientOpSysAclGetEntry Server Request Message
struct ClientOpSysAclGetEntryReqMsg {
  uint64_t aclHandle; // SMB_ACL_T handle
  uint64_t entryId;   // ACL_FIRST_ENTRY or ACL_NEXT_ENTRY
};

struct ClientOpSysAclGetEntryReqMtext {
  struct CommonMsgHdr                 hdr;  // common msg header
  struct ClientOpSysAclGetEntryReqMsg data; // custom message data
};

// ClientOpSysAclGetEntry Server Response Message
struct ClientOpSysAclGetEntryRspMsg {
  uint64_t aclEntryHandle; // ACL_ENTRY_T handle
};

struct ClientOpSysAclGetEntryRspMtext {
  struct CommonMsgHdr                 hdr;  // common msg header
  struct ClientOpSysAclGetEntryRspMsg data; // custom message data
};

// ClientOpSysAclGetPermset Server Request Message
struct ClientOpSysAclGetPermsetReqMsg {
  uint64_t aclEntryHandle; // ACL_Entry_T handle
};

struct ClientOpSysAclGetPermsetReqMtext {
  struct CommonMsgHdr                   hdr;  // common msg header
  struct ClientOpSysAclGetPermsetReqMsg data; // custom message data
};

// ClientOpSysAclGetPermset Server Response Message
struct ClientOpSysAclGetPermsetRspMsg {
  uint64_t permsetHandle;
};

struct ClientOpSysAclGetPermsetRspMtext {
  struct CommonMsgHdr                   hdr;  // common msg header
  struct ClientOpSysAclGetPermsetRspMsg data; // custom message data
};

// ClientOpSysAclGetPerm Server Request Message
struct ClientOpSysAclGetPermReqMsg {
  uint64_t permsetHandle; // ACL_PERMSET_T handle from ClientOpSysAclGetPerm
  uint64_t permToCheck;
};

struct ClientOpSysAclGetPermReqMtext {
  struct CommonMsgHdr                hdr;  // common msg header
  struct ClientOpSysAclGetPermReqMsg data; // custom message data
};

// ClientOpSysAclGetPerm Server Response Message
struct ClientOpSysAclGetPermRspMsg {
  uint64_t isPermInSet;
};

struct ClientOpSysAclGetPermRspMtext {
  struct CommonMsgHdr                hdr;  // common msg header
  struct ClientOpSysAclGetPermRspMsg data; // custom message data
};

// ClientOpSysAclGetTagType Server Request Message
struct ClientOpSysAclGetTagTypeReqMsg {
  uint64_t aclEntryHandle; // ACL_Entry_T handle
};

struct ClientOpSysAclGetTagTypeReqMtext {
  struct CommonMsgHdr                   hdr;  // common msg header
  struct ClientOpSysAclGetTagTypeReqMsg data; // custom message data
};

// ClientOpSysAclGetTagType Server Response Message
struct ClientOpSysAclGetTagTypeRspMsg {
  uint64_t aclTagType;
};

struct ClientOpSysAclGetTagTypeRspMtext {
  struct CommonMsgHdr                   hdr;  // common msg header
  struct ClientOpSysAclGetTagTypeRspMsg data; // custom message data
};

// ClientOpSysAclGetQualifier Server Request Message
struct ClientOpSysAclGetQualifierReqMsg {
  uint64_t aclEntryHandle; // ACL_Entry_T handle
};

struct ClientOpSysAclGetQualifierReqMtext {
  struct CommonMsgHdr                     hdr;  // common msg header
  struct ClientOpSysAclGetQualifierReqMsg data; // custom message data
};

// ClientOpSysAclGetQualifier Server Response Message
struct ClientOpSysAclGetQualifierRspMsg {
  uint64_t aclUorGid;
};

struct ClientOpSysAclGetQualifierRspMtext {
  struct CommonMsgHdr                     hdr;  // common msg header
  struct ClientOpSysAclGetQualifierRspMsg data; // custom message data
};

// ACL Set Operations

// ClientOpSysAclInit Server Request Message
struct ClientOpSysAclInitReqMsg {
  uint64_t aclCount; // Number of initial entries
};

struct ClientOpSysAclInitReqMtext {
  struct CommonMsgHdr             hdr;  // common msg header
  struct ClientOpSysAclInitReqMsg data; // custom message data
};

// ClientOpSysAclInit Server Response Message
struct  ClientOpSysAclInitRspMsg {
  uint64_t resourceHandle;
};

struct ClientOpSysAclInitRspMtext {
  struct CommonMsgHdr             hdr;  // common msg header
  struct ClientOpSysAclInitRspMsg data; // custom message data
};

// ClientOpSysAclSetFile Server Request Message
struct ClientOpSysAclSetFileReqMsg {
  uint64_t uid;     // User ID for checking permission
  uint64_t gid;     // Group ID for checking permission
  uint64_t aclType;
  uint64_t resourceHandle;
  char     relPath[PATH_MAX]; // file or dir name
};

struct ClientOpSysAclSetFileReqMtext {
  struct CommonMsgHdr                hdr;  // common msg header
  struct ClientOpSysAclSetFileReqMsg data; // custom message data
};

// ClientOpSysAclCreateEntry Server Request Message
struct ClientOpSysAclCreateEntryReqMsg {
  uint64_t resourceHandle;
};

struct ClientOpSysAclCreateEntryReqMtext {
  struct CommonMsgHdr                    hdr;  // common msg header
  struct ClientOpSysAclCreateEntryReqMsg data; // custom message data
};

// ClientOpSysAclCreateEntry Server Response Message
struct  ClientOpSysAclCreateEntryRspMsg {
  uint64_t aclEntry;
};

struct ClientOpSysAclCreateEntryRspMtext {
  struct CommonMsgHdr                    hdr;
  struct ClientOpSysAclCreateEntryRspMsg data;
};

// ClientOpSysAclSetTagType Server Request Message
struct ClientOpSysAclSetTagTypeReqMsg {
  uint64_t aclEntryHandle;
  uint64_t aclTagType;
};

struct ClientOpSysAclSetTagTypeReqMtext {
  struct CommonMsgHdr                   hdr;  // common msg header
  struct ClientOpSysAclSetTagTypeReqMsg data; // custom message data
};

// ClientOpSysAclSetQualifier Server Request Message
struct ClientOpSysAclSetQualifierReqMsg {
  uint64_t aclEntryHandle;
  uint64_t aclQualifier;
};

struct ClientOpSysAclSetQualifierReqMtext {
  struct CommonMsgHdr                     hdr;  // common msg header
  struct ClientOpSysAclSetQualifierReqMsg data; // custom message data
};

// ClientOpSysAclSetPermset Server Request Message
struct ClientOpSysAclSetPermsetReqMsg {
  uint64_t aclEntryHandle;
  uint64_t aclPermset;
};

struct ClientOpSysAclSetPermsetReqMtext {
  struct CommonMsgHdr                   hdr;  // common msg header
  struct ClientOpSysAclSetPermsetReqMsg data; // custom message data
};

// ClientOpSysAclClearPerms Server Request Message
struct ClientOpSysAclClearPermsReqMsg {
  uint64_t aclPermset;
};

struct ClientOpSysAclClearPermsReqMtext {
  struct CommonMsgHdr                   hdr;  // common msg header
  struct ClientOpSysAclClearPermsReqMsg data; // custom message data
};

// ClientOpSysAclAddPerm Server Request Message
struct ClientOpSysAclAddPermReqMsg {
  uint64_t aclPermset;
  uint64_t aclPerm;
};

struct ClientOpSysAclAddPermReqMtext {
  struct CommonMsgHdr                hdr;  // common msg header
  struct ClientOpSysAclAddPermReqMsg data; // custom message data
};

// ClientOpSysAclDeletePerm Server Request Message
struct ClientOpSysAclDeletePermReqMsg {
  uint64_t aclPermset;
  uint64_t aclPerm;
};

struct ClientOpSysAclDeletePermReqMtext {
  struct CommonMsgHdr                   hdr;  // common msg header
  struct ClientOpSysAclDeletePermReqMsg data; // custom message data
};

// ClientOpSysAclDeleteDefFile Server Request Message
struct ClientOpSysAclDeleteDefFileReqMsg {
  char     path[PATH_MAX];
  uint64_t uid;
  uint64_t gid;
};

struct ClientOpSysAclDeleteDefFileReqMtext {
  struct CommonMsgHdr                      hdr;  // common msg header
  struct ClientOpSysAclDeleteDefFileReqMsg data; // custom message data
};

// ClientOpHandleToName Server Request Message
struct ClientOpHandleToNameReqMsg {
  struct PersistentHandle persistentHandle; // persistent handle
};

struct ClientOpHandleToNameReqMtext {
  struct CommonMsgHdr               hdr;
  struct ClientOpHandleToNameReqMsg data;
};

// ClientOpHandleToName Server Response Message
struct ClientOpHandleToNameRspMsg {
  char                   path[PATH_MAX];
  struct ClientOpStatRsp statInfo;
};

struct ClientOpHandleToNameRspMtext {
  struct CommonMsgHdr               hdr;
  struct ClientOpHandleToNameRspMsg data;
};

// ClientOpDynamicFsInfo Server Request Message
struct ClientOpDynamicFsInfoReqMsg {
  char     path[PATH_MAX];
};

struct ClientOpDynamicFsInfoReqMtext {
  struct CommonMsgHdr                hdr;
  struct ClientOpDynamicFsInfoReqMsg data;
};

// ClientOpDynamicFsInfo Server Response Message
struct ClientOpDynamicFsInfoRspMsg {
  uint64_t        totalBytes;
  uint64_t        freeBytes;
  uint64_t        availableBytes;
  uint64_t        totalFiles;
  uint64_t        freeFiles;
  uint64_t        availableFiles;
  struct timespec time;
};

struct ClientOpDynamicFsInfoRspMtext {
  struct CommonMsgHdr                hdr;
  struct ClientOpDynamicFsInfoRspMsg data;
};

// ClientOpSymlink Server Request Message
struct ClientOpSymLinkReqMsg {
  char     path[PATH_MAX];
  uint64_t uid;
  uint64_t gid;
  char     linkContent[PATH_MAX];
};

struct ClientOpSymLinkReqMtext {
  struct CommonMsgHdr          hdr;
  struct ClientOpSymLinkReqMsg data;
};

// ClientOpReadLink Server Request Message
struct ClientOpReadLinkReqMsg {
  char     path[PATH_MAX];
  uint64_t uid;
  uint64_t gid;
};

struct ClientOpReadLinkReqMtext {
  struct CommonMsgHdr           hdr;
  struct ClientOpReadLinkReqMsg data;
};

// ClientOpReadLink Server Response Message
struct ClientOpReadLinkRspMsg {
  char     linkContent[PATH_MAX];
};

struct ClientOpReadLinkRspMtext {
  struct CommonMsgHdr           hdr;
  struct ClientOpReadLinkRspMsg data;
};

// ---------------------------------------------------------------------------
// Shared Memory buffer layout structures
// ---------------------------------------------------------------------------
struct shmbuf_read_layout_t {
  struct CommonShmemDataHdr dhdr;
  char                      read_buffer[FSI_IPC_SHMEM_READBUF_SIZE];
};

struct shmbuf_write_layout_t {
  struct CommonShmemDataHdr dhdr;
  char                      write_buffer[FSI_IPC_SHMEM_WRITEBUF_SIZE];
};

struct shmbuf_layout_t {
  struct CommonShmemBufHdr       hdr;
  char                           pad1[FSI_IPC_PAD_SIZE -
                                      sizeof(struct CommonShmemBufHdr)];
  union {
    struct shmbuf_read_layout_t  readbuf[FSI_IPC_SHMEM_READBUF_PER_BUF];
    struct shmbuf_write_layout_t writebuf[FSI_IPC_SHMEM_WRITEBUF_PER_BUF];
  } buffers;

  char                           endPad[FSI_IPC_PAD_SIZE];
};

// ---------------------------------------------------------------------------
// IPC Message definitions
// ---------------------------------------------------------------------------

// TODO - Find a better way to size this depending on the biggest request or
// response message among all messages. For now, we set the size to symlink
// which is the current the biggest message.
#define FSI_IPC_MSG_SIZE sizeof(struct ClientOpSymLinkReqMtext)

// generic message struct
struct msg_t {
  long int mtype;
  char     mtext[FSI_IPC_MSG_SIZE];
};

#endif // ifndef __FSI_IPC_COMMON_H__

