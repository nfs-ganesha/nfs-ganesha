// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_attrs.c
// Description: FSAL attributes operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_attrs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 15:53:39 $
 * \version $Revision: 1.31 $
 * \brief   HPSS-FSAL type translation functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <sys/time.h>

#include "pt_ganesha.h"

extern fsal_status_t 
posixstat64_2_fsal_attributes(struct stat64      * p_buffstat,
                              fsal_attrib_list_t * p_fsalattr_out);

extern fsal_status_t 
ptfsal_xstat_2_fsal_attributes(ptfsal_xstat_t     * p_buffxstat,
                               fsal_attrib_list_t * p_fsalattr_out);

#ifdef _USE_NFS4_ACL
extern fsal_status_t fsal_acl_2_ptfs_acl(fsal_acl_t     * p_fsalacl, 
                                         ptfsal_xstat_t * p_buffxstat);
#endif                          /* _USE_NFS4_ACL */

/**
 * PTFSAL_getattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t
PTFSAL_getattrs(fsal_handle_t      * p_filehandle,        /* IN */
                fsal_op_context_t  * p_context,           /* IN */
                fsal_attrib_list_t * p_object_attributes  /* IN/OUT */)
{
  fsal_status_t   st;
  int             stat_rc;
  fsi_stat_struct buffstat;
  ptfsal_op_context_t     * fsi_op_context  = (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context = 
    fsi_op_context->export_context;

#ifdef _USE_NFS4_ACL
  fsal_accessflags_t access_mask = 0;
#endif

  FSI_TRACE(FSI_DEBUG, "Begin-------------------\n");

  /* sanity checks.
   * note : object_attributes is mandatory in PTFSAL_getattrs.
   */
  if (!p_filehandle || !p_context || !p_object_attributes) { 
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);
  }

  stat_rc = ptfsal_stat_by_handle(p_filehandle, p_context, &buffstat);

  if (stat_rc) {
    Return(ERR_FSAL_INVAL, errno, INDEX_FSAL_getattrs);
  }

  /* convert attributes */

  st = posix2fsal_attributes(&buffstat, p_object_attributes);
  FSI_TRACE(FSI_DEBUG, "Handle type=%d st_mode=%o (octal)", 
            p_object_attributes->type, buffstat.st_mode);

  p_object_attributes->mounted_on_fileid = 
    fsi_export_context->ganesha_export_id;

  if (FSAL_IS_ERROR(st)) {
    FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
    FSAL_SET_MASK(p_object_attributes->asked_attributes, 
                  FSAL_ATTR_RDATTR_ERR);
    ReturnStatus(st, INDEX_FSAL_getattrs);
  }

#ifdef _USE_NFS4_ACL
      /* Check permission to get attributes and ACL. */
      access_mask = FSAL_MODE_MASK_SET(0) |  /* Dummy */
                    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ATTR |
                                       FSAL_ACE_PERM_READ_ACL);

    if (!p_context->export_context->fe_static_fs_info->accesscheck_support) {
      st = fsal_internal_testAccess(p_context, access_mask, NULL, 
                                    p_object_attributes);
    } else {
      st = fsal_internal_access(p_context, p_filehandle, access_mask,
                                p_object_attributes);
    } 
    if (FSAL_IS_ERROR(st)) {
      ReturnStatus(st, INDEX_FSAL_getattrs);
    }
#endif

  FSI_TRACE(FSI_DEBUG, "End-----------------------------\n");
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs);

}

/**
 * PTFSAL_getattrs_descriptor:
 * Get attributes for the object specified by its descriptor or by it's 
 * filehandle.
 *
 * \param p_file_descriptor (input):
 *        The file descriptor of the object to get parameters.
 * \param p_filehandle (input):
 *        The handle of the object to get parameters.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param p_object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t
PTFSAL_getattrs_descriptor(
  fsal_file_t        * p_file_descriptor,  /* IN */
  fsal_handle_t      * p_filehandle,       /* IN */
  fsal_op_context_t  * p_context,          /* IN */
  fsal_attrib_list_t * p_object_attributes /* IN/OUT */) 
{
  if (!p_file_descriptor) {
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs_descriptor);
  }
  ptfsal_file_t * p_file_desc = (ptfsal_file_t *)p_file_descriptor;
  FSI_TRACE(FSI_DEBUG, "FSI---File descriptor=%d\n", p_file_desc->fd);
  return PTFSAL_getattrs(p_filehandle, p_context, p_object_attributes);
}

/**
 * PTFSAL_setattrs:
 * Set attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param cred (input):
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
 *        - Another error code if an error occured.
 */
fsal_status_t
PTFSAL_setattrs(fsal_handle_t      * p_filehandle,       /* IN */
                fsal_op_context_t  * p_context,          /* IN */
                fsal_attrib_list_t * p_attrib_set,       /* IN */
                fsal_attrib_list_t * p_object_attributes /* [ IN/OUT ] */)
{
  unsigned int i;
  fsal_status_t status;

  ptfsal_xstat_t buffxstat;

  fsal_accessflags_t access_mask = 0;
  fsal_attrib_list_t wanted_attrs, current_attrs;
  mode_t             st_mode_in_cache = 0;
  char               fsi_name[PATH_MAX];
  int                rc;
  int fd;

  FSI_TRACE(FSI_DEBUG, "Begin-----------------------------------------\n");

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if (!p_filehandle || !p_context || !p_attrib_set) {
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);
  }

  /* local copy of attributes */
  wanted_attrs = *p_attrib_set;

  /* First, check that FSAL attributes changes are allowed. */
  if (!global_fs_info.cansettime) {

    if (wanted_attrs.asked_attributes
       & (FSAL_ATTR_ATIME | FSAL_ATTR_CREATION | FSAL_ATTR_CTIME 
       | FSAL_ATTR_MTIME)) {
       /* handled as an unsettable attribute. */
       Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_setattrs);
    }
  }

  /* apply umask, if mode attribute is to be changed */
  if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_MODE)) {
    wanted_attrs.mode &= (~global_fs_info.umask);
  }

  /* get current attributes */
  current_attrs.asked_attributes = PTFS_SUPPORTED_ATTRIBUTES;
  status = PTFSAL_getattrs(p_filehandle, p_context, &current_attrs);
  if (FSAL_IS_ERROR(status)) {
    FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
    FSAL_SET_MASK(p_object_attributes->asked_attributes, 
                  FSAL_ATTR_RDATTR_ERR);
    ReturnStatus(status, INDEX_FSAL_setattrs);
  }

  /**************
   *  TRUNCATE  *
   **************/

  if(FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_SIZE)) {

      status = fsal_internal_handle2fd(p_context, p_filehandle, &fd, O_RDONLY);

      if (FSAL_IS_ERROR(status)) {
        ReturnStatus(status, INDEX_FSAL_setattrs);
      }

      status = PTFSAL_truncate( p_filehandle,
                                p_context,   
                                wanted_attrs.filesize,                
                                &fd,     
                                p_object_attributes);

      if (FSAL_IS_ERROR(status)) {
        ReturnStatus(status, INDEX_FSAL_setattrs);
      }
    
   }
 

  /***********
   *  CHMOD  *
   ***********/
  if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_MODE)) {
    FSI_TRACE(FSI_DEBUG, "Begin chmod------------------\n");
    /* The POSIX chmod call don't affect the symlink object, but
     * the entry it points to. So we must ignore it.
     */
    if (current_attrs.type != FSAL_TYPE_LNK) {

      /* For modifying mode, user must be root or the owner */
      if ((p_context->credential.user != 0)
         && (p_context->credential.user != current_attrs.owner)) {
        FSI_TRACE(FSI_DEBUG,
                  "Permission denied for CHMOD opeartion: " 
                  "current owner=%d, credential=%d",
                  current_attrs.owner, 
                  p_context->credential.user);
        Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
      }

#ifdef _USE_NFS4_ACL
          /* Check permission using ACL. */
          access_mask = FSAL_MODE_MASK_SET(0) |  /* Dummy. */
                        FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ATTR);

          if (!p_context->export_context->fe_static_fs_info->accesscheck_support)
            status = fsal_internal_testAccess(p_context, access_mask, NULL, 
            &current_attrs);
          else
            status = fsal_internal_access(p_context, p_filehandle, access_mask,
                                          &current_attrs);

          if (FSAL_IS_ERROR(status))
            ReturnStatus(status, INDEX_FSAL_setattrs);
#endif

      /* Fill wanted mode. */
      buffxstat.buffstat.st_mode = fsal2unix_mode(wanted_attrs.mode);
      FSI_TRACE(FSI_DEBUG,
                "current mode = %o, new mode = %o",
                fsal2unix_mode(current_attrs.mode), 
                buffxstat.buffstat.st_mode);

      rc = fsi_get_name_from_handle(p_context, 
                                    p_filehandle->data.handle.f_handle, 
                                    fsi_name,
                                    NULL);
      if (rc < 0) {
        FSI_TRACE(FSI_ERR, 
                  "Failed to convert file handle back to filename" );
        FSI_TRACE(FSI_DEBUG, "Handle to name failed for hanlde %s", 
                  p_filehandle->data.handle.f_handle);
        Return (ERR_FSAL_BADHANDLE, 0, INDEX_FSAL_setattrs);
      }
      FSI_TRACE(FSI_DEBUG, "Handle to name: %s for handle %s", 
                fsi_name, p_filehandle->data.handle.f_handle);

      rc = ptfsal_chmod(p_context, fsi_name, 
                        buffxstat.buffstat.st_mode);
      if (rc == -1) {
        FSI_TRACE(FSI_ERR, "chmod FAILED");
        Return (ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
      } else {
        st_mode_in_cache = (buffxstat.buffstat.st_mode 
                            | fsal_type2unix(current_attrs.type));
        fsi_update_cache_stat(fsi_name, 
                              st_mode_in_cache, 
                              p_context->export_context->pt_export_id);
        FSI_TRACE(FSI_INFO, 
                  "Chmod SUCCEED with st_mode in cache being %o",
                  st_mode_in_cache);
      }

    }
    FSI_TRACE(FSI_DEBUG, "End chmod-------------------\n");
  }

  /***********
   *  CHOWN  *
   ***********/
  FSI_TRACE(FSI_DEBUG, "Begin chown------------------------------\n");
  /* Only root can change uid and A normal user must be in the group 
   * he wants to set 
   */
  if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_OWNER)) {

    /* For modifying owner, user must be root or current 
     * owner==wanted==client 
     */
    if ((p_context->credential.user != 0) &&
       ((p_context->credential.user != current_attrs.owner) ||
       (p_context->credential.user != wanted_attrs.owner))) {
      FSI_TRACE(FSI_DEBUG,
                "Permission denied for CHOWN opeartion: " 
                "current owner=%d, credential=%d, new owner=%d",
                current_attrs.owner, p_context->credential.user, 
                wanted_attrs.owner);
       Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
    }

#ifdef _USE_NFS4_ACL
          /* Check permission using ACL. */
          access_mask = FSAL_MODE_MASK_SET(0) |  /* Dummy. */
                        FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_OWNER);

        if (!p_context->export_context->fe_static_fs_info->accesscheck_support)
          status = fsal_internal_testAccess(p_context, access_mask, NULL, 
                                            &current_attrs);
        else
          status = fsal_internal_access(p_context, p_filehandle, access_mask,
                                        &current_attrs);

          if (FSAL_IS_ERROR(status))
            ReturnStatus(status, INDEX_FSAL_setattrs);
#endif

        }

  if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_GROUP)) {

    /* For modifying group, user must be root or current owner */
    if ((p_context->credential.user != 0)
       && (p_context->credential.user != current_attrs.owner)) {
       Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
    }

    int in_grp = 0;
    /* set in_grp */
    if (p_context->credential.group == wanted_attrs.group) {
      in_grp = 1;
    } else {
      for(i = 0; i < p_context->credential.nbgroups; i++) {
        if ((in_grp = (wanted_attrs.group == 
            p_context->credential.alt_groups[i])))
          break;
      }
    }
    /* it must also be in target group */
    if (p_context->credential.user != 0 && !in_grp) {
      FSI_TRACE(FSI_DEBUG,
                "Permission denied for CHOWN operation: " 
                "current group=%d, credential=%d, new group=%d",
                current_attrs.group, p_context->credential.group, 
                wanted_attrs.group);
      Return(ERR_FSAL_PERM, 0, INDEX_FSAL_setattrs);
    }

#ifdef _USE_NFS4_ACL
      /* Check permission using ACL. */
      access_mask = FSAL_MODE_MASK_SET(0) |  /* Dummy. */
                    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_OWNER);

      if (!p_context->export_context->fe_static_fs_info->accesscheck_support)
        status = fsal_internal_testAccess(p_context, access_mask, NULL, 
                      &current_attrs);
      else
        status = fsal_internal_access(p_context, p_filehandle, access_mask,
                                      &current_attrs);

      if (FSAL_IS_ERROR(status))
        ReturnStatus(status, INDEX_FSAL_setattrs);
#endif

    }

  if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_OWNER | 
     FSAL_ATTR_GROUP)) {

    /* Fill wanted owner. */
    if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_OWNER)) {
      buffxstat.buffstat.st_uid = (int)wanted_attrs.owner;
    } else {
      buffxstat.buffstat.st_uid = (int)current_attrs.owner;
    }
    FSI_TRACE(FSI_DEBUG,
              "current uid = %d, new uid = %d",
              current_attrs.owner, buffxstat.buffstat.st_uid);

    /* Fill wanted group. */
    if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_GROUP)) {
      buffxstat.buffstat.st_gid = (int)wanted_attrs.group;
    } else {
      buffxstat.buffstat.st_gid = (int)current_attrs.group;
    }
    FSI_TRACE(FSI_DEBUG,
              "current gid = %d, new gid = %d",
              current_attrs.group, buffxstat.buffstat.st_gid);

    rc = fsi_get_name_from_handle(p_context, 
                                  p_filehandle->data.handle.f_handle, 
                                  fsi_name,
                                  NULL);
    if (rc < 0) {
      FSI_TRACE(FSI_ERR, "Failed to convert file handle back to filename" );
      FSI_TRACE(FSI_DEBUG, "Handle to name failed for hanlde %s", 
                p_filehandle->data.handle.f_handle);
      Return (ERR_FSAL_BADHANDLE, 0, INDEX_FSAL_setattrs);
    }
   
    FSI_TRACE(FSI_DEBUG, "handle to name: %s for handle %s", 
              fsi_name, p_filehandle->data.handle.f_handle);
    rc = ptfsal_chown(p_context, fsi_name, buffxstat.buffstat.st_uid, 
                      buffxstat.buffstat.st_gid);
    if (rc == -1) {
      FSI_TRACE(FSI_ERR, "chown FAILED");
      Return (ERR_FSAL_PERM, 1, INDEX_FSAL_setattrs);
    } else {
      FSI_TRACE(FSI_INFO, "Chown SUCCEED");
    }
  }
  FSI_TRACE(FSI_DEBUG, "End chown-----------------------------------\n");

  /***********
   *  UTIME  *
   ***********/
  FSI_TRACE(FSI_DEBUG, "Begin UTIME-----------------------------------\n");
  /* user must be the owner or have read access to modify 'atime' */
  access_mask = FSAL_MODE_MASK_SET(FSAL_R_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ATTR);

  if (!p_context->export_context->fe_static_fs_info->accesscheck_support) {
    status = fsal_internal_testAccess(p_context, access_mask, NULL, 
                                      &current_attrs);
  } else {
    status = fsal_internal_access(p_context, p_filehandle, access_mask,
                                  &current_attrs);
  }

  if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_ATIME)
     && (p_context->credential.user != 0)
     && (p_context->credential.user != current_attrs.owner)
     && (status.major
         != ERR_FSAL_NO_ERROR)) {
    ReturnStatus(status, INDEX_FSAL_setattrs);
  }

  /* user must be the owner or have write access to modify 'mtime' */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ATTR);

  if (!p_context->export_context->fe_static_fs_info->accesscheck_support) {
    status = fsal_internal_testAccess(p_context, access_mask, NULL, 
                                      &current_attrs);
  } else {
    status = fsal_internal_access(p_context, p_filehandle, access_mask,
                                  &current_attrs);
  }
  if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_MTIME)
     && (p_context->credential.user != 0)
     && (p_context->credential.user != current_attrs.owner)
     && (status.major
         != ERR_FSAL_NO_ERROR)) {
     ReturnStatus(status, INDEX_FSAL_setattrs);
  }

  if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_ATIME | 
                    FSAL_ATTR_MTIME)) {

    /* Fill wanted atime. */
    if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_ATIME)) {
      buffxstat.buffstat.st_atime = (time_t) wanted_attrs.atime.seconds;
      FSI_TRACE(FSI_DEBUG,
                "current atime = %lu, new atime = %lu",
                (unsigned long)current_attrs.atime.seconds, 
                (unsigned long)buffxstat.buffstat.st_atime);
    } else {
      buffxstat.buffstat.st_atime = (time_t) current_attrs.atime.seconds;
    }
    FSI_TRACE(FSI_DEBUG,
              "current atime = %lu, new atime = %lu",
              (unsigned long)current_attrs.atime.seconds,
              (unsigned long)buffxstat.buffstat.st_atime);

    /* Fill wanted mtime. */
    if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_MTIME)) {
      buffxstat.buffstat.st_mtime = (time_t) wanted_attrs.mtime.seconds;
    } else {
      buffxstat.buffstat.st_mtime = (time_t) current_attrs.mtime.seconds;
    }
    FSI_TRACE(FSI_DEBUG,
              "current mtime = %lu, new mtime = %lu",
              (unsigned long)current_attrs.mtime.seconds,
              (unsigned long)buffxstat.buffstat.st_mtime);

    rc = fsi_get_name_from_handle(p_context, 
                                  p_filehandle->data.handle.f_handle, 
                                  fsi_name,
                                  NULL);
    if (rc < 0) {
      FSI_TRACE(FSI_ERR, 
                "Failed to convert file handle back to filename "  
                "from cache" );
      FSI_TRACE(FSI_DEBUG, "Handle to name failed for hanlde %s",
                p_filehandle->data.handle.f_handle);
      Return (ERR_FSAL_BADHANDLE, 0, INDEX_FSAL_setattrs);
    }

    FSI_TRACE(FSI_DEBUG, "Handle to name: %s for handle %s", 
              fsi_name, p_filehandle->data.handle.f_handle); 
   
    rc = ptfsal_ntimes(p_context, fsi_name, buffxstat.buffstat.st_atime, 
                       buffxstat.buffstat.st_mtime);
    if (rc == -1) {
      FSI_TRACE(FSI_ERR, "ntime FAILED");
      Return (ERR_FSAL_PERM, 2, INDEX_FSAL_setattrs);
    } else {
      FSI_TRACE(FSI_INFO, "ntime SUCCEED");
    }

  }
  FSI_TRACE(FSI_DEBUG, "End UTIME------------------------------\n");

#ifdef _USE_NFS4_ACL
   /***********
   *  ACL  *
   ***********/

  if (FSAL_TEST_MASK(wanted_attrs.asked_attributes, FSAL_ATTR_ACL)) {
    /* Check permission to set ACL. */
    access_mask = FSAL_MODE_MASK_SET(0) |  /* Dummy */
                  FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_ACL);

    if (!p_context->export_context->fe_static_fs_info->accesscheck_support)
      status = fsal_internal_testAccess(p_context, access_mask, NULL, 
                                        &current_attrs);
    else
      status = fsal_internal_access(p_context, p_filehandle, access_mask,
                                      &current_attrs);

    if (FSAL_IS_ERROR(status))
      ReturnStatus(status, INDEX_FSAL_setattrs);

    if (wanted_attrs.acl) {
      LogDebug(COMPONENT_FSAL, "setattr acl = %p", wanted_attrs.acl);

      /* Convert FSAL ACL to PTFS NFS4 ACL and fill the buffer. */
      status = fsal_acl_2_ptfs_acl(wanted_attrs.acl, &buffxstat);

      if (FSAL_IS_ERROR(status))
        ReturnStatus(status, INDEX_FSAL_setattrs);
    } else {
      LogCrit(COMPONENT_FSAL, "setattr acl is NULL");
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);
    }
  }
#endif                          /* _USE_NFS4_ACL */


  /* Optionaly fills output attributes. */

  if (p_object_attributes) {
    status = PTFSAL_getattrs(p_filehandle, p_context, p_object_attributes);

    /* on error, we set a special bit in the mask. */
    if (FSAL_IS_ERROR(status)) {
      FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
      FSAL_SET_MASK(p_object_attributes->asked_attributes, 
                    FSAL_ATTR_RDATTR_ERR);
    }

  }
  FSI_TRACE(FSI_DEBUG, "End--------------------------------------------\n");
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_setattrs);

}
