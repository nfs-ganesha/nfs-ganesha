/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_truncate.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/29 09:39:05 $
 * \version $Revision: 1.4 $
 * \brief   Truncate function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "namespace.h"

/**
 * FSAL_truncate:
 * Modify the data length of a regular file.
 *
 * \param filehandle (input):
 *        Handle of the file is to be truncated.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param length (input):
 *        The new data length for the file.
 * \param object_attributes (optionnal input/output): 
 *        The post operation attributes of the file.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (filehandle does not address an existing object)
 *        - ERR_FSAL_INVAL        (filehandle does not address a regular file)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t FUSEFSAL_truncate(fsal_handle_t *handle, /* IN */
                                fsal_op_context_t * p_context,      /* IN */
                                fsal_size_t length,     /* IN */
                                fsal_file_t * file_descriptor,      /* Unused in this FSAL */
                                fsal_attrib_list_t * object_attributes  /* [ IN/OUT ] */
    )
{

  int rc;
  char object_path[FSAL_MAX_PATH_LEN];
  fusefsal_handle_t * filehandle = (fusefsal_handle_t *)handle;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!filehandle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  if(!p_fs_ops->truncate)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_truncate);

  /* get the full path for the object */
  rc = NamespacePath(filehandle->data.inode, filehandle->data.device, filehandle->data.validator,
                     object_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_truncate);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  TakeTokenFSCall();
  rc = p_fs_ops->truncate(object_path, (off_t) length);
  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_truncate);

  if(object_attributes)
    {

      fsal_status_t st;

      st = FUSEFSAL_getattrs(handle, p_context, object_attributes);

      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* No error occured */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_truncate);

}
