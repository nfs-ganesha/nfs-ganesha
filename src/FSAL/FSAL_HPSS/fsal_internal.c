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
#include "abstract_mem.h"

/* credential lifetime (1h) */
fsal_uint_t CredentialLifetime = 3600;

/* Behavior for inconsistent direntries */
fsal_uint_t ReturnInconsistentDirent = false;

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
fsal_staticfsinfo_t global_fs_info;

/* filesystem info for HPSS */
static fsal_staticfsinfo_t default_hpss_info = {
  0xFFFFFFFFFFFFFFFFLL,         /* max file size (64bits) */
  NS_MAX_HARD_LINK_VALUE,       /* max links */
  FSAL_MAX_NAME_LEN,            /* max filename */
  FSAL_MAX_PATH_LEN,            /* min filename */
  true,                         /* no_trunc */
  true,                         /* chown restricted */
  false,                        /* case insensitivity */
  true,                         /* case preserving */
  FSAL_EXPTYPE_PERSISTENT,      /* FH expire type */
  true,                         /* hard link support */
  true,                         /* symlink support */
  false,                        /* lock management */
  false,                        /* lock owners */
  false,                        /* async blocking locks */
  true,                         /* named attributes are supported */
  true,                         /* handles are unique and persistent */
  {10, 0},                      /* Duration of lease at FS in seconds */
  FSAL_ACLSUPPORT_ALLOW,        /* ACL support */
  true,                         /* can change times */
  true,                         /* homogenous */
  HPSS_SUPPORTED_ATTRIBUTES,    /* supported attributes */
  (1024 * 1024),                /* maxread size */
  (1024 * 1024),                /* maxwrite size */
  0,                            /* default umask */
  0,                            /* don't allow cross fileset export path */
  0400,                         /* default access rights for xattrs: root=RW, owner=R */
  0,                            /* default access check support in FSAL */
  0,                            /* default share reservation support in FSAL */
  0                             /* default share reservation support with open owners in FSAL */
};

/*
 *  Log Descriptor
 */

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
      else if(fsal_is_retryable(status))
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
          gsh_mem(sizeof(fsal_statistics_t))) == NULL)
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

/**
 * Set credential lifetime.
 * (For internal use in the FSAL).
 * Set the period for thread's credential renewal.
 *
 * \param lifetime_in (input):
 *        The period for thread's credential renewal.
 *
 * \return Nothing.
 */
void fsal_internal_SetCredentialLifetime(fsal_uint_t lifetime_in)
{
  CredentialLifetime = lifetime_in;
}

/**
 * Set behavior when detecting a MD inconsistency in readdir.
 * (For internal use in the FSAL).
 *
 * \param lifetime_in (input):
 *        The period for thread's credential renewal.
 *
 * \return Nothing.
 */
void fsal_internal_SetReturnInconsistentDirent(fsal_uint_t bool_in)
{
  ReturnInconsistentDirent = bool_in;
}

#define SET_INTEGER_PARAM( cfg, p_init_info, _field )             \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
      /* force the value in any case */                           \
      cfg._field = (p_init_info)->values._field;                  \
      break;                                                      \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* check the higher limit */                                \
      if ( cfg._field > (p_init_info)->values._field )            \
        cfg._field = (p_init_info)->values._field ;               \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* check the lower limit */                                 \
      if ( cfg._field < (p_init_info)->values._field )            \
        cfg._field = (p_init_info)->values._field ;               \
      break;                                                      \
    /* In the other cases, we keep the default value. */          \
    }

#define SET_BITMAP_PARAM( cfg, p_init_info, _field )              \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
        /* force the value in any case */                         \
        cfg._field = (p_init_info)->values._field;                \
        break;                                                    \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* proceed a bit AND */                                     \
      cfg._field &= (p_init_info)->values._field ;                \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* proceed a bit OR */                                      \
      cfg._field |= (p_init_info)->values._field ;                \
      break;                                                      \
    /* In the other cases, we keep the default value. */          \
    }

#define SET_BOOLEAN_PARAM( cfg, p_init_info, _field )             \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
        /* force the value in any case */                         \
        cfg._field = (p_init_info)->values._field;                \
        break;                                                    \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* proceed a boolean AND */                                 \
      cfg._field = cfg._field && (p_init_info)->values._field ;   \
      break;                                                      \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* proceed a boolean OR */                                  \
      cfg._field = cfg._field && (p_init_info)->values._field ;   \
      break;                                                      \
    /* In the other cases, we keep the default value. */          \
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

  LogDebug(COMPONENT_FSAL, "FileSystem info :");
  LogDebug(COMPONENT_FSAL, "  maxfilesize  = %llX    ",
           global_fs_info.maxfilesize);
  LogDebug(COMPONENT_FSAL, "  maxlink  = %lu   ", global_fs_info.maxlink);
  LogDebug(COMPONENT_FSAL, "  maxnamelen  = %lu  ",
           global_fs_info.maxnamelen);
  LogDebug(COMPONENT_FSAL, "  maxpathlen  = %lu  ",
           global_fs_info.maxpathlen);
  LogDebug(COMPONENT_FSAL, "  no_trunc  = %d ", global_fs_info.no_trunc);
  LogDebug(COMPONENT_FSAL, "  chown_restricted  = %d ",
           global_fs_info.chown_restricted);
  LogDebug(COMPONENT_FSAL, "  case_insensitive  = %d ",
           global_fs_info.case_insensitive);
  LogDebug(COMPONENT_FSAL, "  case_preserving  = %d ",
           global_fs_info.case_preserving);
  LogDebug(COMPONENT_FSAL, "  fh_expire_type  = %hu ",
           global_fs_info.fh_expire_type);
  LogDebug(COMPONENT_FSAL, "  link_support  = %d  ",
           global_fs_info.link_support);
  LogDebug(COMPONENT_FSAL, "  symlink_support  = %d  ",
           global_fs_info.symlink_support);
  LogDebug(COMPONENT_FSAL, "  lock_support  = %d  ",
           global_fs_info.lock_support);
  LogDebug(COMPONENT_FSAL, "  lock_support_owner  = %d  ",
           global_fs_info.lock_support_owner);
  LogDebug(COMPONENT_FSAL, "  lock_support_async_block  = %d  ",
           global_fs_info.lock_support_async_block);
  LogDebug(COMPONENT_FSAL, "  named_attr  = %d  ",
           global_fs_info.named_attr);
  LogDebug(COMPONENT_FSAL, "  unique_handles  = %d  ",
           global_fs_info.unique_handles);
  LogDebug(COMPONENT_FSAL, "  lease_time  = %u.%u     ",
           global_fs_info.lease_time.seconds,
           global_fs_info.lease_time.nseconds);
  LogDebug(COMPONENT_FSAL, "  acl_support  = %hu  ",
           global_fs_info.acl_support);
  LogDebug(COMPONENT_FSAL, "  cansettime  = %d  ",
           global_fs_info.cansettime);
  LogDebug(COMPONENT_FSAL, "  homogenous  = %d  ",
           global_fs_info.homogenous);
  LogDebug(COMPONENT_FSAL, "  supported_attrs  = %llX  ",
           global_fs_info.supported_attrs);
  LogDebug(COMPONENT_FSAL, "  maxread  = %llX     ",
           global_fs_info.maxread);
  LogDebug(COMPONENT_FSAL, "  maxwrite  = %llX     ",
           global_fs_info.maxwrite);
  LogDebug(COMPONENT_FSAL, "  umask  = %#o ", global_fs_info.umask);
  LogDebug(COMPONENT_FSAL, "  auth_exportpath_xdev  = %d  ",
           global_fs_info.auth_exportpath_xdev);
  LogDebug(COMPONENT_FSAL, "  xattr_access_rights = %#o ",
           global_fs_info.xattr_access_rights);

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
