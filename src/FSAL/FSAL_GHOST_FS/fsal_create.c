/**
 *
 * \file    fsal_create.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 10:24:00 $
 * \version $Revision: 1.6 $
 * \brief   Filesystem objects creation functions.
 *
 */

/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convertions.h"

fsal_status_t FSAL_create(fsal_handle_t * parent_directory_handle,      /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_handle_t * object_handle,        /* OUT */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    )
{

  int rc;
  GHOSTFS_handle_t new_handle;
  GHOSTFS_Attrs_t ghost_attrs;

  /* For logging */
  SetFuncID(INDEX_FSAL_create);

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!parent_directory_handle || !p_context || !object_handle)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_create);

  /* test modification rights on parent directory.
   * for other FS than GHOST_FS, this in done
   * by the FS itself.
   */
  rc = GHOSTFS_Access((GHOSTFS_handle_t) (*parent_directory_handle),
                      GHOSTFS_TEST_WRITE,
                      p_context->credential.user, p_context->credential.group);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_create);

  rc = GHOSTFS_Create((GHOSTFS_handle_t) * parent_directory_handle,
                      p_filename->name,
                      p_context->credential.user,
                      p_context->credential.group,
                      fsal2ghost_mode(accessmode), &new_handle, &ghost_attrs);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_create);

  /* set the output handle */
  *object_handle = (fsal_handle_t) new_handle;

  /* set attributes if asked */
  if(object_attributes)
    ghost2fsal_attrs(object_attributes, &ghost_attrs);

  /* GHOSTFS create is done */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_create);
}

fsal_status_t FSAL_mkdir(fsal_handle_t * parent_directory_handle,       /* IN */
                         fsal_name_t * p_dirname,       /* IN */
                         fsal_op_context_t * p_context, /* IN */
                         fsal_accessmode_t accessmode,  /* IN */
                         fsal_handle_t * object_handle, /* OUT */
                         fsal_attrib_list_t * object_attributes /* [ IN/OUT ] */
    )
{

  int rc;
  GHOSTFS_handle_t new_handle;
  GHOSTFS_Attrs_t ghost_attrs;

  /* For logging */
  SetFuncID(INDEX_FSAL_mkdir);

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!parent_directory_handle || !p_context || !object_handle)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mkdir);

  /* test modification rights on parent directory.
   * for other FS than GHOST_FS, this in done
   * by the FS itself.
   */
  rc = GHOSTFS_Access((GHOSTFS_handle_t) (*parent_directory_handle),
                      GHOSTFS_TEST_WRITE,
                      p_context->credential.user, p_context->credential.group);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_mkdir);

  rc = GHOSTFS_MkDir((GHOSTFS_handle_t) * parent_directory_handle,
                     p_dirname->name,
                     p_context->credential.user,
                     p_context->credential.group,
                     fsal2ghost_mode(accessmode), &new_handle, &ghost_attrs);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_mkdir);

  /* set the output handle */
  *object_handle = (fsal_handle_t) new_handle;

  /* set attributes if asked */
  if(object_attributes)
    ghost2fsal_attrs(object_attributes, &ghost_attrs);

  /* GHOSTFS mkdir is done */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_mkdir);

}

fsal_status_t FSAL_link(fsal_handle_t * target_handle,  /* IN */
                        fsal_handle_t * dir_handle,     /* IN */
                        fsal_name_t * p_link_name,      /* IN */
                        fsal_op_context_t * p_context,  /* IN */
                        fsal_attrib_list_t * attributes /* [ IN/OUT ] */
    )
{

  int rc;
  GHOSTFS_Attrs_t ghost_attrs;

  /* For logging */
  SetFuncID(INDEX_FSAL_link);

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!dir_handle || !p_link_name || !p_context || !target_handle)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_link);

  /* test modification rights on parent directory.
   * for other FS than GHOST_FS, this in done
   * by the FS itself.
   */
  rc = GHOSTFS_Access((GHOSTFS_handle_t) (*dir_handle),
                      GHOSTFS_TEST_WRITE,
                      p_context->credential.user, p_context->credential.group);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_link);

  rc = GHOSTFS_Link((GHOSTFS_handle_t) * dir_handle,
                    p_link_name->name, (GHOSTFS_handle_t) * target_handle, &ghost_attrs);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_link);

  /* set attributes if asked */
  if(attributes)
    ghost2fsal_attrs(attributes, &ghost_attrs);

  /* GHOSTFS create is done */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_link);

}

fsal_status_t FSAL_mknode(fsal_handle_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_node_name,    /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          fsal_nodetype_t nodetype,     /* IN */
                          fsal_dev_t * dev,     /* IN */
                          fsal_handle_t * p_object_handle,      /* OUT (handle to the created node) */
                          fsal_attrib_list_t * node_attributes  /* [ IN/OUT ] */
    )
{

  /* For logging */
  SetFuncID(INDEX_FSAL_mknode);

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!parentdir_handle || !p_context || !nodetype || !dev)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mknode);

  /* GHOSTFS is read only */
  Return(ERR_FSAL_ROFS, 0, INDEX_FSAL_mknode);

}
