/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_lookup.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:37:24 $
 * \version $Revision: 1.8 $
 * \brief   Lookup operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convertions.h"

fsal_status_t FSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_handle_t * p_fsoot_handle,       /* OUT */
                                  fsal_attrib_list_t * p_fsroot_attributes      /* [ IN/OUT ] */
    )
{
  /* no junctions in ghostfs */
  Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupJunction);
}

/**
 * FSAL_lookup :
 * Looks up for an object into a directory.
 *
 * Note : if parent handle is NULL and filename is empty,
 *        this retrieves root's handle.
 *
 * \param parent_directory_handle (input)
 *        Handle of the parent directory to search the object in.
 * \param filename (input)
 *        The name of the object to find.
 * \param cred (input)
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
 * \return
 *          
 */

fsal_status_t FSAL_lookup(fsal_handle_t * parent_directory_handle,      /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_handle_t * object_handle,        /* OUT */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    )
{

  int rc;
  GHOSTFS_handle_t handle;
  fsal_status_t status;

  /* for logging */
  SetFuncID(INDEX_FSAL_lookup);

  /* sanity checks
   * note : object_attributes is optionnal
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  if(!parent_directory_handle)
    {

      /* check that filename is empty,
       * else, parent_directory_handle should not
       * be NULL.
       */
      if(p_filename != NULL)
        Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookup);

      /* get the FS root */
      rc = GHOSTFS_GetRoot(&handle);
      if(rc)
        Return(ghost2fsal_error(rc), rc, INDEX_FSAL_lookup);

    }
  else
    {

      /* test lookup right (X) on parent directory.
       * for other FS than GHOST_FS, this in done
       * by the FS itself.
       */
      rc = GHOSTFS_Access((GHOSTFS_handle_t) (*parent_directory_handle),
                          GHOSTFS_TEST_EXEC,
                          p_context->credential.user, p_context->credential.group);

      if(rc)
        Return(ghost2fsal_error(rc), rc, INDEX_FSAL_lookup);

      /* proceed the lookup */
      rc = GHOSTFS_Lookup((GHOSTFS_handle_t) (*parent_directory_handle),
                          p_filename->name, &handle);

      if(rc)
        Return(ghost2fsal_error(rc), rc, INDEX_FSAL_lookup);

    }

  /* affects the output handle */
  (*object_handle) = handle;

  /* we reach this point if the lookup operation was sucessful.
   * we now perform the getattr operation.
   * If an error occures during getattr operation,
   * it is returned, even though the lookup operation succeeded.
   */
  if(object_attributes)
    {

      switch ((status = FSAL_getattrs(&handle, p_context, object_attributes)).major)
        {
          /* change the FAULT error to appears as an internal error.
           * indeed, parameters should be null. */
        case ERR_FSAL_FAULT:
          Return(ERR_FSAL_SERVERFAULT, ERR_FSAL_FAULT, INDEX_FSAL_lookup);
          break;
        case ERR_FSAL_NO_ERROR:
          /* continue */
          break;
        default:
          Return(status.major, status.minor, INDEX_FSAL_lookup);
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);

}

/**
 * FSAL_lookupPath :
 * Looks up for an object into the namespace.
 *
 * Note : if filename equals "/",
 *        this retrieves root's handle.
 *
 * \param path (input)
 *        The path of the object to find.
 * \param cred (input)
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

fsal_status_t FSAL_lookupPath(fsal_path_t * p_path,     /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_handle_t * object_handle,    /* OUT */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    )
{

  fsal_name_t obj_name = FSAL_NAME_INITIALIZER; /* empty string */
  char *ptr_str;
  fsal_handle_t out_hdl;
  fsal_status_t status;
  int b_is_last = FALSE;        /* is it the last lookup ? */

  /* for logging */
  SetFuncID(INDEX_FSAL_lookupPath);

  /* sanity checks
   * note : object_attributes is optionnal.
   */
  if(!object_handle || !p_context)
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

  if(FSAL_IS_ERROR(status = FSAL_lookup(NULL,   /* looking up for root */
                                        NULL,   /* NULL to get root handle */
                                        p_context,      /* user's credentials */
                                        &out_hdl,       /* output root handle */
                                        /* retrieves attributes if this is the last lookup : */
                                        (b_is_last ? object_attributes : NULL))))
    {

      Return(status.major, status.minor, INDEX_FSAL_lookupPath);
    }
  /* exits if this was the last lookup */
  if(b_is_last)
    {
      (*object_handle) = out_hdl;
      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);
    }

  /* proceed a step by step lookup */
  while(ptr_str[0])
    {

      fsal_handle_t in_hdl;
      char *dest_ptr;

      /* preparing lookup */

      in_hdl = out_hdl;

      /* compute next name */
      dest_ptr = obj_name.name;
      while(ptr_str[0] != '\0' && ptr_str[0] != '/')
        {
          dest_ptr[0] = ptr_str[0];
          dest_ptr++;
          ptr_str++;
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
      if(FSAL_IS_ERROR(status = FSAL_lookup(&in_hdl,    /* parent directory handle */
                                            &obj_name,  /* object name */
                                            p_context,  /* user's credentials */
                                            &out_hdl,   /* output root handle */
                                            /* retrieves attributes if this is the last lookup : */
                                            (b_is_last ? object_attributes : NULL))))
        {
          Return(status.major, status.minor, INDEX_FSAL_lookupPath);
        }

      /* ptr_str is ok, we are ready for next loop */
    }

  (*object_handle) = out_hdl;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);

}
