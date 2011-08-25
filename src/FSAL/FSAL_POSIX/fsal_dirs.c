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
#include "stuff_alloc.h"
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
fsal_status_t POSIXFSAL_opendir(fsal_handle_t * dir_handle,      /* IN */
                                fsal_op_context_t * context,     /* IN */
                                fsal_dir_t * dir_descriptor,     /* OUT */
                                fsal_attrib_list_t * p_dir_attributes   /* [ IN/OUT ] */
    )
{
  posixfsal_handle_t * p_dir_handle = (posixfsal_handle_t *) dir_handle;
  posixfsal_op_context_t * p_context = (posixfsal_op_context_t *) context;
  posixfsal_dir_t * p_dir_descriptor = (posixfsal_dir_t *) dir_descriptor;
  int rc, errsv;
  fsal_status_t status;

  fsal_path_t fsalpath;
  struct stat buffstat;

  /* sanity checks
   * note : dir_attributes is optionnal.
   */
  if(!p_dir_handle || !p_context || !p_dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  /* get the path of the directory */
  status =
      fsal_internal_getPathFromHandle(p_context, p_dir_handle, 1, &fsalpath, &buffstat);
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_opendir);

  /* Test access rights for this directory */

  status = fsal_internal_testAccess(p_context, FSAL_R_OK, &buffstat, NULL);
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_opendir);

  /* if everything is OK, fills the dir_desc structure : */

  TakeTokenFSCall();
  p_dir_descriptor->p_dir = opendir(fsalpath.path);
  ReleaseTokenFSCall();
  if(!p_dir_descriptor->p_dir)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_opendir);

  memcpy(&(p_dir_descriptor->context), p_context, sizeof(posixfsal_op_context_t));
  memcpy(&(p_dir_descriptor->path), &fsalpath, sizeof(fsal_path_t));
  memcpy(&(p_dir_descriptor->handle), p_dir_handle, sizeof(posixfsal_handle_t));

#ifdef _USE_POSIXDB_READDIR_BLOCK
  p_dir_descriptor->p_dbentries = NULL;
  p_dir_descriptor->dbentries_count = 0;
  /* fill the p_dbentries list */
  statusdb = fsal_posixdb_getChildren(p_dir_descriptor->context.p_conn,
                                      &(p_dir_descriptor->handle),
                                      FSAL_POSIXDB_MAXREADDIRBLOCKSIZE,
                                      &(p_dir_descriptor->p_dbentries),
                                      &(p_dir_descriptor->dbentries_count));
  if(FSAL_POSIXDB_IS_ERROR(statusdb))   /* too many entries in the directory, or another error */
    p_dir_descriptor->dbentries_count = -1;

#endif
  if(p_dir_attributes)
    {

      TakeTokenFSCall();
      rc = lstat(fsalpath.path, &buffstat);
      errsv = errno;
      ReleaseTokenFSCall();
      if(rc)                    /* lstat failed */
        goto attr_err;

      status = posix2fsal_attributes(&buffstat, p_dir_attributes);
      if(FSAL_IS_ERROR(status))
        {
 attr_err:
          FSAL_CLEAR_MASK(p_dir_attributes->asked_attributes);
          FSAL_SET_MASK(p_dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

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
fsal_status_t POSIXFSAL_readdir(fsal_dir_t * dir_descriptor,     /* IN */
                                fsal_cookie_t start_pos,      /* IN */
                                fsal_attrib_mask_t get_attr_mask,       /* IN */
                                fsal_mdsize_t buffersize,       /* IN */
                                fsal_dirent_t * p_pdirent,      /* OUT */
                                fsal_cookie_t * end_position,    /* OUT */
                                fsal_count_t * p_nb_entries,    /* OUT */
                                fsal_boolean_t * p_end_of_dir   /* OUT */
    )
{
  posixfsal_dir_t * p_dir_descriptor = (posixfsal_dir_t *) dir_descriptor;
  posixfsal_cookie_t start_position, telldir_pos;
  posixfsal_cookie_t * p_end_position = (posixfsal_cookie_t *) end_position;
  fsal_status_t st;
  fsal_posixdb_status_t stdb;
  fsal_count_t max_dir_entries;
  struct dirent *dp;
  struct dirent dpe;
  struct stat buffstat;
  fsal_path_t fsalpath;
  fsal_posixdb_fileinfo_t infofs;
  int rc;

  /*****************/
  /* sanity checks */
  /*****************/

  if(!p_dir_descriptor || !p_pdirent || !p_end_position || !p_nb_entries || !p_end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  max_dir_entries = (buffersize / sizeof(fsal_dirent_t));

  /***************************/
  /* seek into the directory */
  /***************************/
  start_position.data.cookie = (off_t) start_pos.data;
  errno = 0;
  if(start_position.data.cookie == 0)
    {
      rewinddir(p_dir_descriptor->p_dir);
      rc = errno;
    }
  else
    {
      seekdir(p_dir_descriptor->p_dir, start_position.data.cookie);
      rc = errno;
    }

  if(rc)
    {
      st.major = posix2fsal_error(rc);
      st.minor = rc;
      goto readdir_error;
    }

  /************************/
  /* browse the directory */
  /************************/

  *p_nb_entries = 0;
  while(*p_nb_entries < max_dir_entries)
    {
    /***********************/
      /* read the next entry */
    /***********************/
      TakeTokenFSCall();
      rc = readdir_r(p_dir_descriptor->p_dir, &dpe, &dp);
      ReleaseTokenFSCall();
      if(rc)
        {
          st.major = posix2fsal_error(errno);
          st.minor = errno;
          goto readdir_error;
        }                       /* End of directory */
      if(!dp)
        {
          *p_end_of_dir = 1;
          break;
        }

    /***********************************/
      /* Get information about the entry */
    /***********************************/

      /* build the full path of the file into "fsalpath */
      if(FSAL_IS_ERROR
         (st =
          FSAL_str2name(dp->d_name, FSAL_MAX_NAME_LEN, &(p_pdirent[*p_nb_entries].name))))
        goto readdir_error;
      memcpy(&fsalpath, &(p_dir_descriptor->path), sizeof(fsal_path_t));
      st = fsal_internal_appendFSALNameToFSALPath(&fsalpath,
                                                  &(p_pdirent[*p_nb_entries].name));
      if(FSAL_IS_ERROR(st))
        goto readdir_error;

      /* get object info */
      TakeTokenFSCall();
      rc = lstat(fsalpath.path, &buffstat);
      ReleaseTokenFSCall();
      if(rc)
        {
          st.major = posix2fsal_error(errno);
          st.minor = errno;
          goto readdir_error;
        }
      if(FSAL_IS_ERROR(st = fsal_internal_posix2posixdb_fileinfo(&buffstat, &infofs)))
        goto readdir_error;

    /********************/
      /* fills the handle */
    /********************/

      /* check for "." and ".." */
      if(!strcmp(dp->d_name, "."))
        {
          memcpy(&(p_pdirent[*p_nb_entries].handle), &(p_dir_descriptor->handle),
                 sizeof(posixfsal_handle_t));
        }
      else if(!strcmp(dp->d_name, ".."))
        {
          stdb = fsal_posixdb_getParentDirHandle(p_dir_descriptor->context.p_conn,
                                                 &(p_dir_descriptor->handle),
                                                 (posixfsal_handle_t *) &(p_pdirent[*p_nb_entries].handle));
          if(FSAL_POSIXDB_IS_ERROR(stdb) && FSAL_IS_ERROR(st = posixdb2fsal_error(stdb)))
            goto readdir_error;
        }
      else
        {
#ifdef _USE_POSIXDB_READDIR_BLOCK
          if(p_dir_descriptor->dbentries_count > -1)
            {                   /* look for the entry in dbentries */
              st = fsal_internal_getInfoFromChildrenList(&(p_dir_descriptor->context),
                                                         &(p_dir_descriptor->handle),
                                                         &(p_pdirent[*p_nb_entries].name),
                                                         &infofs,
                                                         p_dir_descriptor->p_dbentries,
                                                         p_dir_descriptor->
                                                         dbentries_count,
                                                         &(p_pdirent[*p_nb_entries].
                                                           handle));
            }
          else
#endif
            {                   /* get handle for the entry */
              st = fsal_internal_getInfoFromName(&(p_dir_descriptor->context),
                                                 &(p_dir_descriptor->handle),
                                                 &(p_pdirent[*p_nb_entries].name),
                                                 &infofs,
                                                 (posixfsal_handle_t *) &(p_pdirent[*p_nb_entries].handle));
            }
          if(FSAL_IS_ERROR(st))
            goto readdir_error;
        }                       /* end of name check for "." and ".." */

    /************************
     * Fills the attributes *
     ************************/
      p_pdirent[*p_nb_entries].attributes.asked_attributes = get_attr_mask;
      st = posix2fsal_attributes(&buffstat, &(p_pdirent[*p_nb_entries].attributes));
      if(FSAL_IS_ERROR(st))
        goto readdir_error;

      /*
       * 
       **/
      telldir_pos.data.cookie = telldir(p_dir_descriptor->p_dir);
      memcpy(&p_pdirent[*p_nb_entries].cookie, &telldir_pos, sizeof(fsal_cookie_t));
      p_pdirent[*p_nb_entries].nextentry = NULL;
      if(*p_nb_entries)
        p_pdirent[*p_nb_entries - 1].nextentry = &(p_pdirent[*p_nb_entries]);

      memcpy(p_end_position, &p_pdirent[*p_nb_entries].cookie, sizeof(fsal_cookie_t));
      (*p_nb_entries)++;
    };

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readdir);

  /* return error, and free what must be freed */
 readdir_error:
  Return(st.major, st.minor, INDEX_FSAL_readdir);
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
fsal_status_t POSIXFSAL_closedir(fsal_dir_t * dir_descriptor     /* IN */  )
{
  posixfsal_dir_t * p_dir_descriptor = (posixfsal_dir_t *) dir_descriptor;
  int rc;

  /* sanity checks */
  if(!p_dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

#ifdef _USE_POSIXDB_READDIR_BLOCK
  if(p_dir_descriptor->p_dbentries)
    Mem_Free(p_dir_descriptor->p_dbentries);
#endif

  rc = closedir(p_dir_descriptor->p_dir);
  if(rc != 0)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_closedir);

  /* fill dir_descriptor with zeros */
  memset(p_dir_descriptor, 0, sizeof(posixfsal_dir_t));

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

}
