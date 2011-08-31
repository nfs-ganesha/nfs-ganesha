/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_lookup.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.17 $
 * \brief   Lookup operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * FSAL_lookup :
 * Looks up for an object into a directory.
 *
 * Note : if parent handle and filename are NULL,
 *        this retrieves root's handle.
 *
 * \param parent_directory_handle (input)
 *        Handle of the parent directory to search the object in.
 * \param filename (input)
 *        The name of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return - ERR_FSAL_NO_ERROR, if no error.
 *         - Another error code else.
 *          
 */
fsal_status_t POSIXFSAL_lookup(fsal_handle_t * parent_directory_handle,  /* IN */
                               fsal_name_t * p_filename,        /* IN */
                               fsal_op_context_t * context,      /* IN */
                               fsal_handle_t * object_handle,    /* OUT */
                               fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */
    )
{
  posixfsal_handle_t * p_parent_directory_handle
    = (posixfsal_handle_t *) parent_directory_handle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  posixfsal_handle_t * p_object_handle = (posixfsal_handle_t *) object_handle;
  int rc, errsv;
  fsal_status_t status;
  fsal_posixdb_status_t statusdb;
  fsal_posixdb_fileinfo_t infofs;
  struct stat buffstat;
  fsal_path_t pathfsal;

  /* sanity checks
   * note : object_attributes is optionnal
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!p_object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  /* filename is NULL => lookup "/" */
  if((p_parent_directory_handle && !p_filename)
     || (!p_parent_directory_handle && p_filename))
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  /* get informations about the parent */
  if(!p_parent_directory_handle)
    {                           /* lookup '/' */
      TakeTokenFSCall();
      rc = lstat("/", &buffstat);
      errsv = errno;
      ReleaseTokenFSCall();
      if(rc)
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_lookup);
    }
  else
    {
      status =
          fsal_internal_getPathFromHandle(p_context, p_parent_directory_handle, 1,
                                          &pathfsal, &buffstat);
      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookup);
    }

  /* Be careful about junction crossing, symlinks, hardlinks,... */
  switch (posix2fsal_type(buffstat.st_mode))
    {
    case FSAL_TYPE_DIR:
      // OK
      break;

    case FSAL_TYPE_JUNCTION:
      // This is a junction
      Return(ERR_FSAL_XDEV, 0, INDEX_FSAL_lookup);

    case FSAL_TYPE_FILE:
    case FSAL_TYPE_LNK:
    case FSAL_TYPE_XATTR:
      // not a directory 
      Return(ERR_FSAL_NOTDIR, 0, INDEX_FSAL_lookup);

    default:
      Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_lookup);
    }

  if(!p_parent_directory_handle)
    {                           /* lookup '/' */
      /* convert struct stat to fsal_posixdb_fileinfo_t */

      if(FSAL_IS_ERROR(status = fsal_internal_posix2posixdb_fileinfo(&buffstat, &infofs)))
        Return(status.major, status.minor, INDEX_FSAL_lookup);

      /* get The handle of '/' */
      status =
          fsal_internal_getInfoFromName(p_context, NULL, NULL, &infofs, p_object_handle);
      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookup);

    }
  else
    {

      LogFullDebug(COMPONENT_FSAL, "lookup of %llu.%i/%s\n", p_parent_directory_handle->data.id,
                   p_parent_directory_handle->data.ts, p_filename->name);

      /* check rights to enter into the directory */
      status = fsal_internal_testAccess(p_context, FSAL_X_OK, &buffstat, NULL);
      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookup);

      /* stat the file to see if it exists and get some information */
      status = fsal_internal_appendFSALNameToFSALPath(&pathfsal, p_filename);
      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookup);

      TakeTokenFSCall();
      rc = lstat(pathfsal.path, &buffstat);
      errsv = errno;
      ReleaseTokenFSCall();
      if(rc)
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_lookup);

      /* getHandleFromName */
      if(!FSAL_namecmp(p_filename, (fsal_name_t *) & FSAL_DOT))
        {
          /* lookup "." */
          memcpy(p_object_handle, p_parent_directory_handle, sizeof(posixfsal_handle_t));

        }
      else if(!FSAL_namecmp(p_filename, (fsal_name_t *) & FSAL_DOT_DOT))
        {
          /* lookup ".." */
          statusdb = fsal_posixdb_getParentDirHandle(p_context->p_conn,
                                                     p_parent_directory_handle,
                                                     p_object_handle);

        }
      else
        {
          /* convert struct stat to fsal_posixdb_fileinfo_t */
          if(FSAL_IS_ERROR
             (status = fsal_internal_posix2posixdb_fileinfo(&buffstat, &infofs)))
            Return(status.major, status.minor, INDEX_FSAL_lookup);

          /* get The handle of the file */
          status =
              fsal_internal_getInfoFromName(p_context, p_parent_directory_handle,
                                            p_filename, &infofs, p_object_handle);
          if(FSAL_IS_ERROR(status))
            Return(status.major, status.minor, INDEX_FSAL_lookup);
        }

    }

  if(p_object_attributes)
    {

      /* convert posix attributes to fsal attributes */
      status = posix2fsal_attributes(&buffstat, p_object_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  /* lookup complete ! */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);

}

/**
 * FSAL_lookupPath :
 * Looks up for an object into the namespace.
 *
 * Note : if path equals "/",
 *        this retrieves root's handle.
 *
 * \param path (input)
 *        The path of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 */

fsal_status_t POSIXFSAL_lookupPath(fsal_path_t * p_path,        /* IN */
                                   fsal_op_context_t * context,  /* IN */
                                   fsal_handle_t * object_hdl,  /* OUT */
                                   fsal_attrib_list_t * object_attributes       /* [ IN/OUT ] */
    )
{
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  posixfsal_handle_t * object_handle = (posixfsal_handle_t *) object_hdl;
  fsal_name_t obj_name = FSAL_NAME_INITIALIZER; /* empty string */
  char *ptr_str;
  posixfsal_handle_t out_hdl;
  fsal_status_t status;
  int b_is_last = FALSE;        /* is it the last lookup ? */

  /* sanity checks
   * note : object_attributes is optionnal.
   */

  if(!object_handle || !p_context || !p_path)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupPath);

  /* test whether the path begins with a slash */

  if(p_path->path[0] != '/')
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupPath);

  /* the pointer now points on the next name in the path,
   * skipping slashes.
   */

  ptr_str = p_path->path + 1;
  while(ptr_str[0] == '/')
    ptr_str++;

  /* is the next name empty ? */

  if(ptr_str[0] == '\0')
    b_is_last = TRUE;

  /* retrieves root directory */

  status = POSIXFSAL_lookup(NULL,       /* looking up for root */
                            NULL,       /* empty string to get root handle */
                            p_context,  /* user's p_contextentials */
                            &out_hdl,   /* output root handle */
                            /* retrieves attributes if this is the last lookup : */
                            (b_is_last ? object_attributes : NULL));

  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_lookupPath);

  /* exits if this was the last lookup */

  if(b_is_last)
    {
      (*object_handle) = out_hdl;
      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);
    }

  /* proceed a step by step lookup */

  while(ptr_str[0])
    {

      posixfsal_handle_t in_hdl;
      char *dest_ptr;

      /* preparing lookup */

    /** @todo : Be carefull about junction crossing, symlinks, hardlinks,... */

      in_hdl = out_hdl;

      /* compute next name */
      dest_ptr = obj_name.name;
      obj_name.len = 0;
      while(ptr_str[0] != '\0' && ptr_str[0] != '/')
        {
          dest_ptr[0] = ptr_str[0];
          dest_ptr++;
          ptr_str++;
          obj_name.len++;
        }
      /* final null char */
      dest_ptr[0] = '\0';

      /* skip multiple slashes */
      while(ptr_str[0] == '/')
        ptr_str++;

      /* is the next name empty ? */
      if(ptr_str[0] == '\0')
        b_is_last = TRUE;

      /*call to FSAL_lookup */
      status = POSIXFSAL_lookup(&in_hdl,        /* parent directory handle */
                                &obj_name,      /* object name */
                                p_context,      /* user's p_contextentials */
                                &out_hdl,       /* output root handle */
                                /* retrieves attributes if this is the last lookup : */
                                (b_is_last ? object_attributes : NULL));

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookupPath);

      /* ptr_str is ok, we are ready for next loop */
    }

  (*object_handle) = out_hdl;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);

}

/**
 * FSAL_lookupJunction :
 * Get the fileset root for a junction.
 *
 * \param p_junction_handle (input)
 *        Handle of the junction to be looked up.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param p_fsroot_handle (output)
 *        The handle of root directory of the fileset.
 * \param p_fsroot_attributes (optional input/output)
 *        Pointer to the attributes of the root directory
 *        for the fileset.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return - ERR_FSAL_NO_ERROR, if no error.
 *         - Another error code else.
 *          
 */
fsal_status_t POSIXFSAL_lookupJunction(fsal_handle_t * p_junction_handle,  /* IN */
                                       fsal_op_context_t * p_context,      /* IN */
                                       fsal_handle_t * p_fsoot_handle,     /* OUT */
                                       fsal_attrib_list_t * p_fsroot_attributes /* [ IN/OUT ] */
    )
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupJunction);
}
