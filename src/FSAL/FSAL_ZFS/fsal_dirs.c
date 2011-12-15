/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_dirs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/29 09:39:04 $
 * \version $Revision: 1.10 $
 * \brief   Directory browsing operations.
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
extern time_t ServerBootTime;

/**
 * FSAL_opendir :
 *     Opens a directory for reading its content.
 *     
 * \param dir_handle (input)
 *         the handle of the directory to be opened.
 * \param p_context (input)
 *         Permission context for the operation (user, export context...).
 * \param dir_descriptor (output)
 *         pointer to an allocated structure that will receive
 *         directory stream informations, on successfull completion.
 * \param dir_attributes (optional output)
 *         On successfull completion,the structure pointed
 *         by dir_attributes receives the new directory attributes.
 *         Can be NULL.
 * 
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (user does not have read permission on directory)
 *        - ERR_FSAL_STALE        (dir_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */
fsal_status_t ZFSFSAL_opendir(fsal_handle_t * dir_hdl,  /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_dir_t * dir_desc, /* OUT */
                           fsal_attrib_list_t * dir_attributes  /* [ IN/OUT ] */
    )
{
  int rc;
  creden_t cred;
  zfsfsal_handle_t * dir_handle = (zfsfsal_handle_t *)dir_hdl;
  zfsfsal_dir_t * dir_descriptor = (zfsfsal_dir_t *)dir_desc;

  /* sanity checks
   * note : dir_attributes is optionnal.
   */
  if(!dir_handle || !p_context || !dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  /* >> You can prepare your directory for beeing read  
   * and check that the user has the right for reading its content <<*/
  libzfswrap_vnode_t *p_vnode;

  /* Get the right VFS */
  ZFSFSAL_VFS_RDLock();
  libzfswrap_vfs_t *p_vfs = ZFSFSAL_GetVFS(dir_handle);
  if(!p_vfs)
  {
    ZFSFSAL_VFS_Unlock();
    Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_opendir);
  }
  cred.uid = p_context->credential.user;
  cred.gid = p_context->credential.group;

  /* Hook for the zfs snapshot directory */
  if(dir_handle->data.zfs_handle.inode == ZFS_SNAP_DIR_INODE)
  {
    LogDebug(COMPONENT_FSAL, "Opening the .zfs pseudo-directory");
    p_vnode = NULL;
    rc = 0;
  }
  else
  {
    TakeTokenFSCall();
    rc = libzfswrap_opendir(p_vfs, &cred,
                            dir_handle->data.zfs_handle, &p_vnode);
    ReleaseTokenFSCall();
  }
  ZFSFSAL_VFS_Unlock();

  if(rc)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_opendir);

  dir_descriptor->cred = cred;
  dir_descriptor->handle = *dir_handle;
  dir_descriptor->p_vnode = p_vnode;

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
 * \param p_dirent (output)
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
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument) 
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */
fsal_status_t ZFSFSAL_readdir(fsal_dir_t * dir_desc, /* IN */
                           fsal_cookie_t start_pos,        /* IN */
                           fsal_attrib_mask_t get_attr_mask,    /* IN */
                           fsal_mdsize_t buffersize,    /* IN */
                           fsal_dirent_t * p_dirent,    /* OUT */
                           fsal_cookie_t * end_pos,     /* OUT */
                           fsal_count_t * nb_entries,   /* OUT */
                           fsal_boolean_t * end_of_dir  /* OUT */
    )
{
  int rc;
  fsal_count_t max_dir_entries = buffersize / sizeof(fsal_dirent_t);
  zfsfsal_dir_t * dir_descriptor = (zfsfsal_dir_t *)dir_desc;
  zfsfsal_cookie_t start_position;
  zfsfsal_cookie_t * end_position = (zfsfsal_cookie_t *)end_pos;
  zfsfsal_handle_t *entry_hdl;

  /* sanity checks */

  if(!dir_descriptor || !p_dirent || !end_position || !nb_entries || !end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  memcpy( (char *)&start_position.data.cookie, start_pos.data, sizeof( off_t ) ) ;

  /* Hook to create the pseudo directory */
  if(dir_descriptor->handle.data.zfs_handle.inode == ZFS_SNAP_DIR_INODE)
  {
    LogDebug(COMPONENT_FSAL, "Listing the snapshots in .zfs/");

    int i;
    struct stat fstat;
    memset(&fstat, 0, sizeof(fstat));
    fstat.st_mode = S_IFDIR | 0755;
    fstat.st_nlink = 3;
    fstat.st_ctime = ServerBootTime;
    fstat.st_atime = ServerBootTime;
    fstat.st_mtime = ServerBootTime;


    ZFSFSAL_VFS_RDLock();
    for(i = 0; i < max_dir_entries && i < i_snapshots; i++)
    {
      entry_hdl = (zfsfsal_handle_t *) &p_dirent[i].handle;

      libzfswrap_getroot(p_snapshots[i + start_position.data.cookie + 1].p_vfs,
                         &entry_hdl->data.zfs_handle);
      entry_hdl->data.i_snap = i + start_position.data.cookie + 1;
      entry_hdl->data.type = FSAL_TYPE_DIR;
      strncpy(p_dirent[i].name.name, p_snapshots[i + start_position.data.cookie + 1].psz_name,
              FSAL_MAX_NAME_LEN);
      p_dirent[i].name.len = strlen(p_snapshots[i + start_position.data.cookie + 1].psz_name);

      fstat.st_dev = i + start_position.data.cookie + 1;
      fstat.st_ino = entry_hdl->data.zfs_handle.inode;
      p_dirent[i].attributes.asked_attributes = get_attr_mask;
      posix2fsal_attributes(&fstat, &p_dirent[i].attributes);

      p_dirent[i].nextentry = NULL;
      if(i)
        p_dirent[i-1].nextentry = &(p_dirent[i]);
    }
    *nb_entries = i;
    if(i == i_snapshots)
      *end_of_dir = 1;
    else
      end_position->data.cookie = start_position.data.cookie + i;
    ZFSFSAL_VFS_Unlock();

    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readdir);
  }

  /* Check that the vfs is still existing */
  ZFSFSAL_VFS_RDLock();
  libzfswrap_vfs_t *p_vfs = ZFSFSAL_GetVFS(&dir_descriptor->handle);
  if(p_vfs == NULL)
  {
    ZFSFSAL_VFS_Unlock();
    Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_readdir);
  }

  libzfswrap_entry_t *entries = malloc( max_dir_entries * sizeof(libzfswrap_entry_t));
  TakeTokenFSCall();

  /* >> read some entry from you filesystem << */
  rc = libzfswrap_readdir(p_vfs, &dir_descriptor->cred, dir_descriptor->p_vnode, entries,
                          max_dir_entries, (off_t*)(&start_position));

  ReleaseTokenFSCall();
  ZFSFSAL_VFS_Unlock();

  /* >> convert error code and return on error << */
  if(rc)
  {
    free(entries);
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_readdir);
  }

  /* >> fill the output dirent array << */

  *nb_entries = 0;
  int index = 0;
  while(index < max_dir_entries)
  {
    entry_hdl = (zfsfsal_handle_t *) &p_dirent[*nb_entries].handle;

    /* If psz_filename is NULL, that's the end of the list */
    if(entries[index].psz_filename[0] == '\0')
      break;

    /* Skip '.' and '..' */
    if(!strcmp(entries[index].psz_filename, ".") || !strcmp(entries[index].psz_filename, ".."))
    {
      index++;
      continue;
    }

    entry_hdl->data.zfs_handle = entries[index].object;
    entry_hdl->data.type = posix2fsal_type(entries[index].type);
    entry_hdl->data.i_snap = dir_descriptor->handle.data.i_snap;
    entries[index].stats.st_dev = dir_descriptor->handle.data.i_snap;
    FSAL_str2name(entries[index].psz_filename, FSAL_MAX_NAME_LEN, &(p_dirent[*nb_entries].name));

    /* Add the attributes */
    p_dirent[*nb_entries].attributes.asked_attributes = get_attr_mask;
    posix2fsal_attributes(&(entries[index].stats), &p_dirent[*nb_entries].attributes);

    p_dirent[*nb_entries].nextentry = NULL;

    if(*nb_entries)
        p_dirent[*nb_entries - 1].nextentry = &(p_dirent[*nb_entries]);

    (*nb_entries)++;
    index++;
  }
  free(entries);

  /* until the requested count is reached
   * or the end of dir is reached...
   */

  /* Don't forget setting output vars : end_position, nb_entries, end_of_dir  */
  if(start_position.data.cookie == 0)
    *end_of_dir = 1;
  else
    *end_position = start_position;

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
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */
fsal_status_t ZFSFSAL_closedir(fsal_dir_t * dir_desc /* IN */
    )
{

  int rc;
  zfsfsal_dir_t * dir_descriptor = (zfsfsal_dir_t *)dir_desc;

  /* sanity checks */
  if(!dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

  /* Hook for the ZFS directory */
  if(dir_descriptor->handle.data.zfs_handle.inode == ZFS_SNAP_DIR_INODE)
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

  /* Check that the vfs still exist */
  ZFSFSAL_VFS_RDLock();
  libzfswrap_vfs_t *p_vfs = ZFSFSAL_GetVFS(&dir_descriptor->handle);
  if(!p_vfs)
  {
    ZFSFSAL_VFS_Unlock();
    Return(ERR_FSAL_NOENT, 0, INDEX_FSAL_closedir);
  }

  /* >> release the resources used for reading your directory << */
  rc = libzfswrap_closedir(p_vfs, &dir_descriptor->cred, dir_descriptor->p_vnode);
  ZFSFSAL_VFS_Unlock();

  if(rc)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_closedir);
  else
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);
}
