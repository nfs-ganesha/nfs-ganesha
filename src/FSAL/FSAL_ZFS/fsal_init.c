/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_init.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.20 $
 * \brief   Initialization functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_common.h"

#include <string.h>
#include <unistd.h>


extern size_t i_snapshots;
extern snapshot_t *p_snapshots;
extern pthread_rwlock_t vfs_lock;
pthread_t snapshot_thread;

static void *SnapshotThread(void *);

size_t stack_size = 0;

/**
 * FSAL_Init : Initializes the FileSystem Abstraction Layer.
 *
 * \param init_info (input, fsal_parameter_t *) :
 *        Pointer to a structure that contains
 *        all initialization parameters for the FSAL.
 *        Specifically, it contains settings about
 *        the filesystem on which the FSAL is based,
 *        security settings, logging policy and outputs,
 *        and other general FSAL options.
 *
 * \return Major error codes :
 *         ERR_FSAL_NO_ERROR     (initialisation OK)
 *         ERR_FSAL_FAULT        (init_info pointer is null)
 *         ERR_FSAL_SERVERFAULT  (misc FSAL error)
 *         ERR_FSAL_ALREADY_INIT (The FS is already initialized)
 *         ERR_FSAL_BAD_INIT     (FS specific init error,
 *                                minor error code gives the reason
 *                                for this error.)
 *         ERR_FSAL_SEC_INIT     (Security context init error).
 */
fsal_status_t ZFSFSAL_Init(fsal_parameter_t * init_info    /* IN */
    )
{
  static int is_initialized = 0;
  fsal_status_t status;
  zfsfs_specific_initinfo_t *spec_info =
	  (zfsfs_specific_initinfo_t *) &init_info->fs_specific_info;

  /* sanity check.  */
  if(is_initialized)
  {
    LogEvent(COMPONENT_FSAL, "INIT: blocking second call to FSAL_Init");
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_Init);
  }

  if(!init_info)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);

  /* proceeds FSAL internal status initialization */

  status = fsal_internal_init_global(&(init_info->fsal_info),
                                     &(init_info->fs_common_info),
                                     &(init_info->fs_specific_info));

  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_Init);

  /* Initilise the libzfswrap library */
  p_zhd = libzfswrap_init();
  if(!p_zhd)
  {
    LogCrit(COMPONENT_FSAL, "FSAL INIT: *** ERROR: Unable to initialize the libzfswrap library.");
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  /* Mount the zpool */
  libzfswrap_vfs_t *p_vfs = libzfswrap_mount(spec_info->psz_zpool, "/tank", "");
  if(!p_vfs)
  {
    libzfswrap_exit(p_zhd);
    LogCrit(COMPONENT_FSAL, "FSAL INIT: *** ERROR: Unable to mount the file system.");
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  /* List the snapshots of the given zpool and mount them */
  const char *psz_error;
  char **ppsz_snapshots;
  i_snapshots = libzfswrap_zfs_get_list_snapshots(p_zhd, spec_info->psz_zpool,
                                                  &ppsz_snapshots, &psz_error);

  if(i_snapshots > 0)
  {
    LogDebug(COMPONENT_FSAL, "FSAL INIT: Found %zu snapshots.", i_snapshots);
    p_snapshots = calloc(i_snapshots + 1, sizeof(*p_snapshots));
    p_snapshots[0].p_vfs = p_vfs;
    p_snapshots[0].index = 0;

    int i,j;
    for(i = 0; i < i_snapshots; i++)
    {
      libzfswrap_vfs_t *p_snap_vfs = libzfswrap_mount(ppsz_snapshots[i], ppsz_snapshots[i], "");
      if(!p_snap_vfs)
      {
        LogCrit(COMPONENT_FSAL, "FSAL INIT: *** ERROR: Unable to mount the snapshot %s", ppsz_snapshots[i]);
        for(j = i; j >= 0; j--)
          libzfswrap_umount(p_snapshots[j].p_vfs, 1);

        libzfswrap_exit(p_zhd);
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
      }

      /* Change the name of the snapshot from zpool_name@snap_name to snap_name
         The '@' character is allways present, so no need to check it */
      p_snapshots[i+1].psz_name = strdup(strchr(ppsz_snapshots[i], '@') + 1);
      p_snapshots[i+1].p_vfs = p_snap_vfs;
      p_snapshots[i+1].index = i + 1;

      free(ppsz_snapshots[i]);
    }
  }
  else
  {
    LogDebug(COMPONENT_FSAL, "FSAL INIT: No snapshot found.");
    p_snapshots = calloc(1, sizeof(*p_snapshots));
    p_snapshots[0].p_vfs = p_vfs;
    i_snapshots = 0;
  }
  pthread_rwlock_init(&vfs_lock, NULL);

  /* Create a thread to handle snapshot creation */
  if(spec_info->auto_snapshots)
  {
    LogDebug(COMPONENT_FSAL, "FSAL INIT: Creating the auto-snapshot thread");
    zfsfs_specific_initinfo_t *fs_configuration = malloc(sizeof(*fs_configuration));
    *fs_configuration = *spec_info;
#if 0
    if(pthread_create(&snapshot_thread, NULL, SnapshotThread, fs_configuration))
    {
      snapshot_thread = (pthread_t)NULL;
      ZFSFSAL_terminate();
      Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_Init);
    }
#endif
  }
  else
  {
    LogDebug(COMPONENT_FSAL, "FSAL INIT: No automatic snapshot creation");
    snapshot_thread = (pthread_t)NULL;
  }

  /* Everything went OK. */
  is_initialized = 1;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_Init);

}

/* To be called before exiting */
fsal_status_t ZFSFSAL_terminate()
{
  /* Join the snapshot thread if it does exist */
  if(snapshot_thread)
    pthread_join(snapshot_thread, NULL);

  /* Unmount every snapshots and free the memory */
  int i;
  for(i = i_snapshots; i >= 0; i--)
    libzfswrap_umount(p_snapshots[i].p_vfs, 1);

  for(i = 0; i < i_snapshots; i++)
    free(p_snapshots[i].psz_name);
  free(p_snapshots);

  pthread_rwlock_destroy(&vfs_lock);

  libzfswrap_exit(p_zhd);
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/* Take a snapshot */
static libzfswrap_vfs_t *TakeSnapshotAndMount(const char *psz_zpool, const char *psz_prefix, char **ppsz_name)
{
    char psz_buffer[FSAL_MAX_NAME_LEN];
    const char *psz_error;
    time_t time_now = time(NULL);
    struct tm *now = gmtime(&time_now);

    asprintf(ppsz_name, "%s%d_%02d_%02d-%02d_%02d",
             psz_prefix, now->tm_year + 1900, now->tm_mon,
             now->tm_mday, now->tm_hour, now->tm_min);
    libzfswrap_zfs_snapshot(p_zhd, psz_zpool, *ppsz_name, &psz_error);

    snprintf(psz_buffer, FSAL_MAX_NAME_LEN, "%s@%s%d_%02d_%02d-%02d_%02d",
             psz_zpool, psz_prefix, now->tm_year + 1900, now->tm_mon,
             now->tm_mday, now->tm_hour, now->tm_min);

    LogDebug(COMPONENT_FSAL, "SNAPSHOTS: creating a new snapshot '%s'", psz_buffer);
    return libzfswrap_mount(psz_buffer, psz_buffer, "");
}

static void AddSnapshot(libzfswrap_vfs_t *p_vfs, char *psz_name)
{
    i_snapshots++;
    if( ( p_snapshots = realloc(p_snapshots, (i_snapshots + 1) * sizeof(*p_snapshots)) ) == NULL )
     {
       LogMajor(COMPONENT_FSAL, "SNAPSHOTS: recan't allocate memory... exiting");
       free( p_snapshots ) ;
       exit( 1 ) ;
       return ;
     }

    p_snapshots[i_snapshots].psz_name = psz_name;
    p_snapshots[i_snapshots].p_vfs = p_vfs;
    p_snapshots[i_snapshots].index = i_snapshots;
}

static int CountSnapshot(const char *psz_prefix)
{
  int i,count = 0;
  size_t len = strlen(psz_prefix);

  for(i = 1; i < i_snapshots + 1; i++)
    if(!strncmp(p_snapshots[i].psz_name, psz_prefix, len))
      count++;

  return count;
}

static void RemoveOldSnapshots(const char *psz_prefix, int number)
{
  int i;
  char *psz_name;
  const char *psz_error;
  libzfswrap_vfs_t *p_vfs;
  size_t len = strlen(psz_prefix);

  for(i = 0; i < number; i++)
  {
    int j, index = 1;
    for(j = 1; j < i_snapshots + 1; j++)
    {
      if(!strncmp(p_snapshots[j].psz_name, psz_prefix, len))
      {
        if(strcmp(p_snapshots[j].psz_name, p_snapshots[index].psz_name) < 0)
          index = j;
      }
    }

    /* We found a snapshot to remove */
    psz_name = p_snapshots[index].psz_name;
    p_vfs = p_snapshots[index].p_vfs;

    if(index != i_snapshots)
        memmove(&p_snapshots[index], &p_snapshots[index+1], (i_snapshots - index) * sizeof(*p_snapshots));

    i_snapshots--;
    if( ( p_snapshots = realloc(p_snapshots, (i_snapshots + 1) * sizeof(*p_snapshots)) ) == NULL )
     {
       LogMajor(COMPONENT_FSAL, "SNAPSHOTS: recan't allocate memory... exiting");
       free( p_snapshots ) ;
       exit( 1 ) ;
       return ;
     }

    /* Really remove the snapshot */
    LogDebug(COMPONENT_FSAL, "SNAPSHOTS: removing the snapshot '%s' (%d/%d)", psz_name, i + 1, number);
    libzfswrap_umount(p_vfs, 1);
    libzfswrap_zfs_snapshot_destroy(p_zhd, "tank", psz_name, &psz_error);
  }
}

/* Thread that handle snapshots */
static void *SnapshotThread(void *data)
{
  zfsfs_specific_initinfo_t *fs_info = (zfsfs_specific_initinfo_t*)data;

  while(1)
  {
    /* Compute the time of the next snapshot */
    time_t time_now = time(NULL);
    struct tm *now = gmtime(&time_now);
    unsigned int i_wait;
    char *psz_name;

    if(now->tm_min >= fs_info->snap_hourly_time)
      i_wait = 60 - (now->tm_min - fs_info->snap_hourly_time);
    else
      i_wait = fs_info->snap_hourly_time - now->tm_min;

    /* Sleep for the given time */
    LogDebug(COMPONENT_FSAL, "SNAPSHOTS: next snapshot in %u minutes", i_wait);
    sleep(i_wait*60);

    /* Create a snapshot */
    libzfswrap_vfs_t *p_new = TakeSnapshotAndMount(fs_info->psz_zpool,
                                                   fs_info->psz_snap_hourly_prefix,
                                                   &psz_name);

    /* Add the snapshot to the list of snapshots */
    ZFSFSAL_VFS_WRLock();
    AddSnapshot(p_new, psz_name);

    /* Remove spurious snapshots */
    int i_hourly_snap = CountSnapshot(fs_info->psz_snap_hourly_prefix);
    if(i_hourly_snap > fs_info->snap_hourly_number)
      RemoveOldSnapshots(fs_info->psz_snap_hourly_prefix, i_hourly_snap - fs_info->snap_hourly_number);

    ZFSFSAL_VFS_Unlock();
  }

  return NULL;
}
