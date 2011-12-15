/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_unlink.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 10:24:05 $
 * \version $Revision: 1.4 $
 * \brief   object removing function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convertions.h"

fsal_status_t FSAL_unlink(fsal_handle_t * parentdir_handle,     /* IN */
                          fsal_name_t * p_object_name,  /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t * parentdir_attributes     /* [IN/OUT ] */
    )
{
  int rc;
  GHOSTFS_handle_t new_handle;
  GHOSTFS_Attrs_t ghost_attrs;

  /* for logging */
  SetFuncID(INDEX_FSAL_unlink);

  /* sanity checks.
   * note : parentdir_attributes are optional.
   */
  if(!parentdir_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlink);

  /* test modification rights on parent directory.
   * for other FS than GHOST_FS, this in done
   * by the FS itself.
   */
  rc = GHOSTFS_Access((GHOSTFS_handle_t) (*parentdir_handle),
                      GHOSTFS_TEST_WRITE,
                      p_context->credential.user, p_context->credential.group);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_unlink);

  /* @todo : check if the user is the owner of the file */

  rc = GHOSTFS_Unlink((GHOSTFS_handle_t) (*parentdir_handle),   /* IN */
                      p_object_name->name,      /* IN */
                      &ghost_attrs);    /* [IN/OUT ] */

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_unlink);

  /* set attributes if asked */
  if(parentdir_attributes)
    ghost2fsal_attrs(parentdir_attributes, &ghost_attrs);

  /* GHOSTFS mkdir is done */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlink);

}
