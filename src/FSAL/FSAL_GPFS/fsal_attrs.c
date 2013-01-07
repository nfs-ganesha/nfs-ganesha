/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 *
 * \file    fsal_attrs.c
 * \brief   Attributes functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <sys/time.h>

extern fsal_status_t gpfsfsal_xstat_2_fsal_attributes(
                                               gpfsfsal_xstat_t *p_buffxstat,
                                               struct attrlist *p_fsalattr_out);

#ifdef _USE_NFS4_ACL
extern fsal_status_t fsal_acl_2_gpfs_acl(fsal_acl_t *p_fsalacl,
                                        gpfsfsal_xstat_t *p_buffxstat);
#endif                          /* _USE_NFS4_ACL */

/**
 * GPFSFSAL_getattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t GPFSFSAL_getattrs(struct fsal_export *export,              /* IN */
                                const struct req_op_context *p_context,   /* IN */
                                struct gpfs_file_handle *p_filehandle,   /* IN */
                                struct attrlist *p_object_attributes) /* IN/OUT*/

{
  fsal_status_t st;
  gpfsfsal_xstat_t buffxstat;
  int mntfd;
#ifdef _USE_NFS4_ACL
  fsal_accessflags_t access_mask = 0;
#endif

  /* sanity checks.
   * note : object_attributes is mandatory in GPFSFSAL_getattrs.
   */
  if(!p_filehandle || !export || !p_object_attributes)
    return fsalstat(ERR_FSAL_FAULT, 0);

  mntfd = gpfs_get_root_fd(export);

  st = fsal_get_xstat_by_handle(mntfd, p_filehandle, &buffxstat);
  if(FSAL_IS_ERROR(st))
    return(st);

  /* convert attributes */
  st = gpfsfsal_xstat_2_fsal_attributes(&buffxstat, p_object_attributes);
  if(FSAL_IS_ERROR(st))
    {
      FSAL_CLEAR_MASK(p_object_attributes->mask);
      FSAL_SET_MASK(p_object_attributes->mask, ATTR_RDATTR_ERR);
      return(st);
    }

#ifdef _USE_NFS4_ACL
   if(p_context == NULL)
      return fsalstat(ERR_FSAL_NO_ERROR, 0);

   /* Check permission to get attributes and ACL. */
    access_mask = FSAL_MODE_MASK_SET(0) |  /* Dummy */
                  FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ATTR |
                                     FSAL_ACE_PERM_READ_ACL);

    if(!export->ops->fs_supports(export, fso_accesscheck_support))
      st = fsal_internal_testAccess(p_context, access_mask, p_object_attributes);
    else
      st = fsal_internal_access(mntfd, p_context, p_filehandle, access_mask,
                                p_object_attributes);

      if(FSAL_IS_ERROR(st))
        return(st);
#endif

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * GPFSFSAL_setattrs:
 * Set attributes for the object specified by its filehandle.
 *
 * \param dir_hdl (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param p_attrib_set (mandatory input):
 *        The attributes to be set for the object.
 *        It defines the attributes that the caller
 *        wants to set and their values.
 * \param p_object_attributes (optionnal input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t GPFSFSAL_setattrs(struct fsal_obj_handle *dir_hdl,         /* IN*/
                            const struct req_op_context * p_context,     /* IN */
                            struct attrlist * p_attrib_set,             /* IN */
                            struct attrlist * p_object_attributes)   /* IN/OUT */
{
  unsigned int i, mntfd;
  fsal_status_t status;
  struct gpfs_fsal_obj_handle *myself;
  gpfsfsal_xstat_t buffxstat;
  struct attrlist current_attrs;

  /* Indicate if stat or acl or both should be changed. */
  int attr_valid = 0;

  /* Indiate which attribute in stat should be changed. */
  int attr_changed = 0;


  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!dir_hdl || !p_context || !p_attrib_set)
    return fsalstat(ERR_FSAL_FAULT, 0);

  myself = container_of(dir_hdl, struct gpfs_fsal_obj_handle, obj_handle);
  mntfd = gpfs_get_root_fd(dir_hdl->export);


  /* First, check that FSAL attributes changes are allowed. */

  /* Is it allowed to change times ? */

  if(!dir_hdl->export->ops->fs_supports(dir_hdl->export, fso_cansettime))
    {

      if(p_object_attributes->mask
         & (ATTR_ATIME | ATTR_CREATION | ATTR_CTIME | ATTR_MTIME |
                 ATTR_MTIME_SERVER | ATTR_ATIME_SERVER))
        {
          /* handled as an unsettable attribute. */
          return fsalstat(ERR_FSAL_INVAL, 0);
        }
    }

  /* apply umask, if mode attribute is to be changed */
  if(FSAL_TEST_MASK(p_object_attributes->mask, ATTR_MODE))
    {
      p_object_attributes->mode &= ~dir_hdl->export->ops->fs_umask(dir_hdl->export);
    }

  /* get current attributes */
  current_attrs.mask = dir_hdl->export->ops->fs_supported_attrs(dir_hdl->export);
  status = GPFSFSAL_getattrs(dir_hdl->export, p_context, myself->handle,
                             &current_attrs);
  if(FSAL_IS_ERROR(status))
      return(status);
  /**************
   *  TRUNCATE  *
   **************/

  if (FSAL_TEST_MASK(p_object_attributes->asked_attributes, ATTR_SIZE)) {
      attr_changed |= XATTR_SIZE;
      /* Fill wanted mode. */
      buffxstat.buffstat.st_size = p_object_attributes->filesize;
      LogDebug(COMPONENT_FSAL,
              "current size = %llu, new size = %llu",
              (unsigned long long) current_attrs.filesize,
              (unsigned long long) buffxstat.buffstat.st_size);
  }

  /***********
   *  CHMOD  *
   ***********/
  if(FSAL_TEST_MASK(p_object_attributes->mask, ATTR_MODE))
    {

      /* The POSIX chmod call don't affect the symlink object, but
       * the entry it points to. So we must ignore it.
       */
      if(current_attrs.type != SYMBOLIC_LINK)
        {
            attr_changed |= XATTR_MODE;
    
            /* Fill wanted mode. */
            buffxstat.buffstat.st_mode = fsal2unix_mode(p_object_attributes->mode);
            LogDebug(COMPONENT_FSAL,
                     "current mode = %o, new mode = %o",
                     fsal2unix_mode(current_attrs.mode), buffxstat.buffstat.st_mode);

        }

    }

  /***********
   *  CHOWN  *
   ***********/

  if(FSAL_TEST_MASK(p_object_attributes->mask, ATTR_OWNER | ATTR_GROUP))
    {
      /*      LogFullDebug(COMPONENT_FSAL, "Performing chown(%s, %d,%d)",
                        fsalpath.path, FSAL_TEST_MASK(p_object_attributes->mask,
                                                      FSAL_ATTR_OWNER) ? (int)p_object_attributes->owner
                        : -1, FSAL_TEST_MASK(p_object_attributes->mask,
			FSAL_ATTR_GROUP) ? (int)p_object_attributes->group : -1);*/

      attr_changed |= FSAL_TEST_MASK(p_object_attributes->mask, ATTR_OWNER) ?
                      XATTR_UID : XATTR_GID;

      /* Fill wanted owner. */
      if(FSAL_TEST_MASK(p_object_attributes->mask, ATTR_OWNER))
        {
          buffxstat.buffstat.st_uid = (int)p_object_attributes->owner;
          LogDebug(COMPONENT_FSAL,
                   "current uid = %ld, new uid = %d",
                   current_attrs.owner, buffxstat.buffstat.st_uid);
        }

      /* Fill wanted group. */
      if(FSAL_TEST_MASK(p_object_attributes->mask, ATTR_GROUP))
        {
          buffxstat.buffstat.st_gid = (int)p_object_attributes->group;
          LogDebug(COMPONENT_FSAL,
                   "current gid = %ld, new gid = %d",
                   current_attrs.group, buffxstat.buffstat.st_gid);
        }

    }

  /***********
   *  UTIME  *
   ***********/

  /* Fill wanted atime. */
  if(FSAL_TEST_MASK(p_object_attributes->mask, ATTR_ATIME))
  {
      attr_changed |= XATTR_ATIME;
      buffxstat.buffstat.st_atime = (time_t) p_object_attributes->atime.seconds;
      buffxstat.buffstat.st_atim.tv_nsec = p_object_attributes->atime.nseconds;
      LogDebug(COMPONENT_FSAL,
              "current atime = %lu, new atime = %lu",
              (unsigned long)current_attrs.atime.seconds, (unsigned long)buffxstat.buffstat.st_atime);
  }

  /* Fill wanted mtime. */
  if(FSAL_TEST_MASK(p_object_attributes->mask, ATTR_MTIME))
  {
      attr_changed |= XATTR_MTIME;
      buffxstat.buffstat.st_mtime = (time_t) p_object_attributes->mtime.seconds;
      buffxstat.buffstat.st_mtim.tv_nsec = p_object_attributes->mtime.nseconds;
      LogDebug(COMPONENT_FSAL,
              "current mtime = %lu, new mtime = %lu",
              (unsigned long)current_attrs.mtime.seconds, (unsigned long)buffxstat.buffstat.st_mtime);
  }
  /* Asking to set atime to NOW */
  if(FSAL_TEST_MASK(p_object_attributes->mask, ATTR_ATIME_SERVER))
  {
      attr_changed |= XATTR_ATIME_NOW;
      LogDebug(COMPONENT_FSAL,
              "current atime = %lu, new atime = NOW",
              (unsigned long)current_attrs.atime.seconds);
  }
  /* Asking to set atime to NOW */
  if(FSAL_TEST_MASK(p_object_attributes->mask, ATTR_MTIME_SERVER))
  {
      attr_changed |= XATTR_MTIME_NOW;
      LogDebug(COMPONENT_FSAL,
              "current mtime = %lu, new mtime = NOW",
              (unsigned long)current_attrs.atime.seconds);
  }

  /* If any stat changed, indicate that */
  if(attr_changed !=0)
      attr_valid |= XATTR_STAT;

#ifdef _USE_NFS4_ACL
   /***********
   *  ACL  *
   ***********/

  if(FSAL_TEST_MASK(p_object_attributes->mask, ATTR_ACL))
    {
      if(p_object_attributes->acl)
        {
          attr_valid |= XATTR_ACL;
          LogDebug(COMPONENT_FSAL, "setattr acl = %p", p_object_attributes->acl);

          /* Convert FSAL ACL to GPFS NFS4 ACL and fill the buffer. */
          status = fsal_acl_2_gpfs_acl(p_object_attributes->acl, &buffxstat);

          if(FSAL_IS_ERROR(status))
            return(status);
        }
      else
        {
          LogCrit(COMPONENT_FSAL, "setattr acl is NULL");
          return fsalstat(ERR_FSAL_FAULT, 0);
        }
    }
#endif                          /* _USE_NFS4_ACL */

  /* If there is any change in stat or acl or both, send it down to file system. */
  if(attr_valid !=0)
    {
      status = fsal_set_xstat_by_handle(mntfd, p_context,
                                        myself->handle,
                                        attr_valid,
                                        attr_changed,
                                        &buffxstat);

      if(FSAL_IS_ERROR(status))
        return(status);
    }

  /* Optionaly fills output attributes. */

  if(p_object_attributes)
    {
      status = GPFSFSAL_getattrs(dir_hdl->export, p_context, myself->handle,
                                 p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->mask);
          FSAL_SET_MASK(p_object_attributes->mask, ATTR_RDATTR_ERR);
        }
    }

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
