#ifndef _NFS_TCB_H__
#define _NFS_TCB_H__

#include "nfs_core.h"
#include "nlm_list.h"
#include <pthread.h>

typedef enum thread_sm
{
  THREAD_SM_RECHECK,
  THREAD_SM_BREAK,
  THREAD_SM_EXIT,
} thread_sm_t;

void tcb_insert(nfs_tcb_t *element);
void tcb_remove(nfs_tcb_t *element);
void tcb_head_init(void);
pause_rc wake_threads(awaken_reason_t reason);
pause_rc pause_threads(pause_reason_t reason);
void notify_threads_of_new_state(void);
void mark_thread_awake(nfs_tcb_t *wcb);
pause_rc mark_thread_existing(nfs_tcb_t *wcb);
void mark_thread_done(nfs_tcb_t *wcb);
void mark_thread_asleep(nfs_tcb_t *wcb);
pause_rc wait_for_threads_to_awaken(void);
void wait_for_threads_to_exit(void);
pause_rc _wait_for_threads_to_pause(void);
int tcb_new(nfs_tcb_t *element, char *name);
thread_sm_t thread_sm_locked(nfs_tcb_t *tcbp);

#endif

