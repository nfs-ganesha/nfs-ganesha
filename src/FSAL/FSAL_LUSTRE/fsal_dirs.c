/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
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
fsal_status_t LUSTREFSAL_opendir(fsal_handle_t * p_dir_handle,    /* IN */
                                 fsal_op_context_t * p_context,   /* IN */
                                 fsal_dir_t *dir_desc,   /* OUT */
                                 fsal_attrib_list_t * p_dir_attributes  /* [ IN/OUT ] */
    )
{
  int rc;
  fsal_status_t status;

  fsal_path_t fsalpath;
  struct stat buffstat;
  lustrefsal_dir_t *p_dir_descriptor = (lustrefsal_dir_t *)dir_desc;

  /* sanity checks
   * note : dir_attributes is optionnal.
   */
  if(!p_dir_handle || !p_context || !p_dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  /* get the path of the directory */
  status = fsal_internal_Handle2FidPath(p_context, p_dir_handle, &fsalpath);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_opendir);

  /* get directory metadata */
  TakeTokenFSCall();
  rc = lstat(fsalpath.path, &buffstat);
  ReleaseTokenFSCall();

  if(rc != 0)
    {
      rc = errno;
      if(rc == ENOENT)
        Return(ERR_FSAL_STALE, rc, INDEX_FSAL_opendir);
      else
        Return(posix2fsal_error(rc), rc, INDEX_FSAL_opendir);
    }

  /* Test access rights for this directory */
  status = fsal_internal_testAccess(p_context, FSAL_R_OK, &buffstat, NULL);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_opendir);

  /* if everything is OK, fills the dir_desc structure : */

  TakeTokenFSCall();
  p_dir_descriptor->p_dir = opendir(fsalpath.path);
  ReleaseTokenFSCall();
  if(!p_dir_descriptor->p_dir)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_opendir);

  memcpy(&(p_dir_descriptor->context), p_context, sizeof(lustrefsal_op_context_t));
  memcpy(&(p_dir_descriptor->path), &fsalpath, sizeof(fsal_path_t));
  memcpy(&(p_dir_descriptor->handle), p_dir_handle, sizeof(lustrefsal_handle_t));

  if(p_dir_attributes)
    {
      status = posix2fsal_attributes(&buffstat, p_dir_attributes);
      if(FSAL_IS_ERROR(status))
        {
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
fsal_status_t LUSTREFSAL_readdir(fsal_dir_t *dir_desc,   /* IN */
                                 fsal_cookie_t start_pos,    /* IN */
                                 fsal_attrib_mask_t get_attr_mask,      /* IN */
                                 fsal_mdsize_t buffersize,      /* IN */
                                 fsal_dirent_t * p_pdirent,     /* OUT */
                                 fsal_cookie_t * p_end_position,  /* OUT */
                                 fsal_count_t * p_nb_entries,   /* OUT */
                                 fsal_boolean_t * p_end_of_dir  /* OUT */
    )
{
  fsal_status_t st;
  fsal_count_t max_dir_entries;
  struct dirent *dp;
  struct dirent dpe;
  fsal_path_t fsalpath;
  int rc;
  lustrefsal_dir_t * p_dir_descriptor = (lustrefsal_dir_t *)dir_desc;
  lustrefsal_cookie_t start_position;

  /*****************/
  /* sanity checks */
  /*****************/

  if(!p_dir_descriptor || !p_pdirent || !p_end_position || !p_nb_entries || !p_end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  max_dir_entries = (buffersize / sizeof(fsal_dirent_t));
  memcpy( (char *)& start_position.data.cookie, start_pos.data, sizeof( off_t ) ) ;

  /***************************/
  /* seek into the directory */
  /***************************/
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
    Return(posix2fsal_error(rc), rc, INDEX_FSAL_readdir);

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
          rc = errno;
          Return(posix2fsal_error(rc), rc, INDEX_FSAL_readdir);
        }
      /* End of directory */
      if(!dp)
        {
          *p_end_of_dir = 1;
          break;
        }

    /***********************************/
      /* Get information about the entry */
    /***********************************/

      /* skip . and .. */
      if(!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
        continue;

      /* build the full path of the file into "fsalpath */
      if(FSAL_IS_ERROR
         (st =
          FSAL_str2name(dp->d_name, FSAL_MAX_NAME_LEN, &(p_pdirent[*p_nb_entries].name))))
        ReturnStatus(st, INDEX_FSAL_readdir);

      memcpy(&fsalpath, &(p_dir_descriptor->path), sizeof(fsal_path_t));
      st = fsal_internal_appendNameToPath(&fsalpath, &(p_pdirent[*p_nb_entries].name));
      if(FSAL_IS_ERROR(st))
        ReturnStatus(st, INDEX_FSAL_readdir);

      /* get object handle */
      TakeTokenFSCall();
      st = fsal_internal_Path2Handle((fsal_op_context_t *) &p_dir_descriptor->context, &fsalpath,
                                     &(p_pdirent[*p_nb_entries].handle));
      ReleaseTokenFSCall();

      if(FSAL_IS_ERROR(st))
        ReturnStatus(st, INDEX_FSAL_readdir);

    /************************
     * Fills the attributes *
     ************************/
      p_pdirent[*p_nb_entries].attributes.asked_attributes = get_attr_mask;

      st = LUSTREFSAL_getattrs(&(p_pdirent[*p_nb_entries].handle),
                               (fsal_op_context_t *) &p_dir_descriptor->context,
                               &p_pdirent[*p_nb_entries].attributes);
      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(p_pdirent[*p_nb_entries].attributes.asked_attributes);
          FSAL_SET_MASK(p_pdirent[*p_nb_entries].attributes.asked_attributes,
                        FSAL_ATTR_RDATTR_ERR);
        }

      ((lustrefsal_cookie_t *) (&p_pdirent[*p_nb_entries].cookie))->data.cookie = telldir(p_dir_descriptor->p_dir);
      p_pdirent[*p_nb_entries].nextentry = NULL;
      if(*p_nb_entries)
        p_pdirent[*p_nb_entries - 1].nextentry = &(p_pdirent[*p_nb_entries]);

      memcpy((char *)p_end_position, (char *)&p_pdirent[*p_nb_entries].cookie,
	     sizeof(lustrefsal_cookie_t));
      (*p_nb_entries)++;
    }

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
fsal_status_t LUSTREFSAL_closedir(fsal_dir_t * p_dir_descriptor   /* IN */
    )
{

  int rc;

  /* sanity checks */
  if(!p_dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

#ifdef _USE_POSIXDB_READDIR_BLOCK
  if(((lustrefsal_dir_t *)p_dir_descriptor)->p_dbentries)
    Mem_Free(p_dir_descriptor->p_dbentries);
#endif

  rc = closedir(((lustrefsal_dir_t *)p_dir_descriptor)->p_dir);
  if(rc != 0)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_closedir);

  /* fill dir_descriptor with zeros */
  memset(p_dir_descriptor, 0, sizeof(lustrefsal_dir_t));

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

}
