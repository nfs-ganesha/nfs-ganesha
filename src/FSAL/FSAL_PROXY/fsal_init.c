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

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <string.h>
#include <signal.h>

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_common.h"

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#else
#include <rpc/rpc.h>
#endif

#ifdef _HANDLE_MAPPING
#include "handle_mapping/handle_mapping.h"
#endif

void *FSAL_proxy_clientid_renewer_thread(void *);

pthread_t thrid_clientid_renewer;

/* A variable to be seen (for read) in other files */
proxyfs_specific_initinfo_t global_fsal_proxy_specific_info;

/** Initializes filesystem, security management... */
static int FS_Specific_Init(proxyfs_specific_initinfo_t * fs_init_info)
{
  sigset_t sigset_in;
  sigset_t sigset_out;
  pthread_attr_t attr_thr;
  int rc;

  memcpy(&global_fsal_proxy_specific_info, fs_init_info,
         sizeof(proxyfs_specific_initinfo_t));

  /* No SIG_PIPE. This is mandatory for reconnection logic. If sigpipe is not blocked, 
   * then when a server crashes the next clnt_call will cause un unhandled sigpipe that
   * will crash the server */
  sigemptyset(&sigset_in);
  sigaddset(&sigset_in, SIGPIPE);

  if(sigprocmask(SIG_BLOCK, &sigset_in, &sigset_out) == -1)
    return -1;

#ifdef _HANDLE_MAPPING

  /* Initialize NFSv2/3 handle mapping management */

  if(fs_init_info->enable_handle_mapping)
    {
      int rc;
      handle_map_param_t param;

      strcpy(param.databases_directory, fs_init_info->hdlmap_dbdir);
      strcpy(param.temp_directory, fs_init_info->hdlmap_tmpdir);
      param.database_count = fs_init_info->hdlmap_dbcount;
      param.hashtable_size = fs_init_info->hdlmap_hashsize;
      param.nb_handles_prealloc = fs_init_info->hdlmap_nb_entry_prealloc;
      param.nb_db_op_prealloc = fs_init_info->hdlmap_nb_db_op_prealloc;
      param.synchronous_insert = FALSE;

      rc = HandleMap_Init(&param);

      if(rc)
        return rc;
    }
#endif
  /* Init the thread in charge of renewing the client id */
  /* Init for thread parameter (mostly for scheduling) */
  pthread_attr_init(&attr_thr);

  pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE);

  if((rc = pthread_create(&thrid_clientid_renewer,
                          &attr_thr,
                          FSAL_proxy_clientid_renewer_thread, (void *)NULL) != 0))
    {
      LogError(COMPONENT_FSAL, ERR_SYS, ERR_PTHREAD_CREATE, rc);
      exit(1);
    }

  return 0;
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
fsal_status_t PROXYFSAL_Init(fsal_parameter_t * init_info       /* IN */
    )
{

  fsal_status_t status;
  int rc;

  /* sanity check.  */

  if(!init_info)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);

  /* >> You can check args bellow << */

  /* proceeds FSAL internal status initialization */

  status = fsal_internal_init_global(&(init_info->fsal_info),
                                     &(init_info->fs_common_info));

  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_Init);

  /* >> You can also initialize some filesystem stuff << */

  if(rc = FS_Specific_Init((proxyfs_specific_initinfo_t *) &init_info->fs_specific_info))
    Return(ERR_FSAL_BAD_INIT, -rc, INDEX_FSAL_Init);

  /* Everything went OK. */

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_Init);

}

/* To be called before exiting */
fsal_status_t PROXYFSAL_terminate()
{
#ifdef _HANDLE_MAPPING
  int rc;

  if(global_fsal_proxy_specific_info.enable_handle_mapping)
    {
      rc = HandleMap_Flush();

      if(rc)
        ReturnCode(ERR_FSAL_SERVERFAULT, rc);
    }
#endif

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
