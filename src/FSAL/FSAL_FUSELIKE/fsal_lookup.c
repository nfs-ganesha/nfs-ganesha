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
#include "namespace.h"
#include <string.h>

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
fsal_status_t FUSEFSAL_lookup(fsal_handle_t * parent_handle,      /* IN */
                              fsal_name_t * p_filename, /* IN */
                              fsal_op_context_t * p_context,        /* IN */
                              fsal_handle_t * obj_handle,        /* OUT */
                              fsal_attrib_list_t * object_attributes    /* [ IN/OUT ] */
    )
{

  int rc;
  fsal_status_t status;
  struct stat stbuff;
  fusefsal_handle_t * object_handle = (fusefsal_handle_t *)obj_handle;
  fusefsal_handle_t * parent_directory_handle = (fusefsal_handle_t *)parent_handle;

  /* sanity checks
   * note : object_attributes is optionnal
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  /* no getattr => no lookup !! */
  if(!p_fs_ops->getattr)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lookup);

  /* set current FS context */
  fsal_set_thread_context(p_context);

  /* retrieves root handle */

  if(!parent_directory_handle)
    {
      LogFullDebug(COMPONENT_FSAL, "lookup: root handle");

      /* check that p_filename is NULL,
       * else, parent_directory_handle should not
       * be NULL.
       */
      if(p_filename != NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      /* get root handle */
      TakeTokenFSCall();
      rc = p_fs_ops->getattr("/", &stbuff);
      ReleaseTokenFSCall();

      /* error getting root ?! => EIO */
      if(rc)
        Return(ERR_FSAL_IO, rc, INDEX_FSAL_lookup);

      if(stbuff.st_ino == 0)
        {
          /* filesystem does not provide inodes ! */
          LogDebug(COMPONENT_FSAL,
                   "WARNING in lookup: filesystem does not provide inode numbers");
          /* root will have inode nbr 1 */
          stbuff.st_ino = 1;
        }

      /* fill root handle */
      object_handle->data.inode = stbuff.st_ino;
      object_handle->data.device = stbuff.st_dev;

      rc = NamespaceGetGen(stbuff.st_ino, stbuff.st_dev, &object_handle->data.validator);

      /* root not in namespace ?! => EIO */
      if(rc)
        Return(ERR_FSAL_IO, rc, INDEX_FSAL_lookup);

      /* set root attributes, if asked */

      if(object_attributes)
        {
          fsal_status_t status = posix2fsal_attributes(&stbuff, object_attributes);

          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(object_attributes->asked_attributes);
              FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
            }
        }

    }
  else                          /* this is a real lookup(parent, name)  */
    {
      char parent_path[FSAL_MAX_PATH_LEN];
      char child_path[FSAL_MAX_PATH_LEN];

      /* the filename should not be null */
      if(p_filename == NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      /* get directory path */
      rc = NamespacePath(parent_directory_handle->data.inode,
                         parent_directory_handle->data.device,
                         parent_directory_handle->data.validator, parent_path);
      if(rc)
        Return(ERR_FSAL_STALE, rc, INDEX_FSAL_lookup);

      LogFullDebug(COMPONENT_FSAL, "lookup: parent path='%s'", parent_path);

      /* TODO: check the parent type */

      /* case of '.' and '..' */

      if(!strcmp(p_filename->name, "."))
        {
          LogFullDebug(COMPONENT_FSAL, "lookup on '.'");
          strcpy(child_path, parent_path);
        }
      else if(!strcmp(p_filename->name, ".."))
        {
          LogFullDebug(COMPONENT_FSAL, "lookup on '..'");

          /* removing last '/<name>' if path != '/' */
          if(!strcmp(parent_path, "/"))
            {
              strcpy(child_path, parent_path);
            }
          else
            {
              char *p_char;

              strcpy(child_path, parent_path);
              p_char = strrchr(child_path, '/');

              /* if path is '/<name>', don't remove the first '/' */
              if(p_char == child_path)
                *(p_char + 1) = '\0';
              else if(p_char)
                *p_char = '\0';
            }
        }
      else
        {
          LogFullDebug(COMPONENT_FSAL, "lookup on '%s/%s'", parent_path, p_filename->name);
          FSAL_internal_append_path(child_path, parent_path, p_filename->name);
        }

      TakeTokenFSCall();
      rc = p_fs_ops->getattr(child_path, &stbuff);
      ReleaseTokenFSCall();

      LogFullDebug(COMPONENT_FSAL, "%s: gettattr status=%d", child_path, rc);

      if(rc)
        Return(fuse2fsal_error(rc, FALSE), rc, INDEX_FSAL_lookup);

      /* no '.' nor '..' in namespace */
      if(strcmp(p_filename->name, ".") && strcmp(p_filename->name, ".."))
        {
          if(stbuff.st_ino == 0)
            {
              /* filesystem does not provide inodes ! */
              LogDebug(COMPONENT_FSAL,
                       "WARNING in lookup: filesystem does not provide inode numbers !!!");

              if(!parent_directory_handle || !p_filename || !p_filename->name)
                {
                  LogCrit(COMPONENT_FSAL,
                          "CRITICAL: Segfault avoided !!!!! %p %p %p",
                          parent_directory_handle, p_filename,
                          p_filename ? p_filename->name : NULL);
                }
              else
                {
                  /* create a fake handle for child = hash of its parent and its name */
                  stbuff.st_ino =
                      hash_peer(parent_directory_handle->data.inode, p_filename->name);
                  LogFullDebug(COMPONENT_FSAL, "handle for %u, %s = %u",
                               (int)parent_directory_handle->data.inode, p_filename->name,
                               (int)stbuff.st_ino);
                }
            }

          object_handle->data.validator = stbuff.st_ctime;

          /* add handle to namespace */
          NamespaceAdd(parent_directory_handle->data.inode,
                       parent_directory_handle->data.device,
                       parent_directory_handle->data.validator,
                       p_filename->name,
                       stbuff.st_ino, stbuff.st_dev, &object_handle->data.validator);
        }
      else
        {
          rc = NamespaceGetGen(stbuff.st_ino, stbuff.st_dev, &object_handle->data.validator);
          LogEvent(COMPONENT_FSAL,
                   ". or .. is stale ??? ino=%d, dev=%d\n, validator=%d",
                   (int)stbuff.st_ino, (int)stbuff.st_dev,
                   (int)object_handle->data.validator);
          if(rc)
            Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_lookup);
        }

      /* output handle */
      object_handle->data.inode = stbuff.st_ino;
      object_handle->data.device = stbuff.st_dev;

      if(object_attributes)
        {
          fsal_status_t status = posix2fsal_attributes(&stbuff, object_attributes);

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
fsal_status_t FUSEFSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                                      fsal_op_context_t * p_context,        /* IN */
                                      fsal_handle_t * p_fsoot_handle,       /* OUT */
                                      fsal_attrib_list_t * p_fsroot_attributes  /* [ IN/OUT ] */
    )
{

  /* Not supported for FUSE FSAL */
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lookupJunction);

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

fsal_status_t FUSEFSAL_lookupPath(fsal_path_t * p_path, /* IN */
                                  fsal_op_context_t * p_context,    /* IN */
                                  fsal_handle_t * object_handle,    /* OUT */
                                  fsal_attrib_list_t * object_attributes        /* [ IN/OUT ] */
    )
{

  fsal_name_t obj_name = FSAL_NAME_INITIALIZER; /* empty string */
  char *ptr_str;
  fsal_handle_t out_hdl;
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

  status = FUSEFSAL_lookup(NULL,        /* looking up for root */
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

      fsal_handle_t in_hdl;
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
      status = FUSEFSAL_lookup(&in_hdl, /* parent directory handle */
                               &obj_name,       /* object name */
                               p_context,       /* user's credentials */
                               &out_hdl,        /* output root handle */
                               /* retrieves attributes if this is the last lookup : */
                               (b_is_last ? object_attributes : NULL));

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_lookupPath);

      /* ptr_str is ok, we are ready for next loop */
    }

  (*object_handle) = out_hdl;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);

}
