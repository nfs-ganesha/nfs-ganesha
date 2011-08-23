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

#include <string.h>

extern size_t i_snapshots;
extern snapshot_t *p_snapshots;

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
fsal_status_t ZFSFSAL_lookup(fsal_handle_t * parent_hdl,      /* IN */
                          fsal_name_t * p_filename,     /* IN */
                          fsal_op_context_t * context,        /* IN */
                          fsal_handle_t * obj_handle,        /* OUT */
                          fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    )
{
  int rc;
  zfsfsal_handle_t * parent_directory_handle = (zfsfsal_handle_t *)parent_hdl;
  zfsfsal_op_context_t * p_context = (zfsfsal_op_context_t *)context;
  zfsfsal_handle_t * object_handle = (zfsfsal_handle_t *)obj_handle;

  /* sanity checks
   * note : object_attributes is optionnal
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  /* retrieves root handle */

  if(!parent_directory_handle)
    {
      /* check that p_filename is NULL,
       * else, parent_directory_handle should not
       * be NULL.
       */
      if(p_filename != NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      /* >> retrieve root handle filehandle here << */
      if((rc = libzfswrap_getroot(p_context->export_context->p_vfs, &(object_handle->data.zfs_handle))))
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      object_handle->data.type = FSAL_TYPE_DIR;
      object_handle->data.i_snap = 0;

      /* >> retrieves root attributes, if asked << */

      if(object_attributes)
        {
          fsal_status_t status = ZFSFSAL_getattrs(obj_handle, context, object_attributes);
          /* On error, we set a flag in the returned attributes */
          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(object_attributes->asked_attributes);
              FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
            }
        }

    }
  else                          /* this is a real lookup(parent, name)  */
    {
      /* the filename should not be null */
      if(p_filename == NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      /* >> Be careful about junction crossing, symlinks, hardlinks,...
       * You may check the parent type if it's sored into the handle <<
       */
      switch (parent_directory_handle->data.type)
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

      TakeTokenFSCall();

      /* >> Call your filesystem lookup function here << */
      /* >> Be carefull you don't traverse junction nor follow symlinks << */
      inogen_t object;
      int type;
      char i_snap = parent_directory_handle->data.i_snap;

      /* Hook to add the hability to go inside a .zfs directory inside the root dir */
      if(parent_directory_handle->data.zfs_handle.inode == 3 &&
          !strcmp(p_filename->name, ZFS_SNAP_DIR))
      {
        LogDebug(COMPONENT_FSAL, "Lookup for the .zfs/ pseudo-directory");

        object.inode = ZFS_SNAP_DIR_INODE;
        object.generation = 0;
        type = S_IFDIR;
        rc = 0;
      }

      /* Hook for the files inside the .zfs directory */
      else if(parent_directory_handle->data.zfs_handle.inode == ZFS_SNAP_DIR_INODE)
      {
        LogDebug(COMPONENT_FSAL, "Lookup inside the .zfs/ pseudo-directory");

        ZFSFSAL_VFS_RDLock();
        int i;
        for(i = 1; i < i_snapshots + 1; i++)
          if(!strcmp(p_snapshots[i].psz_name, p_filename->name))
            break;

        if(i == i_snapshots + 1)
        {
          ReleaseTokenFSCall();
          Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_lookup);
        }

        libzfswrap_getroot(p_snapshots[i].p_vfs, &object);
        ZFSFSAL_VFS_Unlock();

        type = S_IFDIR;
        i_snap = i + 1;
        rc = 0;
      }
      else
      {
        /* Get the right VFS */
        ZFSFSAL_VFS_RDLock();
        libzfswrap_vfs_t *p_vfs = ZFSFSAL_GetVFS(parent_directory_handle);
        if(!p_vfs) {
          rc = ENOENT;
        } else {
	  creden_t cred;

	  cred.uid = p_context->credential.user;
	  cred.gid = p_context->credential.group;
          rc = libzfswrap_lookup(p_vfs, &cred,
                                 parent_directory_handle->data.zfs_handle, p_filename->name,
                                 &object, &type);
	}
        ZFSFSAL_VFS_Unlock();

        //FIXME!!! Hook to remove the i_snap bit when going up from the .zfs directory
        if(object.inode == 3)
          i_snap = 0;
      }

      ReleaseTokenFSCall();

      /* >> convert the error code and return on error << */
      if(rc)
        Return(posix2fsal_error(rc), rc, INDEX_FSAL_lookup);

      /* >> set output handle << */
      object_handle->data.zfs_handle = object;
      object_handle->data.type = posix2fsal_type(type);
      object_handle->data.i_snap = i_snap;
      if(object_attributes)
        {
          fsal_status_t status = ZFSFSAL_getattrs(obj_handle, context, object_attributes);
          /* On error, we set a flag in the returned attributes */
          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(object_attributes->asked_attributes);
              FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
            }
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
fsal_status_t ZFSFSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                                     fsal_op_context_t * p_context,        /* IN */
                                     fsal_handle_t * p_fsoot_handle,       /* OUT */
                                     fsal_attrib_list_t * p_fsroot_attributes      /* [ IN/OUT ] */
    )
{
  /* sanity checks
   * note : p_fsroot_attributes is optionnal
   */
  if(!p_junction_handle || !p_fsoot_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupJunction);

  /* >> you can also check object type if it is in stored in the handle << */

  if(((zfsfsal_handle_t *)p_junction_handle)->data.type != FSAL_TYPE_JUNCTION)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupJunction);

  TakeTokenFSCall();

  /* >> traverse the junction here << */

  ReleaseTokenFSCall();

  /* >> convert the error code and return on error << */

  /* >> set output handle << */

  if(p_fsroot_attributes)
    {

      /* >> fill output attributes if asked << */

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

fsal_status_t ZFSFSAL_lookupPath(fsal_path_t * p_path,     /* IN */
                              fsal_op_context_t * p_context,    /* IN */
                              fsal_handle_t * object_handle,    /* OUT */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    )
{
  fsal_name_t obj_name = FSAL_NAME_INITIALIZER; /* empty string */
  char *ptr_str;
  zfsfsal_handle_t out_hdl;
  fsal_status_t status;
  int b_is_last = FALSE;        /* is it the last lookup ? */

/*>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
 *  this function may be adapted to most FSALs
 *<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/

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

  status = ZFSFSAL_lookup(NULL,    /* looking up for root */
                          NULL,    /* empty string to get root handle */
                          p_context,       /* user's credentials */
                          (fsal_handle_t *) &out_hdl,        /* output root handle */
                          /* retrieves attributes if this is the last lookup : */
                          (b_is_last ? object_attributes : NULL));

  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_lookupPath);

  /* exits if this was the last lookup */

  if(b_is_last)
    {
      (*(zfsfsal_handle_t *)object_handle) = out_hdl;
      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);
    }

  /* proceed a step by step lookup */

  while(ptr_str[0])
    {

      zfsfsal_handle_t in_hdl;
      char *dest_ptr;

      /* preparing lookup */

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
      status = ZFSFSAL_lookup((fsal_handle_t *) &in_hdl,     /* parent directory handle */
                              &obj_name,   /* object name */
                              p_context,   /* user's credentials */
                              (fsal_handle_t *) &out_hdl,    /* output root handle */
                              /* retrieves attributes if this is the last lookup : */
                              (b_is_last ? object_attributes : NULL));

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookupPath);

      /* if the target object is a junction, an we allow cross junction lookups,
       * we cross it.
       */
      if(global_fs_info.auth_exportpath_xdev
         && (out_hdl.data.type == FSAL_TYPE_JUNCTION))
        {
          zfsfsal_handle_t tmp_hdl;

          tmp_hdl = out_hdl;

          /*call to FSAL_lookup */
          status = ZFSFSAL_lookupJunction((fsal_handle_t *) &tmp_hdl,        /* object handle */
                                          p_context,       /* user's credentials */
                                          (fsal_handle_t *) &out_hdl, /* output root handle */
                                          /* retrieves attributes if this is the last lookup : */
                                          (b_is_last ? object_attributes : NULL));

        }

      /* ptr_str is ok, we are ready for next loop */
    }

  (*(zfsfsal_handle_t *)object_handle) = out_hdl;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);

}
