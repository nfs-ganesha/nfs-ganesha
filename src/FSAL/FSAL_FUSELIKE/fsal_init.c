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
#include "namespace.h"

/** Initializes filesystem, security management... */
static int FS_Specific_Init(fusefs_specific_initinfo_t * fs_init_info)
{

  int rc = 0;
  fsal_op_context_t ctx;
  struct stat stbuf;
  unsigned int root_gen;

  struct ganefuse_conn_info conn = {
    .proto_major = 0,           /* XXX is another value necessary ? */
    .proto_minor = 0,           /* XXX is another value necessary ? */
    .async_read = 0,
    .max_write = global_fs_info.maxwrite,
    .max_readahead = global_fs_info.maxread,
    .reserved = {0}
  };

  /* set filesystems operations and opaque info */
  p_fs_ops = fs_init_info->fs_ops;
  fs_user_data = fs_init_info->user_data;

  /* create a "fake" context in case init use it */

  fs_private_data = NULL;

  FUSEFSAL_InitClientContext(&ctx);
  fsal_set_thread_context(&ctx);

  /* call filesystem's init */

  if(p_fs_ops->init)
    {
      fs_private_data = p_fs_ops->init(&conn);
    }

  /* reset context now fs_private_data is known */

  FUSEFSAL_InitClientContext(&ctx);
  fsal_set_thread_context(&ctx);

  /* initialize namespace by getting root inode number
   * getattr is mandatory !
   */

  if(!p_fs_ops->getattr)
    return -ENOSYS;

  rc = p_fs_ops->getattr("/", &stbuf);

  if(rc)
    {
      LogCrit(COMPONENT_FSAL,
              "FSAL INIT: Could not call initial 'getattr' on filesystem root");

      return rc;
    }

  /* generation based on ctime for avoiding stale handles */
  root_gen = stbuf.st_ctime;

  if(stbuf.st_ino == 0)
    {
      /* filesystem does not provide inodes ! */
      LogCrit(COMPONENT_FSAL, "WARNING in lookup: filesystem does not provide inode numbers");
      /* root will have inode nbr 1 */
      stbuf.st_ino = 1;
    }

  /* initialize namespace */
  rc = NamespaceInit(stbuf.st_ino, stbuf.st_dev, &root_gen);

  return rc;

}

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
fsal_status_t FUSEFSAL_Init(fsal_parameter_t * init_info        /* IN */
    )
{

  fsal_status_t status;
  int rc;

  /* sanity check.  */

  if(!init_info)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);

  /* proceeds FSAL internal status initialization */

  status = fsal_internal_init_global(&(init_info->fsal_info),
                                     &(init_info->fs_common_info));

  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_Init);

  /* initialize filesystem stuff */

  if(rc = FS_Specific_Init((fusefs_specific_initinfo_t *) &init_info->fs_specific_info))
    Return(ERR_FSAL_BAD_INIT, -rc, INDEX_FSAL_Init);

  /* Everything went OK. */

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_Init);

}
