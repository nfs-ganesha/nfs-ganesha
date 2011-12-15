/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_rename.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 10:24:04 $
 * \version $Revision: 1.4 $
 * \brief   object renaming/moving function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convertions.h"

fsal_status_t FSAL_rename(fsal_handle_t * old_parentdir_handle, /* IN */
                          fsal_name_t * p_old_name,     /* IN */
                          fsal_handle_t * new_parentdir_handle, /* IN */
                          fsal_name_t * p_new_name,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t * src_dir_attributes,      /* [ IN/OUT ] */
                          fsal_attrib_list_t * tgt_dir_attributes       /* [ IN/OUT ] */
    )
{
  int rc;

  /* for logging */
  SetFuncID(INDEX_FSAL_rename);

  GHOSTFS_Attrs_t src_attr, tgt_attr;

  /* sanity checks.
   * note : src/tgt_dir_attributes are optional.
   */
  if(!old_parentdir_handle ||
     !new_parentdir_handle || !p_old_name || !p_new_name || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

  rc = GHOSTFS_Rename((GHOSTFS_handle_t) * old_parentdir_handle,
                      (GHOSTFS_handle_t) * new_parentdir_handle,
                      p_old_name->name, p_new_name->name, &src_attr, &tgt_attr);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_rename);

  /* set attributes if asked */
  if(src_dir_attributes)
    ghost2fsal_attrs(src_dir_attributes, &src_attr);

  if(tgt_dir_attributes)
    ghost2fsal_attrs(tgt_dir_attributes, &tgt_attr);

  /* GHOSTFS rename is done */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_rename);

}
