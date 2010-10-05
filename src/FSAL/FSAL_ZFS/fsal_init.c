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
#include "HashTable.h"

extern libzfswrap_vfs_t *p_vfs;

/* Macros for analysing parameters. */
#define SET_BITMAP_PARAM( api_cfg, p_init_info, _field )      \
    switch( (p_init_info)->behaviors._field ){                \
      case FSAL_INIT_FORCE_VALUE :                            \
        /* force the value in any case */                     \
        api_cfg._field = (p_init_info)->hpss_config._field;   \
        break;                                                \
      case FSAL_INIT_MAX_LIMIT :                              \
        /* remove the flags not specified by user (AND) */    \
        api_cfg._field &= (p_init_info)->hpss_config._field;  \
        break;                                                \
      case FSAL_INIT_MIN_LIMIT :                              \
        /* add the flags specified by user (OR) */            \
        api_cfg._field |= (p_init_info)->hpss_config._field;  \
        break;                                                \
    /* In the other cases, we keep the default value. */      \
    }                                                         \


#define SET_INTEGER_PARAM( api_cfg, p_init_info, _field )         \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
        /* force the value in any case */                         \
        api_cfg._field = (p_init_info)->hpss_config._field;       \
        break;                                                \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* check the higher limit */                                \
      if ( api_cfg._field > (p_init_info)->hpss_config._field )   \
        api_cfg._field = (p_init_info)->hpss_config._field ;      \
        break;                                                \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* check the lower limit */                                 \
      if ( api_cfg._field < (p_init_info)->hpss_config._field )   \
        api_cfg._field = (p_init_info)->hpss_config._field ;      \
        break;                                                \
    /* In the other cases, we keep the default value. */          \
    }                                                             \


#define SET_STRING_PARAM( api_cfg, p_init_info, _field )          \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
      /* force the value in any case */                           \
      strcpy(api_cfg._field,(p_init_info)->hpss_config._field);   \
      break;                                                \
    /* In the other cases, we keep the default value. */          \
    }                                                             \


/* functions for hashing/comparing lookup peers */
static unsigned long hash_idx(hash_parameter_t *p_conf, hash_buffer_t *p_key)
{
  return 0;
}

static unsigned long hash_rbt(hash_parameter_t *p_conf, hash_buffer_t *p_key)
{
  unsigned int i;
  unsigned long hash;
  const char *psz_key = (const char*)p_key->pdata;

  hash = 1;

  for(; *psz_key; psz_key++)
    hash = ((hash << 5) - hash + (unsigned long)(*psz_key));

  return hash;
}

static int cmp_key(hash_buffer_t * p_key1, hash_buffer_t * p_key2)
{
  const char *psz_key1 = (const char *) p_key1->pdata;
  const char *psz_key2 = (const char *) p_key2->pdata;

  return strncmp(psz_key1, psz_key2, FSAL_MAX_NAME_LEN);
}

/* display functions */

static int print_key(hash_buffer_t * p_val, char *outbuff)
{
  return sprintf(outbuff, "%s", p_val->pdata);
}

static int print_value(hash_buffer_t * p_val, char *outbuff)
{
  return sprintf(outbuff, "%p", p_val->pdata);
}


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

  fsal_status_t status;

  /* sanity check.  */

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
    LogCrit(COMPONENT_FSAL,"FSAL INIT: *** ERROR: Unable to initialize the libzfswrap library.");
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  p_vfs = libzfswrap_mount(init_info->fs_specific_info.psz_zpool, "/tank", "");
  if(!p_vfs)
  {
    LogMajor(COMPONENT_FSAL,"FSAL INIT: *** ERROR: Unable to mount the file system.");
    libzfswrap_exit(p_zhd);
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  /* Mount every snapshots and store them in a hashtable */
  static hash_parameter_t hash_config = {
                .index_size = 1,
                .alphabet_length = 36, /*TODO find the right value here */
                .nb_node_prealloc = 1024,
                .hash_func_key = hash_idx,
                .hash_func_rbt = hash_rbt,
                .compare_key = cmp_key,
                .key_to_str = print_key,
                .val_to_str = print_value
        };

  p_snapshots = HashTable_Init(hash_config);

  libzfswrap_vfs_t *p_snap_vfs = libzfswrap_mount("tank@bla", "/tank/bla", "");
  if(!p_snap_vfs)
  {
    LogMajor(COMPONENT_FSAL, "FSAL INIT: *** ERROR: Unable to mount the snapshot");
    libzfswrap_umount(p_vfs, 1);
    libzfswrap_exit(p_zhd);
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  hash_buffer_t key = { .pdata = "bla", .len = sizeof("bla") };
  hash_buffer_t val = { .pdata = p_snap_vfs, .len = sizeof(p_snap_vfs) };
  HashTable_Set(p_snapshots, &key, &val);

/*
  p_snap_vfs = libzfswrap_mount("tank@blop", "/tank/blop", "");
  key.pdata = "blop"; key.len = sizeof("blop");
  val.pdata = p_snap_vfs; val.len = sizeof(p_snap_vfs);
  HashTable_Set(p_snapshots, &key, &val);
*/

  /* Everything went OK. */

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_Init);

}

/* To be called before exiting */
fsal_status_t ZFSFSAL_terminate()
{
  /* Find a way to walk the list of snapshots */
  hash_buffer_t key, value;
  key.pdata = "bla"; key.len = sizeof("bla");
  HashTable_Get(p_snapshots, &key, &value);
  libzfswrap_umount(value.pdata, 1);

//  key.pdata = "blop"; key.len = sizeof("blop");
//  HashTable_Get(p_snapshots, &key, &value);
//  libzfswrap_umount(value.pdata, 1);

  libzfswrap_umount(p_vfs, 1);
  libzfswrap_exit(p_zhd);
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
