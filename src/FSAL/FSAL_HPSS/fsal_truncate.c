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

#include <hpss_errno.h>

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

fsal_status_t HPSSFSAL_truncate(hpssfsal_handle_t * filehandle, /* IN */
                                hpssfsal_op_context_t * p_context,      /* IN */
                                fsal_size_t length,     /* IN */
                                hpssfsal_file_t * file_descriptor,      /* Unused in this FSAL */
                                fsal_attrib_list_t * object_attributes  /* [ IN/OUT ] */
    )
{

  int rc;
  u_signed64 trunc_size;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!filehandle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  /* convert fsal_size_t to u_signed64 */

  trunc_size = fsal2hpss_64(length);

  /* check if it is a file */

  if(filehandle->data.obj_type != FSAL_TYPE_FILE)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_truncate);
    }

  /* Executes the HPSS truncate operation */

  TakeTokenFSCall();

  rc = hpss_TruncateHandle(&(filehandle->data.ns_handle),    /* IN - handle of file or parent */
                           NULL,        /* IN (handle addressing) */
                           trunc_size,  /* IN - new file length */
                           &(p_context->credential.hpss_usercred)       /* IN - pointer to user's credentials */
      );

  ReleaseTokenFSCall();

  /* The HPSS_ENOENT error actually means that handle is STALE */
  if(rc == HPSS_ENOENT)
    Return(ERR_FSAL_STALE, -rc, INDEX_FSAL_truncate);
  else if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_truncate);

  /* Optionnaly retrieve attributes */
  if(object_attributes)
    {

      fsal_status_t st;

      st = HPSSFSAL_getattrs(filehandle, p_context, object_attributes);

      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* No error occured */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_truncate);

}
