#ifndef NFS_TCB_H
#define NFS_TCB_H

#include "wait_queue.h"
#include "nlm_list.h"

typedef enum pause_state
{
  STATE_STARTUP,
  STATE_AWAKEN,
  STATE_AWAKE,
  STATE_PAUSE,
  STATE_PAUSED,
  STATE_EXIT
} pause_state_t;

typedef enum pause_reason
{
  PAUSE_RELOAD_EXPORTS,
  PAUSE_SHUTDOWN,
} pause_reason_t;

typedef enum awaken_reason
{
  AWAKEN_STARTUP,
  AWAKEN_RELOAD_EXPORTS,
} awaken_reason_t;

typedef enum pause_rc
{
  PAUSE_OK,
  PAUSE_PAUSE, /* Calling thread should pause - most callers can ignore this return code */
  PAUSE_EXIT,  /* Calling thread should exit */
} pause_rc;

extern const char *pause_rc_str[];

typedef struct nfs_thread_control_block__
{
  pthread_cond_t tcb_condvar;
  pthread_mutex_t tcb_mutex;
  int tcb_ready;
  pause_state_t tcb_state;
  char tcb_name[256];
  struct glist_head tcb_list;
} nfs_tcb_t;

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

#endif /* NFS_TCB_H */
