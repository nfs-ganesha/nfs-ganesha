/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_unlink.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.9 $
 * \brief   object removing function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * FSAL_unlink:
 * Remove a filesystem object .
 *
 * \param parentdir_handle (input):
 *        Handle of the parent directory of the object to be deleted.
 * \param p_object_name (input):
 *        Name of the object to be removed.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param parentdir_attributes (optionnal input/output): 
 *        Post operation attributes of the parent directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */

fsal_status_t POSIXFSAL_unlink(fsal_handle_t * parent_directory_handle,  /* IN */
                               fsal_name_t * p_object_name,     /* IN */
                               fsal_op_context_t * context,      /* IN */
                               fsal_attrib_list_t * p_parent_directory_attributes       /* [IN/OUT ] */
    )
{
  posixfsal_handle_t * p_parent_directory_handle
    = (posixfsal_handle_t *) parent_directory_handle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  fsal_status_t status;
  fsal_posixdb_status_t statusdb;
  int rc, errsv;
  struct stat buffstat, buffstat_parent;
  fsal_path_t fsalpath;
  fsal_posixdb_fileinfo_t info;

  /* sanity checks. */
  if(!p_parent_directory_handle || !p_context || !p_object_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlink);

  /* check credential */
  /* need to be able to 'read' the parent directory & to delete it */

  /* build the destination path */
  status =
      fsal_internal_getPathFromHandle(p_context, p_parent_directory_handle, 1, &fsalpath,
                                      &buffstat_parent);
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_unlink);

  status = fsal_internal_appendFSALNameToFSALPath(&fsalpath, p_object_name);
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_unlink);

  /* 
   *  Action depends on the object type to be deleted.
   */

  TakeTokenFSCall();
  rc = lstat(fsalpath.path, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();
  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_unlink);

  if(FSAL_IS_ERROR(status = fsal_internal_posix2posixdb_fileinfo(&buffstat, &info)))
    Return(status.major, status.minor, INDEX_FSAL_unlink);

  /**************************************************************
   * Lock the handle entry related to this file in the database *
   **************************************************************/

  statusdb = fsal_posixdb_lockHandleForUpdate(p_context->p_conn, &info);
  if(FSAL_IS_ERROR(status = posixdb2fsal_error(statusdb)))
    {
      fsal_posixdb_cancelHandleLock(p_context->p_conn);
      Return(status.major, status.minor, INDEX_FSAL_unlink);
    }

  /****************
   * CHECK ACCESS *
   ****************/
  if((buffstat_parent.st_mode & S_ISVTX)        /* Sticky bit on the directory => the user who wants to delete the file must own it or its parent dir */
     && buffstat_parent.st_uid != p_context->credential.user
     && buffstat.st_uid != p_context->credential.user && p_context->credential.user != 0)
    {
      fsal_posixdb_cancelHandleLock(p_context->p_conn);
      Return(ERR_FSAL_ACCESS, 0, INDEX_FSAL_unlink);
    }

  if(FSAL_IS_ERROR
     (status =
      fsal_internal_testAccess(p_context, FSAL_W_OK | FSAL_X_OK, &buffstat_parent, NULL)))
    {
      fsal_posixdb_cancelHandleLock(p_context->p_conn);
      Return(status.major, status.minor, INDEX_FSAL_unlink);
    }

  /******************************
   * DELETE FROM THE FILESYSTEM *
   ******************************/
  TakeTokenFSCall();
  /* If the object to delete is a directory, use 'rmdir' to delete the object, else use 'unlink' */
  rc = (S_ISDIR(buffstat.st_mode)) ? rmdir(fsalpath.path) : unlink(fsalpath.path);
  errsv = errno;
  ReleaseTokenFSCall();
  if(rc)
    {
      fsal_posixdb_cancelHandleLock(p_context->p_conn);
      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_unlink);
    }

  /****************************
   * DELETE FROM THE DATABASE *
   ****************************/

  /* We have to delete the path from the database, and the handle if the object was a directory or has no more hardlink */
  statusdb =
      fsal_posixdb_delete(p_context->p_conn, p_parent_directory_handle, p_object_name,
                          &info);
  /* After this operation, there's no need to 'fsal_posixdb_cancelHandleLock' because the transaction is ended */
  switch (statusdb.major)
    {
    case ERR_FSAL_POSIXDB_NOERR:
    case ERR_FSAL_POSIXDB_NOENT:
      /* nothing to do */
      break;
    default:
      if(FSAL_IS_ERROR(status = posixdb2fsal_error(statusdb)))
        Return(status.major, status.minor, INDEX_FSAL_unlink);
    }

  /***********************
   * FILL THE ATTRIBUTES *
   ***********************/

  if(p_parent_directory_attributes)
    {
      status = posix2fsal_attributes(&buffstat_parent, p_parent_directory_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_parent_directory_attributes->asked_attributes);
          FSAL_SET_MASK(p_parent_directory_attributes->asked_attributes,
                        FSAL_ATTR_RDATTR_ERR);
          Return(status.major, status.minor, INDEX_FSAL_opendir);
        }
    }
  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlink);

}
