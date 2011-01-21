/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_lookup.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.18 $
 * \brief   Lookup operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"
#include "HPSSclapiExt/hpssclapiext.h"

#include <hpss_errno.h>

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
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parent_directory_handle does not address a directory)
 *        - ERR_FSAL_NOENT        (the object designated by p_filename does not exist)
 *        - ERR_FSAL_XDEV         (tried to operate a lookup on a filesystem junction.
 *                                 Use FSAL_lookupJunction instead)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 *          
 */
fsal_status_t HPSSFSAL_lookup(hpssfsal_handle_t * parent_directory_handle,      /* IN */
                              fsal_name_t * p_filename, /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              hpssfsal_handle_t * object_handle,        /* OUT */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    )
{

  int rc;
  fsal_status_t status;
  hpss_fileattr_t root_attr;

  ns_ObjHandle_t obj_hdl;
  hpss_Attrs_t obj_attr;

  /* sanity checks
   * note : object_attributes is optionnal
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  if(!parent_directory_handle)
    {

      /* check that p_filename is NULL,
       * else, parent_directory_handle should not
       * be NULL.
       */
      if(p_filename != NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      /* root handle = fileset root handle */
      object_handle->data.obj_type =
          hpss2fsal_type(p_context->export_context->fileset_root_handle.Type);
      object_handle->data.ns_handle = p_context->export_context->fileset_root_handle;

      /* retrieves root attributes, if asked. */
      if(object_attributes)
        {
          fsal_status_t status;

          status = HPSSFSAL_getattrs(object_handle, p_context, object_attributes);

          /* On error, we set a flag in the returned attributes */

          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(object_attributes->asked_attributes);
              FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
            }
        }

    }
  else
    {

      /* the filename should not be null */
      if(p_filename == NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      /* Be careful about junction crossing, symlinks, hardlinks,... */

      switch (parent_directory_handle->data.obj_type)
        {
        case FSAL_TYPE_DIR:
          /* OK */
          break;

        case FSAL_TYPE_JUNCTION:
          /* This is a junction */
          Return(ERR_FSAL_XDEV, 0, INDEX_FSAL_lookup);

        case FSAL_TYPE_FILE:
        case FSAL_TYPE_LNK:
        case FSAL_TYPE_XATTR:
          /* not a directory */
          Return(ERR_FSAL_NOTDIR, 0, INDEX_FSAL_lookup);

        default:
          Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_lookup);
        }

      /* call to HPSS client api */
      /* We use hpss_GetRawAttrHandle for not chasing junctions nor symlinks. */

      TakeTokenFSCall();

      rc = HPSSFSAL_GetRawAttrHandle(&(parent_directory_handle->data.ns_handle), p_filename->name, &p_context->credential.hpss_usercred, FALSE,      /* don't traverse junctions */
                                     &obj_hdl, NULL, &obj_attr);

      ReleaseTokenFSCall();

    /**
     * /!\ WARNING : When the directory handle is stale, HPSS returns ENOTDIR.
     *     Thus, in this case, we must double check
     *     by checking the directory handle.
     */
      if(rc == HPSS_ENOTDIR)
        {
          if(HPSSFSAL_IsStaleHandle(&parent_directory_handle->data.ns_handle,
                                    &p_context->credential.hpss_usercred))
            {
              Return(ERR_FSAL_STALE, -rc, INDEX_FSAL_lookup);
            }
        }

      if(rc)
        Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_lookup);

      /* set output handle */
      memset( (char *)&(object_handle), 0, sizeof( hpssfsal_handle_t ) ) ;
      object_handle->data.obj_type = hpss2fsal_type(obj_hdl.Type);
      object_handle->data.ns_handle = obj_hdl;

      if(object_attributes)
        {

          /* convert hpss attributes to fsal attributes */

          status = hpss2fsal_attributes(&obj_hdl, &obj_attr, object_attributes);

          if(FSAL_IS_ERROR(status))
            Return(status.major, status.minor, INDEX_FSAL_lookup);

        }

    }

  /* lookup complete ! */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);

}

/**
 * FSAL_lookupJunction :
 * Get the fileset root for a junction.
 *
 * \param p_junction_handle (input)
 *        Handle of the junction to be looked up.
 * \param cred (input)
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
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (p_junction_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 *          
 */
fsal_status_t HPSSFSAL_lookupJunction(hpssfsal_handle_t * p_junction_handle,    /* IN */
                                      hpssfsal_op_context_t * p_context,        /* IN */
                                      hpssfsal_handle_t * p_fsoot_handle,       /* OUT */
                                      fsal_attrib_list_t * p_fsroot_attributes  /* [ IN/OUT ] */
    )
{
  int rc;
  fsal_status_t status;
  hpss_Attrs_t root_attr;

  /* sanity checks
   * note : p_fsroot_attributes is optionnal
   */
  if(!p_junction_handle || !p_fsoot_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupJunction);

  if(p_junction_handle->data.obj_type != FSAL_TYPE_JUNCTION)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupJunction);

  /* call to HPSS client api */
  /* We use hpss_GetRawAttrHandle for chasing junctions. */

  TakeTokenFSCall();

  rc = HPSSFSAL_GetRawAttrHandle(&(p_junction_handle->data.ns_handle), NULL, &p_context->credential.hpss_usercred, TRUE,     /* do traverse junctions !!! */
                                 NULL, NULL, &root_attr);

  ReleaseTokenFSCall();

  if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_lookupJunction);

  /* set output handle */
  p_fsoot_handle->data.obj_type = hpss2fsal_type(root_attr.FilesetHandle.Type);
  p_fsoot_handle->data.ns_handle = root_attr.FilesetHandle;

  if(p_fsroot_attributes)
    {

      /* convert hpss attributes to fsal attributes */

      status = hpss2fsal_attributes(&root_attr.FilesetHandle,
                                    &root_attr, p_fsroot_attributes);

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookupJunction);

    }

  /* lookup complete ! */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupJunction);
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
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - ERR_FSAL_INVAL        (the path argument is not absolute)
 *        - ERR_FSAL_NOENT        (an element in the path does not exist)
 *        - ERR_FSAL_NOTDIR       (an element in the path is not a directory)
 *        - ERR_FSAL_XDEV         (tried to cross a filesystem junction,
 *                                 whereas is has not been authorized in the server
 *                                 configuration - FSAL::auth_xdev_export parameter)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t HPSSFSAL_lookupPath(fsal_path_t * p_path, /* IN */
                                  hpssfsal_op_context_t * p_context,    /* IN */
                                  hpssfsal_handle_t * object_handle,    /* OUT */
                                  fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    )
{
  fsal_name_t obj_name = FSAL_NAME_INITIALIZER; /* empty string */
  char *ptr_str;
  hpssfsal_handle_t out_hdl;
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

  status = HPSSFSAL_lookup(NULL,        /* looking up for root */
                           NULL,        /* empty string to get root handle */
                           p_context,   /* user's credentials */
                           &out_hdl,    /* output root handle */
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

      hpssfsal_handle_t in_hdl;
      char *dest_ptr;

      /* preparing lookup */

    /** @todo : Be carefull about junction crossing, symlinks, hardlinks,... */

      in_hdl = out_hdl;

      /* compute next name */
      obj_name.len = 0;
      dest_ptr = obj_name.name;
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
      status = HPSSFSAL_lookup(&in_hdl, /* parent directory handle */
                               &obj_name,       /* object name */
                               p_context,       /* user's credentials */
                               &out_hdl,        /* output root handle */
                               /* retrieves attributes if this is the last lookup : */
                               (b_is_last ? object_attributes : NULL));

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookupPath);

      /* if the target object is a junction, an we allow cross junction lookups,
       * we cross it.
       */
      if(global_fs_info.auth_exportpath_xdev && (out_hdl.data.obj_type == FSAL_TYPE_JUNCTION))
        {
          hpssfsal_handle_t tmp_hdl;

          tmp_hdl = out_hdl;

          /*call to FSAL_lookup */
          status = HPSSFSAL_lookupJunction(&tmp_hdl,    /* object handle */
                                           p_context,   /* user's credentials */
                                           &out_hdl,    /* output root handle */
                                           /* retrieves attributes if this is the last lookup : */
                                           (b_is_last ? object_attributes : NULL));

        }

      /* ptr_str is ok, we are ready for next loop */
    }

  (*object_handle) = out_hdl;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);

}
