/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_internal.c
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.25 $
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 *
 */
#define FSAL_INTERNAL_C

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include  "fsal.h"
#include "fsal_internal.h"

#include <pthread.h>

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
fsal_staticfsinfo_t global_fs_info;

/* you can define here your supported attributes
 * if your filesystem is "homogenous".
 */
#define YOUR_SUPPORTED_ATTRIBUTES (                                       \
          FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE     | FSAL_ATTR_SIZE      | \
          FSAL_ATTR_FSID     | FSAL_ATTR_ACL      | FSAL_ATTR_FILEID    | \
          FSAL_ATTR_MODE     | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER     | \
          FSAL_ATTR_GROUP    | FSAL_ATTR_ATIME    | FSAL_ATTR_CREATION  | \
          FSAL_ATTR_CTIME    | FSAL_ATTR_MTIME    | FSAL_ATTR_SPACEUSED | \
          FSAL_ATTR_MOUNTFILEID | FSAL_ATTR_CHGTIME  )

/* filesystem info for your filesystem */
static fsal_staticfsinfo_t default_hpss_info = {
  0xFFFFFFFFFFFFFFFFLL,         /* max file size (64bits) */
  1024,                         /* max links for an object of your filesystem */
  FSAL_MAX_NAME_LEN,            /* max filename */
  FSAL_MAX_PATH_LEN,            /* min filename */
  true,                         /* no_trunc */
  true,                         /* chown restricted */
  false,                        /* case insensitivity */
  true,                         /* case preserving */
  FSAL_EXPTYPE_VOLATILE,        /* FH expire type */
  true,                         /* hard link support */
  true,                         /* symlink support */
  false,                        /* lock support */
  false,                        /* lock owners */
  false,                        /* async blocking locks */
  true,                         /* named attributes */
  true,                         /* handles are unique and persistent */
  {10, 0},                      /* Duration of lease at FS in seconds */
  FSAL_ACLSUPPORT_ALLOW,        /* ACL support */
  true,                         /* can change times */
  true,                         /* homogenous */
  YOUR_SUPPORTED_ATTRIBUTES,    /* supported attributes */
  (1024 * 1024),                /* maxread size */
  (1024 * 1024),                /* maxwrite size */
  0,                            /* default umask */
  0,                            /* don't allow cross fileset export path */
  0400,                         /* default access rights for xattrs: root=RW, owner=R */
  0,                            /* default access check support in FSAL */
  0,                            /* default share reservation support in FSAL */
  0                             /* default share reservation support with open owners in FSAL */
};

/* filesystem operations */
struct ganefuse_operations *p_fs_ops = NULL;

/* filesystem data */
void *fs_user_data = NULL;
void *fs_private_data = NULL;

/* variables for limiting the calls to the filesystem */
static int limit_calls = false;

/* threads keys for stats */
static pthread_key_t key_stats;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

/* init keys */
static void init_keys(void)
{
  if(pthread_key_create(&key_stats, NULL) == -1)
    LogError(COMPONENT_FSAL, ERR_SYS, ERR_PTHREAD_KEY_CREATE, errno);

  return;
}                               /* init_keys */

/**
 * fsal_increment_nbcall:
 * Updates fonction call statistics.
 *
 * \param function_index (input):
 *        Index of the function whom number of call is to be incremented.
 * \param status (input):
 *        Status the function returned.
 *
 * \return Nothing.
 */
void fsal_increment_nbcall(int function_index, fsal_status_t status)
{

  fsal_statistics_t *bythread_stat = NULL;

  /* verify index */

  if(function_index >= FSAL_NB_FUNC)
    return;

  /* first, we init the keys if this is the first time */

  if(pthread_once(&once_key, init_keys) != 0)
    {
      LogError(COMPONENT_FSAL, ERR_SYS, ERR_PTHREAD_ONCE, errno);
      return;
    }

  /* we get the specific value */

  bythread_stat = (fsal_statistics_t *) pthread_getspecific(key_stats);

  /* we allocate stats if this is the first time */

  if(bythread_stat == NULL)
    {
      int i;

      bythread_stat = gsh_malloc(sizeof(fsal_statistics_t));

      if(bythread_stat == NULL)
        {
          LogError(COMPONENT_FSAL, ERR_SYS, ERR_MALLOC, ENOMEM);
        }

      /* inits the struct */

      for(i = 0; i < FSAL_NB_FUNC; i++)
        {
          bythread_stat->func_stats.nb_call[i] = 0;
          bythread_stat->func_stats.nb_success[i] = 0;
          bythread_stat->func_stats.nb_err_retryable[i] = 0;
          bythread_stat->func_stats.nb_err_unrecover[i] = 0;
        }

      /* set the specific value */
      pthread_setspecific(key_stats, (void *)bythread_stat);

    }

  /* we increment the values */

  if(bythread_stat)
    {
      bythread_stat->func_stats.nb_call[function_index]++;

      if(!FSAL_IS_ERROR(status))
        bythread_stat->func_stats.nb_success[function_index]++;
      else if(status.major == ERR_FSAL_DELAY )
        bythread_stat->func_stats.nb_err_retryable[function_index]++;
      else
        bythread_stat->func_stats.nb_err_unrecover[function_index]++;
    }

  return;
}

/**
 * fsal_internal_getstats:
 * (For internal use in the FSAL).
 * Retrieve call statistics for current thread.
 *
 * \param output_stats (output):
 *        Pointer to the call statistics structure.
 *
 * \return Nothing.
 */
void fsal_internal_getstats(fsal_statistics_t * output_stats)
{

  fsal_statistics_t *bythread_stat = NULL;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      LogError(COMPONENT_FSAL, ERR_SYS, ERR_PTHREAD_ONCE, errno);
      return;
    }

  /* we get the specific value */
  bythread_stat = (fsal_statistics_t *) pthread_getspecific(key_stats);

  /* we allocate stats if this is the first time */
  if(bythread_stat == NULL)
    {
      int i;

      if((bythread_stat =
          gsh_malloc(sizeof(fsal_statistics_t))) == NULL)
        LogError(COMPONENT_FSAL, ERR_SYS, ERR_MALLOC, ENOMEM);

      /* inits the struct */
      for(i = 0; i < FSAL_NB_FUNC; i++)
        {
          bythread_stat->func_stats.nb_call[i] = 0;
          bythread_stat->func_stats.nb_success[i] = 0;
          bythread_stat->func_stats.nb_err_retryable[i] = 0;
          bythread_stat->func_stats.nb_err_unrecover[i] = 0;
        }

      /* set the specific value */
      pthread_setspecific(key_stats, (void *)bythread_stat);

    }

  if(output_stats)
    (*output_stats) = (*bythread_stat);

  return;

}

/*
 *  This function initializes shared variables of the fsal.
 */
fsal_status_t fsal_internal_init_global(fsal_init_info_t * fsal_info,
                                        fs_common_initinfo_t * fs_common_info)
{

  /* sanity check */
  if(!fsal_info || !fs_common_info)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* inits FS call semaphore */
  if(fsal_info->max_fs_calls > 0)
    {
      int rc;

      limit_calls = true;

      rc = semaphore_init(&sem_fs_calls, fsal_info->max_fs_calls);

      if(rc != 0)
        ReturnCode(ERR_FSAL_SERVERFAULT, rc);

      LogDebug(COMPONENT_FSAL,
               "FSAL INIT: Max simultaneous calls to filesystem is limited to %u.",
               fsal_info->max_fs_calls);

    }
  else
    {
      LogDebug(COMPONENT_FSAL,
               "FSAL INIT: Max simultaneous calls to filesystem is unlimited.");
    }

  /* setting default values. */
  global_fs_info = default_hpss_info;

  /* Analyzing fs_common_info struct */

  if((fs_common_info->behaviors.maxfilesize != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.maxlink != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.maxnamelen != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.maxpathlen != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.no_trunc != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.case_insensitive != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.case_preserving != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.named_attr != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.lease_time != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.supported_attrs != FSAL_INIT_FS_DEFAULT) ||
     (fs_common_info->behaviors.homogenous != FSAL_INIT_FS_DEFAULT))
    ReturnCode(ERR_FSAL_NOTSUPP, 0);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, symlink_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, link_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support_owner);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, lock_support_async_block);
  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, cansettime);

  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxread);
  SET_INTEGER_PARAM(global_fs_info, fs_common_info, maxwrite);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, umask);

  SET_BOOLEAN_PARAM(global_fs_info, fs_common_info, auth_exportpath_xdev);

  SET_BITMAP_PARAM(global_fs_info, fs_common_info, xattr_access_rights);

  display_fsinfo(&global_fs_info);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 * fsal_do_log:
 * Indicates if an FSAL error has to be traced
 * into its log file in the NIV_EVENT level.
 * (in the other cases, return codes are only logged
 * in the NIV_FULL_DEBUG logging lovel).
 *
 * \param status(input): The fsal status that is to be tested.
 *
 * \return - true if the error is to be traced.
 *         - false if the error must not be traced except
 *          in NIV_FULL_DEBUG level.
 */
bool fsal_do_log(fsal_status_t status)
{

  switch (status.major)
    {

      /* here are the code, we want to trace */
    case ERR_FSAL_DELAY:
    case ERR_FSAL_PERM:
    case ERR_FSAL_IO:
    case ERR_FSAL_NXIO:
    case ERR_FSAL_NOT_OPENED:
    case ERR_FSAL_NOMEM:
    case ERR_FSAL_FAULT:
    case ERR_FSAL_XDEV:
    case ERR_FSAL_INVAL:
    case ERR_FSAL_FBIG:
    case ERR_FSAL_NOSPC:
    case ERR_FSAL_MLINK:
    case ERR_FSAL_NAMETOOLONG:
    case ERR_FSAL_SEC:
    case ERR_FSAL_SERVERFAULT:
      return true;

    default:
      return false;
    }

}

/* pthread key for fuse context */
static pthread_key_t key_ctx;
static pthread_once_t once_ctx = PTHREAD_ONCE_INIT;

/* init keys */
static void init_ctx_key(void)
{
  if(pthread_key_create(&key_ctx, NULL) == -1)
    LogError(COMPONENT_FSAL, ERR_SYS, ERR_PTHREAD_KEY_CREATE, errno);

  return;
}

/**
 * This function sets the current context for a filesystem operation,
 * so it can be retrieved with fuse_get_context().
 * The structure pointed by p_ctx must stay allocated and kept unchanged
 * during the FS call.
 */
int fsal_set_thread_context(fsal_op_context_t * p_ctx)
{
  /* first, we init the key if this is the first time */
  if(pthread_once(&once_ctx, init_ctx_key) != 0)
    {
      int rc = errno;
      LogError(COMPONENT_FSAL, ERR_SYS, ERR_PTHREAD_ONCE, rc);
      return rc;
    }

  return pthread_setspecific(key_ctx, (void *)p_ctx);
}

/**
 * This function retrieves the last context associated to a thread.
 */
fsal_op_context_t *fsal_get_thread_context()
{
  return pthread_getspecific(key_ctx);
}
