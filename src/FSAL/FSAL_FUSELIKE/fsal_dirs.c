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
#include "namespace.h"
#include <string.h>

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
fsal_status_t FUSEFSAL_opendir(fsal_handle_t * dir_hdl,  /* IN */
                               fsal_op_context_t * p_context,       /* IN */
                               fsal_dir_t * dir_desc,     /* OUT */
                               fsal_attrib_list_t * dir_attributes      /* [ IN/OUT ] */
    )
{
  int rc;
  char object_path[FSAL_MAX_PATH_LEN];
  fusefsal_dir_t * dir_descriptor = (fusefsal_dir_t *)dir_desc;
  fusefsal_handle_t * dir_handle = (fusefsal_handle_t *)dir_hdl;

  /* sanity checks
   * note : dir_attributes is optionnal.
   */
  if(!dir_handle || !p_context || !dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  /* get the full path for this directory */
  rc = NamespacePath(dir_handle->data.inode, dir_handle->data.device, dir_handle->data.validator,
                     object_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_opendir);

  memset(dir_descriptor, 0, sizeof(fsal_dir_t));

  /* set context for the next operation, so it can be retrieved by FS thread */
  fsal_set_thread_context(p_context);

  /* check opendir call */

  if(p_fs_ops->opendir)
    {
      TakeTokenFSCall();
      rc = p_fs_ops->opendir(object_path, &(dir_descriptor->dir_info));
      ReleaseTokenFSCall();

      if(rc)
        Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_opendir);
    }
  else
    {
      /* ignoring opendir */
      memset(&(dir_descriptor->dir_info), 0, sizeof(struct ganefuse_file_info));
    }

  /* fill the dir descriptor structure */
  dir_descriptor->dir_handle = *dir_handle;

  /* backup context */
  dir_descriptor->context = *(fusefsal_op_context_t *)p_context;

  /* optionaly get attributes */
  if(dir_attributes)
    {
      fsal_status_t status;

      status = FUSEFSAL_getattrs(dir_hdl, p_context, dir_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(dir_attributes->asked_attributes);
          FSAL_SET_MASK(dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_opendir);

}

typedef struct __fsal_dirbuff
{
  fsal_attrib_mask_t getattr_mask;
  fsal_count_t nb_entries;
  fsal_count_t max_entries;
  fsal_dirent_t *p_entries;
  fsal_status_t status;

/* for filesystems that do not support readdir offset */
  fsal_off_t begin_off;
  fsal_off_t curr_off;

} fsal_dirbuff_t;

#define INODE_TO_BE_COMPLETED   ((ino_t)(-1))

static void fill_dirent(fsal_dirent_t * to_be_filled,
                        fsal_attrib_mask_t getattr_mask,
                        const char *name, const struct stat *stbuf, off_t off)
{
  fsal_status_t status;
  struct stat tmp_statbuff;
  int err = FALSE;
  fusefsal_handle_t *fill_handle = (fusefsal_handle_t *) &to_be_filled->handle;

  if(stbuf)
    {
      if(stbuf->st_ino == 0)
        {
          LogDebug(COMPONENT_FSAL,
                   "WARNING in fill_dirent: Filesystem doesn't provide inode numbers !!!");
        }

      fill_handle->data.inode = stbuf->st_ino;
      fill_handle->data.device = stbuf->st_dev;
      FSAL_str2name(name, strlen(name) + 1, &(to_be_filled->name));
      ((fusefsal_cookie_t *) &to_be_filled->cookie)->data = off;

      /* local copy for non "const" calls */
      tmp_statbuff = *stbuf;

      /* set attributes */
      to_be_filled->attributes.asked_attributes = getattr_mask;
      status = posix2fsal_attributes(&tmp_statbuff, &to_be_filled->attributes);

      LogFullDebug(COMPONENT_FSAL,
           "getattr_mask = %X, recupere = %X, status=%d, inode=%llX.%llu, type=%d, posixmode=%#o, mode=%#o",
           getattr_mask, to_be_filled->attributes.asked_attributes, status.major,
           to_be_filled->attributes.fsid.major, to_be_filled->attributes.fileid,
           to_be_filled->attributes.type, tmp_statbuff.st_mode,
           to_be_filled->attributes.mode);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(to_be_filled->attributes.asked_attributes);
          /* set getattr error bit in attr mask */
          FSAL_SET_MASK(to_be_filled->attributes.asked_attributes, FSAL_ATTR_RDATTR_ERR);
          err = TRUE;
        }
    }

  /* if any error occured during conversion,
   * or if values seem to be inconsistent,
   * proceed a lookup afterward */

  if(!stbuf
     || (stbuf->st_ino == 0)
     || err
     || to_be_filled->attributes.type == (fsal_nodetype_t) - 1
     || to_be_filled->attributes.mode == 0 || to_be_filled->attributes.numlinks == 0)
    {
      FSAL_CLEAR_MASK(to_be_filled->attributes.asked_attributes);
      /* we only known entry name, we tag it for a later lookup.
       */
      fill_handle->data.inode = INODE_TO_BE_COMPLETED;
      FSAL_str2name(name, strlen(name) + 1, &(to_be_filled->name));
      ((fusefsal_cookie_t *) &to_be_filled->cookie)->data = off;
    }

}                               /* fill_dirent */

/* this function is used by FUSE dir reader to fill the output buffer */

static int ganefuse_fill_dir(void *buf, const char *name,
                             const struct stat *stbuf, off_t off)
{
  fsal_dirbuff_t *dirbuff = (fsal_dirbuff_t *) buf;
  fsal_dirent_t *tab;
  unsigned int i;

  if(!dirbuff)
    return 1;

  /* missing name parameter */
  if(!name)
    {
      dirbuff->status.major = ERR_FSAL_INVAL;
      dirbuff->status.minor = 0;
      return 1;
    }

  /* if this is . or .., ignore */
  if(!strcmp(name, ".") || !strcmp(name, ".."))
    return 0;

  /* full output buffer */
  if(dirbuff->nb_entries == dirbuff->max_entries)
    {
      /* should not have been called */
      dirbuff->status.major = ERR_FSAL_SERVERFAULT;
      dirbuff->status.minor = 0;
      return 1;
    }

  tab = dirbuff->p_entries;

  /* offset is provided */
  if(off)
    {
      i = dirbuff->nb_entries;
      fill_dirent(&(tab[i]), dirbuff->getattr_mask, name, stbuf, off);
    }
  else
    {
      /* no offset is provided, we must skip some entries */

      if(dirbuff->curr_off < dirbuff->begin_off)
        {
          /* skip entry and go to the next */
          dirbuff->curr_off++;
          return 0;
        }

      /* entry is to be added */
      dirbuff->curr_off++;

      i = dirbuff->nb_entries;
      fill_dirent(&(tab[i]), dirbuff->getattr_mask, name, stbuf, dirbuff->curr_off);
    }

  dirbuff->nb_entries++;

  if(dirbuff->nb_entries == dirbuff->max_entries)
    return 1;
  else
    return 0;
}

/* this function is used by filesystems binded to old version of FUSE
 * that use getdir() instead of readdir().
 */
static int ganefuse_dirfil_old(ganefuse_dirh_t h, const char *name, int type, ino_t ino)
{
  return ganefuse_fill_dir((void *)h, name, NULL, 0);
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
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */
fsal_status_t FUSEFSAL_readdir(fsal_dir_t * dir_desc,     /* IN */
                               fsal_cookie_t start_position,        /* IN */
                               fsal_attrib_mask_t get_attr_mask,        /* IN */
                               fsal_mdsize_t buffersize,        /* IN */
                               fsal_dirent_t * pdirent, /* OUT */
                               fsal_cookie_t * end_position,        /* OUT */
                               fsal_count_t * nb_entries,       /* OUT */
                               fsal_boolean_t * end_of_dir      /* OUT */
    )
{
  int rc;
  char dir_path[FSAL_MAX_PATH_LEN];
  fsal_dirbuff_t reqbuff;
  unsigned int i;
  fsal_status_t st;
  fusefsal_dir_t * dir_descriptor = (fusefsal_dir_t *)dir_desc;

  /* sanity checks */

  if(!dir_descriptor || !pdirent || !end_position || !nb_entries || !end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  /* get the full path for dir inode */
  rc = NamespacePath(dir_descriptor->dir_handle.data.inode,
                     dir_descriptor->dir_handle.data.device,
                     dir_descriptor->dir_handle.data.validator, dir_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_readdir);

  if(!p_fs_ops->readdir && !p_fs_ops->getdir)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_readdir);

  /* set context so it can be retrieved by FS */
  fsal_set_thread_context((fsal_op_context_t *) &dir_descriptor->context);

  /* prepare reaadir structure */

  reqbuff.getattr_mask = get_attr_mask;
  reqbuff.nb_entries = 0;
  reqbuff.max_entries = (buffersize / sizeof(fsal_dirent_t));
  reqbuff.p_entries = pdirent;
  reqbuff.status.major = 0;
  reqbuff.status.minor = 0;
  memcpy( (char *)&reqbuff.begin_off, start_position.data, sizeof( off_t ) ) ;
  reqbuff.curr_off = 0 ;

  TakeTokenFSCall();

  if(p_fs_ops->readdir)
    rc = p_fs_ops->readdir(dir_path, (void *)&reqbuff, ganefuse_fill_dir,
                           (off_t)start_position.data, &dir_descriptor->dir_info);
  else
    rc = p_fs_ops->getdir(dir_path, (ganefuse_dirh_t) & reqbuff, ganefuse_dirfil_old);

  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_readdir);
  else if(FSAL_IS_ERROR(reqbuff.status))
    Return(reqbuff.status.major, reqbuff.status.minor, INDEX_FSAL_readdir);

  /* if no entry found */

  if(reqbuff.nb_entries == 0)
    {
      *end_position = start_position;
      *end_of_dir = TRUE;
      *nb_entries = 0;

      LogFullDebug(COMPONENT_FSAL, "No entries found");

      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readdir);
    }

  /* at least 1 entry found */

  /* we must do some operations on the final dirent array:
   * - chaining entries together
   * - if the filesystem did not provide stat buffers,
   *   we must do lookup operations.
   * - adding dir entries to namespace
   */

  for(i = 0; i < reqbuff.nb_entries; i++)
    {
      /* 1) chaining entries together */

      if(i == reqbuff.nb_entries - 1)
        {                       /* last entry */
          pdirent[i].nextentry = NULL;
          *end_position = pdirent[i].cookie;
        }
      else
        pdirent[i].nextentry = &(pdirent[i + 1]);

      /* 2) check weither the filesystem provided stat buff */

      if(((fusefsal_handle_t *) &pdirent[i].handle)->data.inode == INODE_TO_BE_COMPLETED)
        {
          /* If not, make a lookup operation for this entry.
           * (this with automatically add it to namespace) */

          pdirent[i].attributes.asked_attributes = get_attr_mask;

          LogFullDebug(COMPONENT_FSAL, "Inode to be completed");

          st = FUSEFSAL_lookup((fsal_handle_t *) &dir_descriptor->dir_handle,
                               &pdirent[i].name,
                               (fsal_op_context_t *) &dir_descriptor->context,
                               &pdirent[i].handle, &pdirent[i].attributes);

          if(FSAL_IS_ERROR(st))
            Return(st.major, st.minor, INDEX_FSAL_readdir);
        }
      else
        {
          /* 3) just add entry to namespace except for '.' and '..'
           *    Also set a validator for this entry.
           */

          if(strcmp(pdirent[i].name.name, ".") && strcmp(pdirent[i].name.name, ".."))
            {
              LogFullDebug(COMPONENT_FSAL, "adding entry to namespace: %lX.%ld %s",
                     ((fusefsal_handle_t *) &pdirent[i].handle)->data.device,
                     ((fusefsal_handle_t *) &pdirent[i].handle)->data.inode, pdirent[i].name.name);

              ((fusefsal_handle_t *) &pdirent[i].handle)->data.validator = pdirent[i].attributes.ctime.seconds;

              NamespaceAdd(dir_descriptor->dir_handle.data.inode,
                           dir_descriptor->dir_handle.data.device,
                           dir_descriptor->dir_handle.data.validator,
                           pdirent[i].name.name,
                           ((fusefsal_handle_t *) &pdirent[i].handle)->data.inode,
                           ((fusefsal_handle_t *) &pdirent[i].handle)->data.device,
			   &(((fusefsal_handle_t *) &pdirent[i].handle)->data.validator));
            }
        }

    }

  /* end of dir was reached if not enough entries were provided */
  *end_of_dir = (reqbuff.nb_entries < reqbuff.max_entries);
  *nb_entries = reqbuff.nb_entries;

  LogFullDebug(COMPONENT_FSAL, "EOD = %d", *end_of_dir);

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
fsal_status_t FUSEFSAL_closedir(fsal_dir_t * dir_desc     /* IN */
    )
{

  int rc;
  char dir_path[FSAL_MAX_PATH_LEN];
  fusefsal_dir_t * dir_descriptor = (fusefsal_dir_t *)dir_desc;

  /* sanity checks */
  if(!dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

  /* get the full path for dir inode */
  rc = NamespacePath(dir_descriptor->dir_handle.data.inode,
                     dir_descriptor->dir_handle.data.device,
                     dir_descriptor->dir_handle.data.validator, dir_path);
  if(rc)
    Return(ERR_FSAL_STALE, rc, INDEX_FSAL_closedir);

  if(!p_fs_ops->releasedir)
    /* ignore this call */
    Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

  /* set context so it can be retrieved by FS */
  fsal_set_thread_context((fsal_op_context_t *) &dir_descriptor->context);

  /* release the resources used for reading directory */

  TakeTokenFSCall();

  rc = p_fs_ops->releasedir(dir_path, &dir_descriptor->dir_info);

  ReleaseTokenFSCall();

  if(rc)
    Return(fuse2fsal_error(rc, TRUE), rc, INDEX_FSAL_closedir);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

}
