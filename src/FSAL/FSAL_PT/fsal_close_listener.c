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

int g_closeHandle_req_msgq;
int g_closeHandle_rsp_msgq;

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

  rc = ptfsal_closeHandle_attach_to_queues();
  if (rc == -1) {
    exit (1);
  }

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
      close_rc = ccl_find_oldest_handle();
      if (close_rc != -1) {
        ccl_update_handle_nfs_state (close_rc, CCL_CLOSE);
        close_rc = ccl_implicit_close_for_nfs(close_rc);
      }

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
