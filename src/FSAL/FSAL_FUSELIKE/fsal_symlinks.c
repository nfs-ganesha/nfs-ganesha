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
#include "fsal_common.h"
#include "namespace.h"
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
 *        - ERR_FSAL_STALE        (linkhandle does not address an existing object)
 *        - ERR_FSAL_INVAL        (linkhandle does not address a symbolic link)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 * */

fsal_status_t FUSEFSAL_readlink(fsal_handle_t * link_hdl, /* IN */
                                fsal_op_context_t * p_context,      /* IN */
                                fsal_path_t * p_link_content,   /* OUT */
                                fsal_attrib_list_t * link_attributes    /* [ IN/OUT ] */
    )
{
  fusefsal_handle_t * linkhandle = (fusefsal_handle_t *)link_hdl;
  int rc;
  fsal_status_t st;
  char link_content_out[FSAL_MAX_PATH_LEN];
  char object_path[FSAL_MAX_PATH_LEN];

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!linkhandle || !p_context || !p_link_content)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readlink);

  /* get the full path for this inode */
  rc = NamespacePath(linkhandle->data.inode, linkhandle->data.device, linkhandle->data.validator,
                     object_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_readlink);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  if(p_fs_ops->readlink)
    {
      TakeTokenFSCall();

      rc = p_fs_ops->readlink(object_path, link_content_out, FSAL_MAX_PATH_LEN);

      ReleaseTokenFSCall();

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_readlink);

    }
  else
    /* return empty link */
    link_content_out[0] = '\0';

  /* convert string to fsal_path_t */

  st = FSAL_str2path(link_content_out, FSAL_MAX_PATH_LEN, p_link_content);

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_readlink);

  /* retrieves object attributes, if asked */

  if(link_attributes)
    {

      fsal_status_t status;

      status = FUSEFSAL_getattrs(link_hdl, p_context, link_attributes);

      /* On error, we set a flag in the returned attributes */

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(link_attributes->asked_attributes);
          FSAL_SET_MASK(link_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
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
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parent_directory_handle does not address a directory)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t FUSEFSAL_symlink(fsal_handle_t * parent,     /* IN */
                               fsal_name_t * p_linkname,        /* IN */
                               fsal_path_t * p_linkcontent,     /* IN */
                               fsal_op_context_t * p_context,       /* IN */
                               fsal_accessmode_t accessmode,    /* IN (ignored) */
                               fsal_handle_t * link_hdl, /* OUT */
                               fsal_attrib_list_t * link_attributes     /* [ IN/OUT ] */
    )
{

  int rc;
  char parent_path[FSAL_MAX_PATH_LEN];
  char child_path[FSAL_MAX_PATH_LEN];
  struct stat buffstat;
  fusefsal_handle_t * parent_directory_handle = (fusefsal_handle_t *)parent;
  fusefsal_handle_t * link_handle = (fusefsal_handle_t *)link_hdl;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!parent_directory_handle ||
     !p_context || !link_handle || !p_linkname || !p_linkcontent)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_symlink);

  /* Tests if symlinking is allowed by configuration and filesystem. */

  if(!global_fs_info.symlink_support || !p_fs_ops->symlink)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_symlink);

  /* get the full path for parent inode */
  rc = NamespacePath(parent_directory_handle->data.inode,
                     parent_directory_handle->data.device,
                     parent_directory_handle->data.validator, parent_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_symlink);

  /* append child name to parent path */
  FSAL_internal_append_path(child_path, parent_path, p_linkname->name);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  TakeTokenFSCall();

  rc = p_fs_ops->symlink(p_linkcontent->path, child_path);

  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_symlink);

  /* set the owner for the newly created entry */

  if(p_fs_ops->chown)
    {
      TakeTokenFSCall();
      rc = p_fs_ops->chown(child_path, p_context->credential.user,
                           p_context->credential.group);
      ReleaseTokenFSCall();

      LogFullDebug(COMPONENT_FSAL, "chown: status = %d", rc);

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_symlink);
    }

  /* get info about this symlink */

  TakeTokenFSCall();
  rc = p_fs_ops->getattr(child_path, &buffstat);
  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_symlink);

  link_handle->data.validator = buffstat.st_ctime;

  /* add handle to namespace */
  NamespaceAdd(parent_directory_handle->data.inode,
               parent_directory_handle->data.device,
               parent_directory_handle->data.validator,
               p_linkname->name,
               buffstat.st_ino, buffstat.st_dev, &link_handle->data.validator);

  /* set output handle */
  link_handle->data.inode = buffstat.st_ino;
  link_handle->data.device = buffstat.st_dev;

  if(link_attributes)
    {
      fsal_status_t status = posix2fsal_attributes(&buffstat, link_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(link_attributes->asked_attributes);
          FSAL_SET_MASK(link_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_symlink);
}
