/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_attrs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/09/09 15:22:49 $
 * \version $Revision: 1.19 $
 * \brief   Attributes functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "namespace.h"

/**
 * FSAL_getattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user, export...).
 * \param object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (object_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Another error code if an error occured.
 */
fsal_status_t FUSEFSAL_getattrs(fsal_handle_t *handle, /* IN */
                                fsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * object_attributes  /* IN/OUT */
    )
{
  fusefsal_handle_t * filehandle = (fusefsal_handle_t *)handle;
  int rc;
  fsal_status_t status;
  struct stat obj_stat;
  char object_path[FSAL_MAX_PATH_LEN];

  /* sanity checks.
   * note : object_attributes is mandatory in FSAL_getattrs.
   */
  if(!filehandle || !p_context || !object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

  /* get the full path for the object */
  rc = NamespacePath(filehandle->data.inode, filehandle->data.device, filehandle->data.validator,
                     object_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_getattrs);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  if(p_fs_ops->getattr)
    {
      TakeTokenFSCall();

      rc = p_fs_ops->getattr(object_path, &obj_stat);

      ReleaseTokenFSCall();

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_getattrs);
    }
  else
    {
      /* return void attributes...
       * Actually, should never occur since getattr
       * is needed for building entry's handle.
       */

      LogDebug(COMPONENT_FSAL,
               "FSAL_getattr WARNING: getattr is not implemented on this filesystem! Returning dummy values.");

      obj_stat.st_dev = filehandle->data.device;
      obj_stat.st_ino = filehandle->data.inode;
      obj_stat.st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
      obj_stat.st_nlink = 1;
      obj_stat.st_uid = 0;
      obj_stat.st_gid = 0;
      obj_stat.st_rdev = 0;
      obj_stat.st_size = 0;
      obj_stat.st_blksize = 512;
      obj_stat.st_blocks = 0;
      obj_stat.st_atime = time(NULL);
      obj_stat.st_mtime = time(NULL);
      obj_stat.st_ctime = time(NULL);
    }

  /* convert to FSAL attributes */

  status = posix2fsal_attributes(&obj_stat, object_attributes);

  if(FSAL_IS_ERROR(status))
    {
      FSAL_CLEAR_MASK(object_attributes->asked_attributes);
      FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
      Return(status.major, status.minor, INDEX_FSAL_getattrs);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs);

}

/**
 * FSAL_setattrs:
 * Set attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param attrib_set (mandatory input):
 *        The attributes to be set for the object.
 *        It defines the attributes that the caller
 *        wants to set and their values.
 * \param object_attributes (optionnal input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (object_handle does not address an existing object)
 *        - ERR_FSAL_INVAL        (tried to modify a read-only attribute)
 *        - ERR_FSAL_ATTRNOTSUPP  (tried to modify a non-supported attribute)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Another error code if an error occured.
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */

fsal_status_t FUSEFSAL_setattrs(fsal_handle_t *handle, /* IN */
                                fsal_op_context_t * p_context,      /* IN */
                                fsal_attrib_list_t * attrib_set,        /* IN */
                                fsal_attrib_list_t * object_attributes  /* [ IN/OUT ] */
    )
{
  fusefsal_handle_t * filehandle = (fusefsal_handle_t *)handle;
  int rc;
  fsal_status_t status;
  fsal_attrib_list_t attrs;
  fsal_attrib_list_t tmp_attrs;
  char object_path[FSAL_MAX_PATH_LEN];

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!filehandle || !p_context || !attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  /* local copy of attributes */
  attrs = *attrib_set;

  /* First, check that FSAL attributes changes are allowed. */

  /* Is it allowed to change times ? */

  if(!global_fs_info.cansettime)
    {

      if(attrs.asked_attributes
         & (FSAL_ATTR_ATIME | FSAL_ATTR_CREATION | FSAL_ATTR_CTIME | FSAL_ATTR_MTIME))
        {

          /* handled as an unsettable attribute. */
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_setattrs);
        }

    }

  /* apply umask, if mode attribute is to be changed */

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    {
      attrs.mode &= (~global_fs_info.umask);
    }

  /* get the path for this entry */

  rc = NamespacePath(filehandle->data.inode, filehandle->data.device, filehandle->data.validator,
                     object_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_setattrs);

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  /**********
   *  CHMOD *
   **********/
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    {
      /* /!\ this must be ignored for symlinks */
      /* We must retrieve initial value of atime and mtime because
       * utimens changes both of them    */
      fsal_status_t status;

      FSAL_CLEAR_MASK(tmp_attrs.asked_attributes);
      FSAL_SET_MASK(tmp_attrs.asked_attributes, FSAL_ATTR_TYPE);

      status = FUSEFSAL_getattrs(filehandle, p_context, &tmp_attrs);

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_setattrs);

      if((tmp_attrs.type != FSAL_TYPE_LNK) && (p_fs_ops->chmod != NULL))
        {
          TakeTokenFSCall();
          rc = p_fs_ops->chmod(object_path, fsal2unix_mode(attrs.mode));
          ReleaseTokenFSCall();

          LogFullDebug(COMPONENT_FSAL, "chmod: status = %d", rc);

          if(rc)
            Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_setattrs);
        }
      /* else : ignored */

    }

  /*************
   *  TRUNCATE *
   *************/
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_SIZE))
    {

      if(p_fs_ops->truncate)
        {
          TakeTokenFSCall();
          rc = p_fs_ops->truncate(object_path, (off_t) attrs.filesize);
          ReleaseTokenFSCall();

          LogFullDebug(COMPONENT_FSAL, "truncate: status = %d", rc);

          if(rc)
            Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_setattrs);
        }
      /* else : ignored */

    }

  /***********
   *  CHOWN  *
   ***********/
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER))
    {
      if((p_context->credential.user != 0) && (p_context->credential.user != attrs.owner))
        {
          LogEvent(COMPONENT_FSAL,
                   "FSAL_setattr: Denied user %d to change object's owner to %d",
                   p_context->credential.user, attrs.owner);
          Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
        }
    }

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_GROUP))
    {
      if((p_context->credential.user != 0)
         && (p_context->credential.group != attrs.group))
        {
          LogEvent(COMPONENT_FSAL,
                   "FSAL_setattr: Denied user %d (group %d) to change object's group to %d",
                   p_context->credential.user, p_context->credential.group,
                   attrs.group);
          Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
        }
    }

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER | FSAL_ATTR_GROUP))
    {
      if(p_fs_ops->chown)
        {
          TakeTokenFSCall();
          rc = p_fs_ops->chown(object_path,
                               FSAL_TEST_MASK(attrs.asked_attributes,
                                              FSAL_ATTR_OWNER) ? (uid_t) attrs.owner : -1,
                               FSAL_TEST_MASK(attrs.asked_attributes,
                                              FSAL_ATTR_GROUP) ? (gid_t) attrs.group :
                               -1);
          ReleaseTokenFSCall();

          LogFullDebug(COMPONENT_FSAL, "chown: status = %d", rc);

          if(rc)
            Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_setattrs);
        }
      /* else : ignored */
    }

  /***********
   *  UTIME  *
   ***********/
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME | FSAL_ATTR_MTIME))
    {

      /* We must retrieve initial value of atime and mtime because
       * utimens changes both of them    */
      fsal_status_t status;

      FSAL_CLEAR_MASK(tmp_attrs.asked_attributes);
      FSAL_SET_MASK(tmp_attrs.asked_attributes, FSAL_ATTR_ATIME);
      FSAL_SET_MASK(tmp_attrs.asked_attributes, FSAL_ATTR_MTIME);

      status = FUSEFSAL_getattrs(filehandle, p_context, &tmp_attrs);

      if(FSAL_IS_ERROR(status))
        Return(status.major, status.minor, INDEX_FSAL_setattrs);

      /* utimens is provided */

      if(p_fs_ops->utimens)
        {
          struct timespec tv[2];

          tv[0].tv_sec =
              (FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME) ? (time_t)
               attrs.atime.seconds : tmp_attrs.atime.seconds);
          tv[0].tv_nsec =
              (FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME) ? (time_t)
               attrs.atime.nseconds : tmp_attrs.atime.nseconds);

          tv[1].tv_sec =
              (FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME) ? (time_t)
               attrs.mtime.seconds : tmp_attrs.mtime.seconds);
          tv[1].tv_nsec =
              (FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME) ? (time_t)
               attrs.mtime.nseconds : tmp_attrs.mtime.nseconds);

          TakeTokenFSCall();
          rc = p_fs_ops->utimens(object_path, tv);
          ReleaseTokenFSCall();

          LogFullDebug(COMPONENT_FSAL, "utimens: status = %d", rc);

          if(rc)
            Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_setattrs);
        }
      else if(p_fs_ops->utime)
        {
          /* utime is provided */
          struct utimbuf utb;

          utb.actime =
              (FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME) ? (time_t)
               attrs.atime.seconds : tmp_attrs.atime.seconds);
          utb.modtime =
              (FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME) ? (time_t)
               attrs.mtime.seconds : tmp_attrs.mtime.seconds);

          TakeTokenFSCall();
          rc = p_fs_ops->utime(object_path, &utb);
          ReleaseTokenFSCall();

          LogFullDebug(COMPONENT_FSAL, "utime: status = %d", rc);

          if(rc)
            Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_setattrs);
        }
      /* else : ignored */

    }

  /* atime/mtime */
  /* Optionaly fill output attributes. */
  if(object_attributes)
    {

      status = FUSEFSAL_getattrs(filehandle, p_context, object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_setattrs);

}                               /* FSAL_setattrs */
