#ifndef _NFS_TCB_H__
#define _NFS_TCB_H__

#include "nfs_core.h"
#include "nlm_list.h"
#include <pthread.h>

void tcb_insert(nfs_tcb_t *element);
void tcb_remove(nfs_tcb_t *element);
void tcb_head_init(void);
pause_rc wake_workers(awaken_reason_t reason);
pause_rc pause_workers(pause_reason_t reason);
void notify_threads_of_new_state(void);
void mark_thread_awake(nfs_worker_data_t *pmydata);
pause_rc mark_thread_existing(nfs_worker_data_t *pmydata);
void mark_thread_done(nfs_worker_data_t *pmydata);
void mark_thread_asleep(nfs_worker_data_t *pmydata);
pause_rc wait_for_workers_to_awaken(void);
void wait_for_workers_to_exit(void);
pause_rc wait_for_workers_to_pause(void);
#endif

