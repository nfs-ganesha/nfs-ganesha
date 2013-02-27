// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_dirs.c
// Description: FSAL directory operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_dirs.c
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

#include "pt_ganesha.h"
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <string.h>


/**
 * FSAL_opendir :
 *     Opens a directory for reading its content.
 *
 * \param dir_handle (input)
 *         the handle of the directory to be opened.
 * \param cred (input)
 *         Permission context for the operation (user,...).
 * \param dir_descriptor (output)
 *         pointer to an allocated structure that will receive
 *         directory stream informations, on successfull completion.
 * \param dir_attributes (optional output)
 *         On successfull completion,the structure pointed
 *         by dir_attributes receives the new directory attributes.
 *         May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t
PTFSAL_opendir(fsal_handle_t      * p_dir_handle,    /* IN */
               fsal_op_context_t  * p_context,       /* IN */
               fsal_dir_t         * dir_desc,        /* OUT */
               fsal_attrib_list_t * p_dir_attributes /* [ IN/OUT ] */)
{
  fsal_status_t status;
  fsal_accessflags_t access_mask = 0;
  fsal_attrib_list_t dir_attrs;
  ptfsal_dir_t *p_dir_descriptor = (ptfsal_dir_t *)dir_desc;

  int fsi_dir_handle;
  ptfsal_handle_t *p_fsi_handle = (ptfsal_handle_t *)p_dir_handle;
  char fsi_name[PATH_MAX];

  FSI_TRACE(FSI_DEBUG, "Begin opendir------------------------------\n");

  /* sanity checks
   * note : dir_attributes is optionnal.
   */
  if(!p_dir_handle || !p_context || !p_dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  FSI_TRACE(FSI_DEBUG, "FSI - Handle = %s", 
            p_fsi_handle->data.handle.f_handle);

  // FSI will open the dir here (so we don't leave it open forever
  if (fsi_get_name_from_handle(
    p_context, p_fsi_handle->data.handle.f_handle, fsi_name, NULL) < 0) {
    FSI_TRACE(FSI_DEBUG, "FSI - cannot find name for handle %s\n", 
              p_fsi_handle->data.handle.f_handle);
    Return(ERR_FSAL_NOENT, errno, INDEX_FSAL_opendir);
  }

  FSI_TRACE(FSI_DEBUG, "FSI - Dir name=%s\n", fsi_name);  
  fsi_dir_handle = ptfsal_opendir (p_context, fsi_name, NULL, 0);
  if (fsi_dir_handle < 0) {
    Return(ERR_FSAL_FAULT, errno, INDEX_FSAL_opendir);
  }
  p_dir_descriptor->fd = fsi_dir_handle;
  strncpy(p_dir_descriptor->path.path, fsi_name, 
          sizeof(p_dir_descriptor->path.path));
  p_dir_descriptor->path.path[sizeof(p_dir_descriptor->path.path) -1] = '\0';

  /* get directory metadata */
  dir_attrs.asked_attributes = PTFS_SUPPORTED_ATTRIBUTES;
    status = PTFSAL_getattrs(p_dir_handle, p_context, &dir_attrs);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_opendir);

  /* Test access rights for this directory */

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_R_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR);

  if(!p_context->export_context->fe_static_fs_info->accesscheck_support) {
    status = fsal_internal_testAccess(p_context, access_mask, NULL, 
                                      &dir_attrs); 
  } else {
    status = fsal_internal_access(p_context, p_dir_handle, access_mask,
                                  &dir_attrs);
  }

  if(FSAL_IS_ERROR(status)) {
    ReturnStatus(status, INDEX_FSAL_opendir);
  }

  /* if everything is OK, fills the dir_desc structure : */

  memcpy(&(p_dir_descriptor->context), p_context, sizeof(fsal_op_context_t));
  memcpy(&(p_dir_descriptor->handle), p_dir_handle, sizeof(fsal_handle_t));

  if(p_dir_attributes) {
      *p_dir_attributes = dir_attrs;
  }
  p_dir_descriptor->dir_offset = 0;

  FSI_TRACE(FSI_DEBUG, "End opendir----------------------------");
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_opendir);

}

/**
 * FSAL_readdir :
 *     Read the entries of an opened directory.
 *
 * \param dir_descriptor (input):
 *        Pointer to the directory descriptor filled by FSAL_opendir.
 * \param start_position (input):
 *        Cookie that indicates the first object to be read during
 *        this readdir operation.
 *        This should be :
 *        - FSAL_READDIR_FROM_BEGINNING for reading the content
 *          of the directory from the beginning.
 *        - The end_position parameter returned by the previous
 *          call to FSAL_readdir.
 * \param get_attr_mask (input)
 *        Specify the set of attributes to be retrieved for directory entries.
 * \param buffersize (input)
 *        The size (in bytes) of the buffer where
 *        the direntries are to be stored.
 * \param pdirent (output)
 *        Adresse of the buffer where the direntries are to be stored.
 * \param end_position (output)
 *        Cookie that indicates the current position in the directory.
 * \param nb_entries (output)
 *        Pointer to the number of entries read during the call.
 * \param end_of_dir (output)
 *        Pointer to a boolean that indicates if the end of dir
 *        has been reached during the call.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */

fsal_status_t
PTFSAL_readdir(fsal_dir_t       * dir_desc,      /* IN */
               fsal_op_context_t * p_context,    /* IN */
               fsal_cookie_t      startposition, /* IN */
               fsal_attrib_mask_t get_attr_mask, /* IN */
               fsal_mdsize_t      buffersize,    /* IN */
               fsal_dirent_t    * p_pdirent,     /* OUT */
               fsal_cookie_t    * end_position,  /* OUT */
               fsal_count_t     * p_nb_entries,  /* OUT */
               fsal_boolean_t   * p_end_of_dir   /* OUT */)
{
  fsal_status_t st;
  fsal_count_t max_dir_entries;
  fsal_name_t entry_name;
  int bpos = 0;
  ptfsal_dir_t *p_dir_descriptor = (ptfsal_dir_t *)dir_desc;
  ptfsal_cookie_t start_position;
  ptfsal_cookie_t *p_end_position = (ptfsal_cookie_t *)end_position;
  ptfsal_op_context_t * fsi_op_context  = 
    (ptfsal_op_context_t *)(&dir_desc->context);

  FSI_TRACE(FSI_DEBUG, "Begin readdir========================\n");

  ptfsal_handle_t *p_fsi_handle;
  fsi_stat_struct buffstat;
  int readdir_rc;
  char fsi_dname[FSAL_MAX_PATH_LEN];
  char fsi_parent_dir_path[FSAL_MAX_PATH_LEN];
  char fsi_name[FSAL_MAX_PATH_LEN];

  int rc = 0;
  int readdir_record = 0;

  memset(&entry_name, 0, sizeof(entry_name));

  /*****************/
  /* sanity checks */
  /*****************/

  if(!p_dir_descriptor || !p_pdirent || !p_end_position || !p_nb_entries || 
     !p_end_of_dir) {
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);
  }

  *p_end_of_dir = 0;
  max_dir_entries = (buffersize / sizeof(fsal_dirent_t));
  strncpy(fsi_parent_dir_path, p_dir_descriptor->path.path, 
          sizeof(fsi_parent_dir_path));
  fsi_parent_dir_path[sizeof(fsi_parent_dir_path)-1] = '\0';
  FSI_TRACE(FSI_DEBUG, "Parent dir path --- %s\n", fsi_parent_dir_path);

  /***************************/
  /* seek into the directory */
  /***************************/
  start_position.data.cookie = startposition.data.cookie;
  errno = 0;
  if(start_position.data.cookie == 0) {
    rc = errno;
  } else {
    FSI_TRACE(FSI_DEBUG, 
              "FSI - seekdir called - NOT SUPPORTED RIGHT NOW!!!\n");
    rc = errno;
  }

  if(rc) {
    Return(posix2fsal_error(rc), rc, INDEX_FSAL_readdir);
  }

  /************************/
  /* browse the directory */
  /************************/

  *p_nb_entries = 0;
  while(*p_nb_entries < max_dir_entries) {
    /***********************/
    /* read the next entry */
    /***********************/

    readdir_rc = ptfsal_readdir(dir_desc, &buffstat, fsi_dname);
    memset(fsi_name, 0, sizeof(fsi_name));
    fsi_get_whole_path(fsi_parent_dir_path, fsi_dname, fsi_name);
    FSI_TRACE(FSI_DEBUG, "fsi_dname %s, whole path %s\n", fsi_dname, 
              fsi_name);

    // convert FSI return code to rc
    rc = 1;
    if (readdir_rc < 0)
      rc = 0;

    /* End of directory */
    if(rc == 0) {
      *p_end_of_dir = 1;
      break;
    }

    /***********************************/
      /* Get information about the entry */
    /***********************************/

    for(bpos = 0; bpos < rc;) {
      bpos++;

      if(!(*p_nb_entries < max_dir_entries))
        break;

      // /* skip . and .. */
      if(!strcmp(fsi_dname, ".") || !strcmp(fsi_dname, ".."))
        continue;

      /* build the full path of the file into "fsalpath */
      if(FSAL_IS_ERROR (st = FSAL_str2name(fsi_dname, FSAL_MAX_NAME_LEN,
                        &(p_pdirent[*p_nb_entries].name))))
         ReturnStatus(st, INDEX_FSAL_readdir);

      strncpy(entry_name.name, fsi_dname, sizeof(entry_name.name));
      entry_name.name[sizeof(entry_name.name)-1] = '\0';
      entry_name.len = strlen(entry_name.name);

      // load the FSI based handle
      p_fsi_handle = 
        (ptfsal_handle_t *)(&(p_pdirent[*p_nb_entries].handle));
      memcpy(&(p_fsi_handle->data.handle.f_handle), 
             &buffstat.st_persistentHandle.handle, 
             FSI_PERSISTENT_HANDLE_N_BYTES);
      p_fsi_handle->data.handle.handle_size = 
        FSI_PERSISTENT_HANDLE_N_BYTES;
      p_fsi_handle->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
      p_fsi_handle->data.handle.handle_version = OPENHANDLE_VERSION;
      p_fsi_handle->data.handle.handle_type = 
        posix2fsal_type(buffstat.st_mode);

      /************************
       * Fills the attributes *
       ************************/
      p_pdirent[*p_nb_entries].attributes.asked_attributes = get_attr_mask;

      st = posix2fsal_attributes(&buffstat, 
                                 &p_pdirent[*p_nb_entries].attributes);
      fsi_cache_name_and_handle(fsi_op_context, 
                                p_fsi_handle->data.handle.f_handle, 
                                fsi_name); 
      p_pdirent[*p_nb_entries].attributes.mounted_on_fileid = 
        PTFSAL_FILESYSTEM_NUMBER;


      if(FSAL_IS_ERROR(st)) {
        FSAL_CLEAR_MASK(
          p_pdirent[*p_nb_entries].attributes.asked_attributes);
        FSAL_SET_MASK(
          p_pdirent[*p_nb_entries].attributes.asked_attributes,
            FSAL_ATTR_RDATTR_ERR);
      }

      ((ptfsal_cookie_t *)(&p_pdirent[*p_nb_entries].cookie))->data.cookie
        = readdir_record;

      FSI_TRACE(FSI_DEBUG, "readdir [%s] rec %d\n",fsi_dname, 
                readdir_record);

      readdir_record++;

      p_pdirent[*p_nb_entries].nextentry = NULL;
      if(*p_nb_entries)
         p_pdirent[*p_nb_entries - 1].nextentry = 
           &(p_pdirent[*p_nb_entries]);

      memcpy((char *)p_end_position, 
             (char *)&p_pdirent[*p_nb_entries].cookie,
             sizeof(ptfsal_cookie_t));
      (*p_nb_entries)++;

    }                       /* for */
  }                         /* While */

  FSI_TRACE(FSI_DEBUG, "End readdir==============================\n");
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readdir);

}

/**
 * FSAL_closedir :
 * Free the resources allocated for reading directory entries.
 *
 * \param dir_descriptor (input):
 *        Pointer to a directory descriptor filled by FSAL_opendir.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t
PTFSAL_closedir(fsal_dir_t * p_dir_descriptor, fsal_op_context_t * p_context)
{
  int rc;

  /* sanity checks */
  if(!p_dir_descriptor) {
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);
  }
  rc = ptfsal_closedir(p_dir_descriptor);
  if(rc != 0) {
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_closedir);
  }
  /* fill dir_descriptor with zeros */
  memset(p_dir_descriptor, 0, sizeof(ptfsal_dir_t));

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

}
