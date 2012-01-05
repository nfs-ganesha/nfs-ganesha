/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
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

#include <unistd.h>
#include <sys/types.h>

#ifdef _SHOOK
#include "shook_svr.h"
#endif

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
 *        - Another error code if an error occurred.
 */

fsal_status_t LUSTREFSAL_truncate(fsal_handle_t * p_filehandle,   /* IN */
                                  fsal_op_context_t * p_context,  /* IN */
                                  fsal_size_t length,   /* IN */
                                  fsal_file_t * file_descriptor,        /* Unused in this FSAL */
                                  fsal_attrib_list_t * p_object_attributes      /* [ IN/OUT ] */
    )
{

  int rc, errsv;
  fsal_path_t fsalpath;
  fsal_status_t st;
  int no_trunc = 0;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_filehandle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  /* get the path of the file and its handle */
  st = fsal_internal_Handle2FidPath(p_context, p_filehandle, &fsalpath);
  if(FSAL_IS_ERROR(st))
    ReturnStatus(st, INDEX_FSAL_truncate);

#ifdef _SHOOK
    /* If the file is not online:
     * - if truncate(0) => call tuncate(0), then "shook restore_trunc"
     * - if truncate(>0) => call "shook restore", then truncate
     */
    shook_state state;
    rc = shook_get_status(fsalpath.path, &state, FALSE);
    if (rc)
    {
        LogEvent(COMPONENT_FSAL, "Error retrieving shook status of %s: %s",
                 fsalpath.path, strerror(-rc));
        if (rc)
            Return(posix2fsal_error(-rc), -rc, INDEX_FSAL_truncate);
    }
    else if (state != SS_ONLINE)
    {
        if (length == 0)
        {
            LogInfo(COMPONENT_FSAL, "File is offline: calling shook restore_trunc");

            /* first truncate the file, them call the shook_svr to clear
             * the 'released' flag */

            TakeTokenFSCall();
            rc = truncate(fsalpath.path, 0);
            errsv = errno;
            ReleaseTokenFSCall();

            if (rc == 0)
            {
                /* use a short timeout of 2s */
                rc = shook_server_call(SA_RESTORE_TRUNC, ((lustrefsal_op_context_t *)p_context)->export_context->fsname,
                                       &((lustrefsal_handle_t *)p_filehandle)->data.fid, 2);
                if (rc)
                    Return(posix2fsal_error(-rc), -rc, INDEX_FSAL_truncate);
                /* file is already truncated, no need to truncate again */
                no_trunc = 1;
            }
            else
            {
                  if(errsv == ENOENT)
                    Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_truncate);
                  else
                    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_truncate);
            }
        }
        else /* length > 0 */
        {
            /* trigger restore. Give it a chance to retrieve the file in less than a second.
             * Else, it returns ETIME that is converted in ERR_DELAY */
            rc = shook_server_call(SA_RESTORE, ((lustrefsal_op_context_t *)p_context)->export_context->fsname,
                                   &((lustrefsal_handle_t *)p_filehandle)->data.fid, 1);
            if (rc)
                Return(posix2fsal_error(-rc), -rc, INDEX_FSAL_truncate);

            /* if rc = 0, file can be opened */
        }
    }
    /* else file is on line */
#endif

  /* Executes the POSIX truncate operation */

  if (!no_trunc)
  {
    TakeTokenFSCall();
    rc = truncate(fsalpath.path, length);
    errsv = errno;
    ReleaseTokenFSCall();
  }

  /* convert return code */
  if(rc)
    {
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_truncate);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_truncate);
    }

  /* Optionally retrieve attributes */
  if(p_object_attributes)
    {

      fsal_status_t st;

      st = LUSTREFSAL_getattrs(p_filehandle, p_context, p_object_attributes);

      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* No error occurred */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_truncate);

}
