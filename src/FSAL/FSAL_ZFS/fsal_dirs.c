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
fsal_status_t ZFSFSAL_opendir(zfsfsal_handle_t * dir_handle,  /* IN */
                           zfsfsal_op_context_t * p_context,       /* IN */
                           zfsfsal_dir_t * dir_descriptor, /* OUT */
                           fsal_attrib_list_t * dir_attributes  /* [ IN/OUT ] */
    )
{
  int rc;
  fsal_status_t st;

  /* sanity checks
   * note : dir_attributes is optionnal.
   */
  if(!dir_handle || !p_context || !dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  /* >> You can prepare your directory for beeing read  
   * and check that the user has the right for reading its content <<*/
  libzfswrap_vnode_t *p_vnode;
  if((rc = libzfswrap_opendir(p_context->export_context->p_vfs, &p_context->user_credential.cred,
                              dir_handle->data.zfs_handle, &p_vnode)))
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_create);

  dir_descriptor->p_vnode = p_vnode;
  dir_descriptor->zfs_handle = dir_handle->data.zfs_handle;
  dir_descriptor->p_vfs = p_context->export_context->p_vfs;
  dir_descriptor->cred = p_context->user_credential.cred;

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
fsal_status_t ZFSFSAL_readdir(zfsfsal_dir_t * dir_descriptor, /* IN */
                           zfsfsal_cookie_t start_position,        /* IN */
                           fsal_attrib_mask_t get_attr_mask,    /* IN */
                           fsal_mdsize_t buffersize,    /* IN */
                           fsal_dirent_t * p_dirent,     /* OUT */
                           zfsfsal_cookie_t * end_position,        /* OUT */
                           fsal_count_t * nb_entries,   /* OUT */
                           fsal_boolean_t * end_of_dir  /* OUT */
    )
{
  int rc;
  fsal_count_t max_dir_entries;
  /* sanity checks */

  if(!dir_descriptor || !p_dirent || !end_position || !nb_entries || !end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  max_dir_entries = buffersize / sizeof(fsal_dirent_t);
  libzfswrap_entry_t *entries = malloc( max_dir_entries * sizeof(libzfswrap_entry_t));

  TakeTokenFSCall();

  /* >> read some entry from you filesystem << */
  rc = libzfswrap_readdir(dir_descriptor->p_vfs, &dir_descriptor->cred,
                          dir_descriptor->p_vnode, entries, max_dir_entries, (off_t*)(&start_position));

  ReleaseTokenFSCall();

  /* >> convert error code and return on error << */
  if(rc)
  {
    free(entries);
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_create);
  }

  /* >> fill the output dirent array << */

  *nb_entries = 0;
  int index = 0;
  while(index < max_dir_entries)
  {
    /* If psz_filename is NULL, that's the end of the list */
    if(entries[index].psz_filename[0] == '\0')
      break;

    /* Skip '.' and '..' */
    if(!strcmp(entries[index].psz_filename, ".") || !strcmp(entries[index].psz_filename, ".."))
    {
      index++;
      continue;
    }

    p_dirent[*nb_entries].handle.data.zfs_handle = entries[index].object;
    p_dirent[*nb_entries].handle.data.type = posix2fsal_type(entries[index].type);
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
fsal_status_t ZFSFSAL_closedir(zfsfsal_dir_t * dir_descriptor /* IN */
    )
{

  int rc;

  /* sanity checks */
  if(!dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

  /* >> release the resources used for reading your directory << */
  if((rc = libzfswrap_closedir(dir_descriptor->p_vfs, &dir_descriptor->cred, dir_descriptor->p_vnode)))
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_create);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

}
