/*
 * -----------------------------------------------------------------------------
 * Copyright IBM Corp. 2010, 2011
 * All Rights Reserved
 * -----------------------------------------------------------------------------
 * -----------------------------------------------------------------------------
 * Filename:    pt_ganesha.c
 * Description: Main layer for PT's Ganesha FSAL
 * Author:      FSI IPC Team
 * -----------------------------------------------------------------------------
 */
#include "pt_ganesha.h"
#include <unistd.h>
#include "log.h"
int g_closeHandle_req_msgq;
int g_closeHandle_rsp_msgq;
bool g_poll_for_timeouts;	/* check for timed-out handles */

/* number of times poll thread has been called */
uint64_t g_poll_iterations;

/* this flag will control whether threads created for PT continue or stop. */
bool g_terminate_ptfsal_threads = false;

int ptfsal_closeHandle_attach_to_queues(void)
{
	int rc;

	/* Get IO and non-IO request, response message queue IDs */
	g_closeHandle_req_msgq = msgget(FSI_CCL_IPC_CLOSE_HANDLE_REQ_Q_KEY, 0);
	if (g_closeHandle_req_msgq < 0) {
		FSI_TRACE(FSI_FATAL,
			  "error getting close handle Req Msg Q "
			  "id %d (errno = %d)",
			  FSI_CCL_IPC_CLOSE_HANDLE_REQ_Q_KEY, errno);
		/*
		 * cleanup the attach made earlier,
		 * nothing to clean up for the queues
		 */
		rc = shmdt(g_shm_at_fsal);
		if (rc == -1) {
			FSI_TRACE(FSI_FATAL,
				  "shmdt returned rc = %d errno = %d", rc,
				  errno);
		}
		return -1;
	}

	g_closeHandle_rsp_msgq = msgget(FSI_CCL_IPC_CLOSE_HANDLE_RSP_Q_KEY, 0);
	if (g_closeHandle_rsp_msgq < 0) {
		FSI_TRACE(FSI_FATAL,
			  "error getting close handle Rsp Msg Q "
			  "id %d (errno = %d)",
			  FSI_CCL_IPC_CLOSE_HANDLE_RSP_Q_KEY, errno);
		/*
		 * cleanup the attach made earlier, nothing
		 * to clean up for the queues
		 */
		rc = shmdt(g_shm_at_fsal);
		if (rc == -1) {
			FSI_TRACE(FSI_FATAL,
				  "shmdt returned rc = %d errno = %d", rc,
				  errno);
		}
		return -1;
	}

	FSI_TRACE(FSI_NOTICE,
		  "Successful attaching to Close Handle req/rsp queues");
	return 0;
}

void *ptfsal_closeHandle_listener_thread(void *args)
{
	SetNameFunction("PT Cls Handler");

	int rc = ptfsal_closeHandle_attach_to_queues();
	if (rc == -1)
		exit(1);

	g_terminate_ptfsal_threads = false;
	while (!g_terminate_ptfsal_threads) {
		FSI_TRACE(FSI_DEBUG, "Periodic calling close listener.");
		CCL_CLOSE_LISTENER(g_closeHandle_req_msgq,
				   g_closeHandle_rsp_msgq);
	}
	FSI_TRACE(FSI_NOTICE, "The close handler listener thread exit. ");
	return NULL;
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
	int close_rc = 0;

	for (index = FSI_CIFS_RESERVED_STREAMS;
	     index < g_fsi_handles_fsal->m_count; index++) {
		FSI_TRACE(FSI_DEBUG, "Flushing any pending IO for handle %d",
			  index);
		struct ccl_msg_t msg;
		int rc;
		int lock_rc;
		GET_ANY_IO_RESPONSES(index, &rc, &msg);

		/*
		 * only poll for timed out handles every
		 * PTFSAL_POLLING_HANDLE_TIMEOUT_SEC iterations
		 */
		if (g_poll_for_timeouts) {
			FSI_TRACE(FSI_INFO,
				  "Last IO time[%ld] handle index [%d]"
				  "current_time[%ld] handle state[%d]"
				  "m_hndl_in_use[%d]",
				  g_fsi_handles_fsal->m_handle[index].
				  m_last_io_time, index, current_time,
				  g_fsi_handles_fsal->m_handle[index].
				  m_nfs_state,
				  g_fsi_handles_fsal->m_handle[index].
				  m_hndl_in_use);

			lock_rc = CCL_LOCK_IO_OPERATION_MUTEX(index);
			if (lock_rc) {
				FSI_TRACE(FSI_ERR,
					  "Got error when acquire mutex lock= %d",
					  lock_rc);
			} else {
				bool can_close = 0;
				can_close = CCL_CAN_CLOSE_HANDLE(index,
				      polling_thread_handle_timeout_sec);
				if (can_close) {
					close_rc =
					ptfsal_implicit_close_for_nfs(
						index, CCL_CLOSE_STYLE_NORMAL);
					if (close_rc == -1) {
						FSI_TRACE(FSI_ERR,
						"Failed to implicitly"
						"close handle[%d]",
						index);
					}
				}
				CCL_UNLOCK_IO_OPERATION_MUTEX(index);

				usleep(1000);
			}
		}
	}

	if (g_poll_for_timeouts)
		g_poll_for_timeouts = false;

	return;
}

void *ptfsal_polling_closeHandler_thread(void *args)
{
	SetNameFunction("PT Polling Cls");

	g_poll_iterations = 1;
	g_poll_for_timeouts = false;
	g_terminate_ptfsal_threads = false;

	while (!g_terminate_ptfsal_threads) {
		FSI_TRACE(FSI_DEBUG,
			  "Periodic check for opened handle to close");
		ptfsal_close_timedout_handle_bkg();
		sleep(PTFSAL_POLLING_THREAD_FREQUENCY_SEC);

		if ((g_poll_iterations * PTFSAL_POLLING_THREAD_FREQUENCY_SEC)
		    % PTFSAL_POLLING_HANDLE_TIMEOUT_SEC == 0) {
			g_poll_for_timeouts = true;
			g_poll_iterations = 1;
		} else {
			++g_poll_iterations;
		}
	}
	FSI_TRACE(FSI_NOTICE, "The polling close handler thread exit.");
	return NULL;
}

int ptfsal_implicit_close_for_nfs(int handle_index_to_close, int close_style)
{
	int rc;
	int close_rc;
	CACHE_TABLE_ENTRY_T cacheEntry;
	char key[FSI_CCL_PERSISTENT_HANDLE_N_BYTES];

	FSI_TRACE(FSI_NOTICE, "Closing handle [%d] close_style[%d]",
		  handle_index_to_close, close_style);

	CCL_LOCK_IO_HANDLE_MUTEX(handle_index_to_close);
	memset(&cacheEntry, 0x00, sizeof(CACHE_TABLE_ENTRY_T));
	memcpy(key,
	       &g_fsi_handles_fsal->m_handle[handle_index_to_close].m_stat.
	       st_persistentHandle.handle[0],
	       FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
	cacheEntry.key = key;
	CCL_UNLOCK_IO_HANDLE_MUTEX(handle_index_to_close);

	close_rc =
	    CCL_IMPLICIT_CLOSE_FOR_NFS(handle_index_to_close, close_style);
	FSI_TRACE(FSI_DEBUG, "Returned rc=%d", close_rc);

	/* Removed the cache no matter close NFS is successful or not. */
	pthread_rwlock_wrlock(&g_fsi_cache_handle_rw_lock);
	rc = fsi_cache_deleteEntry(&g_fsi_name_handle_cache_opened_files,
				   &cacheEntry);
	pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);
	if (rc != FSI_CCL_IPC_EOK) {
		FSI_TRACE(FSI_ERR,
			  "Failed to delete cache entry to cache ID = %d",
			  g_fsi_name_handle_cache_opened_files.cacheMetaData.
			  cacheTableID);
		ptfsal_print_handle(cacheEntry.key);
	}
	return close_rc;
}

void ptfsal_terminate_ptfsal_threads()
{
	g_terminate_ptfsal_threads = true;
}
