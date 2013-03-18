/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
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
#include <unistd.h>

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

fsal_status_t LUSTREFSAL_unlink(fsal_handle_t * p_parent_directory_handle,        /* IN */
                                fsal_name_t * p_object_name,    /* IN */
                                fsal_op_context_t * p_context,    /* IN */
                                fsal_attrib_list_t * p_parent_directory_attributes      /* [IN/OUT ] */
    )
{

  fsal_status_t status;
  int rc, errsv;
  struct stat buffstat;
  fsal_path_t fsalpath;

  /* sanity checks. */
  if(!p_parent_directory_handle || !p_context || !p_object_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlink);

  /* build the FID path */
  status = fsal_internal_Handle2FidPath(p_context, p_parent_directory_handle, &fsalpath);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_unlink);

  /* build the child path */
  status = fsal_internal_appendNameToPath(&fsalpath, p_object_name);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_unlink);

  /* get file metadata */
  TakeTokenFSCall();
  rc = lstat(fsalpath.path, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();
  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_unlink);

  /******************************
   * DELETE FROM THE FILESYSTEM *
   ******************************/
  TakeTokenFSCall();
  /* If the object to delete is a directory, use 'rmdir' to delete the object, else use 'unlink' */
  rc = (S_ISDIR(buffstat.st_mode)) ? rmdir(fsalpath.path) : unlink(fsalpath.path);
  errsv = errno;
  ReleaseTokenFSCall();
  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_unlink);

  /***********************
   * FILL THE ATTRIBUTES *
   ***********************/

  if(p_parent_directory_attributes)
    {
      status =
          LUSTREFSAL_getattrs(p_parent_directory_handle, p_context,
                              p_parent_directory_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_parent_directory_attributes->asked_attributes);
          FSAL_SET_MASK(p_parent_directory_attributes->asked_attributes,
                        FSAL_ATTR_RDATTR_ERR);
        }
    }
  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlink);

}
