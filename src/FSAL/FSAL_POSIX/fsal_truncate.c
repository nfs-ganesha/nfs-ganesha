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

fsal_status_t POSIXFSAL_truncate(fsal_handle_t * filehandle,     /* IN */
                                 fsal_op_context_t * context,    /* IN */
                                 fsal_size_t length,    /* IN */
                                 fsal_file_t * file_descriptor,    /* Unused in this FSAL */
                                 fsal_attrib_list_t * p_object_attributes       /* [ IN/OUT ] */
    )
{
  posixfsal_handle_t * p_filehandle = (posixfsal_handle_t *) filehandle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  int rc, errsv;
  struct stat buffstat;
  fsal_path_t fsalpath;
  fsal_status_t st;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_filehandle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  /* get the path of the file and its POSIX information */
  st = fsal_internal_getPathFromHandle(p_context, p_filehandle, 0, &fsalpath, &buffstat);
  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_truncate);

  /* check if it is a file */
  if(!S_ISREG(buffstat.st_mode))
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_truncate);
    }

  /* Executes the POSIX truncate operation */

  TakeTokenFSCall();
  rc = truncate(fsalpath.path, length);
  errsv = errno;
  ReleaseTokenFSCall();

  /* convert return code */
  if(rc)
    Return(posix2fsal_error(errsv), -errsv, INDEX_FSAL_truncate);

  /* Optionally retrieve attributes */
  if(p_object_attributes)
    {

      fsal_status_t st;

      st = POSIXFSAL_getattrs(p_filehandle, p_context, p_object_attributes);

      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* No error occurred */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_truncate);

}
