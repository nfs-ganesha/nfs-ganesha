// -----------------------------------------------------------------------------
// Copyright IBM Corp. 2010, 2011
// All Rights Reserved
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Filename:    pt_ganesha.c
// Description: Main layer for PT's Ganesha FSAL
// Author:      FSI IPC Team
// -----------------------------------------------------------------------------
#include "pt_ganesha.h"
#include <unistd.h>
#include "log.h"
int g_closeHandle_req_msgq;
int g_closeHandle_rsp_msgq;
extern struct file_handles_struct_t g_fsi_handles; // FSI client handles
pthread_mutex_t g_close_handle_mutex; // file handle processing mutex

int ptfsal_closeHandle_attach_to_queues(void)
{
  int rc;

  /* Get IO and non-IO request, response message queue IDs */
  g_closeHandle_req_msgq = msgget(FSI_IPC_CLOSE_HANDLE_REQ_Q_KEY, 0);
  if (g_closeHandle_req_msgq < 0) {
    FSI_TRACE(FSI_FATAL, "error getting close handle Req Msg Q "
              "id %d (errno = %d)", FSI_IPC_CLOSE_HANDLE_REQ_Q_KEY, errno);
    /* cleanup the attach made earlier, nothing to clean up for the queues */
    if ((rc = shmdt(g_shm_at)) == -1) {
      FSI_TRACE(FSI_FATAL, "shmdt returned rc = %d errno = %d", rc, errno);
    }
    return -1;
  }

  g_closeHandle_rsp_msgq = msgget(FSI_IPC_CLOSE_HANDLE_RSP_Q_KEY, 0);
  if (g_closeHandle_rsp_msgq < 0) {
    FSI_TRACE(FSI_FATAL, "error getting close handle Rsp Msg Q "
              "id %d (errno = %d)", FSI_IPC_CLOSE_HANDLE_RSP_Q_KEY, errno);
    /* cleanup the attach made earlier, nothing to clean up for the queues */
    if ((rc = shmdt(g_shm_at)) == -1) {
      FSI_TRACE(FSI_FATAL, "shmdt returned rc = %d errno = %d", rc, errno);
    }
    return -1;
  }

  FSI_TRACE(FSI_NOTICE, "Successful attaching to Close Handle req/rsp queues");
  return 0;
}

void *ptfsal_closeHandle_listener_thread(void *args)
{
  int i;
  int rc;
  struct msg_t msg;
  int msg_rc;
  int msg_bytes;
  struct CommonMsgHdr         * p_hdr;
  int close_rc;
  struct CommonMsgHdr *msgHdr;
  SetNameFunction("CloseOnOpen Handler");

  rc = ptfsal_closeHandle_attach_to_queues();
  if (rc == -1) {
    exit (1);
  }

  pthread_mutex_init(&g_close_handle_mutex, NULL);
  while (1) {
    msg_bytes = rcv_msg_wait(g_closeHandle_req_msgq,
                             &msg,
                             sizeof(struct CommonMsgHdr),
                             0,
                             &msg_rc);
    if (msg_bytes != -1) {
      close_rc = -1;
      FSI_TRACE(FSI_NOTICE, "Finding oldest handles");
      /* TBD: we need to address a design if we have more than one
              close thread or NFS4 support (that can issue close itself)
              in order to ensure proper locking to the handle table.
              Currently, one close thread model will work since we don't
              shuffle handle around and there is only one place to
              actually close the handle (which is here) in the code */
      ccl_up_mutex_lock(&g_close_handle_mutex);
      close_rc = ptfsal_find_oldest_handle();
      if (close_rc != -1) {
        ptfsal_update_handle_nfs_state (close_rc, CCL_CLOSE);
        close_rc = ptfsal_implicit_close_for_nfs(close_rc, 0);
      }
      ccl_up_mutex_unlock(&g_close_handle_mutex);
      /* Send the response back */
      msgHdr = (struct CommonMsgHdr *) &msg.mtext[0];
      msgHdr->transactionRc = close_rc;
      msg_bytes = send_msg(g_closeHandle_rsp_msgq,
                           &msg,
                           sizeof(struct CommonMsgHdr),
                           &msg_rc);
    }
  }
}

void ptfsal_close_timedout_handle_bkg(void)
{
  /* This function will find out from out handle table
   * which handle has timed out and close it.  This is
   * used by background polling thread
   * ptfsal_polling_closeHandler_thread()
   */

  int index;
  time_t current_time = time(NULL);
  int close_rc;

  for (index = FSI_CIFS_RESERVED_STREAMS;
       index < g_fsi_handles.m_count;
       index++) {
    FSI_TRACE(FSI_INFO, "Last IO time[%ld] handle index [%d]"
              "current_time[%ld] handle state[%d] m_hndl_in_use[%d]",
              g_fsi_handles.m_handle[index].m_last_io_time, index,
              current_time, g_fsi_handles.m_handle[index].m_nfs_state,
              g_fsi_handles.m_handle[index].m_hndl_in_use);
    ccl_up_mutex_lock(&g_close_handle_mutex);
    if ((g_fsi_handles.m_handle[index].m_nfs_state == NFS_CLOSE) &&
        (g_fsi_handles.m_handle[index].m_hndl_in_use) &&
        ((current_time - g_fsi_handles.m_handle[index].m_last_io_time)
         >= PTFSAL_POLLING_HANDLE_TIMEOUT_SEC)) {
      /* We've found timed out handle and we are sending an explicit
       * close to close it out.
       *
       * NOTE: we are putting a mutex around this close.  This mutex
       *       spans across the close_on_open logic and this polling
       *       for close logic.
       *
       *       We are putting a usleep() in the search loop so that we
       *       allow other close_on_open logic to come in
       */
      FSI_TRACE(FSI_NOTICE, "Found timed-out handle[%d]",index);
      close_rc = ptfsal_implicit_close_for_nfs(index, 1);
      ccl_up_mutex_unlock(&g_close_handle_mutex);
      if (close_rc == -1) {
        FSI_TRACE(FSI_ERR, "Failed to implicitly close handle[%d]",index);
      }
      usleep(1000);
    } else {
      ccl_up_mutex_unlock(&g_close_handle_mutex);
    }

  }
  return;
}

void *ptfsal_polling_closeHandler_thread(void *args)
{
  int i;
  int rc;
  struct msg_t msg;
  int msg_rc;
  int msg_bytes;
  struct CommonMsgHdr         * p_hdr;
  int close_rc;
  struct CommonMsgHdr *msgHdr;
  SetNameFunction("Polling Close");
  while (1) {
    FSI_TRACE(FSI_INFO, "Periodic check for opened handle to close");
    ptfsal_close_timedout_handle_bkg();
    sleep (PTFSAL_POLLING_HANDLE_TIMEOUT_SEC);
  }
}

int ptfsal_find_oldest_handle(void)
{
  int fsihandle = -1;
  int index;
  time_t oldest_time = time(NULL);
  time_t current_time = oldest_time;

  ccl_up_mutex_lock(&g_handle_mutex);

  for (index = FSI_CIFS_RESERVED_STREAMS;
       index < g_fsi_handles.m_count;
       index++) {
    FSI_TRACE(FSI_INFO, "Last IO time[%ld] handle index [%d]"
              "oldest_time[%ld] handle state[%d] m_hndl_in_use[%d]",
              g_fsi_handles.m_handle[index].m_last_io_time, index,
              oldest_time, g_fsi_handles.m_handle[index].m_nfs_state,
              g_fsi_handles.m_handle[index].m_hndl_in_use);
    if ((g_fsi_handles.m_handle[index].m_last_io_time < oldest_time) &&
        (g_fsi_handles.m_handle[index].m_hndl_in_use) &&
        (g_fsi_handles.m_handle[index].m_nfs_state == NFS_CLOSE) &&
        (current_time - g_fsi_handles.m_handle[index].m_last_io_time)
         >= PTFSAL_OLDEST_HANDLE_TIMEOUT_SEC) {
        oldest_time = g_fsi_handles.m_handle[index].m_last_io_time;
        fsihandle = index;
    }
  }
  ccl_up_mutex_unlock(&g_handle_mutex);
  FSI_TRACE(FSI_NOTICE, "fsi file handle = %d", fsihandle);

  return fsihandle;
}

void ptfsal_update_handle_nfs_state(int handle_index, enum e_nfs_state state)
{
  if (ccl_check_handle_index(handle_index) < 0) {
    FSI_TRACE(FSI_ERR, "Invalid handle index to update nfs_state with = %d",
              handle_index);
    return;
  }

  FSI_TRACE(FSI_DEBUG, "Setting m_nfs_state[%d]", state);
  ccl_up_mutex_lock(&g_handle_mutex);
  g_fsi_handles.m_handle[handle_index].m_nfs_state = state;
  ccl_up_mutex_unlock(&g_handle_mutex);
}

int ptfsal_implicit_close_for_nfs(int handle_index_to_close, int useLock)
{
  ccl_context_t context;

  if (ccl_check_handle_index(handle_index_to_close) < 0) {
    FSI_TRACE(FSI_ERR, "Invalid handle index to close = %d",
              handle_index_to_close);
    return -1;
  }

  memset (&context, 0, sizeof(context));
  context.export_id = g_fsi_handles.m_handle[handle_index_to_close].m_exportId;
  context.uid       = geteuid();
  context.gid       = getegid();
  FSI_TRACE(FSI_NOTICE, "Closing handle [%d]", handle_index_to_close);
  return (ccl_close(&context, handle_index_to_close, useLock));

}

