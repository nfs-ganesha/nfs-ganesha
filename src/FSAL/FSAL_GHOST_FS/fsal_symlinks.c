/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_symlinks.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 10:24:04 $
 * \version $Revision: 1.8 $
 * \brief   symlinks operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convertions.h"
#include <string.h>

fsal_status_t FSAL_readlink(fsal_handle_t * linkhandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            fsal_path_t * p_link_content,       /* OUT */
                            fsal_attrib_list_t * link_attributes        /* [ IN/OUT ] */
    )
{

  int rc;

  /* for logging */
  SetFuncID(INDEX_FSAL_readlink);

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!linkhandle || !p_context || !p_link_content)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readlink);

  rc = GHOSTFS_ReadLink((GHOSTFS_handle_t) (*linkhandle),
                        p_link_content->path, FSAL_MAX_PATH_LEN);
  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_readlink);

  /* retrieves object attributes, if asked */
  if(link_attributes)
    {

      fsal_status_t status;

      switch ((status = FSAL_getattrs(linkhandle, p_context, link_attributes)).major)
        {
          /* change the FAULT error to appears as an internal error.
           * indeed, parameters should be null. */
        case ERR_FSAL_FAULT:
          Return(ERR_FSAL_SERVERFAULT, ERR_FSAL_FAULT, INDEX_FSAL_readlink);
          break;
        case ERR_FSAL_NO_ERROR:
          /* continue */
          break;
        default:
          Return(status.major, status.minor, INDEX_FSAL_readlink);
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readlink);

}

fsal_status_t FSAL_symlink(fsal_handle_t * parent_directory_handle,     /* IN */
                           fsal_name_t * p_linkname,    /* IN */
                           fsal_path_t * p_linkcontent, /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_accessmode_t accessmode,        /* IN  */
                           fsal_handle_t * link_handle, /* OUT */
                           fsal_attrib_list_t * link_attributes /* [ IN/OUT ] */
    )
{
  int rc;
  GHOSTFS_handle_t new_handle;
  GHOSTFS_Attrs_t ghost_attrs;

  /* For logging */
  SetFuncID(INDEX_FSAL_symlink);

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!parent_directory_handle ||
     !p_linkname || !p_linkcontent || !link_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_symlink);

  /* test modification rights on parent directory.
   * for other FS than GHOST_FS, this in done
   * by the FS itself.
   */
  rc = GHOSTFS_Access((GHOSTFS_handle_t) (*parent_directory_handle),
                      GHOSTFS_TEST_WRITE,
                      p_context->credential.user, p_context->credential.group);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_symlink);

  rc = GHOSTFS_Symlink((GHOSTFS_handle_t) * parent_directory_handle,
                       p_linkname->name,
                       p_linkcontent->path,
                       p_context->credential.user,
                       p_context->credential.group,
                       fsal2ghost_mode(accessmode), &new_handle, &ghost_attrs);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_symlink);

  /* set the output handle */
  *link_handle = (fsal_handle_t) new_handle;

  /* set attributes if asked */
  if(link_attributes)
    ghost2fsal_attrs(link_attributes, &ghost_attrs);

  /* GHOSTFS create is done */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_symlink);

}
