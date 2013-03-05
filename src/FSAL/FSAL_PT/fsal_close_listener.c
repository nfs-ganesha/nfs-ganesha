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
//#include "pt_util_cache.h"
#include <unistd.h>
#include "log.h"
int g_closeHandle_req_msgq;
int g_closeHandle_rsp_msgq;
bool     g_poll_for_timeouts; // check for timed-out handles
uint64_t g_poll_iterations;   // number of times poll thread has been called

int ptfsal_closeHandle_attach_to_queues(void)
{
  int rc;

  /* Get IO and non-IO request, response message queue IDs */
  g_closeHandle_req_msgq = msgget(FSI_IPC_CLOSE_HANDLE_REQ_Q_KEY, 0);
  if (g_closeHandle_req_msgq < 0) {
    FSI_TRACE(FSI_FATAL, "error getting close handle Req Msg Q "
              "id %d (errno = %d)", FSI_IPC_CLOSE_HANDLE_REQ_Q_KEY, errno);
    /* cleanup the attach made earlier, nothing to clean up for the queues */
    if ((rc = shmdt(g_shm_at_fsal)) == -1) {
      FSI_TRACE(FSI_FATAL, "shmdt returned rc = %d errno = %d", rc, errno);
    }
    return -1;
  }

  g_closeHandle_rsp_msgq = msgget(FSI_IPC_CLOSE_HANDLE_RSP_Q_KEY, 0);
  if (g_closeHandle_rsp_msgq < 0) {
    FSI_TRACE(FSI_FATAL, "error getting close handle Rsp Msg Q "
              "id %d (errno = %d)", FSI_IPC_CLOSE_HANDLE_RSP_Q_KEY, errno);
    /* cleanup the attach made earlier, nothing to clean up for the queues */
    if ((rc = shmdt(g_shm_at_fsal)) == -1) {
      FSI_TRACE(FSI_FATAL, "shmdt returned rc = %d errno = %d", rc, errno);
    }
    return -1;
  }

  FSI_TRACE(FSI_NOTICE, "Successful attaching to Close Handle req/rsp queues");
  return 0;
}

void *ptfsal_closeHandle_listener_thread(void *args)
{
  int rc;
  struct msg_t msg;
  int msg_bytes;
  int close_rc;
  int i;
  struct CommonMsgHdr *msgHdr;
  int handleIdxFound;

  SetNameFunction("PT FSAL CloseOnOpen Handler");

  rc = ptfsal_closeHandle_attach_to_queues();
  if (rc == -1) {
    exit (1);
  }

  while (1) {
    msg_bytes = RCV_MSG_WAIT_BLOCK(g_closeHandle_req_msgq,
                                   &msg,
                                   sizeof(struct CommonMsgHdr),
                                   0);
    if (msg_bytes != -1) {
      close_rc = -1;
      FSI_TRACE(FSI_NOTICE, "Finding oldest handles");
      /* TBD: we need to address a design if we have more than one
              close thread or NFS4 support (that can issue close itself)
              in order to ensure proper locking to the handle table.
              Currently, one close thread model will work since we don't
              shuffle handle around and there is only one place to
              actually close the handle (which is here) in the code */

      handleIdxFound = CCL_FIND_OLDEST_HANDLE();
      if (handleIdxFound != -1) {
        close_rc = ptfsal_implicit_close_for_nfs(handleIdxFound,
                                                 CCL_CLOSE_STYLE_NORMAL);
      }
      /* Send the response back */
      msgHdr = (struct CommonMsgHdr *) &msg.mtext[0];
      msgHdr->transactionRc = close_rc;
      msg_bytes = SEND_MSG(g_closeHandle_rsp_msgq,
                           &msg,
                           sizeof(struct CommonMsgHdr));
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
       index < g_fsi_handles_fsal->m_count;
       index++) {
    FSI_TRACE(FSI_DEBUG, "Flushing any pending IO for handle %d", index);
    struct msg_t msg;
    int          rc;
    GET_ANY_IO_RESPONSES(index, &rc, &msg);

    // only poll for timed out handles every PTFSAL_POLLING_HANDLE_TIMEOUT_SEC
    // iterations
    if (g_poll_for_timeouts) {
      FSI_TRACE(FSI_INFO, "Last IO time[%ld] handle index [%d]"
                "current_time[%ld] handle state[%d] m_hndl_in_use[%d]",
                g_fsi_handles_fsal->m_handle[index].m_last_io_time, index,
                current_time, g_fsi_handles_fsal->m_handle[index].m_nfs_state,
                g_fsi_handles_fsal->m_handle[index].m_hndl_in_use);

      if (CCL_CAN_CLOSE_HANDLE(index,
			       polling_thread_handle_timeout_sec)) {
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
        close_rc = ptfsal_implicit_close_for_nfs(index, CCL_CLOSE_STYLE_NORMAL);
        if (close_rc == -1) {
          FSI_TRACE(FSI_ERR, "Failed to implicitly close handle[%d]",index);
        } 
        usleep(1000);
      }
    }
  }

  if (g_poll_for_timeouts) {
    g_poll_for_timeouts = false;
  }

  return;
}

void *ptfsal_polling_closeHandler_thread(void *args)
{
  SetNameFunction("PT FSAL Polling Close");
  
  g_poll_iterations   = 1;
  g_poll_for_timeouts = false;

  while (1) {
    FSI_TRACE(FSI_DEBUG, "Periodic check for opened handle to close");
    ptfsal_close_timedout_handle_bkg();
    sleep (PTFSAL_POLLING_THREAD_FREQUENCY_SEC);

    if ((g_poll_iterations * PTFSAL_POLLING_THREAD_FREQUENCY_SEC)
        % PTFSAL_POLLING_HANDLE_TIMEOUT_SEC == 0) {
      g_poll_for_timeouts = true;
      g_poll_iterations   = 1;
    } else {
      ++g_poll_iterations;
    }
  }
}

int ptfsal_implicit_close_for_nfs(int handle_index_to_close, int close_style)
{
  int rc;
  int close_rc;
  CACHE_TABLE_ENTRY_T cacheEntry;
  char key[FSI_PERSISTENT_HANDLE_N_BYTES];

  FSI_TRACE(FSI_NOTICE, "Closing handle [%d] close_style[%d]", handle_index_to_close, close_style);

  memset (&cacheEntry, 0x00, sizeof(CACHE_TABLE_ENTRY_T));
  memcpy (key,
          &g_fsi_handles_fsal->m_handle[handle_index_to_close].m_stat.st_persistentHandle.handle[0],
          FSI_PERSISTENT_HANDLE_N_BYTES);
  cacheEntry.key =  key;

  close_rc = CCL_IMPLICIT_CLOSE_FOR_NFS(handle_index_to_close, close_style);
  FSI_TRACE(FSI_DEBUG, "Returned rc=%d", rc);

  if (close_rc != -1) {
    pthread_rwlock_wrlock(&g_fsi_cache_handle_rw_lock);
    rc = fsi_cache_deleteEntry(&g_fsi_name_handle_cache_opened_files, &cacheEntry);
    pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);
    if (rc != FSI_IPC_EOK) {
      FSI_TRACE(FSI_ERR, "Failed to delete cache entry to cache ID = %d",
          g_fsi_name_handle_cache_opened_files.cacheMetaData.cacheTableID);
      ptfsal_print_handle(cacheEntry.key);
    }
  }
  return close_rc;
}


