/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_create.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:36 $
 * \version $Revision: 1.18 $
 * \brief   Filesystem objects creation functions.
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
 * FSAL_create:
 * Create a regular file.
 *
 * \param parent_directory_handle (input):
 *        Handle of the parent directory where the file is to be created.
 * \param p_filename (input):
 *        Pointer to the name of the file to be created.
 * \param cred (input):
 *        Authentication context for the operation (user, export...).
 * \param accessmode (input):
 *        Mode for the file to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param object_handle (output):
 *        Pointer to the handle of the created file.
 * \param object_attributes (optionnal input/output): 
 *        The postop attributes of the created file.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        Can be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_EXIST, ERR_FSAL_IO, ...
 *            
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */
fsal_status_t HPSSFSAL_create(hpssfsal_handle_t * parent_directory_handle,      /* IN */
                              fsal_name_t * p_filename, /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              hpssfsal_handle_t * object_handle,        /* OUT */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    )
{

  int rc;
  mode_t unix_mode;

  hpss_Attrs_t new_attrs;
  ns_ObjHandle_t new_hdl;

  /* cos management */
  hpss_cos_hints_t hint;
  hpss_cos_priorities_t hintpri;

  /* If no COS is specified in the config file,
   * we give NULL pointers to CreateHandle,
   * to use the default Cos for this Fileset.
   */
  hpss_cos_hints_t *p_hint = NULL;
  hpss_cos_priorities_t *p_hintpri = NULL;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!parent_directory_handle || !p_context || !object_handle || !p_filename)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_create);

  /* convert fsal mode to unix mode. */
  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~global_fs_info.umask;

  /* Eventually set cos */

  if(p_context->export_context->default_cos != 0)
    {
      HPSSFSAL_BuildCos(p_context->export_context->default_cos, &hint, &hintpri);
      p_hint = &hint;
      p_hintpri = &hintpri;
    }

  if(p_context->export_context->default_cos != 0)
    LogDebug(COMPONENT_FSAL, "Creating file with COS = %d",
                      p_context->export_context->default_cos);
  else
    LogDebug(COMPONENT_FSAL, "Creating file with default fileset COS.");

  LogDebug(COMPONENT_FSAL, "Creation mode: 0%o", accessmode);

  /* call to API */

  TakeTokenFSCall();

  rc = HPSSFSAL_CreateHandle(&(parent_directory_handle->data.ns_handle),     /* IN - Parent object handle */
                             p_filename->name,  /* IN - Name of the file to be created */
                             unix_mode, /* IN - Desired file perms */
                             &(p_context->credential.hpss_usercred),    /* IN - User credentials */
                             p_hint,    /* IN - Desired class of service */
                             p_hintpri, /* IN - Priorities of hint struct */
                             NULL,      /* OUT - Granted class of service */
                             (object_attributes ? &new_attrs : NULL),   /* OUT - File attributes */
                             &new_hdl,  /* OUT - File handle */
                             NULL);     /* OUT - Client authorization */

  ReleaseTokenFSCall();

  /* /!\ WARNING : When the directory handle is stale, HPSS returns ENOTDIR.
   * If the returned value is HPSS_ENOTDIR, parent handle MAY be stale.
   * Thus, we must double-check by calling getattrs.   
   */
  if(rc == HPSS_ENOTDIR || rc == HPSS_ENOENT)
    {
      if(HPSSFSAL_IsStaleHandle(&parent_directory_handle->data.ns_handle,
                                &p_context->credential.hpss_usercred))
        {
          Return(ERR_FSAL_STALE, -rc, INDEX_FSAL_create);
        }
    }

  /* other errors */

  if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_create);

  /* set output handle */
  memset( (char *)object_handle, 0, sizeof( hpssfsal_handle_t ) ) ;
  object_handle->data.obj_type = FSAL_TYPE_FILE;
  object_handle->data.ns_handle = new_hdl;

  if(object_attributes)
    {

      fsal_status_t status;

      /* convert hpss attributes to fsal attributes */

      status = hpss2fsal_attributes(&new_hdl, &new_attrs, object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_create);

}

/**
 * FSAL_mkdir:
 * Create a directory.
 *
 * \param parent_directory_handle (input):
 *        Handle of the parent directory where
 *        the subdirectory is to be created.
 * \param p_dirname (input):
 *        Pointer to the name of the directory to be created.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the directory to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param object_handle (output):
 *        Pointer to the handle of the created directory.
 * \param object_attributes (optionnal input/output): 
 *        The attributes of the created directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_EXIST, ERR_FSAL_IO, ...
 *            
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */
fsal_status_t HPSSFSAL_mkdir(hpssfsal_handle_t * parent_directory_handle,       /* IN */
                             fsal_name_t * p_dirname,   /* IN */
                             hpssfsal_op_context_t * p_context, /* IN */
                             fsal_accessmode_t accessmode,      /* IN */
                             hpssfsal_handle_t * object_handle, /* OUT */
                             fsal_attrib_list_t * object_attributes     /* [ IN/OUT ] */
    )
{

  int rc;
  mode_t unix_mode;
  ns_ObjHandle_t lnk_hdl;
  hpss_Attrs_t lnk_attr;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!parent_directory_handle || !p_context || !object_handle || !p_dirname)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mkdir);

  /* convert FSAL mode to HPSS mode. */
  unix_mode = fsal2unix_mode(accessmode);

  /* Apply umask */
  unix_mode = unix_mode & ~global_fs_info.umask;

  TakeTokenFSCall();

  rc = HPSSFSAL_MkdirHandle(&(parent_directory_handle->data.ns_handle),
                            p_dirname->name,
                            unix_mode,
                            &(p_context->credential.hpss_usercred),
                            &lnk_hdl, (object_attributes ? &lnk_attr : NULL));

  ReleaseTokenFSCall();

  /* /!\ WARNING : When the directory handle is stale, HPSS returns ENOTDIR.
   * If the returned value is HPSS_ENOTDIR, parent handle MAY be stale.
   * Thus, we must double-check by calling getattrs.   
   */
  if(rc == HPSS_ENOTDIR || rc == HPSS_ENOENT)
    {
      if(HPSSFSAL_IsStaleHandle(&parent_directory_handle->data.ns_handle,
                                &p_context->credential.hpss_usercred))
        {
          Return(ERR_FSAL_STALE, -rc, INDEX_FSAL_mkdir);
        }
    }

  /* other errors */

  if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_mkdir);

  /* set output handle   */
  memset( (char *)object_handle, 0, sizeof( hpssfsal_handle_t ) ) ;
  object_handle->data.obj_type = FSAL_TYPE_DIR;
  object_handle->data.ns_handle = lnk_hdl;

  if(object_attributes)
    {

      fsal_status_t status;

      /* convert hpss attributes to fsal attributes */

      status = hpss2fsal_attributes(&lnk_hdl, &lnk_attr, object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_mkdir);

}

/**
 * FSAL_link:
 * Create a hardlink.
 *
 * \param target_handle (input):
 *        Handle of the target object.
 * \param dir_handle (input):
 *        Pointer to the directory handle where
 *        the hardlink is to be created.
 * \param p_link_name (input):
 *        Pointer to the name of the hardlink to be created.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the directory to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param attributes (optionnal input/output): 
 *        The post_operation attributes of the linked object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (target_handle or dir_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_EXIST, ERR_FSAL_IO, ...
 *            
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the attributes->asked_attributes field.
 */
fsal_status_t HPSSFSAL_link(hpssfsal_handle_t * target_handle,  /* IN */
                            hpssfsal_handle_t * dir_handle,     /* IN */
                            fsal_name_t * p_link_name,  /* IN */
                            hpssfsal_op_context_t * p_context,  /* IN */
                            fsal_attrib_list_t * attributes     /* [ IN/OUT ] */
    )
{

  int rc;

  /* sanity checks.
   * note : attributes is optional.
   */
  LogFullDebug(COMPONENT_FSAL,"%p %p %p %p \n", target_handle, dir_handle, p_context, p_link_name);

  if(!target_handle || !dir_handle || !p_context || !p_link_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_link);

  /* Tests if hardlinking is allowed by configuration. */

  if(!global_fs_info.link_support)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_link);

  /* Call to HPSS API */

  TakeTokenFSCall();

  rc = hpss_LinkHandle(&(target_handle->data.ns_handle),     /* IN - Handle of existing file */
                       &(dir_handle->data.ns_handle),        /* IN - parent directory handle */
                       p_link_name->name,       /* IN - New name of the object */
                       &(p_context->credential.hpss_usercred)); /* IN - pointer to user credentials */

  ReleaseTokenFSCall();

  /* /!\ WARNING : When one of the handles is stale, HPSS returns ENOTDIR or ENOENT.
   * Thus, we must check this by calling HPSSFSAL_IsStaleHandle.
   */
  if(rc == HPSS_ENOTDIR || rc == HPSS_ENOENT)
    {
      if(HPSSFSAL_IsStaleHandle(&dir_handle->data.ns_handle,
                                &p_context->credential.hpss_usercred)
         ||
         HPSSFSAL_IsStaleHandle(&target_handle->data.ns_handle,
                                &p_context->credential.hpss_usercred))
        {
          Return(ERR_FSAL_STALE, -rc, INDEX_FSAL_link);
        }
    }

  /* other errors */
  if(rc)
    Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_link);

  /* optionnaly get attributes */

  if(attributes)
    {

      fsal_status_t st;

      st = HPSSFSAL_getattrs(target_handle, p_context, attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(attributes->asked_attributes);
          FSAL_SET_MASK(attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_link);

}

/**
 * FSAL_mknode:
 * Create a special object in the filesystem.
 * Not supported upon HPSS.
 *
 * \return ERR_FSAL_NOTSUPP.
 */
fsal_status_t HPSSFSAL_mknode(hpssfsal_handle_t * parentdir_handle,     /* IN */
                              fsal_name_t * p_node_name,        /* IN */
                              hpssfsal_op_context_t * p_context,        /* IN */
                              fsal_accessmode_t accessmode,     /* IN */
                              fsal_nodetype_t nodetype, /* IN */
                              fsal_dev_t * dev, /* IN */
                              hpssfsal_handle_t * p_object_handle,      /* OUT (handle to the created node) */
                              fsal_attrib_list_t * node_attributes      /* [ IN/OUT ] */
    )
{

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!parentdir_handle || !p_context || !nodetype || !dev || !p_node_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mknode);

  /* Not implemented */
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_mknode);

}
