/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_init.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/03/04 08:37:28 $
 * \version $Revision: 1.4 $
 * \brief   Initialization functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convertions.h"
#include <string.h>

static int CreateInitDir(ghostfs_dir_def_t * p_dir)
{
  char buff[FSAL_MAX_PATH_LEN];
  char *cur_str;
  GHOSTFS_handle_t handle, next_handle;

  strcpy(buff, p_dir->path);

  /* tokenizes the dir path */
  if(buff[0] != '/')
    return -1;

  if(GHOSTFS_GetRoot(&handle))
    return -1;

  cur_str = buff + 1;

  cur_str = strtok(cur_str, "/");
  if(cur_str == NULL)
    return -1;

  do
    {
      int rc;
      rc = GHOSTFS_Lookup(handle, cur_str, &next_handle);

      if(rc == ERR_GHOSTFS_NOENT)
        {
          LogEvent(COMPONENT_FSAL,"FSAL: Creating predefined directory '%s'",
                   cur_str);

          /* create the entry */

          rc = GHOSTFS_MkDir(handle, cur_str,
                             p_dir->dir_owner,
                             p_dir->dir_group,
                             fsal2ghost_mode(p_dir->dir_mode), &next_handle, NULL);
        }

      if(rc != 0)
        return -1;

      handle = next_handle;
      cur_str = strtok(NULL, "/");

    }
  while((cur_str != NULL) && (cur_str[0] != '\0'));

  return 0;

}

/**
 * FSAL_Init : Initializes the FileSystem Abstraction Layer.
 *
 * \param init_info (input, fsal_parameter_t *) :
 *        Pointer to a structure that contains
 *        all initialization parameters for the FSAL.
 *        Specifically it contains settings about
 *        the filesystem on which the FSAL is based,
 *        security settings, logging policy and outputs,
 *        and other general FSAL options….
 *
 * \return Major error codes :
 *         ERR_FSAL_NO_ERROR     (initialisation OK)
 *         ERR_FSAL_FAULT        (init_info pointer is null)
 *         ERR_FSAL_SERVERFAULT  (misc FSAL error)
 *         ERR_FSAL_ALREADY_INIT (The FS is already initialized)
 *         ERR_FSAL_BAD_INIT     (FS specific init error,
 *                                minor error code gives the reason
 *                                for this error.)
 */
fsal_status_t FSAL_Init(fsal_parameter_t * init_info    /* IN */
    )
{

  fsal_status_t status;
  int rc;

  GHOSTFS_parameter_t param;

  ghostfs_dir_def_t *p_cur;

  /* For logging */
  SetFuncID(INDEX_FSAL_Init);

  /* sanity check.  */
  if(!init_info)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);

  param.root_mode = fsal2ghost_mode(init_info->fs_specific_info.root_mode);
  param.root_owner = init_info->fs_specific_info.root_owner;
  param.root_group = init_info->fs_specific_info.root_group;
  param.dot_dot_root_eq_root = init_info->fs_specific_info.dot_dot_root_eq_root;
  param.root_access = init_info->fs_specific_info.root_access;

  LogFullDebug(COMPONENT_FSAL, "init_info->fs_specific_info.root_owner = %d\n",
         init_info->fs_specific_info.root_owner);
  LogFullDebug(COMPONENT_FSAL, "param.root_owner = %d\n", param.root_owner);

  rc = GHOSTFS_Init(param);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_Init);

  /* proceeds FSAL initialization */
  switch ((status =
           fsal_internal_init_global(&(init_info->fsal_info),
                                     &(init_info->fs_common_info))).major)
    {
    case ERR_FSAL_NO_ERROR:
      /*continue */
      break;

      /* change the FAULT error to appears as an internal error.
       * indeed, parameters should be null. */
    case ERR_FSAL_FAULT:
      Return(ERR_FSAL_SERVERFAULT, ERR_FSAL_FAULT, INDEX_FSAL_Init);
      break;

    default:
      Return(status.major, status.minor, INDEX_FSAL_Init);
    }

  /* now, create predefined directories, according to parameters */

  for(p_cur = init_info->fs_specific_info.dir_list; p_cur != NULL; p_cur = p_cur->next)
    {
      rc = CreateInitDir(p_cur);
      if(rc)
        LogCrit(COMPONENT_FSAL,
                "FSAL: /!\\ WARNING /!\\ Could not create init dir '%s'",
                p_cur->path);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_Init);
}

/* To be called before exiting */
fsal_status_t FSAL_terminate()
{
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
