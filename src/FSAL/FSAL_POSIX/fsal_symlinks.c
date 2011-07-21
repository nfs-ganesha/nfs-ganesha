/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
fsal_status_t POSIXFSAL_readlink(fsal_handle_t * linkhandle,     /* IN */
                                 fsal_op_context_t * context,    /* IN */
                                 fsal_path_t * p_link_content,  /* OUT */
                                 fsal_attrib_list_t * p_link_attributes /* [ IN/OUT ] */
    )
{
  posixfsal_handle_t * p_linkhandle = (posixfsal_handle_t *) linkhandle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  int rc, errsv;
  fsal_status_t status;
  char link_content_out[FSAL_MAX_PATH_LEN];
  fsal_path_t fsalpath;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!p_linkhandle || !p_context || !p_link_content)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readlink);

  status = fsal_internal_getPathFromHandle(p_context, p_linkhandle, 0, &fsalpath, NULL);
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_readlink);

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
    Return(status.major, status.minor, INDEX_FSAL_readlink);

  /* retrieves object attributes, if asked */

  if(p_link_attributes)
    {

      status = POSIXFSAL_getattrs(p_linkhandle, p_context, p_link_attributes);

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
fsal_status_t POSIXFSAL_symlink(fsal_handle_t * parent_directory_handle, /* IN */
                                fsal_name_t * p_linkname,       /* IN */
                                fsal_path_t * p_linkcontent,    /* IN */
                                fsal_op_context_t * context,     /* IN */
                                fsal_accessmode_t accessmode,   /* IN (ignored) */
                                fsal_handle_t * link_handle,     /* OUT */
                                fsal_attrib_list_t * p_link_attributes  /* [ IN/OUT ] */
    )
{
  posixfsal_handle_t * p_parent_directory_handle
    = (posixfsal_handle_t *) parent_directory_handle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  posixfsal_handle_t * p_link_handle = (posixfsal_handle_t *) link_handle;
  int rc, errsv;
  fsal_status_t status;
  fsal_path_t fsalpath;
  fsal_posixdb_fileinfo_t infofs;
  struct stat buffstat;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!p_parent_directory_handle ||
     !p_context || !p_link_handle || !p_linkname || !p_linkcontent)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_symlink);

  /* Tests if symlinking is allowed by configuration. */

  if(!global_fs_info.symlink_support)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_symlink);

  /* build the new path and check the permissions on the parent directory */
  status =
      fsal_internal_getPathFromHandle(p_context, p_parent_directory_handle, 1, &fsalpath,
                                      &buffstat);
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_symlink);

  status = fsal_internal_testAccess(p_context, FSAL_W_OK, &buffstat, NULL);
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_symlink);

  status = fsal_internal_appendFSALNameToFSALPath(&fsalpath, p_linkname);
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_symlink);

  /* create the symlink on the filesystem. */

  TakeTokenFSCall();
  rc = symlink(p_linkcontent->path, fsalpath.path);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_symlink);

  /* stat the file & add it to the database */
  TakeTokenFSCall();
  rc = lstat(fsalpath.path, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_symlink);

  if(FSAL_IS_ERROR(status = fsal_internal_posix2posixdb_fileinfo(&buffstat, &infofs)))
    Return(status.major, status.minor, INDEX_FSAL_symlink);
  if(FSAL_IS_ERROR
     (status =
      fsal_internal_posixdb_add_entry(p_context->p_conn, p_linkname, &infofs,
                                      p_parent_directory_handle, p_link_handle)))
    Return(status.major, status.minor, INDEX_FSAL_symlink);

  /* chown the symlink to the current user */

  TakeTokenFSCall();
  rc = lchown(fsalpath.path, p_context->credential.user, -1);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_symlink);

  /* get attributes if asked */

  if(p_link_attributes)
    {

      status = posix2fsal_attributes(&buffstat, p_link_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_link_attributes->asked_attributes);
          FSAL_SET_MASK(p_link_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_symlink);
}
