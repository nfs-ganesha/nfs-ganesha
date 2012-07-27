// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2010, 2011
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    pt_ganesha.c
// Description: Main layer for PT's Ganesha FSAL
// Author:      FSI IPC Team
// ----------------------------------------------------------------------------
#include "pt_ganesha.h"

struct  fsi_handle_cache_t  g_fsi_name_handle_cache;
pthread_mutex_t g_fsi_name_handle_mutex;

// ----------------------------------------------------------------------------
void
fsi_get_whole_path(const char * parentPath,
                   const char * name,
                   char       * path)
{
  FSI_TRACE(FSI_DEBUG, "parentPath=%s, name=%s\n", parentPath, name);
  if(!strcmp(parentPath, "/") || !strcmp(parentPath, "")) {
    snprintf(path, PATH_MAX, "%s", name);
  } else {
    snprintf(path, PATH_MAX, "%s/%s", parentPath, name);
  }
  FSI_TRACE(FSI_DEBUG, "Full Path: %s", path);
}
// ----------------------------------------------------------------------------
int
fsi_cache_name_and_handle(fsal_op_context_t * p_context,
                          char              * handle,
                          char              * name)
{
  int rc;
  struct fsi_handle_cache_entry_t handle_entry;

  rc = fsi_get_name_from_handle(p_context, handle, name);

  if (rc < 0) {
    pthread_mutex_lock(&g_fsi_name_handle_mutex);
    g_fsi_name_handle_cache.m_count = (g_fsi_name_handle_cache.m_count + 1) 
      % FSI_MAX_HANDLE_CACHE_ENTRY;

    memcpy(
      &g_fsi_name_handle_cache
      .m_entry[g_fsi_name_handle_cache.m_count].m_handle,
      &handle[0],sizeof(handle_entry.m_handle));
    strncpy(
      g_fsi_name_handle_cache.m_entry[g_fsi_name_handle_cache.m_count].m_name,
      name, sizeof(handle_entry.m_name));
    g_fsi_name_handle_cache.m_entry[g_fsi_name_handle_cache.m_count]
    .m_name[sizeof(handle_entry.m_name)-1] = '\0';
    FSI_TRACE(FSI_DEBUG, "FSI - added %s to name cache entry %d\n",
              name,g_fsi_name_handle_cache.m_count);
    pthread_mutex_unlock(&g_fsi_name_handle_mutex);
  }

  return 0;
}
// -----------------------------------------------------------------------------
int
fsi_get_name_from_handle(fsal_op_context_t * p_context,
                         char              * handle,
                         char              * name)
{
  int index;
  int rc;
  ccl_context_t           ccl_context;
  struct PersistentHandle pt_handler;
  char                  * client_ip;
  struct fsi_handle_cache_entry_t handle_entry;

  FSI_TRACE(FSI_DEBUG, "Get name from handle: \n");
  ptfsal_print_handle(handle);
  
  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  pthread_mutex_lock(&g_fsi_name_handle_mutex);
  for (index = 0; index < FSI_MAX_HANDLE_CACHE_ENTRY; index++) {
    if (memcmp(&handle[0], &g_fsi_name_handle_cache.m_entry[index].m_handle, 
        FSI_PERSISTENT_HANDLE_N_BYTES) == 0) {
      strncpy(name, g_fsi_name_handle_cache.m_entry[index].m_name, 
              sizeof(handle_entry.m_name));
      name[sizeof(handle_entry.m_name)-1] = '\0';
      FSI_TRACE(FSI_DEBUG, "FSI - name = %s \n", name);
      pthread_mutex_unlock(&g_fsi_name_handle_mutex);
      return 0;
    }
  }
  pthread_mutex_unlock(&g_fsi_name_handle_mutex);

  /* Not in cache */
  memset(&pt_handler.handle, 0, FSI_PERSISTENT_HANDLE_N_BYTES);
  memcpy(&pt_handler.handle, handle, FSI_PERSISTENT_HANDLE_N_BYTES);
  FSI_TRACE(FSI_DEBUG, "Handle: \n");
  ptfsal_print_handle(handle);
  rc = ccl_handle_to_name(&ccl_context, &pt_handler, name); 

  FSI_TRACE(FSI_DEBUG, "The rc %d, handle %s, name %s", rc, handle, name);
  
  if (rc == 0) {
    pthread_mutex_lock(&g_fsi_name_handle_mutex);
    g_fsi_name_handle_cache.m_count = (g_fsi_name_handle_cache.m_count + 1)
      % FSI_MAX_HANDLE_CACHE_ENTRY;

    memcpy(
      &g_fsi_name_handle_cache
      .m_entry[g_fsi_name_handle_cache.m_count].m_handle,
      &handle[0],FSI_PERSISTENT_HANDLE_N_BYTES);
    strncpy(
      g_fsi_name_handle_cache.m_entry[g_fsi_name_handle_cache.m_count].m_name,
      name, sizeof(handle_entry.m_name));
    g_fsi_name_handle_cache.m_entry[g_fsi_name_handle_cache.m_count]
    .m_name[sizeof(handle_entry.m_name)-1] = '\0';
    FSI_TRACE(FSI_DEBUG, "FSI - added %s to name cache entry %d\n",
              name,g_fsi_name_handle_cache.m_count);
    pthread_mutex_unlock(&g_fsi_name_handle_mutex);
  }  

  return rc;

}
// -----------------------------------------------------------------------------
int
fsi_update_cache_name(char * oldname,
                      char * newname)
{
  int    index;
  struct fsi_handle_cache_entry_t handle_entry;

  FSI_TRACE(FSI_DEBUG, "oldname[%s]->newname[%s]",oldname,newname);
  
  pthread_mutex_lock(&g_fsi_name_handle_mutex);
  for (index = 0; index < FSI_MAX_HANDLE_CACHE_ENTRY; index++) {
    FSI_TRACE(FSI_DEBUG, "cache entry[%d]: %s",index,
              g_fsi_name_handle_cache.m_entry[index].m_name);
    if (strncmp((const char *)oldname, 
                (const char *)
                &g_fsi_name_handle_cache.m_entry[index].m_name, PATH_MAX) 
                == 0) {
      FSI_TRACE(FSI_DEBUG, 
                "FSI - Updating cache old name[%s]-> new name[%s] \n",
                g_fsi_name_handle_cache.m_entry[index].m_name, newname);
      strncpy(g_fsi_name_handle_cache.m_entry[index].m_name, newname, 
              sizeof(handle_entry.m_name));
      g_fsi_name_handle_cache.m_entry[index]
      .m_name[sizeof(handle_entry.m_name)-1] = '\0';
    }
  }
  pthread_mutex_unlock(&g_fsi_name_handle_mutex);
  
  return 0;
}

void 
fsi_remove_cache_by_handle(char * handle)
{
  int index;
  pthread_mutex_lock(&g_fsi_name_handle_mutex);
  for (index = 0; index < FSI_MAX_HANDLE_CACHE_ENTRY; index++) {

    if (memcmp(handle, &g_fsi_name_handle_cache.m_entry[index].m_handle, 
      FSI_PERSISTENT_HANDLE_N_BYTES) == 0) {
      FSI_TRACE(FSI_DEBUG, "Handle will be removed from cache:")
      ptfsal_print_handle(handle);
      /* Mark the both handle and name to 0 */
      g_fsi_name_handle_cache.m_entry[index].m_handle[0] = '\0';
      g_fsi_name_handle_cache.m_entry[index].m_name[0] = '\0';
      break;
    }
  }
  pthread_mutex_unlock(&g_fsi_name_handle_mutex);
}

void
fsi_remove_cache_by_fullpath(char * path)
{
  int index;
  int len = strlen(path);

  if (len > PATH_MAX)
     return;

  /* TBD. The error return from pthread_mutex_lock will be handled
   * when improve read/write lock.
   */
  pthread_mutex_lock(&g_fsi_name_handle_mutex);
  for (index = 0; index < FSI_MAX_HANDLE_CACHE_ENTRY; index++) {
    if (memcmp(path, g_fsi_name_handle_cache.m_entry[index].m_name, len) 
        == 0) {
      FSI_TRACE(FSI_DEBUG, "Handle will be removed from cache by path %s:", 
                path);
      /* Mark the both handle and name to 0 */
      g_fsi_name_handle_cache.m_entry[index].m_handle[0] = '\0';
      g_fsi_name_handle_cache.m_entry[index].m_name[0] = '\0';
      break;
    }
  }
  pthread_mutex_unlock(&g_fsi_name_handle_mutex);
}

// -----------------------------------------------------------------------------
int
fsi_check_handle_index(int handle_index)
{
    // check handle
    if ((handle_index >=                 0) &&
        (handle_index <  (FSI_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS))) {
      return 0;
    } else {
      return -1;
    }
}
// -----------------------------------------------------------------------------
int
ptfsal_rename(fsal_op_context_t * p_context,
              fsal_handle_t * p_old_parentdir_handle,
              char * p_old_name,
              fsal_handle_t * p_new_parentdir_handle,
              char * p_new_name)
{
  int rc;

  ccl_context_t ccl_context;
  ptfsal_op_context_t     * fsi_op_context     
    = (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context = 
    fsi_op_context->export_context;
  char fsi_old_parent_dir_name[PATH_MAX];
  char fsi_new_parent_dir_name[PATH_MAX];
  char fsi_old_fullpath[PATH_MAX];
  char fsi_new_fullpath[PATH_MAX];
  ptfsal_handle_t * p_old_parent_dir_handle = 
    (ptfsal_handle_t *)p_old_parentdir_handle;
  ptfsal_handle_t * p_new_parent_dir_handle = 
    (ptfsal_handle_t *)p_new_parentdir_handle;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  rc = fsi_get_name_from_handle(p_context, 
                                p_old_parent_dir_handle->data.handle.f_handle, 
                                fsi_old_parent_dir_name);
  if( rc < 0 )
  {
    FSI_TRACE(FSI_DEBUG, "Failed to get name from handle.");
    return rc;
  }
  rc = fsi_get_name_from_handle(p_context, 
                                p_new_parent_dir_handle->data.handle.f_handle, 
                                fsi_new_parent_dir_name);
  if( rc < 0 )
  {
    FSI_TRACE(FSI_DEBUG, "Failed to get name from handle.");
    return rc;
  }
  fsi_get_whole_path(fsi_old_parent_dir_name, p_old_name, fsi_old_fullpath);
  fsi_get_whole_path(fsi_new_parent_dir_name, p_new_name, fsi_new_fullpath);
  FSI_TRACE(FSI_DEBUG, "Full path is %s", fsi_old_fullpath);
  FSI_TRACE(FSI_DEBUG, "Full path is %s", fsi_new_fullpath);

  rc = ccl_rename(&ccl_context, fsi_old_fullpath, fsi_new_fullpath);
  if(rc == 0) {
    fsi_update_cache_name(fsi_old_fullpath, fsi_new_fullpath);
  }

  return rc;
}
// -----------------------------------------------------------------------------
void
ptfsal_convert_fsi_name(ccl_context_t      * ccl_context,
                        const char         * filename,
                        char               * sv_filename,
                        enum e_fsi_name_enum fsi_name_type)
{
  convert_fsi_name(ccl_context, filename, sv_filename, fsi_name_type);
}

// -----------------------------------------------------------------------------
int
ptfsal_stat_by_parent_name(fsal_op_context_t * p_context,
                           fsal_handle_t     * p_parentdir_handle,
                           char              * p_filename,
                           fsi_stat_struct   * p_stat)
{
  int stat_rc;

  ccl_context_t ccl_context;
  char fsi_parent_dir_name[PATH_MAX];
  char fsi_fullpath[PATH_MAX];
  ptfsal_handle_t * p_parent_dir_handle = 
    (ptfsal_handle_t *)p_parentdir_handle;
  ptfsal_op_context_t * fsi_op_context     = (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context = 
    fsi_op_context->export_context;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  stat_rc = fsi_get_name_from_handle(p_context, 
                                     p_parent_dir_handle->data.handle.f_handle, 
                                     fsi_parent_dir_name);
  if( stat_rc < 0 )
  {
    FSI_TRACE(FSI_DEBUG, "Failed to get name from handle.");
    return stat_rc;
  }
  fsi_get_whole_path(fsi_parent_dir_name, p_filename, fsi_fullpath);
  FSI_TRACE(FSI_DEBUG, "Full path is %s", fsi_fullpath);

  stat_rc = ccl_stat(&ccl_context, fsi_fullpath, p_stat);

  ptfsal_print_handle(p_stat->st_persistentHandle.handle);
  return stat_rc;
}

// -----------------------------------------------------------------------------
int
ptfsal_stat_by_name(fsal_op_context_t * p_context,
                    fsal_path_t       * p_fsalpath,
                    fsi_stat_struct   * p_stat)
{
  int stat_rc;

  ccl_context_t ccl_context;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  FSI_TRACE(FSI_DEBUG, "FSI - name = %s\n", p_fsalpath->path);

  stat_rc = ccl_stat(&ccl_context, p_fsalpath->path, p_stat);

  ptfsal_print_handle(p_stat->st_persistentHandle.handle);

  return stat_rc;
}
// -----------------------------------------------------------------------------
int
ptfsal_stat_by_handle(fsal_handle_t     * p_filehandle,
                      fsal_op_context_t * p_context,
                      fsi_stat_struct   * p_stat)
{
  int  stat_rc;
  char fsi_name[PATH_MAX];

  ccl_context_t ccl_context;
  ptfsal_handle_t         * p_fsi_handle       = 
    (ptfsal_handle_t *)p_filehandle;
  ptfsal_op_context_t     * fsi_op_context     = 
    (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context = 
    fsi_op_context->export_context;

  FSI_TRACE(FSI_DEBUG, "FSI - handle: \n");
  ptfsal_print_handle(p_fsi_handle->data.handle.f_handle);

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  memset(fsi_name, 0, sizeof(fsi_name));
  stat_rc =  fsi_get_name_from_handle(p_context, 
                                      p_fsi_handle->data.handle.f_handle, 
                                      fsi_name);
  FSI_TRACE(FSI_DEBUG, "FSI - rc = %d\n", stat_rc);
  if (stat_rc) {
    FSI_TRACE(FSI_DEBUG, "Return rc %d from get name from handle %s", 
              stat_rc, p_fsi_handle->data.handle.f_handle);
    return stat_rc;
  }
  FSI_TRACE(FSI_DEBUG, "FSI - name = %s\n", fsi_name);

  stat_rc = ccl_stat(&ccl_context, fsi_name, p_stat);
  ptfsal_print_handle(p_stat->st_persistentHandle.handle);

  return stat_rc;
}
// -----------------------------------------------------------------------------
int
ptfsal_opendir(fsal_op_context_t * p_context,
               const char        * filename,
               const char        * mask,
               uint32              attr)
{
  int dir_handle;

  ccl_context_t ccl_context;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  FSI_TRACE(FSI_DEBUG, "This will be full path: %s\n", filename);
  dir_handle = ccl_opendir(&ccl_context, filename, mask, attr);
  FSI_TRACE(FSI_DEBUG, "ptfsal_opendir index %d\n", dir_handle);

  return dir_handle;
}
// -----------------------------------------------------------------------------
int
ptfsal_readdir(fsal_dir_t      * dir_desc,
               fsi_stat_struct * sbuf,
               char            * fsi_dname)
{

  int readdir_rc;
  int dir_hnd_index;

  ccl_context_t ccl_context;
  ptfsal_dir_t            * p_dir_descriptor   = (ptfsal_dir_t *)dir_desc;
  ptfsal_op_context_t     * fsi_op_context     = 
    (ptfsal_op_context_t *)(&dir_desc->context);
  ptfsal_export_context_t * fsi_export_context = 
    fsi_op_context->export_context;

  dir_hnd_index     = p_dir_descriptor->fd;
  ptfsal_set_fsi_handle_data(fsi_op_context, &ccl_context);

  struct fsi_struct_dir_t * dirp = 
    (struct fsi_struct_dir_t *)
    &g_fsi_dir_handles.m_dir_handle[dir_hnd_index].m_fsi_struct_dir;

  readdir_rc = ccl_readdir(&ccl_context, dirp, sbuf);
  if (readdir_rc == 0) {
    strncpy(fsi_dname, dirp->dname, FSAL_MAX_PATH_LEN);
    fsi_dname[FSAL_MAX_PATH_LEN-1] = '\0';
  } else {
    fsi_dname[0] = '\0';
  }

  return readdir_rc;
}
// -----------------------------------------------------------------------------
int
ptfsal_closedir(fsal_dir_t * dir_desc)
{
  int dir_hnd_index;
  ccl_context_t ccl_context;
  ptfsal_op_context_t     * fsi_op_context        = 
    (ptfsal_op_context_t *)(&dir_desc->context);
  ptfsal_export_context_t * fsi_export_context    = 
    fsi_op_context->export_context;
  ptfsal_dir_t            * ptfsal_dir_descriptor = 
    (ptfsal_dir_t *)dir_desc;

  ptfsal_set_fsi_handle_data(fsi_op_context, &ccl_context);

  dir_hnd_index = ptfsal_dir_descriptor->fd;

  struct fsi_struct_dir_t * dirp = 
    (struct fsi_struct_dir_t *)
    &g_fsi_dir_handles.m_dir_handle[dir_hnd_index].m_fsi_struct_dir;

  return ccl_closedir(&ccl_context, dirp);
}
// -----------------------------------------------------------------------------
int
ptfsal_fsync(fsal_file_t * p_file_descriptor)
{
  int handle_index;

  ptfsal_file_t * p_file_desc  = (ptfsal_file_t *)p_file_descriptor;
  ccl_context_t ccl_context;

  handle_index = ((ptfsal_file_t *)p_file_descriptor)->fd;
  if (fsi_check_handle_index (handle_index) < 0) {
    return -1;
  }

  ccl_context.handle_index = p_file_desc->fd;
  ccl_context.export_id = p_file_desc->export_id;
  ccl_context.uid       = p_file_desc->uid;
  ccl_context.gid       = p_file_desc->gid;

  return ccl_fsync(&ccl_context,handle_index);
}
// -----------------------------------------------------------------------------
int
ptfsal_open_by_handle(fsal_op_context_t * p_context,
                      fsal_handle_t     * p_object_handle,
                      int                 oflags,
                      mode_t              mode)
{
  int  open_rc, rc;
  char fsi_filename[PATH_MAX];

  ptfsal_handle_t         * p_fsi_handle       = 
    (ptfsal_handle_t *)p_object_handle;
  ptfsal_op_context_t     * fsi_op_context     = 
    (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context = 
    fsi_op_context->export_context;
  ccl_context_t ccl_context;

  FSI_TRACE(FSI_DEBUG, "Open by Handle:");
  ptfsal_print_handle(p_fsi_handle->data.handle.f_handle);

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  strcpy(fsi_filename,"");
  rc = fsi_get_name_from_handle(p_context, 
                                (char *)&p_fsi_handle->data.handle.f_handle,
                                (char *)&fsi_filename);
  if(rc < 0)
  {
    FSI_TRACE(FSI_DEBUG, "Handle to name failed rc=%d", rc);
  }
  FSI_TRACE(FSI_DEBUG, "handle to name %s for handle:", fsi_filename);
  ptfsal_print_handle(p_fsi_handle->data.handle.f_handle);
  open_rc = ccl_open(&ccl_context, fsi_filename, oflags, mode);

  return open_rc;
}
// -----------------------------------------------------------------------------
int
ptfsal_open(fsal_handle_t     * p_parent_directory_handle,
            fsal_name_t       * p_filename,
            fsal_op_context_t * p_context,
            mode_t              mode,
            fsal_handle_t     * p_object_handle)
{
  int  rc;
  char fsi_name[PATH_MAX];
  char fsi_parent_dir_name[PATH_MAX];

  ptfsal_handle_t         * p_fsi_handle        = 
    (ptfsal_handle_t *)p_object_handle;
  ptfsal_handle_t         * p_fsi_parent_handle = 
    (ptfsal_handle_t *)p_parent_directory_handle;
  ptfsal_op_context_t     * fsi_op_context      = 
    (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context  = 
    fsi_op_context->export_context;
  ccl_context_t ccl_context;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  rc = fsi_get_name_from_handle(p_context, 
                                p_fsi_parent_handle->data.handle.f_handle, 
                                fsi_parent_dir_name);
  if(rc < 0)
  {
    FSI_TRACE(FSI_DEBUG, "Handle to name failed rc=%d", rc);
    return rc;
  }
  FSI_TRACE(FSI_DEBUG, "FSI - Parent dir name = %s\n", fsi_parent_dir_name);
  FSI_TRACE(FSI_DEBUG, "FSI - File name %s\n", p_filename->name);

  memset(&fsi_name, 0, sizeof(fsi_name));
  fsi_get_whole_path(fsi_parent_dir_name, p_filename->name, fsi_name);

  rc = ccl_open(&ccl_context, fsi_name, O_CREAT, mode);

  if (rc >=0) {
    fsal_path_t fsal_path;
    memset(&fsal_path, 0, sizeof(fsal_path_t));
    memcpy(&fsal_path.path, &fsi_name, sizeof(fsi_name));
    ptfsal_name_to_handle(p_context, &fsal_path, p_object_handle);
    ccl_close(&ccl_context, rc, 1 /* Use lock */);
    fsi_cache_name_and_handle(p_context, 
                              (char *)&p_fsi_handle->data.handle.f_handle, 
                              fsi_name);
  }

  return rc;
}
// -----------------------------------------------------------------------------
int
ptfsal_close_mount_root(fsal_export_context_t * p_export_context)
{
  ccl_context_t ccl_context;
  ptfsal_export_context_t * fsi_export_context = 
    (ptfsal_export_context_t *)p_export_context;

  ccl_context.export_id = fsi_export_context->pt_export_id;
  ccl_context.uid       = 0;
  ccl_context.gid       = 0;

  ptfsal_update_handle_nfs_state(fsi_export_context->mount_root_fd, NFS_CLOSE);
  return 0;
}
// -----------------------------------------------------------------------------
int
ptfsal_ftruncate(fsal_op_context_t * p_context,
                 int                 handle_index,
                 uint64_t            offset)
{
  ccl_context_t ccl_context;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  return ccl_ftruncate(&ccl_context, handle_index, offset);
}
// -----------------------------------------------------------------------------
int
ptfsal_unlink(fsal_op_context_t * p_context,
              fsal_handle_t * p_parent_directory_handle, 
              char * p_filename)
{
  int rc;
  ccl_context_t ccl_context;
  ptfsal_op_context_t     * fsi_op_context     = 
    (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context = 
    fsi_op_context->export_context;
  char fsi_parent_dir_name[PATH_MAX];
  char fsi_fullpath[PATH_MAX];
  ptfsal_handle_t * p_parent_dir_handle = 
    (ptfsal_handle_t *)p_parent_directory_handle;

  rc = fsi_get_name_from_handle(p_context, 
                                p_parent_dir_handle->data.handle.f_handle, 
                                fsi_parent_dir_name);
  if( rc < 0 )
  {
    FSI_TRACE(FSI_DEBUG, "Failed to get name from handle.");
    return rc;
  }
  fsi_get_whole_path(fsi_parent_dir_name, p_filename, fsi_fullpath);
  FSI_TRACE(FSI_DEBUG, "Full path is %s", fsi_fullpath);

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  rc = ccl_unlink(&ccl_context, fsi_fullpath);
  /* remove from cache even unlink is not succesful */
  fsi_remove_cache_by_fullpath(fsi_fullpath);

  return rc;
}
// -----------------------------------------------------------------------------
int
ptfsal_chmod(fsal_op_context_t * p_context,
             const char        * path,
             mode_t              mode)
{
  ccl_context_t ccl_context;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  return ccl_chmod(&ccl_context, path, mode);
}
// -----------------------------------------------------------------------------
int
ptfsal_chown(fsal_op_context_t * p_context,
             const char        * path,
             uid_t               uid,
             gid_t               gid)
{
  ccl_context_t ccl_context;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  return ccl_chown(&ccl_context, path, uid, gid);
}
// -----------------------------------------------------------------------------
int
ptfsal_ntimes(fsal_op_context_t * p_context,
              const char        * filename,
              uint64_t            atime,
              uint64_t            mtime)
{
  ccl_context_t ccl_context;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  return ccl_ntimes(&ccl_context, filename, atime, mtime);
}
// -----------------------------------------------------------------------------
int
ptfsal_mkdir(fsal_handle_t     * p_parent_directory_handle,
             fsal_name_t       * p_dirname,
             fsal_op_context_t * p_context,
             mode_t              mode,
             fsal_handle_t     * p_object_handle)
{
  int  rc;
  char fsi_parent_dir_name[PATH_MAX];
  char fsi_name[PATH_MAX];

  ptfsal_handle_t         * p_fsi_parent_handle = 
    (ptfsal_handle_t *)p_parent_directory_handle;
  ptfsal_handle_t         * p_fsi_handle        = 
    (ptfsal_handle_t *)p_object_handle;
  ptfsal_op_context_t     * fsi_op_context      = 
    (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context  = 
    fsi_op_context->export_context;
  ccl_context_t ccl_context;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  /* build new entry path */
  rc = fsi_get_name_from_handle(p_context, 
                                p_fsi_parent_handle->data.handle.f_handle, 
                                fsi_parent_dir_name);
  if(rc < 0)
  {
    FSI_TRACE(FSI_DEBUG, "Handle to name failed for hanlde %s", 
              p_fsi_parent_handle->data.handle.f_handle);
    return rc;
  }
  FSI_TRACE(FSI_DEBUG, "Parent dir name=%s\n", fsi_parent_dir_name);

  memset(fsi_name, 0, sizeof(fsi_name));
  fsi_get_whole_path(fsi_parent_dir_name, p_dirname->name, fsi_name);

  rc = ccl_mkdir(&ccl_context, fsi_name, mode);

  if (rc == 0) {
    // get handle
    fsal_path_t fsal_path;
    memset(&fsal_path, 0, sizeof(fsal_path_t));
    memcpy(&fsal_path.path, &fsi_name, sizeof(fsi_name));

    ptfsal_name_to_handle(p_context, &fsal_path, p_object_handle);
    fsi_cache_name_and_handle(p_context, 
                              (char *)&p_fsi_handle->data.handle.f_handle, 
                              fsi_name);
  }

  return rc;
}
// -----------------------------------------------------------------------------
int
ptfsal_rmdir(fsal_op_context_t * p_context,
             fsal_handle_t * p_parent_directory_handle,
             char  * p_object_name)
{
  int rc;
  ccl_context_t ccl_context;
  ptfsal_op_context_t     * fsi_op_context     = 
    (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context = 
    fsi_op_context->export_context;
  char fsi_parent_dir_name[PATH_MAX];
  char fsi_fullpath[PATH_MAX];
  ptfsal_handle_t * p_parent_dir_handle = 
    (ptfsal_handle_t *)p_parent_directory_handle;

  rc = fsi_get_name_from_handle(p_context, 
    p_parent_dir_handle->data.handle.f_handle, fsi_parent_dir_name);
  if( rc < 0 )
  {
    FSI_TRACE(FSI_DEBUG, "Failed to get name from handle.");
    return rc;
  }
  fsi_get_whole_path(fsi_parent_dir_name, p_object_name, fsi_fullpath);
  FSI_TRACE(FSI_DEBUG, "Full path is %s", fsi_fullpath);

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  rc = ccl_rmdir(&ccl_context, fsi_fullpath);
  fsi_remove_cache_by_fullpath(fsi_fullpath);
  return rc;
}
// -----------------------------------------------------------------------------
uint64_t
ptfsal_read(ptfsal_file_t * p_file_descriptor,
            char          * buf,
            size_t          size,
            off_t           offset,
            int             in_handle)
{
  off_t  cur_offset  = offset;
  size_t cur_size    = size;
  int    split_count = 0;
  size_t buf_offset  = 0;
  int    rc;

  ccl_context_t ccl_context;

  ccl_context.handle_index = p_file_descriptor->fd;
  ccl_context.export_id    = p_file_descriptor->export_id;
  ccl_context.uid          = p_file_descriptor->uid;
  ccl_context.gid          = p_file_descriptor->gid;

  // we will use 256K i/o with vtl but allow larger i/o from NFS
  FSI_TRACE(FSI_DEBUG, "FSI - [%4d] xmp_read off %ld size %ld\n", 
            in_handle, offset, size);
  while (cur_size > IO_BUFFER_SIZE) {
    FSI_TRACE(FSI_DEBUG, "FSI - [%4d] pread - split %d\n", 
              in_handle, split_count);
    rc = ccl_pread(&ccl_context, &buf[buf_offset], IO_BUFFER_SIZE, cur_offset);
    if (rc == -1) {
      return rc;
    }
    cur_size   -= IO_BUFFER_SIZE;
    cur_offset += IO_BUFFER_SIZE;
    buf_offset += IO_BUFFER_SIZE;
    split_count++;
  }

  // call with remainder
  if (cur_size > 0) {
    FSI_TRACE(FSI_DEBUG, "FSI - [%4d] pread - split %d\n", in_handle, 
              split_count);
    rc = ccl_pread(&ccl_context, &buf[buf_offset], cur_size, cur_offset);
    if (rc == -1) {
      return rc;
    }
  }

  return size;
}
// -----------------------------------------------------------------------------
uint64_t
ptfsal_write(fsal_file_t * file_desc,
             const char  * buf,
             size_t        size,
             off_t         offset,
             int           in_handle)
{
  off_t  cur_offset  = offset;
  size_t cur_size    = size;
  int    split_count = 0;
  size_t buf_offset  = 0;
  int    rc;

  ptfsal_file_t * p_file_descriptor = (ptfsal_file_t *)file_desc;
  ccl_context_t   ccl_context;

  ccl_context.handle_index = p_file_descriptor->fd;
  ccl_context.export_id    = p_file_descriptor->export_id;
  ccl_context.uid          = p_file_descriptor->uid;
  ccl_context.gid          = p_file_descriptor->gid;

  // we will use 256K i/o with vtl but allow larger i/o from NFS
  FSI_TRACE(FSI_DEBUG, "FSI - [%4d] xmp_write off %ld size %ld\n", 
            in_handle, offset, size);
  while (cur_size > IO_BUFFER_SIZE) {
    FSI_TRACE(FSI_DEBUG, "FSI - [%4d] pwrite - split %d\n", 
              in_handle, split_count);
    rc = ccl_pwrite(&ccl_context, in_handle, &buf[buf_offset], IO_BUFFER_SIZE, 
                    cur_offset);
    if (rc == -1) {
      return rc;
    }
    cur_size   -= IO_BUFFER_SIZE;
    cur_offset += IO_BUFFER_SIZE;
    buf_offset += IO_BUFFER_SIZE;
    split_count++;
  }

  // call with remainder
  if (cur_size > 0) {
    FSI_TRACE(FSI_DEBUG, "FSI - [%4d]  pwrite - split %d\n", in_handle,  
              split_count);
    rc = ccl_pwrite(&ccl_context, in_handle, &buf[buf_offset], cur_size, 
                    cur_offset);
    if (rc == -1) {
      return rc;
    }
  }

  return size;
}
// -----------------------------------------------------------------------------
int
ptfsal_dynamic_fsinfo(fsal_handle_t        * p_filehandle,
                      fsal_op_context_t    * p_context,
                      fsal_dynamicfsinfo_t * p_dynamicinfo)
{
  int  rc;
  char fsi_name[PATH_MAX];

  ccl_context_t ccl_context;
  struct ClientOpDynamicFsInfoRspMsg fs_info;

  rc = ptfsal_handle_to_name(p_filehandle, p_context, fsi_name);
  if (rc) {
    return rc;
  }

  FSI_TRACE(FSI_DEBUG, "Name = %s", fsi_name);

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);
  rc = ccl_dynamic_fsinfo(&ccl_context, fsi_name, &fs_info);
  if (rc) {
    return rc;
  }

  p_dynamicinfo->total_bytes = fs_info.totalBytes;
  p_dynamicinfo->free_bytes  = fs_info.freeBytes;
  p_dynamicinfo->avail_bytes = fs_info.availableBytes;

  p_dynamicinfo->total_files = fs_info.totalFiles;
  p_dynamicinfo->free_files  = fs_info.freeFiles;
  p_dynamicinfo->avail_files = fs_info.availableFiles;

  p_dynamicinfo->time_delta.seconds  = fs_info.time.tv_sec;
  p_dynamicinfo->time_delta.nseconds = fs_info.time.tv_nsec;

  return 0;
}
// -----------------------------------------------------------------------------
int
ptfsal_readlink(fsal_handle_t     * p_linkhandle,
                fsal_op_context_t * p_context,
                char              * p_buf)
{
  int  rc;
  char fsi_name[PATH_MAX];

  ccl_context_t             ccl_context;
  struct PersistentHandle   pt_handler;
  ptfsal_handle_t         * p_fsi_handle = (ptfsal_handle_t *)p_linkhandle;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  memcpy(&pt_handler.handle, &p_fsi_handle->data.handle.f_handle, 
         sizeof(pt_handler.handle));

  FSI_TRACE(FSI_DEBUG, "Handle=%s", pt_handler.handle);

  memset(fsi_name, 0, sizeof(fsi_name));
  rc = ptfsal_handle_to_name(p_linkhandle, p_context, fsi_name);
  if (rc) {
    return rc;
  }

  rc = ccl_readlink(&ccl_context, fsi_name, p_buf);
  return rc;
}
// -----------------------------------------------------------------------------
int
ptfsal_symlink(fsal_handle_t     * p_parent_directory_handle,
               fsal_name_t       * p_linkname,
               fsal_path_t       * p_linkcontent,
               fsal_op_context_t * p_context,
               fsal_accessmode_t   accessmode,
               fsal_handle_t     * p_link_handle)
{
  int rc;

  ccl_context_t ccl_context;
  fsal_path_t               pt_path;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  rc = ccl_symlink(&ccl_context, p_linkname->name, p_linkcontent->path);
  if (rc) {
    return rc;
  }

  memset(&pt_path, 0, sizeof(fsal_path_t));
  memcpy(&pt_path, p_linkname, sizeof(fsal_name_t));

  rc = ptfsal_name_to_handle(p_context, &pt_path, p_link_handle);

  return rc;
}
// -----------------------------------------------------------------------------
int
ptfsal_name_to_handle(fsal_op_context_t * p_context,
                      fsal_path_t       * p_fsalpath,
                      fsal_handle_t     * p_handle)
{
  int rc;

  ccl_context_t ccl_context;
  struct PersistentHandle   pt_handler;
  ptfsal_handle_t         * p_fsi_handle       = (ptfsal_handle_t *)p_handle;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  memset(&pt_handler, 0, sizeof(struct PersistentHandle));
  rc = ccl_name_to_handle(&ccl_context, p_fsalpath->path, &pt_handler);
  if (rc) {
    FSI_TRACE(FSI_DEBUG, "CCL name to handle failed %d!", rc);
    return rc;
  }

  memcpy(&p_fsi_handle->data.handle.f_handle, &pt_handler.handle, 
         sizeof(p_fsi_handle->data.handle.f_handle));

  p_fsi_handle->data.handle.handle_size     = FSI_PERSISTENT_HANDLE_N_BYTES;
  p_fsi_handle->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
  p_fsi_handle->data.handle.handle_version  = OPENHANDLE_VERSION;

  FSI_TRACE(FSI_DEBUG, "Name to Handle: \n");
  ptfsal_print_handle(pt_handler.handle);
  ptfsal_print_handle(p_fsi_handle->data.handle.f_handle);
  return 0;
}
// -----------------------------------------------------------------------------
int
ptfsal_handle_to_name(fsal_handle_t     * p_filehandle,
                      fsal_op_context_t * p_context,
                      char              * path)
{
  int rc;

  ccl_context_t ccl_context;
  struct PersistentHandle pt_handler;
  ptfsal_handle_t * p_fsi_handle = (ptfsal_handle_t *)p_filehandle;

  ptfsal_set_fsi_handle_data(p_context, &ccl_context);

  memcpy(&pt_handler.handle, &p_fsi_handle->data.handle.f_handle, 
         sizeof(pt_handler.handle));
  ptfsal_print_handle(pt_handler.handle);

  rc = ccl_handle_to_name(&ccl_context, &pt_handler, path);

  return rc;
}
// -----------------------------------------------------------------------------
void ptfsal_print_handle(char * handle)
{
  uint64_t * handlePtr = (uint64_t *) handle;
  FSI_TRACE(FSI_DEBUG, "FSI - handle 0x%lx %lx %lx %lx",
            handlePtr[0], handlePtr[1], handlePtr[2], handlePtr[3]);
}



// -----------------------------------------------------------------------------
int
fsi_update_cache_stat(const char * p_filename,
                      uint64_t     newMode)
{
  int index;
  int rc;

  pthread_mutex_lock(&g_non_io_mutex);
  index = ccl_find_handle_by_name(p_filename);
  if (index != -1) {
    g_fsi_handles.m_handle[index].m_stat.st_mode = newMode;
    rc = 0;
  } else {
    FSI_TRACE(FSI_DEBUG, "ERROR: Update cache stat");
    rc = -1;
  }
  pthread_mutex_unlock(&g_non_io_mutex);

  return rc;
}

// This function will convert Ganesha FSAL type to the upper
// bits for unix stat structure
mode_t fsal_type2unix(int fsal_type)
{
  mode_t outMode = 0;
  FSI_TRACE(FSI_DEBUG, "fsal_type: %d",fsal_type);
  switch (fsal_type)
  {
    case FSAL_TYPE_FIFO:
      outMode = S_IFIFO;
      break;
    case FSAL_TYPE_CHR:
      outMode = S_IFCHR;
      break;
    case FSAL_TYPE_DIR:
      outMode = S_IFDIR;
      break;
    case FSAL_TYPE_BLK:
      outMode = S_IFBLK;
      break;
    case FSAL_TYPE_FILE:
      outMode = S_IFREG;
      break;
    case FSAL_TYPE_LNK:
      outMode = S_IFLNK;
      break;
    case FSAL_TYPE_SOCK:
      outMode = FSAL_TYPE_SOCK;
      break;
    default:
      FSI_TRACE(FSI_ERR, "Unknown fsal type: %d", fsal_type);
    }

    return outMode;
}

// This function will fill in ccl_context_t
void ptfsal_set_fsi_handle_data(fsal_op_context_t * p_context, 
                                ccl_context_t     * ccl_context) 
{
  ptfsal_op_context_t     * fsi_op_context     = 
    (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context = 
    fsi_op_context->export_context;

  ccl_context->export_id = fsi_export_context->pt_export_id;
  ccl_context->uid       = fsi_op_context->credential.user;
  ccl_context->gid       = fsi_op_context->credential.group;
  ccl_context->export_path = fsi_export_context->mount_point;
  FSI_TRACE(FSI_DEBUG, "Export ID = %ld, uid = %ld, gid = %ld, Export Path = \
            %s\n", fsi_export_context->pt_export_id, 
            fsi_op_context->credential.user,
            fsi_op_context->credential.group, 
            fsi_export_context->mount_point);
}

