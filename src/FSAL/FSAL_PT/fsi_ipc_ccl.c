// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2010, 2011
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsi_ipc_client.c
// Description: Samba VFS library - client non-I/O functions
// Author:      FSI IPC Team
// ----------------------------------------------------------------------------

#include "fsal_types.h"
#include "fsi_ipc_ccl.h"

int                          g_shm_id;             // SHM ID
char                       * g_shm_at = 0;         // SHM Base Address
int                          g_io_req_msgq;
int                          g_io_rsp_msgq;
int                          g_non_io_req_msgq;
int                          g_non_io_rsp_msgq;
int                          g_shmem_req_msgq;
int                          g_shmem_rsp_msgq;
struct file_handles_struct_t g_fsi_handles;        // FSI client handles
struct dir_handles_struct_t  g_fsi_dir_handles;    // FSI client Dir handles
struct acl_handles_struct_t  g_fsi_acl_handles;    // FSI client ACL handles
uint64_t                     g_client_pid;         // FSI client pid
uint64_t                     g_server_pid;         // Server pid
uint64_t                     g_client_trans_id = 0;// FSI global transaction id
char                         g_chdir_dirpath[PATH_MAX];
                                                   // global chmod path
char                         g_client_address[256];
                                                   // string vers of client IP
int                          g_close_trace = 0;

struct    fsi_handle_cache_t    g_fsi_name_handle_cache;

// BRUTAL PTFSAL HACK - this is where all the FSI functions are stuffed for now

// ----------------------------------------------------------------------------
// This is needed to make FSI_TRACE macro work
// Part of the magic of __attribute__ is this function needs to be defined as
// well as declared with the extern - though it's a noop
inline void
compile_time_check_func(const char * fmt, ...)
{
  // do nothing
}


// HELPER FUNCTIONS

int fsi_cache_name_and_handle(char *handle, char *name)
{
  int rc;

  rc = fsi_get_name_from_handle(handle, name);

  if (rc < 0) {
    memset(&g_fsi_name_handle_cache.m_entry[g_fsi_name_handle_cache.m_count].m_handle, 0, FSI_HANDLE_SIZE);
    memcpy(&g_fsi_name_handle_cache.m_entry[g_fsi_name_handle_cache.m_count].m_handle,&handle[0],FSI_HANDLE_SIZE);  // FIX FIX FIX make this right size
    strncpy(g_fsi_name_handle_cache.m_entry[g_fsi_name_handle_cache.m_count].m_name,  name,  PATH_MAX);
    FSI_TRACE(FSI_DEBUG, "FSI - added %s to name cache entry %d\n",name,g_fsi_name_handle_cache.m_count);
    g_fsi_name_handle_cache.m_count++;
  }

  return 0;
}

int fsi_get_name_from_handle(char *handle, char *name)
{
  int index;

  for (index = 0; index < g_fsi_name_handle_cache.m_count; index++) {

    if (memcmp(&handle[0], &g_fsi_name_handle_cache.m_entry[index].m_handle, FSI_HANDLE_SIZE) == 0) {
      strncpy(name, g_fsi_name_handle_cache.m_entry[index].m_name, PATH_MAX);
      return 0;
    }
  }

  return -1;

}

int fsi_check_handle_index (int handle_index)
{

    // check handle
    if ((handle_index >=                 0) &&
        (handle_index <  (FSI_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS))) {
      return 0;
    } else {
      return -1;
    }

}

uint64_t get_export_id()
{
// FIX FIX FIX - HARDCODED!!!!!
//  const char * param =
//    (handle->param ? handle->param : SAMBA_FSI_IPC_PARAM_NAME);
//
//  unsigned long eid = lp_parm_ulong(SNUM((handle)->conn), param,
//                                    SAMBA_EXPORT_ID_PARAM_NAME, 0); // 0 - default value
//  if (debug_flag >= 1) printf ("FSI -  %s parameter ExportID %ld \n", param, eid);
//  return eid;
return FUSE_EXPORT_ID;
}

// load UID/GID from handle
void ld_uid_gid(uint64_t                * uid,
                uint64_t                * gid,
                fsi_handle_struct   * handle)
{
//  if (handle == NULL) {
//    if (debug_flag >= 1) printf ("FSI - ld_uid_gid got NULL handle");
//  }

//  if (current_user.ut.uid == -1) {
//    * uid = geteuid();
//  } else {
//    * uid = current_user.ut.uid;
//  }
//
//  if (current_user.ut.gid == -1) {
//    * gid = getegid();
//  } else {
//    * gid = current_user.ut.gid;
//  }

// PTFSAL HACK
// FIX FIX FIX - hard coded for fsi75
* uid = 0;
* gid = 0;


}

int io_msgid_from_index (int index)
{
  int io_msgid;

  io_msgid = FSI_IPC_FUSE_MSGID_BASE + index;
  return io_msgid;

}

// convert incoming directory name to something the server
// can use
void convert_fsi_name(fsi_handle_struct   * handle,
                      const char          * filename,
                      char                * sv_filename,
                      enum e_fsi_name_enum  fsi_name_type)
{
  char       * p_parsename;
  int          parsename_len;
  const char * p_export_path;

// PTFSAL HACK
//  p_export_path = get_export_path(handle);
  // start at beginning of filename
  p_parsename = (char *)filename;

  // remove the /share_name from the converted path since
  // fsi_name should be wihtout leading export path
  if (strnlen(p_parsename, 1) > 0 && p_parsename[0] == '/')  {
    if (strstr(p_parsename, p_export_path) == p_parsename + 1) {
      FSI_TRACE(FSI_DEBUG, "removing leading export path [/%s] from [%s]",
                p_export_path, p_parsename);
      p_parsename += 1 + strnlen(p_export_path, PATH_MAX);
    }
  }
  // if this is windows the incoming file may have
  // ./ at its beginning - strip this because the
  // server will reject the name
  if (strncmp(p_parsename, "./", 2) == 0) {
    p_parsename += 2;
  }

  // strip leading "." from name
  while ((strnlen(p_parsename, 1) > 0) &&
         (p_parsename[0] == '.')) {
    p_parsename++;
  }

  // get the length of p_parsename
  parsename_len = strnlen(p_parsename, PATH_MAX);

  // strip any leading / from the name
  if (parsename_len >= 1) {
    if (p_parsename[0] == '/') {
      p_parsename++;
    }
  }

  // p_parsename is not null - copy it to
  // sv_filename
  if (parsename_len >= PATH_MAX) {
    // parsed name too long - log and chop
    // this should not occur, we've checked already...
    if (parsename_len >= PATH_MAX) {
      // directory name is too long - not supported
      FSI_TRACE(FSI_ERR, "parsed dir len %d name = [80%s] too long",
                parsename_len, p_parsename);
      parsename_len = PATH_MAX - 1;
    }
  }

  // Ignore the rc since the size is already checked
  snprintf(sv_filename, PATH_MAX, "%s", p_parsename);

  return;
}

// generic msgsnd function
int send_msg(int          msg_id,
             const void * p_msg_buf,
             size_t       msg_size,
             int        * p_msg_error_code)
{
  int msg_snd_rc;

  // Assume no FSI error
  *p_msg_error_code = FSI_IPC_EOK;

  // send message
  msg_snd_rc = msgsnd(msg_id, p_msg_buf, msg_size, IPC_NOWAIT);

  // handle msgsnd failure scenarios
  if (msg_snd_rc < 0) {
    if (errno == EAGAIN) {
      FSI_TRACE(FSI_NOTICE, "Message queue is full, performing blocking send");
      msg_snd_rc = msgsnd(msg_id, p_msg_buf, msg_size, 0);

      if (msg_snd_rc < 0) {
        FSI_TRACE(FSI_ERR, "sending msg on Q %d size %ld msg_snd_rc %d "
                  "errno %d", msg_id, msg_size, msg_snd_rc, errno);
      }
    } else {
      FSI_TRACE(FSI_ERR, "sending msg on Q %d size %ld msg_snd_rc %d"
                "errno %d", msg_id, msg_size, msg_snd_rc, errno);
      *p_msg_error_code = ECOMM;
    }
  }

  return msg_snd_rc;
}

// generic msgrcv (blocking)
int rcv_msg_wait(int     msg_id,
                 void  * p_msg_buf,
                 size_t  msg_size,
                 long    msg_type,
                 int   * p_msg_error_code)
{
  int msg_rcv_rc;

  // Assume no FSI error
  *p_msg_error_code = FSI_IPC_EOK;

  msg_rcv_rc = msgrcv(msg_id, p_msg_buf, msg_size, msg_type, 0);

  // try continually if interrupts are occurring
  while (msg_rcv_rc < 0 && errno == EINTR) {
    msg_rcv_rc = msgrcv(msg_id, p_msg_buf, msg_size, msg_type, 0);
  }

  // handle failure scenarios
  if (msg_rcv_rc < 0 && errno != EINTR) {
    FSI_TRACE(FSI_ERR, "rcving msg on Q %d type %ld msg_rcv_rc %d errno %d",
              msg_id, msg_type, msg_rcv_rc, errno);
    *p_msg_error_code = ECOMM;

    // if queue was deleted then this is error recovery scenario where server
    // has gone down and sibling cleaning up, exit immediately
    if (errno == EIDRM) {
      FSI_TRACE(FSI_NOTICE, "message queue has been deleted, exiting");
      exit(0);
    }
  }

  return msg_rcv_rc;
}

// generic msgrcv (non-blocking)
int rcv_msg_nowait(int     msg_id,
                   void  * p_msg_buf,
                   size_t  msg_size,
                   long    msg_type,
                   int   * p_msg_error_code)
{
  int msg_rcv_rc;

  // Assume no FSI error
  *p_msg_error_code = FSI_IPC_EOK;

  msg_rcv_rc = msgrcv(msg_id, p_msg_buf, msg_size, msg_type, IPC_NOWAIT);

  return msg_rcv_rc;
}


// add handle to global client handle structure
int add_fsi_handle(struct file_handle_t * p_new_handle)
{
  int index;

  // PTFSAL
  pthread_mutex_lock(&g_handle_mutex);

  // flag current handle as in use then scan existing handles for one that is
  // not in use
  p_new_handle->m_hndl_in_use = 1;

  for (index = 4; index < g_fsi_handles.m_count; index++) {
    if (g_fsi_handles.m_handle[index].m_hndl_in_use == 0) {
      // this is an empty entry - use it
      memcpy(&g_fsi_handles.m_handle[index], p_new_handle,
             sizeof(struct file_handle_t));
      // PTFSAL
      pthread_mutex_unlock(&g_handle_mutex);
      // return index to caller
      return index;
    }
  }

  // no empty entries - extend list
  index = g_fsi_handles.m_count;

  if (index < (FSI_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS)) {
    memcpy(&g_fsi_handles.m_handle[g_fsi_handles.m_count], p_new_handle,
           sizeof(struct file_handle_t));
    g_fsi_handles.m_count++;
  } else {
    FSI_TRACE(FSI_FATAL, "Too many file/dir handles open");
    index = -1;
  }
  // PTFSAL
  pthread_mutex_unlock(&g_handle_mutex);

  return index;
}

// demark a handle in the global client handle structure
int delete_fsi_handle(int handle_index)
{
  int delete_handle_rc = FSI_IPC_EOK;

  // PTFSAL
  pthread_mutex_lock(&g_handle_mutex);

  // Mark entry as not in use
  g_fsi_handles.m_handle[handle_index].m_hndl_in_use = 0;

  // PTFSAL
  pthread_mutex_unlock(&g_handle_mutex);

  // return
  return delete_handle_rc;
}

// add dir handle to global client dir handle structure
int add_dir_handle(uint64_t fs_dir_handle)
{
  int index;

  // PTFSAL
  pthread_mutex_lock(&g_dir_mutex);

  // flag current dir handle as in use then scan existing dir handles for one
  // that is
  // not in use

  for (index = 0; index < g_fsi_dir_handles.m_count; index++) {
    if (g_fsi_dir_handles.m_dir_handle[index].m_dir_handle_in_use == 0) {
      // this is an empty entry - use it
      // dirHandle is in use
      g_fsi_dir_handles.m_dir_handle[index].m_dir_handle_in_use
        = 1;
      // store server handle
      g_fsi_dir_handles.m_dir_handle[index].m_fs_dir_handle
        = fs_dir_handle;
      // store server handle
      g_fsi_dir_handles.m_dir_handle[index].m_resourceHandle
        = fs_dir_handle;
      // store this entry's handle (index) in smb struct
      g_fsi_dir_handles.m_dir_handle[index].m_fsi_struct_dir.m_dir_handle_index
        = index;
      // PTFSAL
      pthread_mutex_unlock(&g_dir_mutex);
      // return index to caller
      return index;
    }
  }

  // no empty entries - extend list
  index = g_fsi_dir_handles.m_count;

  if (index < (FSI_MAX_STREAMS)) {
    // dirHandle is in use
    g_fsi_dir_handles.m_dir_handle[index].m_dir_handle_in_use =
      1;
    // store server handle
    g_fsi_dir_handles.m_dir_handle[index].m_resourceHandle =
      fs_dir_handle;
    // store this entry's handle (index) in smb struct
    g_fsi_dir_handles.m_dir_handle[index].m_fsi_struct_dir.m_dir_handle_index =
      index;
    g_fsi_dir_handles.m_count++;
  } else {
    FSI_TRACE(FSI_FATAL, "Too many file/dir handles open");
    index = -1;
  }
  // PTFSAL
  pthread_mutex_unlock(&g_dir_mutex);

  return index;
}

// demark a dir handle in the global client dir handle structure
int delete_dir_handle(int dir_handle_index)
{
  int delete_dir_handle_rc = FSI_IPC_EOK;

  // PTFSAL
  pthread_mutex_lock(&g_dir_mutex);

  // Mark entry as not in use
  g_fsi_dir_handles.m_dir_handle[dir_handle_index].m_dir_handle_in_use = 0;

  // PTFSAL
  pthread_mutex_unlock(&g_dir_mutex);

  // return
  return delete_dir_handle_rc;
}

// add acl handle to global client acl handle structure
int add_acl_handle(uint64_t fs_acl_handle)
{
  int index;

  // PTFSAL
  pthread_mutex_lock(&g_acl_mutex);

  // flag current acl handle as in use then scan existing dir handles for one
  // that is
  // not in use
  for (index = 0; index < g_fsi_acl_handles.m_count; index++) {
    if (g_fsi_acl_handles.m_acl_handle[index].m_acl_handle_in_use == 0) {

      // this is an empty entry - use it
      // aclHandle is in use
      g_fsi_acl_handles.m_acl_handle[index].m_acl_handle_in_use = 1;

      // store server handle
      g_fsi_acl_handles.m_acl_handle[index].m_acl_handle = fs_acl_handle;

      // PTFSAL
      pthread_mutex_unlock(&g_acl_mutex);

      // return index to caller
      FSI_TRACE(FSI_INFO, "using index %i in acl handle array", index);
      return index;
    }
  }

  // no empty entries - extend list
  index = g_fsi_acl_handles.m_count;

  if (index < (FSI_MAX_STREAMS)) {

    // aclHandle is in use
    g_fsi_acl_handles.m_acl_handle[index].m_acl_handle_in_use = 1;

    // store server handle
    g_fsi_acl_handles.m_acl_handle[index].m_acl_handle = fs_acl_handle;

    g_fsi_acl_handles.m_count++;
  } else {
    FSI_TRACE(FSI_ERR, "Too many file/dir handles open");
    index = -1;
  }

  // PTFSAL
  pthread_mutex_unlock(&g_acl_mutex);

  FSI_TRACE(FSI_INFO, "using index %i in acl handle array", index);
  return index;
}

// delete an acl handle in the global client acl handle structure
int delete_acl_handle(uint64_t aclHandle)
{
  int found = -1;

  // PTFSAL
  pthread_mutex_lock(&g_acl_mutex);

  // TODO need to have a mechanism for deleting handles from the array
  //      that isn't O(n) complexity
  int i;
  for (i = 0; i < g_fsi_acl_handles.m_count; ++i) {
    if (g_fsi_acl_handles.m_acl_handle[i].m_acl_handle == aclHandle) {
      // Mark entry as not in use
      g_fsi_acl_handles.m_acl_handle[i].m_acl_handle_in_use = 0;
      found = 0;
      break;
    }
  }

  // PTFSAL
  pthread_mutex_unlock(&g_acl_mutex);

  return found;
}

// retrieve the resourceHandle associated with this ACL
uint64_t get_acl_resource_handle(uint64_t aclHandle)
{
  uint64_t found = 0;

  // PTFSAL
  pthread_mutex_lock(&g_acl_mutex);

  int i;
  for (i = 0; i < g_fsi_acl_handles.m_count; ++i) {
    if (g_fsi_acl_handles.m_acl_handle[i].m_acl_handle == aclHandle) {
      found = g_fsi_acl_handles.m_acl_handle[i].m_acl_handle;
      break;
    }
  }

  // PTFSAL
  pthread_mutex_unlock(&g_acl_mutex);

  return found;
}

// load a common message header
void ld_common_msghdr(CommonMsgHdr * p_msg_hdr,
                      uint64_t       transaction_type,
                      uint64_t       data_length,
                      uint64_t       export_id,
                      int            handle_index,
                      int            fs_handle,
                      int            use_crc)
{
  // load fixed header length
  p_msg_hdr->msgHeaderLength = sizeof(CommonMsgHdr);

  // length of data portion of message
  p_msg_hdr->dataLength = data_length;

  // current time in microsec
  gettimeofday(&p_msg_hdr->msgTimeval, NULL);

  // load client pid from global
  p_msg_hdr->clientPid = g_client_pid;

  // use global transaction id and increment it
  // PTFSAL MUTEX FIX
  pthread_mutex_lock(&g_transid_mutex);
  p_msg_hdr->transactionId = g_client_trans_id;
  g_client_trans_id++;
  pthread_mutex_unlock(&g_transid_mutex);

  // zero msg type override
  p_msg_hdr->ioMtypeOverride = 0;

  // zero out return code
  p_msg_hdr->transactionRc = 0;

  // load transaction type
  p_msg_hdr->transactionType = transaction_type;

  // load client handle
  p_msg_hdr->clientHandle = handle_index;

  // load fs_handle
  p_msg_hdr->fsHandle = fs_handle;

  //// load exportId
  p_msg_hdr->exportId = export_id;

  if (use_crc != 0) {
    // don't use CRC
    p_msg_hdr->dbgMsgCrc        = 0;
  } else {
    p_msg_hdr->dbgMsgCrc        = 0;
  }
}

void load_shmem_hdr(CommonShmemDataHdr * p_shmem_hdr,
                    uint64_t             transaction_type,
                    uint64_t             data_length,
                    uint64_t             offset,
                    int                  handle_index,
                    uint64_t             transaction_id,
                    int                  use_crc)
{
  // zero header
  memset(p_shmem_hdr, 0, sizeof(CommonShmemDataHdr));

  p_shmem_hdr->clientPid             = g_client_pid;
  p_shmem_hdr->clientFileHandleIndex = handle_index;
  p_shmem_hdr->transactionType       = transaction_type;
  p_shmem_hdr->transactionId         = transaction_id;
  p_shmem_hdr->requestDataBytes      = data_length;
  p_shmem_hdr->requestOffset         = offset;
  gettimeofday(&p_shmem_hdr->requestTimeval, NULL);
  // Server loaded data - client loads 0
  p_shmem_hdr->transactionResponseId = 0;
  p_shmem_hdr->location              = 0;
  p_shmem_hdr->size = 0;
  p_shmem_hdr->transactionRc         = 0;
  p_shmem_hdr->responseDataBytes     = 0;
  // debug CRC
  if (use_crc != 0) {
    // don't use CRC
    p_shmem_hdr->dbgCrc           = 0;
  } else {
    p_shmem_hdr->dbgCrc           = 0;
  }
}

// determine if there are outstanding io messages
// return 1 if there are outstanding messages
// return 0 if there are none
int have_pending_io_response(int handle_index)
{
  int pending_rc = 0;
  int index;

  if (g_fsi_handles.m_handle[handle_index].m_prev_io_op == IoOpRead) {
    // we are reading - check for outstanding read messages
    for (index = 0;
         index < g_fsi_handles.m_handle[handle_index].m_readbuf_cnt;
         index++) {
      if (g_fsi_handles.m_handle[handle_index].m_readbuf_state[index].
          m_buf_rc_state ==
          BUF_RC_STATE_PENDING) {
        pending_rc = 1;
      }
    }
  }

  if (g_fsi_handles.m_handle[handle_index].m_prev_io_op == IoOpWrite) {
    // we are writing - check for outstanding write messages
    for (index = 0;
         index < g_fsi_handles.m_handle[handle_index].m_writebuf_cnt;
         index++) {
      if (g_fsi_handles.m_handle[handle_index].m_writebuf_state[index].
          m_buf_rc_state ==
          BUF_RC_STATE_PENDING) {
        pending_rc = 1;
      }
    }
  }

  return pending_rc;
}

// This is function to look up fsi file handler by file name
int fsi_find_handle_by_name(const char * filename)
{
  int fsihandle = -1;
  int index;

  // PTFSAL
  pthread_mutex_lock(&g_handle_mutex);

  for (index = FSI_CIFS_RESERVED_STREAMS;
       index < g_fsi_handles.m_count;
       index++) {
    if (g_fsi_handles.m_handle[index].m_hndl_in_use != 0) {
      // this is a valid entry
      const char * tempname = g_fsi_handles.m_handle[index].m_filename;
      FSI_TRACE(FSI_DEBUG, "index=%d, filename=%s, cachefilename=%s",
                index, filename, tempname);
      if (strncmp(filename,
                  tempname,
                  sizeof(g_fsi_handles.m_handle[index].m_filename)) == 0) {
        // this is the file
        fsihandle = index;
        break;
      }
    }
  }
  // PTFSAL
  pthread_mutex_unlock(&g_handle_mutex);
  FSI_TRACE(FSI_INFO, "fsi file handle = %d", fsihandle);

  return fsihandle;
}

// this is the common stat function called by skel_stat and skel_lstat
// (but not fstat which is managed by the client)
// General ganesha comment PTFSAL HACK - changed SMB_STRUCT_STAT to struct stat
int fsi_stat(fsi_handle_struct * handle,
             const char        * filename,
             fsi_stat_struct   * sbuf)  // PTFSAL HACK
{
  int                    stat_rc = FSI_IPC_EOK;
  CommonMsgHdr         * p_stat_hdr;
  ClientOpStatReqMsg   * p_stat_req;
  ClientOpStatReqMtext * p_stat_req_mtext;
  ClientOpStatRspMsg   * p_stat_rsp;
  ClientOpStatRspMtext * p_stat_rsp_mtext;
  msg_t                  msg;                   // common message buffer
  int                    msg_rc;                // message error code
  int                    msg_bytes;             // bytes sent/received
  int                    wait_for_stat_rsp;     // waiting for Stat response
  char                   st_filename[PATH_MAX]; // dir name used by server
  int                    snprintf_rc;
  uint8_t                fn[] = "fsi_stat";

  FSI_TRACE(FSI_INFO, "entry");

  // PTFSAL MUTEX FIX
  //pthread_mutex_lock(&g_non_io_mutex);

  // wait for init
  WAIT_SHMEM_ATTACH();

  // Validate the name
  if (filename == NULL) {
    stat_rc = EINVAL;
    FSI_TRACE(FSI_FATAL, "filename is NULL, exit stat_rc = %d", stat_rc);

    // store rc in errno and return -1
    errno   = stat_rc;
    stat_rc = -1;
    // PTFSAL MUTEX FIX
   // pthread_mutex_unlock(&g_non_io_mutex);
    return stat_rc;
  }

  FSI_TRACE(FSI_INFO, "filename: %s", filename);

  // Validate the sbuf pointer
  if (sbuf == NULL) {
    stat_rc = ENOMEM;
    FSI_TRACE(FSI_FATAL, "sbuf is NULL, exit stat_rc = %d", stat_rc);

    // store rc in errno and return -1
    errno   = stat_rc;
    stat_rc = -1;
    // PTFSAL MUTEX FIX
    //pthread_mutex_unlock(&g_non_io_mutex);
    return stat_rc;
  } 

  // Zero out the stat output struct
  memset(sbuf, 0, sizeof(fsi_stat_struct));   // PTFSAL HACK

  // convert filename to st_filename
  convert_fsi_name(handle, filename, st_filename, FSI_NAME_DEFAULT);

  // Check whether the file is already in IPC client cache.
  int fsihandle = fsi_find_handle_by_name(st_filename);
  if (fsihandle != -1) {
    memcpy(sbuf, &g_fsi_handles.m_handle[fsihandle].m_stat,
           sizeof(struct stat));
    FSI_TRACE(FSI_INFO, "Find fsi handle in IPC client cache: st_filename %s rc %d st_size %ld ino %ld errno %d",
              st_filename, stat_rc, sbuf->st_size, sbuf->st_ino, errno);
    // PTFSAL MUTEX FIX
    //pthread_mutex_unlock(&g_non_io_mutex);
    return stat_rc;
  }

  // Set up messaging pointers and flush our handle
  if (stat_rc == FSI_IPC_EOK) {
    p_stat_req_mtext = (ClientOpStatReqMtext *)&msg.mtext;
    p_stat_hdr       = &p_stat_req_mtext->hdr;
    p_stat_req       = &p_stat_req_mtext->data;
    p_stat_rsp_mtext = (ClientOpStatRspMtext *)&msg.mtext;
    p_stat_rsp       = &p_stat_rsp_mtext->data;
  }

  // Build Stat request header
  if (stat_rc == FSI_IPC_EOK) {
    // build the message header
    uint64_t export_id = get_export_id();  // PTFSAL HACK
    ld_common_msghdr (p_stat_hdr,
                      ClientOpStat,
                      sizeof(ClientOpStatReqMsg),
                      export_id,
                      0,                // no handle
                      0,                // no fsHandle
                      0);               // no debug CRC
  }

  // build stat request
  snprintf_rc =
    snprintf(p_stat_req->path, PATH_MAX, "%s", st_filename);
  if (snprintf_rc > PATH_MAX) {
    FSI_TRACE(FSI_ERR, "[%s] name too long.", st_filename);
    stat_rc = ENAMETOOLONG;
  }

  // load UID/GID information
  ld_uid_gid(&p_stat_req->uid, &p_stat_req->gid, handle);

  // Send stat request message to server
  if (stat_rc == FSI_IPC_EOK) {
    msg.mtype = g_client_pid;
    FSI_TRACE(FSI_INFO, "sending Stat req type %ld",
              p_stat_hdr->transactionType);
    msg_bytes = send_msg(g_non_io_req_msgq,
                         &msg,
                         sizeof(ClientOpStatReqMtext),
                         &msg_rc);
  }

  if (stat_rc == FSI_IPC_EOK) {
    // loop until we get our response
    wait_for_stat_rsp = 1;

    while (wait_for_stat_rsp) {

      // wait for response from server
      FSI_TRACE(FSI_INFO, "g_non_io_rsp_msgq=%ld, g_client_pid=%ld",g_non_io_rsp_msgq, g_client_pid);
      msg_bytes = rcv_msg_wait(g_non_io_rsp_msgq,
                               &msg,
                               sizeof(msg.mtext),
                               g_client_pid,
                               &msg_rc);

      // parse response
      if ((p_stat_hdr->transactionType == ClientOpStat) &&
          (msg_bytes                   >  0) &&
          (p_stat_hdr->clientPid       == g_client_pid)) {
        // this is the response we expected
        // determine if the directory is opened
        FSI_TRACE(FSI_INFO, "got Stat rsp %d bytes", msg_bytes);
        if (p_stat_hdr->transactionRc == FSI_IPC_EOK) {
          // Good response - Copy the stat info
          sbuf->st_dev   = PTFSAL_FILESYSTEM_NUMBER;
          sbuf->st_ino   = p_stat_rsp->statInfo.ino;
          sbuf->st_mode  = p_stat_rsp->statInfo.mode;
          sbuf->st_nlink = p_stat_rsp->statInfo.nlink;
          sbuf->st_uid   = p_stat_rsp->statInfo.uid;
          sbuf->st_gid   = p_stat_rsp->statInfo.gid;
          sbuf->st_rdev  = p_stat_rsp->statInfo.rDevice;
          sbuf->st_size  = p_stat_rsp->statInfo.size;
          sbuf->st_atime = p_stat_rsp->statInfo.atime.tv_sec;
          sbuf->st_ctime = p_stat_rsp->statInfo.ctime.tv_sec;
          sbuf->st_mtime = p_stat_rsp->statInfo.mtime.tv_sec;
          sbuf->st_blksize = p_stat_rsp->statInfo.blksize;
          sbuf->st_blocks  = p_stat_rsp->statInfo.blocks;

          // fix blocksize = 0
          if (sbuf->st_blksize == 0) {
            sbuf->st_blksize = 4096;
          }
          // fix blocks = 0
          if (sbuf->st_blocks == 0) {
            sbuf->st_blocks = sbuf->st_size / 512 + 1;
          }
          // nlink must be >= 0
          if (sbuf->st_nlink == 0) {
            sbuf->st_nlink = 1;
          }

          FSI_TRACE(FSI_INFO,"FSI - %s st_fname %s rc %d "
                  "atime %12ld ctime %12ld mtime %12ld dev %d\n",
                  fn, st_filename, stat_rc,
                  sbuf->st_atime,
                  sbuf->st_ctime,
                  sbuf->st_mtime,
                  sbuf->st_dev);


        } else {
          // stat error - store error code
          stat_rc = p_stat_hdr->transactionRc;
          FSI_TRACE_COND_RC(stat_rc, ENOENT, "transactionRc = %d", stat_rc);
        }
        // we got an Stat response message
        wait_for_stat_rsp = 0;

      } else {
        // this is not a stat response
        // log an error, keep waiting
        FSI_TRACE(FSI_FATAL, "got bad type %d[want %d] clientPid "
                  "%d[want %d]", p_stat_hdr->transactionType,
                  ClientOpStat, p_stat_hdr->clientPid, g_client_pid);
        stat_rc = ENOMSG;
      }
    }
  }

  // return to client
  if (stat_rc == FSI_IPC_EOK) {
    FSI_TRACE(FSI_INFO, "st_filename %s rc %d st_size %ld ino %ld errno %d",
              st_filename, stat_rc, sbuf->st_size, sbuf->st_ino, errno);
  } else {

    // store rc in errno and return -1
    errno   = stat_rc;
    stat_rc = -1;

    FSI_TRACE_COND_RC(errno, ENOENT, "st_filename %s rc %d errno %d",
                      st_filename, stat_rc, errno);
  }

  // PTFSAL MUTEX FIX
  //pthread_mutex_unlock(&g_non_io_mutex);
  return stat_rc;
}

// PTFSAL HACK - change call to path...
int fsi_unlink(fsi_handle_struct         * handle,
                char * path)
//                const struct smb_filename * smb_filename)
{
  int                        unlink_rc = FSI_IPC_EOK;
  CommonMsgHdr             * p_unlink_hdr;
  ClientOpUnlinkReqMsg     * p_unlink_req;
  ClientOpUnlinkReqMtext   * p_unlink_req_mtext;
  msg_t                      msg;         // common message buffer
  int                        msg_rc;      // message error code
  int                        msg_bytes;   // bytes sent/received
  int                        wait_for_unlink_rsp;
  int                        snprintf_rc;

//  FSI_TRACE(FSI_INFO, "entry, stream name %s", smb_filename->stream_name);
  FSI_TRACE(FSI_INFO, "entry, stream name %s", path);

//  const char * path = smb_filename->base_name;
  FSI_TRACE(FSI_INFO, "path=[%s]\n", path);

  // PTFSAL MUTEX FIX
  pthread_mutex_lock(&g_non_io_mutex);

  // wait for init
  WAIT_SHMEM_ATTACH();

  // Set up messaging pointers
  if (unlink_rc == FSI_IPC_EOK) {
    p_unlink_req_mtext = (ClientOpUnlinkReqMtext *)&msg.mtext;
    p_unlink_hdr       = &p_unlink_req_mtext->hdr;
    p_unlink_req       = &p_unlink_req_mtext->data;
  }

  // Build unlink request header
  if (unlink_rc == FSI_IPC_EOK) {
    // build the message header
//    uint64_t export_id = get_samba_param(handle, SAMBA_EXPORT_ID_PARAM_NAME, 0);
    uint64_t export_id = get_export_id();  // PTFSAL HACK
    ld_common_msghdr (p_unlink_hdr,
                      ClientOpUnlink,
                      sizeof(ClientOpUnlinkReqMsg),
                      export_id,
                      0,                // no handle
                      0,                // no fsHandle
                      0);               // no debug CRC
  }

  // Build unlink request message data
  if (unlink_rc == FSI_IPC_EOK) {
    snprintf_rc =
      snprintf(p_unlink_req->relPath, sizeof(p_unlink_req->relPath), "%s",
               path);
    if (snprintf_rc > sizeof(p_unlink_req->relPath)) {
      FSI_TRACE(FSI_ERR, "path [%s] name too long.", path);
      unlink_rc = ENAMETOOLONG;
    }

    // set uid/gid
    ld_uid_gid(&p_unlink_req->uid, &p_unlink_req->gid, handle);
  }

  FSI_TRACE(FSI_NOTICE, "sending unlink req type %ld path [%s]",
            p_unlink_hdr->transactionType, p_unlink_req->relPath);

  // Send unlink request message to server
  if (unlink_rc == FSI_IPC_EOK) {
    msg.mtype = g_client_pid;
    msg_bytes = send_msg(g_non_io_req_msgq,
                         &msg,
                         sizeof(ClientOpUnlinkReqMtext),
                         &msg_rc);
  }

  if (unlink_rc == FSI_IPC_EOK) {

    // loop until we get our response
    wait_for_unlink_rsp = 1;

    while (wait_for_unlink_rsp) {

      // wait for response from server
      msg_bytes = rcv_msg_wait(g_non_io_rsp_msgq,
                               &msg,
                               sizeof(msg.mtext),
                               g_client_pid,
                               &msg_rc);

      // parse response
      if ((p_unlink_hdr->transactionType == ClientOpUnlink) &&
          (msg_bytes                     >  0) &&
          (p_unlink_hdr->clientPid       == g_client_pid)) {
        // this is the response we expected
        // determine if unlink succeeded
        FSI_TRACE(FSI_INFO, "got unlink rsp %d bytes", msg_bytes);
        if (p_unlink_hdr->transactionRc != FSI_IPC_EOK) {
          // unlink failed, log an error
          unlink_rc = p_unlink_hdr->transactionRc;
          FSI_TRACE(FSI_ERR, "rc = %d", p_unlink_hdr->transactionRc);
        }
        // terminate loop
        wait_for_unlink_rsp = 0;

      } else {

        // got bad response
        FSI_TRACE(FSI_FATAL, "got bad type %d[want %d] clientPid %d[want %d]",
                  p_unlink_hdr->transactionType, ClientOpUnlink,
                  p_unlink_hdr->clientPid, g_client_pid);
        unlink_rc = ENOMSG;

      }
    }
  }

  // return to client
  if (unlink_rc == FSI_IPC_EOK) {
    FSI_TRACE(FSI_INFO, "returning rc = %d",unlink_rc);
    // PTFSAL MUTEX FIX
    pthread_mutex_unlock(&g_non_io_mutex);
    FSI_RETURN(unlink_rc);
  } else {
    errno = unlink_rc;
    FSI_TRACE(FSI_ERR, "returning rc = -1 errno = %d",errno);
    // PTFSAL MUTEX FIX
    pthread_mutex_unlock(&g_non_io_mutex);
    FSI_RETURN(-1);
  }
}



int skel_chmod(fsi_handle_struct * handle,
               const char        * path,
               mode_t              mode)
{
  int                        chmod_rc = FSI_IPC_EOK;
  CommonMsgHdr             * p_chmod_hdr;
  ClientOpChmodReqMsg      * p_chmod_req;
  ClientOpChmodReqMtext    * p_chmod_req_mtext;
  msg_t                      msg;         // common message buffer
  int                        msg_rc;      // message error code
  int                        msg_bytes;   // bytes sent/received
  int                        wait_for_chmod_rsp;
  int                        snprintf_rc;
  char                       st_path[PATH_MAX];

  FSI_TRACE(FSI_INFO, "entry, path=[%s] mode %ld", path, mode);
  convert_fsi_name(handle, path, st_path, FSI_NAME_DEFAULT);
  FSI_TRACE(FSI_DEBUG, "converted path %s to %s ", path, st_path);

  // PTFSAL MUTEX FIX
  pthread_mutex_lock(&g_non_io_mutex);

  // wait for init
  WAIT_SHMEM_ATTACH();

  // Set up messaging pointers
  if (chmod_rc == FSI_IPC_EOK) {
    p_chmod_req_mtext = (ClientOpChmodReqMtext *)&msg.mtext;
    p_chmod_hdr       = &p_chmod_req_mtext->hdr;
    p_chmod_req       = &p_chmod_req_mtext->data;
  }

  // Build chmod request header
  if (chmod_rc == FSI_IPC_EOK) {
    // build the message header
//    uint64_t export_id = get_samba_param(handle, SAMBA_EXPORT_ID_PARAM_NAME, 0);
    uint64_t export_id = get_export_id();  // PTFSAL HACK
    ld_common_msghdr (p_chmod_hdr,
                      ClientOpChmod,
                      sizeof(ClientOpChmodReqMsg),
                      export_id,
                      0,                // no handle
                      0,                // no fsHandle
                      0);               // no debug CRC
  }

  // Build chmod request message data
  if (chmod_rc == FSI_IPC_EOK) {
    snprintf_rc =
      snprintf(p_chmod_req->relPath, sizeof(p_chmod_req->relPath),
               "%s", st_path);
    if (snprintf_rc > sizeof(p_chmod_req->relPath)) {
      FSI_TRACE(FSI_ERR, "st_path [%s] name too long.", st_path);
      chmod_rc = ENAMETOOLONG;
    }
    p_chmod_req->mode     = mode;

    // set uid/gid
    ld_uid_gid(&p_chmod_req->uid, &p_chmod_req->gid, handle);
  }

  FSI_TRACE(FSI_NOTICE, "sending chmod req type %ld path [%s] mode %ld",
            p_chmod_hdr->transactionType, p_chmod_req->relPath,
            p_chmod_req->mode);
  // Send chmod request message to server
  if (chmod_rc == FSI_IPC_EOK) {
    msg.mtype = g_client_pid;
    msg_bytes = send_msg(g_non_io_req_msgq,
                         &msg,
                         sizeof(ClientOpChmodReqMtext),
                         &msg_rc);
  }

  if (chmod_rc == FSI_IPC_EOK) {

    // loop until we get our response
    wait_for_chmod_rsp = 1;

    while (wait_for_chmod_rsp) {

      // wait for response from server
      msg_bytes = rcv_msg_wait(g_non_io_rsp_msgq,
                               &msg,
                               sizeof(msg.mtext),
                               g_client_pid,
                               &msg_rc);

      // parse response
      if ((p_chmod_hdr->transactionType == ClientOpChmod) &&
          (msg_bytes                    >  0) &&
          (p_chmod_hdr->clientPid       == g_client_pid)) {
        // this is the response we expected
        // determine if the directory is opened
        FSI_TRACE(FSI_INFO, "got chmod rsp %d bytes", msg_bytes);
        if (p_chmod_hdr->transactionRc != FSI_IPC_EOK) {
          // chmod failed, log an error
          chmod_rc = p_chmod_hdr->transactionRc;
          FSI_TRACE(FSI_ERR, "rc = %d", p_chmod_hdr->transactionRc);
        }
        // terminate loop
        wait_for_chmod_rsp = 0;
      } else {
        // got bad response
        FSI_TRACE(FSI_FATAL, "got bad type %d[want %d] clientPid %d[want %d]",
                  p_chmod_hdr->transactionType, ClientOpChmod,
                  p_chmod_hdr->clientPid, g_client_pid);
        chmod_rc = ENOMSG;
      }
    }
  }

  // return to client
  if (chmod_rc == FSI_IPC_EOK) {
    FSI_TRACE(FSI_INFO, "returning rc = %d", chmod_rc);
    // PTFSAL MUTEX FIX
    pthread_mutex_unlock(&g_non_io_mutex);
    FSI_RETURN(chmod_rc);
  } else {
    errno = chmod_rc;
    FSI_TRACE(FSI_INFO, "returning rc = -1 errno = %d",errno);
    // PTFSAL MUTEX FIX
    pthread_mutex_unlock(&g_non_io_mutex);
    FSI_RETURN(-1);
  }
}

void ptfsal_convert_fsi_name(
                      const char          * filename,
                      char                * sv_filename,
                      enum e_fsi_name_enum  fsi_name_type)
{

  //convert_fsi_name("Ganesha", filename, sv_filename, fsi_name_type);
  fsi_handle_struct handler;
  handler.p_filename = "Ganesha";
  convert_fsi_name(&handler, filename, sv_filename, fsi_name_type);
}

int ptfsal_stat(const char        * filename,
                fsi_stat_struct   * sbuf) {
//  return (fsi_stat("Ganesha", filename, sbuf));
  fsi_handle_struct handler;
  handler.p_filename = "Ganesha";
  return (fsi_stat(&handler, filename, sbuf));
}

int ptfsal_opendir(const char        * filename,
                   const char        * mask,
                   uint32              attr)
{
  int dir_handle;
  fsi_handle_struct handler;
  handler.p_filename = "Ganesha";
  dir_handle = fsi_opendir(&handler, filename, mask, attr);
  FSI_TRACE(FSI_DEBUG, "ptfsal_opendir index %d\n", dir_handle);
  return dir_handle;

}

int ptfsal_readdir (int dir_hnd_index,
                    struct stat   * sbuf,
                    char * fsi_dname)
{

  int readdir_rc;
  fsi_handle_struct handler;
  handler.p_filename = "Ganesha";
  struct fsi_struct_dir_t *dirp = (struct fsi_struct_dir_t *)&g_fsi_dir_handles.m_dir_handle[dir_hnd_index].m_fsi_struct_dir;
  readdir_rc = fsi_readdir(&handler, dirp, sbuf);
  if (readdir_rc == 0)
  {
    strcpy(fsi_dname, dirp->dname);
  }
  else
  {
    fsi_dname[0] = 0;
  }
  return readdir_rc;

}


int ptfsal_closedir(int dir_hnd_index)
{

  fsi_handle_struct handler;
  handler.p_filename = "Ganesha";
  struct fsi_struct_dir_t *dirp = (struct fsi_struct_dir_t *)&g_fsi_dir_handles.m_dir_handle[dir_hnd_index].m_fsi_struct_dir;

  return fsi_closedir(&handler, dirp);

}

int ptfsal_fsync (int handle_index)
{
  fsi_handle_struct handler;
  handler.p_filename = "Gansync";
  return fsi_fsync(&handler,handle_index);
}

int ptfsal_close (int handle_index)
{
  fsi_handle_struct handler;
  handler.p_filename = "Ganclose";
  return fsi_close(&handler, handle_index);
}

int ptfsal_ftruncate(int handle_index,
                     uint64_t           offset)
{
  fsi_handle_struct handler;
  handler.p_filename = "Gantrunc";
  return fsi_ftruncate(&handler, handle_index, offset);
}

int ptfsal_unlink(char *path)
{
  fsi_handle_struct handler;
  handler.p_filename = "Ganunlink";
  return fsi_unlink(&handler, path);
}

