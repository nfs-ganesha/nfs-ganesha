/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_rename.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.9 $
 * \brief   object renaming/moving function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * FSAL_rename:
 * Change name and/or parent dir of a filesystem object.
 *
 * \param old_parentdir_handle (input):
 *        Source parent directory of the object is to be moved/renamed.
 * \param p_old_name (input):
 *        Pointer to the current name of the object to be moved/renamed.
 * \param new_parentdir_handle (input):
 *        Target parent directory for the object.
 * \param p_new_name (input):
 *        Pointer to the new name for the object.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param src_dir_attributes (optionnal input/output): 
 *        Post operation attributes for the source directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 * \param tgt_dir_attributes (optionnal input/output): 
 *        Post operation attributes for the target directory.
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

fsal_status_t LUSTREFSAL_rename(fsal_handle_t * p_old_parentdir_handle,   /* IN */
                                fsal_name_t * p_old_name,       /* IN */
                                fsal_handle_t * p_new_parentdir_handle,   /* IN */
                                fsal_name_t * p_new_name,       /* IN */
                                fsal_op_context_t * p_context,    /* IN */
                                fsal_attrib_list_t * p_src_dir_attributes,      /* [ IN/OUT ] */
                                fsal_attrib_list_t * p_tgt_dir_attributes       /* [ IN/OUT ] */
    )
{

  int rc, errsv;
  fsal_status_t status;
  struct stat old_parent_buffstat, new_parent_buffstat, buffstat;
  fsal_path_t old_fsalpath, new_fsalpath;
  int src_equal_tgt = FALSE;

  /* sanity checks.
   * note : src/tgt_dir_attributes are optional.
   */
  if(!p_old_parentdir_handle || !p_new_parentdir_handle
     || !p_old_name || !p_new_name || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

  /* Get directory access path by fid */

  status = fsal_internal_Handle2FidPath(p_context, p_old_parentdir_handle, &old_fsalpath);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_rename);

  /* retrieve directory metadata for checking access rights */

  TakeTokenFSCall();
  rc = lstat(old_fsalpath.path, &old_parent_buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    {
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_rename);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);
    }

  /* optimisation : don't do the job twice if source dir = dest dir  */
  if(!LUSTREFSAL_handlecmp(p_old_parentdir_handle, p_new_parentdir_handle, &status))
    {
      FSAL_pathcpy(&new_fsalpath, &old_fsalpath);
      src_equal_tgt = TRUE;
      new_parent_buffstat = old_parent_buffstat;
    }
  else
    {
      status =
          fsal_internal_Handle2FidPath(p_context, p_new_parentdir_handle, &new_fsalpath);
      if(FSAL_IS_ERROR(status))
        ReturnStatus(status, INDEX_FSAL_rename);

      /* retrieve destination attrs */
      TakeTokenFSCall();
      rc = lstat(new_fsalpath.path, &new_parent_buffstat);
      errsv = errno;
      ReleaseTokenFSCall();

      if(rc)
        {
          if(errsv == ENOENT)
            Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_rename);
          else
            Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);
        }

    }

  /* check access rights */

  status =
      fsal_internal_testAccess(p_context, FSAL_W_OK | FSAL_X_OK, &old_parent_buffstat,
                               NULL);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_rename);

  if(!src_equal_tgt)
    {
      status =
          fsal_internal_testAccess(p_context, FSAL_W_OK | FSAL_X_OK, &new_parent_buffstat,
                                   NULL);
      if(FSAL_IS_ERROR(status))
        ReturnStatus(status, INDEX_FSAL_rename);
    }

  /* build file paths */

  status = fsal_internal_appendNameToPath(&old_fsalpath, p_old_name);
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_rename);
  status = fsal_internal_appendNameToPath(&new_fsalpath, p_new_name);
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_rename);

  TakeTokenFSCall();
  rc = lstat(old_fsalpath.path, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();
  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);

  /* Check sticky bits */

  /* Sticky bit on the source directory => the user who wants to delete the file must own it or its parent dir */
  if((old_parent_buffstat.st_mode & S_ISVTX)
     && old_parent_buffstat.st_uid != p_context->credential.user
     && buffstat.st_uid != p_context->credential.user && p_context->credential.user != 0)
    Return(ERR_FSAL_ACCESS, 0, INDEX_FSAL_rename);

  /* Sticky bit on the target directory => the user who wants to create the file must own it or its parent dir */
  if(new_parent_buffstat.st_mode & S_ISVTX)
    {
      TakeTokenFSCall();
      rc = lstat(new_fsalpath.path, &buffstat);
      errsv = errno;
      ReleaseTokenFSCall();
      if(rc)
        {
          if(errsv != ENOENT)
            Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);
        }
      else if(new_parent_buffstat.st_uid != p_context->credential.user
              && buffstat.st_uid != p_context->credential.user
              && p_context->credential.user != 0)
        Return(ERR_FSAL_ACCESS, 0, INDEX_FSAL_rename);
    }

  /*************************************
   * Rename the file on the filesystem *
   *************************************/
  TakeTokenFSCall();
  rc = rename(old_fsalpath.path, new_fsalpath.path);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);

  /***********************
   * Fill the attributes *
   ***********************/

  if(p_src_dir_attributes)
    {

      status =
          LUSTREFSAL_getattrs(p_old_parentdir_handle, p_context, p_src_dir_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_src_dir_attributes->asked_attributes);
          FSAL_SET_MASK(p_src_dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  if(p_tgt_dir_attributes)
    {

      status =
          LUSTREFSAL_getattrs(p_new_parentdir_handle, p_context, p_tgt_dir_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_tgt_dir_attributes->asked_attributes);
          FSAL_SET_MASK(p_tgt_dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_rename);

}
