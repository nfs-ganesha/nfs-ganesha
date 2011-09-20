/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 * \file    fsal_symlinks.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/29 09:39:04 $
 * \version $Revision: 1.15 $
 * \brief   symlinks operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <string.h>
#include <unistd.h>

/**
 * FSAL_readlink:
 * Read the content of a symbolic link.
 *
 * \param linkhandle (input):
 *        Handle of the link to be read.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param p_link_content (output):
 *        Pointer to an fsal path structure where
 *        the link content is to be stored..
 * \param link_attributes (optionnal input/output): 
 *        The post operation attributes of the symlink link.
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
fsal_status_t LUSTREFSAL_readlink(fsal_handle_t * p_linkhandle,   /* IN */
                                  fsal_op_context_t * p_context,  /* IN */
                                  fsal_path_t * p_link_content, /* OUT */
                                  fsal_attrib_list_t * p_link_attributes        /* [ IN/OUT ] */
    )
{

  int rc, errsv;
  fsal_status_t status;
  char link_content_out[FSAL_MAX_PATH_LEN];
  fsal_path_t fsalpath;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!p_linkhandle || !p_context || !p_link_content)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readlink);

  status = fsal_internal_Handle2FidPath(p_context, p_linkhandle, &fsalpath);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_readlink);

  memset(link_content_out, 0, FSAL_MAX_PATH_LEN);

  /* Read the link on the filesystem */

  TakeTokenFSCall();
  rc = readlink(fsalpath.path, link_content_out, FSAL_MAX_PATH_LEN);
  errsv = errno;
  ReleaseTokenFSCall();

  /* rc is the length for the symlink content or -1 on error !!! */
  if(rc < 0)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_readlink);

  /* convert char * to fsal_path_t */
  status = FSAL_str2path(link_content_out, FSAL_MAX_PATH_LEN, p_link_content);

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_readlink);

  /* retrieves object attributes, if asked */

  if(p_link_attributes)
    {

      status = LUSTREFSAL_getattrs(p_linkhandle, p_context, p_link_attributes);

      /* On error, we set a flag in the returned attributes */

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_link_attributes->asked_attributes);
          FSAL_SET_MASK(p_link_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readlink);

}

/**
 * FSAL_symlink:
 * Create a symbolic link.
 *
 * \param parent_directory_handle (input):
 *        Handle of the parent directory where the link is to be created.
 * \param p_linkname (input):
 *        Name of the link to be created.
 * \param p_linkcontent (input):
 *        Content of the link to be created.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (ignored input):
 *        Mode of the link to be created.
 *        It has no sense in HPSS nor UNIX filesystems.
 * \param link_handle (output):
 *        Pointer to the handle of the created symlink.
 * \param link_attributes (optionnal input/output): 
 *        Attributes of the newly created symlink.
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
fsal_status_t LUSTREFSAL_symlink(fsal_handle_t * p_parent_directory_handle,       /* IN */
                                 fsal_name_t * p_linkname,      /* IN */
                                 fsal_path_t * p_linkcontent,   /* IN */
                                 fsal_op_context_t * p_context,   /* IN */
                                 fsal_accessmode_t accessmode,  /* IN (ignored) */
                                 fsal_handle_t * p_link_handle,   /* OUT */
                                 fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */
    )
{

  int rc, errsv;
  fsal_status_t status;
  fsal_path_t fsalpath;
  struct stat buffstat;
  int setgid_bit = FALSE;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!p_parent_directory_handle || !p_context ||
     !p_link_handle || !p_linkname || !p_linkcontent)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_symlink);

  /* Tests if symlinking is allowed by configuration. */

  if(!global_fs_info.symlink_support)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_symlink);

  /* build the new path and check the permissions on the parent directory */
  status = fsal_internal_Handle2FidPath(p_context, p_parent_directory_handle, &fsalpath);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_symlink);

  /* retrieve directory metadata, for checking access */
  TakeTokenFSCall();
  rc = lstat(fsalpath.path, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    {
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_symlink);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_symlink);
    }

  if(buffstat.st_mode & S_ISGID)
    setgid_bit = TRUE;

  status = fsal_internal_testAccess(p_context, FSAL_W_OK, &buffstat, NULL);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_symlink);

  /* build symlink path */

  status = fsal_internal_appendNameToPath(&fsalpath, p_linkname);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_symlink);

  /* create the symlink on the filesystem. */

  TakeTokenFSCall();
  rc = symlink(p_linkcontent->path, fsalpath.path);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_symlink);

  /* Get symlink handle */
  TakeTokenFSCall();
  status = fsal_internal_Path2Handle(p_context, &fsalpath, p_link_handle);
  ReleaseTokenFSCall();
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_lookup);

  /* chown the symlink to the current user/group */

  TakeTokenFSCall();
  rc = lchown(fsalpath.path, p_context->credential.user,
              setgid_bit ? -1 : p_context->credential.group);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_symlink);

  /* get attributes if asked */

  if(p_link_attributes)
    {

      status = LUSTREFSAL_getattrs(p_link_handle, p_context, p_link_attributes);

      /* On error, we set a flag in the returned attributes */

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_link_attributes->asked_attributes);
          FSAL_SET_MASK(p_link_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_symlink);
}
