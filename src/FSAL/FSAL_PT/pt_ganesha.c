/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2010, 2011
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    pt_ganesha.c
 * Description: Main layer for PT's Ganesha FSAL
 * Author:      FSI IPC Team
 * ----------------------------------------------------------------------------
 */
#include "pt_ganesha.h"
#include "fsal_types.h"
#include "export_mgr.h"
#include "nfs_exports.h"
/*#include "pt_util_cache.h" */

/* global context caching flag. Allows turning off aching for debugging*/
int g_ptfsal_context_flag = 1;

struct fsi_handle_cache_t g_fsi_name_handle_cache;
pthread_rwlock_t g_fsi_cache_handle_rw_lock;
static pthread_key_t ptfsal_thread_key;
static pthread_once_t ptfsal_once_key = PTHREAD_ONCE_INIT;

typedef struct ptfsal_threadcontext_t {
	int cur_namecache_handle_index;
	int cur_fsi_handle_index;
} ptfsal_threadcontext_t;

/*
 * the function mapping that will be used in the FSAL layer, externed in
 * pt_ganesha.h and instantiated here
 */
struct vfs_fn_pointers g_ccl_function_map;
void *g_ccl_lib_handle;
struct file_handles_struct_t *g_fsal_fsi_handles;

/* ------------------------------------------------------------------------- */
int handle_index_is_valid(int handle_index)
{
	if (handle_index < 0)
		return 0;
	if (handle_index >= (FSI_CCL_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS))
		return 0;
	return 1;
}

/* ------------------------------------------------------------------------- */
static void ptfsal_create_key()
{
	if (pthread_key_create(&ptfsal_thread_key, NULL) < 0) {
		FSI_TRACE(FSI_FATAL, "cannot create fsal pthread key errno=%d",
			  errno);
	}
}

/* ------------------------------------------------------------------------- */
ptfsal_threadcontext_t *ptfsal_get_thread_context()
{
	ptfsal_threadcontext_t *p_cur_context;

	/* init keys if first time */
	if (pthread_once(&ptfsal_once_key, &ptfsal_create_key) < 0)
		FSI_TRACE(FSI_FATAL, "cannot init fsal pthread specific vars");

	p_cur_context =
	    (ptfsal_threadcontext_t *) pthread_getspecific(ptfsal_thread_key);

	if (p_cur_context == NULL) {
		p_cur_context = gsh_malloc(sizeof(ptfsal_threadcontext_t));
		FSI_TRACE(FSI_NOTICE, "malloc %lu bytes fsal specific data",
			  sizeof(ptfsal_threadcontext_t));
		if (p_cur_context != NULL) {
			/* we init our stuff for the first time */
			p_cur_context->cur_namecache_handle_index = -1;
			p_cur_context->cur_fsi_handle_index = -1;
			/* now set the thread context */
			pthread_setspecific(ptfsal_thread_key,
					    (void *)p_cur_context);
		} else {
			FSI_TRACE(FSI_FATAL,
				  "cannot malloc fsal pthread key errno=%d",
				  errno);
		}
	}
	return p_cur_context;
}

/* ------------------------------------------------------------------------- */
void fsi_get_whole_path(const char *parentPath, const char *name, char *path)
{
	FSI_TRACE(FSI_DEBUG, "parentPath=%s, name=%s\n", parentPath, name);
	if (!strcmp(parentPath, "/") || !strcmp(parentPath, "")) {
		snprintf(path, PATH_MAX, "%s", name);
	} else {
		size_t export_len = strnlen(parentPath, PATH_MAX);
		if (parentPath[export_len - 1] == '/')
			snprintf(path, PATH_MAX, "%s%s", parentPath, name);
		else
			snprintf(path, PATH_MAX, "%s/%s", parentPath, name);
	}
	FSI_TRACE(FSI_DEBUG, "Full Path: %s", path);
}

/* ------------------------------------------------------------------------- */
int fsi_cache_name_and_handle(const struct req_op_context *p_context,
			      char *handle, char *name)
{
	struct fsi_handle_cache_entry_t handle_entry;
	uint64_t *handlePtr = (uint64_t *) handle;

	pthread_rwlock_wrlock(&g_fsi_cache_handle_rw_lock);
	g_fsi_name_handle_cache.m_count = (g_fsi_name_handle_cache.m_count + 1)
	    % FSI_MAX_HANDLE_CACHE_ENTRY;

	memcpy(&g_fsi_name_handle_cache.
	       m_entry[g_fsi_name_handle_cache.m_count].m_handle, &handle[0],
	       sizeof(handle_entry.m_handle));
	strncpy(g_fsi_name_handle_cache.
		m_entry[g_fsi_name_handle_cache.m_count].m_name, name,
		sizeof(handle_entry.m_name));
	g_fsi_name_handle_cache.m_entry[g_fsi_name_handle_cache.m_count]
	    .m_name[sizeof(handle_entry.m_name) - 1] = '\0';
	FSI_TRACE(FSI_DEBUG, "FSI - added %s to name cache entry %d\n", name,
		  g_fsi_name_handle_cache.m_count);
	pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);

	if (strnlen(name, 1) == 0) {
		FSI_TRACE(FSI_NOTICE,
			  "The name is empty string for handle : "
			  "%p->0x%lx %lx %lx %lx", handle, handlePtr[0],
			  handlePtr[1], handlePtr[2], handlePtr[3]);
	}
	return 0;
}

/* -------------------------------------------------------------------------- */
int fsi_get_name_from_handle(const struct req_op_context *p_context, /* IN */
			     struct fsal_export *export,
			     ptfsal_handle_t *pt_handle, /* IN */
			     char *name, /* OUT */
			     int *handle_index) /* OUT */
{
	int index;
	int rc;
	int err = 0;
	ccl_context_t ccl_context;
	struct CCLPersistentHandle pt_handler;
	struct fsi_handle_cache_entry_t handle_entry;
	uint64_t *handlePtr = (uint64_t *) pt_handle->data.handle.f_handle;
	ptfsal_threadcontext_t *p_cur_context;
	CACHE_TABLE_ENTRY_T cacheLookupEntry;
	CACHE_ENTRY_DATA_HANDLE_TO_NAME_T *handleToNameEntryPtr;
	FSI_TRACE(FSI_DEBUG, "Get name from handle:\n");
	ptfsal_print_handle(pt_handle->data.handle.f_handle);

	if (handle_index != NULL)
		*handle_index = -1;
	/* Get name from cache by index cached. */
	if (g_ptfsal_context_flag) {
		p_cur_context = ptfsal_get_thread_context();
		if (p_cur_context != NULL
		    && p_cur_context->cur_namecache_handle_index != -1) {
			/* look for context cache match */
			FSI_TRACE(FSI_DEBUG, "cur namecache index %d",
				  p_cur_context->cur_namecache_handle_index);
			/* try and get a direct hit, else drop
			 * through code as exists now */
			index = p_cur_context->cur_namecache_handle_index;
			pthread_rwlock_rdlock(&g_fsi_cache_handle_rw_lock);
			if (memcmp
			    (&handlePtr[0],
			     &g_fsi_name_handle_cache.m_entry[index].m_handle,
			     FSI_CCL_PERSISTENT_HANDLE_N_BYTES) == 0) {
				strncpy(name,
					g_fsi_name_handle_cache.m_entry[index].
					m_name, sizeof(handle_entry.m_name));
				name[sizeof(handle_entry.m_name) - 1] = '\0';
				FSI_TRACE(FSI_DEBUG,
					  "FSI - name = %s cache index %d DIRECT HIT\n",
					  name, index);
				/* Check whether the name from cache is empty */
				if (strnlen(name, 1) == 0) {
					FSI_TRACE(FSI_NOTICE,
						  "The name is empty string from cache by index:"
						  "%p->0x%lx %lx %lx %lx",
						  handlePtr, handlePtr[0],
						  handlePtr[1], handlePtr[2],
						  handlePtr[3]);
					/* Need get name from PT side, so will
					 * not return and clear cache. */
					memset(g_fsi_name_handle_cache.
					     m_entry[index].m_handle, 0,
					     FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
					g_fsi_name_handle_cache.m_entry[index].
					    m_name[0] = '\0';
				} else {
					/* Return. */
					pthread_rwlock_unlock
					    (&g_fsi_cache_handle_rw_lock);
					return 0;
				}
			}
			pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);
		} else {
			FSI_TRACE(FSI_DEBUG, "context is null");
		}
	}

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	/* Look up our front end opened handle cache */

	pthread_rwlock_rdlock(&g_fsi_cache_handle_rw_lock);
	cacheLookupEntry.key = &handlePtr[0];

	rc = fsi_cache_getEntry(&g_fsi_name_handle_cache_opened_files,
				&cacheLookupEntry);

	if (rc == FSI_CCL_IPC_EOK) {
		handleToNameEntryPtr =
		    (CACHE_ENTRY_DATA_HANDLE_TO_NAME_T *) cacheLookupEntry.data;
		strncpy(name, handleToNameEntryPtr->m_name,
			sizeof(handle_entry.m_name));
		name[sizeof(handle_entry.m_name) - 1] = '\0';
		FSI_TRACE(FSI_DEBUG, "FSI - name = %s opened file cache HIT\n",
			  name);
		/* Check whether the name from cache is empty */
		if (strnlen(name, 1) == 0) {
			FSI_TRACE(FSI_NOTICE,
				  "The name is empty string from opened file cache:"
				  "%p->0x%lx %lx %lx %lx.  Continue searching other caches",
				  handlePtr, handlePtr[0], handlePtr[1],
				  handlePtr[2], handlePtr[3]);
		} else {
			/* Return. */
			if (handle_index != NULL) {
				*handle_index =
				    handleToNameEntryPtr->handle_index;
				FSI_TRACE(FSI_DEBUG,
					  "Handle index = %d found in open file cache",
					  *handle_index);
			}
			pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);
			return 0;
		}
	}
	pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);

	/* Get name from cache by iterate all cache entries. */
	pthread_rwlock_rdlock(&g_fsi_cache_handle_rw_lock);
	for (index = 0; index < FSI_MAX_HANDLE_CACHE_ENTRY; index++) {
		if (memcmp
		    (&handlePtr[0],
		     &g_fsi_name_handle_cache.m_entry[index].m_handle,
		     FSI_CCL_PERSISTENT_HANDLE_N_BYTES) == 0) {
			strncpy(name,
				g_fsi_name_handle_cache.m_entry[index].m_name,
				sizeof(handle_entry.m_name));
			name[sizeof(handle_entry.m_name) - 1] = '\0';

			if (g_ptfsal_context_flag && p_cur_context != NULL) {
				/* store current index in context cache */
				FSI_TRACE(FSI_DEBUG,
					  "FSI - name = %s cache index %d\n",
					  name, index);
				p_cur_context->cur_namecache_handle_index =
				    index;
			}

			FSI_TRACE(FSI_DEBUG, "FSI - name = %s\n", name);

			/* Check whether the name from cache is empty */
			if (strnlen(name, 1) == 0) {
				FSI_TRACE(FSI_NOTICE,
					  "The name is empty string from cache by loop: "
					  "%p->0x%lx %lx %lx %lx", handlePtr,
					  handlePtr[0], handlePtr[1],
					  handlePtr[2], handlePtr[3]);
				/* Need get name from PT side,
				 * so will not return,  */
				memset(g_fsi_name_handle_cache.m_entry[index].
				       m_handle, 0,
				       FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
				g_fsi_name_handle_cache.m_entry[index].
				    m_name[0] = '\0';
				break;
			} else {
				/* Return, find the non-empty name. */
				pthread_rwlock_unlock
				    (&g_fsi_cache_handle_rw_lock);
				return 0;
			}
		}
	}
	pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);

	/* Not in cache, so send request to PT.  */
	memset(&pt_handler.handle, 0, FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
	memcpy(&pt_handler.handle, handlePtr,
	       FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
	FSI_TRACE(FSI_DEBUG, "Handle:\n");
	ptfsal_print_handle((char *)handlePtr);
	err = 0;
	rc = CCL_HANDLE_TO_NAME(&ccl_context, &pt_handler, name);
	if (rc != 0)
		err = errno;
	FSI_TRACE(FSI_DEBUG, "The rc %d, handle 0x%lx %lx %lx %lx, name %s", rc,
		  handlePtr[0], handlePtr[1], handlePtr[2], handlePtr[3], name);

	if (rc == 0) {
		if (strnlen(name, 1) == 0) {
			FSI_TRACE(FSI_NOTICE,
				  "The name is empty string from PT: "
				  "%p->0x%lx %lx %lx %lx", handlePtr,
				  handlePtr[0], handlePtr[1], handlePtr[2],
				  handlePtr[3]);
		} else {
			pthread_rwlock_rdlock(&g_fsi_cache_handle_rw_lock);
			g_fsi_name_handle_cache.m_count =
			    (g_fsi_name_handle_cache.m_count + 1)
			    % FSI_MAX_HANDLE_CACHE_ENTRY;

			memcpy(&g_fsi_name_handle_cache.
			       m_entry[g_fsi_name_handle_cache.m_count].
			       m_handle, &handlePtr[0],
			       FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
			strncpy(g_fsi_name_handle_cache.
				m_entry[g_fsi_name_handle_cache.m_count].m_name,
				name, sizeof(handle_entry.m_name));
			g_fsi_name_handle_cache.m_entry[g_fsi_name_handle_cache.
							m_count]
			    .m_name[sizeof(handle_entry.m_name) - 1] = '\0';
			FSI_TRACE(FSI_DEBUG,
				  "FSI - added %s to name cache entry %d\n",
				  name, g_fsi_name_handle_cache.m_count);
			if (g_ptfsal_context_flag && p_cur_context != NULL) {
				/* store current index in context cache */
				p_cur_context->cur_namecache_handle_index =
				    g_fsi_name_handle_cache.m_count;
			}
			pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);
		}
	} else {
		FSI_TRACE(FSI_ERR, "The ccl_handle_to_name got error!");
		errno = err;
	}

	return rc;
}

/* -------------------------------------------------------------------------- */
int fsi_update_cache_name(char *oldname, char *newname)
{
	int index;
	struct fsi_handle_cache_entry_t handle_entry;

	FSI_TRACE(FSI_DEBUG, "oldname[%s]->newname[%s]", oldname, newname);
	if (strnlen(newname, 1) == 0) {
		FSI_TRACE(FSI_ERR, "The file name is empty string.");
		return -1;
	}

	pthread_rwlock_wrlock(&g_fsi_cache_handle_rw_lock);
	for (index = 0; index < FSI_MAX_HANDLE_CACHE_ENTRY; index++) {
		FSI_TRACE(FSI_DEBUG, "cache entry[%d]: %s", index,
			  g_fsi_name_handle_cache.m_entry[index].m_name);
		if (strncmp((const char *)oldname, (const char *)
			    g_fsi_name_handle_cache.m_entry[index].m_name,
			    PATH_MAX)
		    == 0) {
			FSI_TRACE(FSI_DEBUG,
				  "FSI - Updating cache old name[%s]-> new name[%s]\n",
				  g_fsi_name_handle_cache.m_entry[index].m_name,
				  newname);
			strncpy(g_fsi_name_handle_cache.m_entry[index].m_name,
				newname, sizeof(handle_entry.m_name));
			g_fsi_name_handle_cache.m_entry[index]
			    .m_name[sizeof(handle_entry.m_name) - 1] = '\0';
		}
	}
	pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);

	return 0;
}

void fsi_remove_cache_by_handle(char *handle)
{
	int index;
	pthread_rwlock_wrlock(&g_fsi_cache_handle_rw_lock);
	for (index = 0; index < FSI_MAX_HANDLE_CACHE_ENTRY; index++) {

		if (memcmp
		    (handle, &g_fsi_name_handle_cache.m_entry[index].m_handle,
		     FSI_CCL_PERSISTENT_HANDLE_N_BYTES) == 0) {
			FSI_TRACE(FSI_DEBUG,
				  "Handle will be removed from cache:")
			    ptfsal_print_handle(handle);
			/* Mark the both handle and name to 0 */
			memset(g_fsi_name_handle_cache.m_entry[index].m_handle,
			       0, FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
			g_fsi_name_handle_cache.m_entry[index].m_name[0] = '\0';
			break;
		}
	}
	pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);
}

void fsi_remove_cache_by_fullpath(char *path)
{
	int index;
	int len = strlen(path);

	if (len > PATH_MAX)
		return;

	/* TBD. The error return from pthread_mutex_lock will be handled
	 * when improve read/write lock.
	 */
	pthread_rwlock_wrlock(&g_fsi_cache_handle_rw_lock);
	for (index = 0; index < FSI_MAX_HANDLE_CACHE_ENTRY; index++) {
		if (memcmp
		    (path, g_fsi_name_handle_cache.m_entry[index].m_name, len)
		    == 0) {
			FSI_TRACE(FSI_DEBUG,
				  "Handle will be removed from cache by path %s:",
				  path);
			/* Mark the both handle and name to 0 */
			memset(g_fsi_name_handle_cache.m_entry[index].m_handle,
			       0, FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
			g_fsi_name_handle_cache.m_entry[index].m_name[0] = '\0';
			break;
		}
	}
	pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);
}

/* -------------------------------------------------------------------------- */
int fsi_check_handle_index(int handle_index)
{
	/* check handle */
	if ((handle_index >= 0)
	    && (handle_index <
		(FSI_CCL_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS))) {
		return 0;
	} else {
		return -1;
	}
}

/* -------------------------------------------------------------------------- */
int ptfsal_rename(const struct req_op_context *p_context,
		  struct pt_fsal_obj_handle *p_old_parentdir_handle,
		  const char *p_old_name,
		  struct pt_fsal_obj_handle *p_new_parentdir_handle,
		  const char *p_new_name)
{
	int rc;

	ccl_context_t ccl_context;
	char fsi_old_parent_dir_name[PATH_MAX];
	char fsi_new_parent_dir_name[PATH_MAX];
	char fsi_old_fullpath[PATH_MAX];
	char fsi_new_fullpath[PATH_MAX];

	ptfsal_set_fsi_handle_data(p_context->fsal_export,
				   p_context, &ccl_context);

	rc = fsi_get_name_from_handle(p_context,
				      p_context->fsal_export,
				      p_old_parentdir_handle->handle,
				      fsi_old_parent_dir_name, NULL);
	if (rc < 0) {
		FSI_TRACE(FSI_ERR, "Failed to get name from handle.");
		return rc;
	}
	rc = fsi_get_name_from_handle(p_context,
				      p_context->fsal_export,
				      p_new_parentdir_handle->handle,
				      fsi_new_parent_dir_name, NULL);
	if (rc < 0) {
		FSI_TRACE(FSI_ERR, "Failed to get name from handle.");
		return rc;
	}
	fsi_get_whole_path(fsi_old_parent_dir_name, p_old_name,
			   fsi_old_fullpath);
	fsi_get_whole_path(fsi_new_parent_dir_name, p_new_name,
			   fsi_new_fullpath);
	FSI_TRACE(FSI_DEBUG, "Full path is %s", fsi_old_fullpath);
	FSI_TRACE(FSI_DEBUG, "Full path is %s", fsi_new_fullpath);

	if (strnlen(fsi_new_fullpath, PATH_MAX) == 0) {
		FSI_TRACE(FSI_ERR, "The file name is empty string.");
		return -1;
	}
	rc = CCL_RENAME(&ccl_context, fsi_old_fullpath, fsi_new_fullpath);
	if (rc == 0)
		fsi_update_cache_name(fsi_old_fullpath, fsi_new_fullpath);

	return rc;
}

/* -------------------------------------------------------------------------- */
int ptfsal_stat_by_parent_name(const struct req_op_context *p_context,
			       struct pt_fsal_obj_handle *p_parentdir_handle,
			       const char *p_filename, fsi_stat_struct *p_stat)
{
	int stat_rc;

	ccl_context_t ccl_context;
	char fsi_parent_dir_name[PATH_MAX];
	char fsi_fullpath[PATH_MAX];

	ptfsal_set_fsi_handle_data(p_context->fsal_export,
				   p_context, &ccl_context);

	stat_rc =
	    fsi_get_name_from_handle(p_context,
				     p_context->fsal_export,
				     p_parentdir_handle->handle,
				     fsi_parent_dir_name, NULL);
	if (stat_rc < 0) {
		FSI_TRACE(FSI_ERR, "Failed to get name from handle.");
		return stat_rc;
	}
	fsi_get_whole_path(fsi_parent_dir_name, p_filename, fsi_fullpath);
	FSI_TRACE(FSI_DEBUG, "Full path is %s", fsi_fullpath);

	memset(p_stat, 0, sizeof(fsi_stat_struct));
	stat_rc = CCL_STAT(&ccl_context, fsi_fullpath, p_stat);

	ptfsal_print_handle(p_stat->st_persistentHandle.handle);
	return stat_rc;
}

/* ------------------------------------------------------------------------- */
int ptfsal_stat_by_name(const struct req_op_context *p_context,
			struct fsal_export *export, const char *p_fsalpath,
			fsi_stat_struct *p_stat)
{
	int stat_rc;

	ccl_context_t ccl_context;

	ptfsal_set_fsi_handle_data_path(export, p_context,
				      (char *)p_fsalpath, &ccl_context);

	FSI_TRACE(FSI_DEBUG, "FSI - name = %s\n", p_fsalpath);

	stat_rc = CCL_STAT(&ccl_context, p_fsalpath, p_stat);

	ptfsal_print_handle(p_stat->st_persistentHandle.handle);

	return stat_rc;
}

/* ------------------------------------------------------------------------- */
void fsi_stat2stat(fsi_stat_struct *fsi_stat, struct stat *p_stat)
{
	p_stat->st_mode = fsi_stat->st_mode;
	p_stat->st_size = fsi_stat->st_size;
	p_stat->st_dev = fsi_stat->st_dev;
	p_stat->st_ino = fsi_stat->st_ino;
	p_stat->st_nlink = fsi_stat->st_nlink;
	p_stat->st_uid = fsi_stat->st_uid;
	p_stat->st_gid = fsi_stat->st_gid;
	p_stat->st_atime = fsi_stat->st_atime_sec;
	p_stat->st_ctime = fsi_stat->st_ctime_sec;
	p_stat->st_mtime = fsi_stat->st_mtime_sec;
	p_stat->st_blocks = fsi_stat->st_blocks;
	p_stat->st_rdev = fsi_stat->st_rdev;
}

int ptfsal_stat_by_handle(const struct req_op_context *p_context,
			  struct fsal_export *export,
			  ptfsal_handle_t *p_filehandle, struct stat *p_stat)
{
	int stat_rc;
	char fsi_name[PATH_MAX];
	fsi_stat_struct fsi_stat;
	ccl_context_t ccl_context;
	struct CCLPersistentHandle pt_handler;
	ptfsal_handle_t *p_fsi_handle = p_filehandle;

	FSI_TRACE(FSI_DEBUG, "FSI - handle:\n");
	memset(&fsi_stat, 0, sizeof(fsi_stat));
	ptfsal_print_handle(p_fsi_handle->data.handle.f_handle);

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	memset(fsi_name, 0, sizeof(fsi_name));
	stat_rc =
	    fsi_get_name_from_handle(p_context, export, p_filehandle, fsi_name,
				     NULL);
	FSI_TRACE(FSI_DEBUG, "FSI - rc = %d\n", stat_rc);
	if (stat_rc) {
		FSI_TRACE(FSI_ERR, "Return rc %d from get name from handle %s",
			  stat_rc, p_fsi_handle->data.handle.f_handle);
		return stat_rc;
	}
	FSI_TRACE(FSI_DEBUG, "FSI - name = %s\n", fsi_name);

	if (g_ptfsal_context_flag) {
		ptfsal_threadcontext_t *p_cur_context;
		p_cur_context = ptfsal_get_thread_context();
		if (p_cur_context != NULL
		    && p_cur_context->cur_fsi_handle_index != -1) {
			/*
			 * attempt a direct stat based on the stored index,
			 * if it fails, get stat by calling normal ccl routines
			 */

			FSI_TRACE(FSI_DEBUG,
				  "FSI - faststat handle [%d] name [%s]\n",
				  p_cur_context->cur_fsi_handle_index,
				  fsi_name);
			if (CCL_FSAL_TRY_STAT_BY_INDEX
			    (&ccl_context, p_cur_context->cur_fsi_handle_index,
			     fsi_name, &fsi_stat) == 0) {
				fsi_stat2stat(&fsi_stat, p_stat);
				return 0;
			}
		} else {
			FSI_TRACE(FSI_DEBUG, "context is null");
		}
	}

	int fsihandle =
	    CCL_FIND_HANDLE_BY_NAME_AND_EXPORT(fsi_name, &ccl_context);

	if (fsihandle != -1) {
		/*
		 * If we have cached stat information,
		 * then we call regular stat
		 */
		stat_rc = CCL_STAT(&ccl_context, fsi_name, &fsi_stat);
		fsi_stat2stat(&fsi_stat, p_stat);
	} else {
		memset(&pt_handler.handle, 0, sizeof(pt_handler.handle));
		memcpy(&pt_handler.handle, &p_fsi_handle->data.handle.f_handle,
		       sizeof(pt_handler.handle));
		stat_rc =
		    CCL_STAT_BY_HANDLE(&ccl_context, &pt_handler, &fsi_stat);
		fsi_stat2stat(&fsi_stat, p_stat);

	}

	if (stat_rc == -1)
		FSI_TRACE(FSI_ERR, "FSI - stat failed. fsi_name[%s]", fsi_name);

	ptfsal_print_handle(fsi_stat.st_persistentHandle.handle);

	return stat_rc;
}

/* -------------------------------------------------------------------------- */
int ptfsal_opendir(const struct req_op_context *p_context,
		   struct fsal_export *export, const char *filename,
		   const char *mask, uint32_t attr)
{
	int dir_handle;
	int err = 0;

	ccl_context_t ccl_context;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	FSI_TRACE(FSI_DEBUG, "This will be full path: %s\n", filename);
	dir_handle = CCL_OPENDIR(&ccl_context, filename, mask, attr);
	if (dir_handle < 0)
		err = errno;
	FSI_TRACE(FSI_DEBUG, "ptfsal_opendir index %d\n", dir_handle);

	if (dir_handle < 0)
		errno = err;

	return dir_handle;
}

/* ------------------------------------------------------------------------- */
int ptfsal_readdir(const struct req_op_context *p_context,
		   struct fsal_export *export, int dir_hnd_index,
		   fsi_stat_struct *sbuf, char *fsi_dname)
{

	int readdir_rc;

	ccl_context_t ccl_context;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	struct fsi_struct_dir_t *dirp = (struct fsi_struct_dir_t *)
	    &g_fsi_dir_handles_fsal->m_dir_handle[dir_hnd_index].
	    m_fsi_struct_dir;

	readdir_rc = CCL_READDIR(&ccl_context, dirp, sbuf);
	if (readdir_rc == 0) {
		strncpy(fsi_dname, dirp->dname, PATH_MAX);
		fsi_dname[PATH_MAX - 1] = '\0';
	} else {
		fsi_dname[0] = '\0';
	}

	return readdir_rc;
}

/* ------------------------------------------------------------------------- */
int ptfsal_closedir(const struct req_op_context *p_context,
		    struct fsal_export *export, ptfsal_dir_t *dir_desc)
{
	int dir_hnd_index;
	ccl_context_t ccl_context;
	ptfsal_dir_t *ptfsal_dir_descriptor = (ptfsal_dir_t *) dir_desc;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	dir_hnd_index = ptfsal_dir_descriptor->fd;

	struct fsi_struct_dir_t *dirp = (struct fsi_struct_dir_t *)
	    &g_fsi_dir_handles_fsal->m_dir_handle[dir_hnd_index].
	    m_fsi_struct_dir;

	return CCL_CLOSEDIR(&ccl_context, dirp);
}

int ptfsal_closedir_fd(const struct req_op_context *p_context,
		       struct fsal_export *export, int fd)
{
	int dir_hnd_index = fd;
	ccl_context_t ccl_context;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	struct fsi_struct_dir_t *dirp = (struct fsi_struct_dir_t *)
	    &g_fsi_dir_handles_fsal->m_dir_handle[dir_hnd_index].
	    m_fsi_struct_dir;

	return CCL_CLOSEDIR(&ccl_context, dirp);
}

/* ------------------------------------------------------------------------- */
int ptfsal_fsync(struct pt_fsal_obj_handle *p_file_descriptor,
		 const struct req_op_context *opctx)
{
	int handle_index;
	int fsync_rc;

	ccl_context_t ccl_context;

	handle_index = p_file_descriptor->u.file.fd;
	if (fsi_check_handle_index(handle_index) < 0)
		return -1;

	ccl_context.handle_index = p_file_descriptor->u.file.fd;
	ccl_context.export_id = opctx->export->export_id;
	ccl_context.uid = opctx->creds->caller_uid;
	ccl_context.gid = opctx->creds->caller_gid;

	fsync_rc = CCL_FSYNC(&ccl_context, handle_index);

	return fsync_rc;

}

/* ------------------------------------------------------------------------- */
int ptfsal_open_by_handle(const struct req_op_context *p_context,
			  struct pt_fsal_obj_handle *p_object_handle,
			  int oflags, mode_t mode)
{
	int open_rc, rc, err;
	char fsi_filename[PATH_MAX];
	int handle_index;
	ptfsal_handle_t *p_fsi_handle = p_object_handle->handle;
	ccl_context_t ccl_context;
	uint64_t *handlePtr = (uint64_t *) p_fsi_handle->data.handle.f_handle;
	CACHE_TABLE_ENTRY_T cacheEntry;
	CACHE_ENTRY_DATA_HANDLE_TO_NAME_T handle_to_name_cache_data;

	FSI_TRACE(FSI_DEBUG, "Open by Handle:");
	ptfsal_print_handle(p_fsi_handle->data.handle.f_handle);

	ptfsal_set_fsi_handle_data(p_context->fsal_export,
				   p_context, &ccl_context);

	strcpy(fsi_filename, "");
	rc = fsi_get_name_from_handle(p_context,
				      p_context->fsal_export,
				      p_object_handle->handle, fsi_filename,
				      &handle_index);
	err = 0;
	if (rc > 0)
		err = rc;
	else if (rc < 0)
		err = errno;
	if (err == ENOENT)
		err = ESTALE;

	if (err != 0) {
		FSI_TRACE(FSI_ERR, "Handle to name failed rc=%d", rc);
		errno = err;
		return -1;
	}
	FSI_TRACE(FSI_DEBUG, "handle to name %s for handle:", fsi_filename);

	/*
	 * The file name should not be empty "". In case it is empty, we
	 * return error.
	 */
	if (strnlen(fsi_filename, 1) == 0) {
		FSI_TRACE(FSI_ERR,
			  "The file name is empty string for handle: "
			  "0x%lx %lx %lx %lx", handlePtr[0], handlePtr[1],
			  handlePtr[2], handlePtr[3]);
		return -1;
	}
	/*
	 * If we found the handle index in the opened file handle cache, we
	 * call fastopen right away.
	 */
	if (handle_index != -1) {
		int handle_index_return = -1;
		FSI_TRACE(FSI_DEBUG, "cur handle index %d", handle_index);
		handle_index_return =
		    CCL_FSAL_TRY_FASTOPEN_BY_INDEX(&ccl_context, handle_index,
						   fsi_filename);
		if (handle_index_return >= 0)
			return handle_index_return;
	}
	/*
	 * since we called fsi_get_name_from_handle, we know the pthread
	 * specific is initialized, so we can just check it
	 */
	ptfsal_threadcontext_t *p_cur_context;
	if (g_ptfsal_context_flag) {
		int existing_handle_index;
		p_cur_context = ptfsal_get_thread_context();
		if (p_cur_context != NULL) {
			/*
			 * try and get a direct hit from context cache,
			 * else drop through to normal search code
			 */
			FSI_TRACE(FSI_DEBUG, "cur handle index %d",
				  p_cur_context->cur_fsi_handle_index);
			existing_handle_index =
			    CCL_FSAL_TRY_FASTOPEN_BY_INDEX(&ccl_context,
							   p_cur_context->
							   cur_fsi_handle_index,
							   fsi_filename);
			if (existing_handle_index >= 0)
				return existing_handle_index;
		} else {
			FSI_TRACE(FSI_DEBUG, "context is null");
		}
	}

	errno = 0;
	open_rc = CCL_OPEN(&ccl_context, fsi_filename, oflags, mode);

	if (open_rc != -1) {
		memset(&cacheEntry, 0x00, sizeof(CACHE_TABLE_ENTRY_T));
		handle_to_name_cache_data.handle_index = open_rc;
		strncpy(handle_to_name_cache_data.m_name, fsi_filename,
			sizeof(handle_to_name_cache_data.m_name));
		handle_to_name_cache_data.m_name
			[sizeof(handle_to_name_cache_data.m_name)-1] = '\0';
		cacheEntry.key = p_fsi_handle->data.handle.f_handle;
		cacheEntry.data = &handle_to_name_cache_data;
		pthread_rwlock_wrlock(&g_fsi_cache_handle_rw_lock);
		rc = fsi_cache_insertEntry
		    (&g_fsi_name_handle_cache_opened_files, &cacheEntry);
		pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);
	}

	if (g_ptfsal_context_flag && open_rc != -1) {
		if (p_cur_context != NULL) {
			/* update our context */
			if (p_cur_context->cur_fsi_handle_index != open_rc)
				p_cur_context->cur_fsi_handle_index = open_rc;
		}
	}

	return open_rc;
}

/* ------------------------------------------------------------------------- */
void ptfsal_close(int handle_index)
{

	if (g_ptfsal_context_flag) {
		ptfsal_threadcontext_t *p_cur_context;
		p_cur_context = ptfsal_get_thread_context();
		if (p_cur_context != NULL) {
			/* update context cache */
			p_cur_context->cur_fsi_handle_index = handle_index;
		}
	}

}

/* -------------------------------------------------------------------------- */

int ptfsal_open(struct pt_fsal_obj_handle *p_parent_directory_handle,
		const char *p_filename, const struct req_op_context *p_context,
		mode_t mode, ptfsal_handle_t *p_object_handle)
{
	int rc;
	char fsi_name[PATH_MAX];
	char fsi_parent_dir_name[PATH_MAX];
	int handleOpened;

	ptfsal_handle_t *p_fsi_handle = p_object_handle;
	ccl_context_t ccl_context;

	ptfsal_set_fsi_handle_data(p_context->fsal_export,
				   p_context, &ccl_context);

	rc = fsi_get_name_from_handle(p_context,
				      p_context->fsal_export,
				      p_parent_directory_handle->handle,
				      fsi_parent_dir_name, NULL);
	if (rc < 0) {
		FSI_TRACE(FSI_ERR,
			  "Handle to name failed rc=%d, "
			  "failed to get parent directory name.", rc);
		return rc;
	}
	FSI_TRACE(FSI_DEBUG, "FSI - Parent dir name = %s\n",
		  fsi_parent_dir_name);
	FSI_TRACE(FSI_DEBUG, "FSI - File name %s\n", p_filename);

	memset(&fsi_name, 0, sizeof(fsi_name));
	fsi_get_whole_path(fsi_parent_dir_name, p_filename, fsi_name);

	/*
	 * The file name should not be empty "". In case it is empty, we
	 * return error.
	 */
	if (strnlen(fsi_name, 1) == 0) {
		FSI_TRACE(FSI_ERR, "The file name is empty string.");
		return -1;
	}
	/* Will create a new file in backend. */
	handleOpened = CCL_OPEN(&ccl_context, fsi_name, O_CREAT, mode);

	if (handleOpened >= 0) {
		char fsal_path[PATH_MAX];
		memset(fsal_path, 0, PATH_MAX);
		memcpy(fsal_path, &fsi_name, PATH_MAX);
		rc = ptfsal_name_to_handle(p_context,
					   p_context->fsal_export,
					   fsal_path,
					   p_object_handle);
		if (rc != 0) {
			FSI_TRACE(FSI_ERR, "Name to handle failed\n");
			return -1;
		}

		rc = CCL_CLOSE(&ccl_context, handleOpened,
			       CCL_CLOSE_STYLE_NORMAL);
		if (rc == -1) {
			FSI_TRACE(FSI_ERR, "Failed to close handle %d",
				  handleOpened);
		}
		fsi_cache_name_and_handle(p_context,
					  (char *)&p_fsi_handle->data.handle.
					  f_handle, fsi_name);
	}
	return handleOpened;
}

/* ------------------------------------------------------------------------- */
int ptfsal_ftruncate(const struct req_op_context *p_context,
		     struct fsal_export *export, int handle_index,
		     uint64_t offset)
{
	ccl_context_t ccl_context;
	int ftrunc_rc;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	ftrunc_rc = CCL_FTRUNCATE(&ccl_context, handle_index, offset);

	return ftrunc_rc;

}

/* ------------------------------------------------------------------------- */
int ptfsal_unlink(const struct req_op_context *p_context,
		  struct pt_fsal_obj_handle *p_parent_directory_handle,
		  const char *p_filename)
{
	int rc;
	ccl_context_t ccl_context;
	char fsi_parent_dir_name[PATH_MAX];
	char fsi_fullpath[PATH_MAX];
	CACHE_TABLE_ENTRY_T cacheEntry;
	char key[FSI_CCL_PERSISTENT_HANDLE_N_BYTES];
	int cacheDeleteRC;
	int handle_index_to_close;
	rc = fsi_get_name_from_handle(p_context,
				      p_context->fsal_export,
				      p_parent_directory_handle->handle,
				      fsi_parent_dir_name, NULL);
	if (rc < 0) {
		FSI_TRACE(FSI_ERR, "Failed to get name from handle.");
		return rc;
	}
	fsi_get_whole_path(fsi_parent_dir_name, p_filename, fsi_fullpath);
	FSI_TRACE(FSI_DEBUG, "Full path is %s", fsi_fullpath);

	ptfsal_set_fsi_handle_data(p_context->fsal_export,
				   p_context, &ccl_context);

	handle_index_to_close =
	    CCL_FIND_HANDLE_BY_NAME_AND_EXPORT(fsi_fullpath, &ccl_context);
	if (handle_index_to_close != -1) {
		memset(&cacheEntry, 0x00, sizeof(CACHE_TABLE_ENTRY_T));
		memcpy(key,
		       &g_fsi_handles_fsal->m_handle[handle_index_to_close].
		       m_stat.st_persistentHandle.handle[0],
		       FSI_CCL_PERSISTENT_HANDLE_N_BYTES);
		cacheEntry.key = key;
	}

	rc = CCL_UNLINK(&ccl_context, fsi_fullpath);
	/* remove from cache even unlink is not succesful */
	fsi_remove_cache_by_fullpath(fsi_fullpath);

	if (handle_index_to_close != -1) {
		pthread_rwlock_wrlock(&g_fsi_cache_handle_rw_lock);
		cacheDeleteRC =
		    fsi_cache_deleteEntry(&g_fsi_name_handle_cache_opened_files,
					  &cacheEntry);
		pthread_rwlock_unlock(&g_fsi_cache_handle_rw_lock);
		if (cacheDeleteRC != FSI_CCL_IPC_EOK) {
			FSI_TRACE(FSI_ERR,
				  "Failed to delete cache entry to cache ID = %d",
				  g_fsi_name_handle_cache_opened_files.
				  cacheMetaData.cacheTableID);
			ptfsal_print_handle(cacheEntry.key);
		}
	}
	return rc;
}

/* ------------------------------------------------------------------------- */
int ptfsal_chmod(const struct req_op_context *p_context,
		 struct fsal_export *export, const char *path, mode_t mode)
{
	ccl_context_t ccl_context;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	return CCL_CHMOD(&ccl_context, path, mode);
}

/* ------------------------------------------------------------------------- */
int ptfsal_chown(const struct req_op_context *p_context,
		 struct fsal_export *export, const char *path, uid_t uid,
		 gid_t gid)
{
	ccl_context_t ccl_context;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	return CCL_CHOWN(&ccl_context, path, uid, gid);
}

/* ------------------------------------------------------------------------- */
int ptfsal_ntimes(const struct req_op_context *p_context,
		  struct fsal_export *export, const char *filename,
		  uint64_t atime, uint64_t mtime)
{
	ccl_context_t ccl_context;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	/* not changing create time in NFS */
	return CCL_NTIMES(&ccl_context, filename, atime, mtime, 0);
}

/* ------------------------------------------------------------------------- */
int ptfsal_mkdir(struct pt_fsal_obj_handle *p_parent_directory_handle,
		 const char *p_dirname, const struct req_op_context *p_context,
		 mode_t mode, ptfsal_handle_t *p_object_handle)
{
	int rc;
	char fsi_parent_dir_name[PATH_MAX];
	char fsi_name[PATH_MAX];

	ptfsal_handle_t *p_fsi_parent_handle =
	    p_parent_directory_handle->handle;
	ptfsal_handle_t *p_fsi_handle = p_object_handle;
	ccl_context_t ccl_context;

	ptfsal_set_fsi_handle_data(p_context->fsal_export,
				   p_context, &ccl_context);

	/* build new entry path */
	rc = fsi_get_name_from_handle(p_context,
				      p_context->fsal_export,
				      p_parent_directory_handle->handle,
				      fsi_parent_dir_name, NULL);
	if (rc < 0) {
		FSI_TRACE(FSI_ERR, "Handle to name failed for hanlde %s",
			  p_fsi_parent_handle->data.handle.f_handle);
		return rc;
	}
	FSI_TRACE(FSI_DEBUG, "Parent dir name=%s\n", fsi_parent_dir_name);

	memset(fsi_name, 0, sizeof(fsi_name));
	fsi_get_whole_path(fsi_parent_dir_name, p_dirname, fsi_name);

	/*
	 * The dir name should not be empty "". In case it is empty, we
	 * return error.
	 */
	if (strnlen(fsi_name, 1) == 0) {
		FSI_TRACE(FSI_ERR, "The directory name is empty string.");
		return -1;
	}

	rc = CCL_MKDIR(&ccl_context, fsi_name, mode);

	if (rc == 0) {
		/* get handle */
		char fsal_path[PATH_MAX];
		memset(fsal_path, 0, PATH_MAX);
		memcpy(fsal_path, &fsi_name, PATH_MAX);

		ptfsal_name_to_handle(p_context,
				      p_context->fsal_export,
				      fsal_path, p_object_handle);
		fsi_cache_name_and_handle(p_context,
					  (char *)&p_fsi_handle->data.handle.
					  f_handle, fsi_name);
	}

	return rc;
}

/* ------------------------------------------------------------------------- */
int ptfsal_rmdir(const struct req_op_context *p_context,
		 struct pt_fsal_obj_handle *p_parent_directory_handle,
		 const char *p_object_name)
{
	int rc;
	ccl_context_t ccl_context;
	char fsi_parent_dir_name[PATH_MAX];
	char fsi_fullpath[PATH_MAX];

	rc = fsi_get_name_from_handle(p_context,
				      p_context->fsal_export,
				      p_parent_directory_handle->handle,
				      fsi_parent_dir_name, NULL);
	if (rc < 0) {
		FSI_TRACE(FSI_ERR, "Failed to get name from handle.");
		return rc;
	}
	fsi_get_whole_path(fsi_parent_dir_name, p_object_name, fsi_fullpath);
	FSI_TRACE(FSI_DEBUG, "Full path is %s", fsi_fullpath);

	ptfsal_set_fsi_handle_data(p_context->fsal_export,
				   p_context, &ccl_context);

	rc = CCL_RMDIR(&ccl_context, fsi_fullpath);
	fsi_remove_cache_by_fullpath(fsi_fullpath);
	return rc;
}

/* ------------------------------------------------------------------------- */
uint64_t ptfsal_read(struct pt_fsal_obj_handle *p_file_descriptor,
		     const struct req_op_context *opctx, char *buf,
		     size_t size, off_t offset, int in_handle)
{
	off_t cur_offset = offset;
	size_t cur_size = size;
	int split_count = 0;
	size_t buf_offset = 0;
	int rc;
	int read_amount = 0;
	int total_read = 0;

	ccl_context_t ccl_context;
	uint64_t max_readahead_offset = UINT64_MAX;

	ccl_context.handle_index = p_file_descriptor->u.file.fd;
	ccl_context.export_id = opctx->export->export_id;
	ccl_context.uid = opctx->creds->caller_uid;
	ccl_context.gid = opctx->creds->caller_gid;

	/* we will use 256K i/o with vtl but allow larger i/o from NFS */
	FSI_TRACE(FSI_DEBUG, "FSI - [%4d] xmp_read off %ld size %ld\n",
		  in_handle, offset, size);

	if (size > PTFSAL_USE_READSIZE_THRESHOLD) {
		/*
		 * this is an optimized linux mount
		 * probably 1M rsize
		 */
		max_readahead_offset = offset + size;
	}
	while (cur_size > 0) {
		FSI_TRACE(FSI_DEBUG, "FSI - [%4d] pread - split %d\n",
			  in_handle, split_count);

		if (cur_size >= READ_IO_BUFFER_SIZE)
			read_amount = READ_IO_BUFFER_SIZE;
		else
			read_amount = cur_size;

		rc = CCL_PREAD(&ccl_context, &buf[buf_offset], read_amount,
			       cur_offset, max_readahead_offset);

		if (rc == -1)
			return rc;
		cur_size -= read_amount;
		cur_offset += read_amount;
		buf_offset += read_amount;
		total_read += read_amount;
		split_count++;
	}

	return total_read;
}

/* ------------------------------------------------------------------------- */
uint64_t ptfsal_write(struct pt_fsal_obj_handle *p_file_descriptor,
		      const struct req_op_context *opctx, const char *buf,
		      size_t size, off_t offset, int in_handle)
{
	off_t cur_offset = offset;
	size_t cur_size = size;
	int split_count = 0;
	size_t buf_offset = 0;
	int bytes_written;
	int total_written = 0;
	int write_amount = 0;

	ccl_context_t ccl_context;

	ccl_context.handle_index = p_file_descriptor->u.file.fd;
	ccl_context.export_id = opctx->export->export_id;
	ccl_context.uid = opctx->creds->caller_uid;
	ccl_context.gid = opctx->creds->caller_gid;

	/* we will use 256K i/o with vtl but allow larger i/o from NFS */
	FSI_TRACE(FSI_DEBUG, "FSI - [%4d] xmp_write off %ld size %ld\n",
		  in_handle, offset, size);
	while (cur_size > 0) {
		FSI_TRACE(FSI_DEBUG, "FSI - [%4d] pwrite - split %d\n",
			  in_handle, split_count);

		if (cur_size >= WRITE_IO_BUFFER_SIZE)
			write_amount = WRITE_IO_BUFFER_SIZE;
		else
			write_amount = cur_size;

		bytes_written =
		    CCL_PWRITE(&ccl_context, in_handle, &buf[buf_offset],
			       write_amount, cur_offset);
		if (bytes_written < 0)
			return bytes_written;
		total_written += bytes_written;
		cur_size -= bytes_written;
		cur_offset += bytes_written;
		buf_offset += bytes_written;
		split_count++;
	}

	return total_written;
}

/* ------------------------------------------------------------------------- */
int ptfsal_dynamic_fsinfo(struct pt_fsal_obj_handle *p_filehandle,
			  const struct req_op_context *p_context,
			  fsal_dynamicfsinfo_t *p_dynamicinfo)
{
	int rc;
	char fsi_name[PATH_MAX];

	ccl_context_t ccl_context;
	struct CCLClientOpDynamicFsInfoRspMsg fs_info;

	rc = ptfsal_handle_to_name(p_filehandle->handle, p_context,
				   p_context->fsal_export, fsi_name);
	if (rc)
		return rc;

	FSI_TRACE(FSI_DEBUG, "Name = %s", fsi_name);

	ptfsal_set_fsi_handle_data(p_context->fsal_export, p_context,
				   &ccl_context);
	rc = CCL_DYNAMIC_FSINFO(&ccl_context, fsi_name, &fs_info);
	if (rc)
		return rc;

	p_dynamicinfo->total_bytes = fs_info.totalBytes;
	p_dynamicinfo->free_bytes = fs_info.freeBytes;
	p_dynamicinfo->avail_bytes = fs_info.availableBytes;

	p_dynamicinfo->total_files = fs_info.totalFiles;
	p_dynamicinfo->free_files = fs_info.freeFiles;
	p_dynamicinfo->avail_files = fs_info.availableFiles;

	p_dynamicinfo->time_delta.tv_sec = fs_info.time.tv_sec;
	p_dynamicinfo->time_delta.tv_nsec = fs_info.time.tv_nsec;

	return 0;
}

/* ------------------------------------------------------------------------- */
int ptfsal_readlink(ptfsal_handle_t *p_linkhandle, struct fsal_export *export,
		    const struct req_op_context *p_context, char *p_buf)
{
	int rc;
	char fsi_name[PATH_MAX];

	ccl_context_t ccl_context;
	struct CCLPersistentHandle pt_handler;
	ptfsal_handle_t *p_fsi_handle = p_linkhandle;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	memcpy(&pt_handler.handle, &p_fsi_handle->data.handle.f_handle,
	       sizeof(pt_handler.handle));

	FSI_TRACE(FSI_DEBUG, "Handle=%s", pt_handler.handle);

	memset(fsi_name, 0, sizeof(fsi_name));
	rc = ptfsal_handle_to_name(p_linkhandle, p_context, export, fsi_name);
	if (rc)
		return rc;

	rc = CCL_READLINK(&ccl_context, fsi_name, p_buf);
	return rc;
}

/* -------------------------------------------------------------------------- */
int ptfsal_symlink(struct pt_fsal_obj_handle *p_parent_directory_handle,
		   const char *p_linkname, const char *p_linkcontent,
		   const struct req_op_context *p_context, mode_t accessmode,
		   ptfsal_handle_t *p_link_handle)
{
	int rc;

	ccl_context_t ccl_context;
	char pt_path[PATH_MAX];

	ptfsal_set_fsi_handle_data(p_context->fsal_export,
				   p_context, &ccl_context);

	rc = CCL_SYMLINK(&ccl_context, p_linkname, p_linkcontent);
	if (rc)
		return rc;

	memset(pt_path, 0, PATH_MAX);
	memcpy(pt_path, p_linkname, PATH_MAX);

	rc = ptfsal_name_to_handle(p_context,
				   p_context->fsal_export,
				   pt_path, p_link_handle);

	return rc;
}

/* -------------------------------------------------------------------------- */
int ptfsal_name_to_handle(const struct req_op_context *p_context,
			  struct fsal_export *export, const char *p_fsalpath,
			  ptfsal_handle_t *p_handle)
{
	int rc;

	ccl_context_t ccl_context;
	struct CCLPersistentHandle pt_handler;
	ptfsal_handle_t *p_fsi_handle = p_handle;
	fsi_stat_struct fsi_stat;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	memset(&pt_handler, 0, sizeof(struct CCLPersistentHandle));
	rc = CCL_NAME_TO_HANDLE(&ccl_context, p_fsalpath, &pt_handler);
	if (rc) {
		FSI_TRACE(FSI_DEBUG, "CCL name to handle failed %d!", rc);
		return rc;
	}

	rc = ptfsal_stat_by_name(p_context, export, p_fsalpath, &fsi_stat);

	if (rc) {
		FSI_TRACE(FSI_DEBUG, "stat by name failed %d!", rc);
		return rc;
	}

	memset(p_handle->data.handle.f_handle, 0,
	       sizeof(p_handle->data.handle.f_handle));
	memcpy(&p_fsi_handle->data.handle.f_handle, &pt_handler.handle,
	       sizeof(pt_handler.handle));

	p_fsi_handle->data.handle.handle_size =
	    FSI_CCL_PERSISTENT_HANDLE_N_BYTES;
	p_fsi_handle->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
	p_fsi_handle->data.handle.handle_version = OPENHANDLE_VERSION;
	p_fsi_handle->data.handle.handle_type =
	    posix2fsal_type(fsi_stat.st_mode);

	FSI_TRACE(FSI_DEBUG, "Name to Handle:\n");
	ptfsal_print_handle(pt_handler.handle);
	ptfsal_print_handle(p_fsi_handle->data.handle.f_handle);
	return 0;
}

/* -------------------------------------------------------------------------- */
int ptfsal_handle_to_name(ptfsal_handle_t *p_filehandle,
			  const struct req_op_context *p_context,
			  struct fsal_export *export, char *path)
{
	int rc;
	ccl_context_t ccl_context;
	struct CCLPersistentHandle pt_handler;
	ptfsal_handle_t *p_fsi_handle = p_filehandle;

	ptfsal_set_fsi_handle_data(export, p_context, &ccl_context);

	memcpy(&pt_handler.handle, &p_fsi_handle->data.handle.f_handle,
	       sizeof(pt_handler.handle));
	ptfsal_print_handle(pt_handler.handle);

	rc = CCL_HANDLE_TO_NAME(&ccl_context, &pt_handler, path);

	return rc;
}

/* -------------------------------------------------------------------------- */
void ptfsal_print_handle(char *handle)
{
	uint64_t *handlePtr = (uint64_t *) handle;
	FSI_TRACE(FSI_DEBUG, "FSI - handle 0x%lx %lx %lx %lx", handlePtr[0],
		  handlePtr[1], handlePtr[2], handlePtr[3]);
}

/* -------------------------------------------------------------------------- */
int fsi_update_cache_stat(const char *p_filename, uint64_t newMode,
			  uint64_t export_id)
{
	return CCL_UPDATE_CACHE_STAT(p_filename, newMode, export_id);
}

/*
 * This function will convert Ganesha FSAL type to the upper
 * bits for unix stat structure
 */
mode_t fsal_type2unix(int fsal_type)
{
	mode_t outMode = 0;
	FSI_TRACE(FSI_DEBUG, "fsal_type: %d", fsal_type);
	switch (fsal_type) {
	case FIFO_FILE:
		outMode = S_IFIFO;
		break;
	case CHARACTER_FILE:
		outMode = S_IFCHR;
		break;
	case DIRECTORY:
		outMode = S_IFDIR;
		break;
	case BLOCK_FILE:
		outMode = S_IFBLK;
		break;
	case REGULAR_FILE:
		outMode = S_IFREG;
		break;
	case SYMBOLIC_LINK:
		outMode = S_IFLNK;
		break;
	case SOCKET_FILE:
		outMode = SOCKET_FILE;
		break;
	default:
		FSI_TRACE(FSI_ERR, "Unknown fsal type: %d", fsal_type);
	}

	return outMode;
}

void ptfsal_set_fsi_handle_data(struct fsal_export *exp_hdl,
				const struct req_op_context *p_context,
				ccl_context_t *ccl_context) {
	char *export_path = NULL;
	if (p_context != NULL)
		export_path = (char *)p_context->export->fullpath;
	ptfsal_set_fsi_handle_data_path(exp_hdl,
			p_context,
			export_path,
			ccl_context);
}


/* This function will fill in ccl_context_t */
void ptfsal_set_fsi_handle_data_path(struct fsal_export *exp_hdl,
				const struct req_op_context *p_context,
				char *export_path,
				ccl_context_t *ccl_context)
{
	unsigned char *bytes;
	struct pt_fsal_export *myself;
	myself = container_of(exp_hdl, struct pt_fsal_export, export);

	ccl_context->export_id = myself->pt_export_id;
	ccl_context->uid = 0;
	ccl_context->gid = 0;
	if (p_context != NULL)
		ccl_context->export_path = p_context->export->fullpath;
	else
		ccl_context->export_path = export_path;
	memset(ccl_context->client_address, 0,
	       sizeof(ccl_context->client_address));
	if (p_context == NULL) {
		bytes = NULL;
	} else {
		bytes = (unsigned char *)&((((struct sockaddr_in *)
					     (&(p_context->caller_addr)))->
					    sin_addr).s_addr);
	}
	if (bytes) {
		snprintf(ccl_context->client_address,
			 sizeof(ccl_context->client_address), "%u.%u.%u.%u",
			 bytes[0], bytes[1], bytes[2], bytes[3]);
	}
	FSI_TRACE(FSI_DEBUG,
		  "Export ID = %lu, uid = %lu, gid = %lu, Export Path = "
		  "%s, client ip = %s\n",
		  ccl_context->export_id,
		  ccl_context->uid,
		  ccl_context->gid,
		  ccl_context->export_path,
		  ccl_context->client_address);
}

/*
 * ----------------------------------------------------------------------------
 * Initialize the cache table and setup memory
 *
 * Return: FSI_IPC_OK = success
 *         otherwise  = failure
 */
int fsi_cache_table_init(CACHE_TABLE_T *cacheTableToInit,
			 CACHE_TABLE_INIT_PARAM *cacheTableInitParam)
{
	/* Validate the input parameters. */
	if ((cacheTableToInit == NULL) || (cacheTableInitParam == NULL)
	    || (cacheTableInitParam->keyLengthInBytes == 0)
	    || (cacheTableInitParam->dataSizeInBytes == 0)
	    || (cacheTableInitParam->maxNumOfCacheEntries == 0)
	    || (cacheTableInitParam->cacheKeyComprefn == NULL)
	    || (cacheTableInitParam->cacheTableID == 0)) {
		FSI_TRACE(FSI_ERR, "Failed to initialize ");
		return -1;
	}
	/* Populate the cache table meta data */
	memset(cacheTableToInit, 0x00, sizeof(CACHE_TABLE_T));
	cacheTableToInit->cacheEntries =
	    gsh_malloc(sizeof(CACHE_TABLE_ENTRY_T) *
		       cacheTableInitParam->maxNumOfCacheEntries);

	if (cacheTableToInit->cacheEntries == NULL) {
		FSI_TRACE(FSI_ERR,
			  "Unable to allocate memory for cache table"
			  " (cache id = %d", cacheTableInitParam->cacheTableID);
		return -1;
	}

	cacheTableToInit->cacheMetaData.keyLengthInBytes =
	    cacheTableInitParam->keyLengthInBytes;
	cacheTableToInit->cacheMetaData.dataSizeInBytes =
	    cacheTableInitParam->dataSizeInBytes;
	cacheTableToInit->cacheMetaData.maxNumOfCacheEntries =
	    cacheTableInitParam->maxNumOfCacheEntries;
	cacheTableToInit->cacheMetaData.cacheKeyComprefn =
	    cacheTableInitParam->cacheKeyComprefn;
	cacheTableToInit->cacheMetaData.cacheTableID =
	    cacheTableInitParam->cacheTableID;

	return FSI_CCL_IPC_EOK;
}
/*
 * ----------------------------------------------------------------------------
 * Compare two key entry and indicates the order of those two entries
 * This is intended for the use of binary search and binary insertion routine
 * used in this cache utilities
 *
 * Return:  1 if string1 >  string2
 *          0 if string1 == string2
 *         -1 if string1 <  string2
 *
 * Sample:
 *    int fsi_cache_keyCompare(const void *cacheEntry1, const void *cacheEntry2)
 *    {
 *      CACHE_TABLE_ENTRY_T *entry1 = (CACHE_TABLE_ENTRY_T *) cacheEntry1;
 *      CACHE_TABLE_ENTRY_T *entry2 = (CACHE_TABLE_ENTRY_T *) cacheEntry2;
 *      uint64_t num1 = *((uint64_t *) entry1->key);
 *      uint64_t num2 = *((uint64_t *) entry2->key);
 *
 *      if (num1 < num2)
 *        return -1;
 *      else if (num1 > num2)
 *        return 1;
 *      else
 *        return 0;
 *    }
 */
int fsi_cache_handle2name_keyCompare(const void *cacheEntry1,
				     const void *cacheEntry2)
{
	CACHE_TABLE_ENTRY_T *entry1 = (CACHE_TABLE_ENTRY_T *) cacheEntry1;
	CACHE_TABLE_ENTRY_T *entry2 = (CACHE_TABLE_ENTRY_T *) cacheEntry2;
	uint64_t *num1 = (uint64_t *) entry1->key;
	uint64_t *num2 = (uint64_t *) entry2->key;
	int i;

	FSI_TRACE(FSI_INFO, "Comparing two keys");
	ptfsal_print_handle(entry1->key);
	ptfsal_print_handle(entry2->key);

	for (i = 0; i < 4; i++) {
		if (num1[i] < num2[i]) {
			FSI_TRACE(FSI_INFO,
				  "Comparison exited at i=%d num1[0x%lx] < num2[0x%lx]",
				  i, num1[i], num2[i]);
			return -1;
		} else if (num1[i] > num2[i]) {
			FSI_TRACE(FSI_INFO,
				  "Comparison exited at i=%d num1[0x%lx] > num2[0x%lx]",
				  i, num1[i], num2[i]);
			return 1;
		}
	}

	FSI_TRACE(FSI_INFO, "All matched");
	return 0;
}

/*
 * ----------------------------------------------------------------------------
 * This routine will perform a binary search and identify where an cache should
 * be placed in the cache table such that the resulting will still remain in
 * proper sorted order.
 *
 * Return 0 if existing entry
 * Return 1 if insertion point found
 */
int fsi_cache_getInsertionPoint(CACHE_TABLE_T *cacheTable,
				CACHE_TABLE_ENTRY_T *whatToInsert,
				int *whereToInsert)
{
	int first, last, mid = 0;
	int found = 0;
	int compareRC = 0;
	*whereToInsert = 0;
	first = 0;
	last = cacheTable->cacheMetaData.numElementsOccupied - 1;

	while ((!found) && (first <= last)) {
		mid = first + (last - first) / 2;
		compareRC =
		    cacheTable->cacheMetaData.cacheKeyComprefn(whatToInsert,
							       &cacheTable->
							       cacheEntries
							       [mid]);
		FSI_TRACE(FSI_INFO,
			  "compareRC = %d, first %d mid %d, last %d\n",
			  compareRC, first, mid, last);
		if (compareRC == 0) {
			return 0;	/* existing entry */
		} else if (compareRC < 0) {
			*whereToInsert = mid;
			last = mid - 1;
		} else if (compareRC > 0) {
			*whereToInsert = mid + 1;
			first = mid + 1;
		}
	}
	return 1;
}

/*
 * ----------------------------------------------------------------------------
 * Insert and entry to the array at the correct location in order
 * to keep the correct order.
 *
 * Return: FSI_IPC_OK = success
 *         otherwise  = failure
 */
int fsi_cache_insertEntry(CACHE_TABLE_T *cacheTable,
			  CACHE_TABLE_ENTRY_T *whatToInsert)
{
	int rc;
	int whereToInsert;
	void *ptr;

	if (cacheTable == NULL) {
		FSI_TRACE(FSI_ERR, "param check");
		return -1;
	}
	/* Log result of the insert */
	FSI_TRACE(FSI_INFO, "Inserting the following handle:");
	ptfsal_print_handle(whatToInsert->key);

	if (cacheTable->cacheMetaData.numElementsOccupied ==
	    cacheTable->cacheMetaData.maxNumOfCacheEntries) {
		FSI_TRACE(FSI_ERR, "Cache table is full.  Cache ID = %d",
			  cacheTable->cacheMetaData.cacheTableID);
		return -1;
	}

	fsi_cache_handle2name_dumpTableKeys(FSI_INFO, cacheTable,
					    "Dumping cache table keys before insertion:");

	rc = fsi_cache_getInsertionPoint(cacheTable, whatToInsert,
					 &whereToInsert);

	if (rc == 0) {
		FSI_TRACE(FSI_INFO, "** Duplicated entry **");
		/* Log result of the insert */
		FSI_TRACE(FSI_INFO,
			  "Attempted to insert the following handle:");
		fsi_cache_32Bytes_rawDump(FSI_INFO, whatToInsert->key, 0);
		fsi_cache_handle2name_dumpTableKeys(FSI_INFO, cacheTable,
						    "Dumping cache table keys currently:");
		return -1;
	}
	/* Insert the element to the array */
	memmove(&cacheTable->cacheEntries[whereToInsert + 1],
		&cacheTable->cacheEntries[whereToInsert],
		(cacheTable->cacheMetaData.numElementsOccupied -
		 whereToInsert) * sizeof(CACHE_TABLE_ENTRY_T));

	ptr = gsh_malloc(cacheTable->cacheMetaData.keyLengthInBytes);
	if (ptr == NULL) {
		FSI_TRACE(FSI_ERR, "Failed allocate memory for inserting key");
		return -1;
	}
	cacheTable->cacheEntries[whereToInsert].key = ptr;

	ptr = gsh_malloc(cacheTable->cacheMetaData.dataSizeInBytes);
	if (ptr == NULL) {
		gsh_free(cacheTable->cacheEntries[whereToInsert].key);
		FSI_TRACE(FSI_ERR, "Failed allocate memory for inserting data");
		return -1;
	}
	cacheTable->cacheEntries[whereToInsert].data = ptr;

	memcpy(cacheTable->cacheEntries[whereToInsert].key, whatToInsert->key,
	       cacheTable->cacheMetaData.keyLengthInBytes);
	memcpy(cacheTable->cacheEntries[whereToInsert].data, whatToInsert->data,
	       cacheTable->cacheMetaData.dataSizeInBytes);

	cacheTable->cacheMetaData.numElementsOccupied++;

	fsi_cache_handle2name_dumpTableKeys(FSI_INFO, cacheTable,
					    "Dumping cache table keys after insertion:");
	return FSI_CCL_IPC_EOK;
}

/*
 * ----------------------------------------------------------------------------
 * Delete an entry in the array at the correct location in order
 * to keep the correct order.
 *
 * Return: FSI_IPC_OK = success
 *         otherwise  = failure
 */
int fsi_cache_deleteEntry(CACHE_TABLE_T *cacheTable,
			  CACHE_TABLE_ENTRY_T *whatToDelete)
{
	CACHE_TABLE_ENTRY_T *entryMatched = NULL;
	int whereToDeleteIdx = 0;

	/* Validate parameter */
	if ((cacheTable == NULL) || (whatToDelete == NULL)) {
		FSI_TRACE(FSI_ERR, "Param check");
		return -1;
	}
	/* Log result of the delete */
	FSI_TRACE(FSI_INFO, "Deleting the following handle:");
	ptfsal_print_handle(whatToDelete->key);

	if (cacheTable->cacheMetaData.numElementsOccupied <= 0) {
		FSI_TRACE(FSI_ERR, "Cache is empty.  Skipping delete entry.");
		return -1;
	}

	fsi_cache_handle2name_dumpTableKeys(FSI_INFO, cacheTable,
					    "Dumping cache table keys before deletion:");

	entryMatched =
	    bsearch(whatToDelete, &cacheTable->cacheEntries[0],
		    cacheTable->cacheMetaData.numElementsOccupied,
		    sizeof(CACHE_TABLE_ENTRY_T),
		    cacheTable->cacheMetaData.cacheKeyComprefn);

	if (entryMatched == NULL) {
		FSI_TRACE(FSI_INFO, "No match for delete");
		return -1;
	}

	whereToDeleteIdx = entryMatched - cacheTable->cacheEntries;
	FSI_TRACE(FSI_INFO, "whereToDeleteIdx = %d", whereToDeleteIdx);

	/* Now we have a match. Deleting the cache entry */

	/* Free the current entry and set the current entry pointers to NULL */
	gsh_free(cacheTable->cacheEntries[whereToDeleteIdx].key);
	gsh_free(cacheTable->cacheEntries[whereToDeleteIdx].data);
	cacheTable->cacheEntries[whereToDeleteIdx].key = NULL;
	cacheTable->cacheEntries[whereToDeleteIdx].data = NULL;

	/* If what we are deleting now is the not
	 * the last element in the cache table,
	 * we need to "shift" the cache entry up
	 * so that they are still continous
	 */
	if (whereToDeleteIdx !=
	    (cacheTable->cacheMetaData.numElementsOccupied - 1)) {
		memmove(&cacheTable->cacheEntries[whereToDeleteIdx],
			&cacheTable->cacheEntries[whereToDeleteIdx + 1],
			((cacheTable->cacheMetaData.numElementsOccupied -
			  whereToDeleteIdx) - 1) * sizeof(CACHE_TABLE_ENTRY_T));
		cacheTable->cacheEntries[cacheTable->cacheMetaData.
					 numElementsOccupied - 1].key = NULL;
		cacheTable->cacheEntries[cacheTable->cacheMetaData.
					 numElementsOccupied - 1].data = NULL;
	}
	cacheTable->cacheMetaData.numElementsOccupied--;

	fsi_cache_handle2name_dumpTableKeys(FSI_INFO, cacheTable,
					    "Dumping cache table keys after deletion:");
	return FSI_CCL_IPC_EOK;
}

/*
 * ----------------------------------------------------------------------------
 * Search and return an entry that matches the key pointed by *buffer.key
 * The matching data will be pointed by *buffer.data
 *
 * buffer (IN/OUT) = 'key' contains pointer to key to
 *                   search on in the cache table
 *                   'data' contains pointer to data retrieved
 *
 * Return: FSI_IPC_OK = success
 *         otherwise  = failure
 */
int fsi_cache_getEntry(CACHE_TABLE_T *cacheTable, CACHE_TABLE_ENTRY_T *buffer)
{
	CACHE_TABLE_ENTRY_T *entryMatched = NULL;
	int i;
	/* Validate parameter */
	if ((cacheTable == NULL) || (buffer == NULL)) {
		FSI_TRACE(FSI_ERR, "Param check");
		return -1;
	}

	FSI_TRACE(FSI_INFO, "Looking for the following handle:");
	ptfsal_print_handle(buffer->key);

	if (cacheTable->cacheMetaData.numElementsOccupied <= 0) {
		FSI_TRACE(FSI_INFO, "Cache is empty.");
		return -1;
	}

	FSI_TRACE(FSI_INFO, "Dumping current cache table keys:");
	for (i = 0; i < cacheTable->cacheMetaData.numElementsOccupied; i++)
		ptfsal_print_handle(cacheTable->cacheEntries[i].key);

	entryMatched =
	    bsearch(buffer, &cacheTable->cacheEntries[0],
		    cacheTable->cacheMetaData.numElementsOccupied,
		    sizeof(CACHE_TABLE_ENTRY_T),
		    cacheTable->cacheMetaData.cacheKeyComprefn);

	if (entryMatched == NULL) {
		FSI_TRACE(FSI_INFO, "No match for handle");
		return -1;
	}

	buffer->data = entryMatched->data;
	return FSI_CCL_IPC_EOK;
}

/*
 * ----------------------------------------------------------------------------
 * This function is used to dump first 32 bytes of data pointed by *data
 *
 * Input: data = data to be dumped.
 *        index = indicate the index of this particular piece of data within
 *                an whole array of similar data
 */
void fsi_cache_32Bytes_rawDump(fsi_ipc_trace_level loglevel, void *data,
			       int index)
{
	uint64_t *ptr = (uint64_t *) data;

	if (data != NULL) {
		ptr = (uint64_t *) data;
		FSI_TRACE(loglevel, "Data[%d] = 0x%lx %lx %lx %lx", index,
			  ptr[0], ptr[1], ptr[2], ptr[3]);
	}
}

/*
 * ----------------------------------------------------------------------------
 * This function is used to dump all keys in the
 * g_fsi_name_handle_cache_opened_files to the log.
 *
 * Input: cacheTable = cacheTable holding the keys to be printed.
 *        titleString = Description the purpose of this print.  It's for
 *                      logging purpose only. (NOTE: this can be NULL)
 */
void fsi_cache_handle2name_dumpTableKeys(fsi_ipc_trace_level logLevel,
					 CACHE_TABLE_T *cacheTable,
					 char *titleString)
{
#ifdef PRINT_CACHE_KEY
	int i;

	if (titleString != NULL)
		FSI_TRACE(logLevel, titleString);

	for (i = 0; i < cacheTable->cacheMetaData.numElementsOccupied; i++) {
		fsi_cache_32Bytes_rawDump(logLevel,
					  cacheTable->cacheEntries[i].key, i);
	}
#endif
}
