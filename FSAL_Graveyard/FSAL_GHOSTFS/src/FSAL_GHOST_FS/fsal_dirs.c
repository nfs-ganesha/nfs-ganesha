/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_dirs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/03/04 08:37:28 $
 * \version $Revision: 1.5 $
 * \brief   Directory browsing operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convertions.h"
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
 */
fsal_status_t FSAL_opendir(fsal_handle_t * dir_handle,  /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           fsal_dir_t * dir_descriptor, /* OUT */
                           fsal_attrib_list_t * dir_attributes  /* [ IN/OUT ] */
    )
{
  int rc;
  dir_descriptor_t dir;

  /* For logging */
  SetFuncID(INDEX_FSAL_opendir);

  /* sanity checks
   * note : dir_attributes is optionnal.
   */
  if(!dir_handle || !p_context || !dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  /* test access perms. For other FS than GHOST_FS,
   * this is done by the FS itself.
   */
  rc = GHOSTFS_Access((GHOSTFS_handle_t) (*dir_handle),
                      GHOSTFS_TEST_READ,
                      p_context->credential.user, p_context->credential.group);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_opendir);

  rc = GHOSTFS_Opendir((GHOSTFS_handle_t) (*dir_handle), &dir);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_opendir);

  /* building dir descriptor */
  dir_descriptor->dir_descriptor = dir;
  dir_descriptor->context = *p_context;

  /* optionaly gets the directory attributes.
   * If an error occures during getattr operation,
   * it is returned, even though the opendir operation succeeded.
   */
  if(dir_attributes)
    {

      fsal_status_t status;

      switch ((status = FSAL_getattrs(dir_handle, p_context, dir_attributes)).major)
        {
          /* change the FAULT error to appears as an internal error.
           * indeed, parameters should be null. */
        case ERR_FSAL_FAULT:
          Return(ERR_FSAL_SERVERFAULT, ERR_FSAL_FAULT, INDEX_FSAL_opendir);
          break;
        case ERR_FSAL_NO_ERROR:
          /* continue */
          break;
        default:
          Return(status.major, status.minor, INDEX_FSAL_opendir);
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_opendir);

}

fsal_status_t FSAL_readdir(fsal_dir_t * dir_descriptor, /* IN */
                           fsal_cookie_t start_position,        /* IN */
                           fsal_attrib_mask_t get_attr_mask,    /* IN */
                           fsal_mdsize_t buffersize,    /* IN */
                           fsal_dirent_t * pdirent,     /* OUT */
                           fsal_cookie_t * end_position,        /* OUT */
                           fsal_count_t * nb_entries,   /* OUT */
                           fsal_boolean_t * end_of_dir  /* OUT */
    )
{
  int rc;
  unsigned int max_entries;
  fsal_dirent_t *curr_ent;
  fsal_dirent_t *last_ent;
  GHOSTFS_cookie_t last_cookie;
  GHOSTFS_dirent_t entry;
  fsal_status_t status;

  /* For logging */
  SetFuncID(INDEX_FSAL_readdir);

  /* sanity checks */
  if(!dir_descriptor || !pdirent || !end_position || !nb_entries || !end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  /* seeking directory position  */
  rc = GHOSTFS_Seekdir(&(dir_descriptor->dir_descriptor), start_position.cookie);

  if(rc)
    Return(ghost2fsal_error(rc), rc, INDEX_FSAL_readdir);

  /* how many entries can we get ? */
  if(buffersize < sizeof(fsal_dirent_t))
    Return(ERR_FSAL_TOOSMALL, 0, INDEX_FSAL_readdir);
  max_entries = buffersize / sizeof(fsal_dirent_t);

  /* initialize output values */
  *nb_entries = 0;
  *end_of_dir = FALSE;
  curr_ent = pdirent;
  last_ent = NULL;
  last_cookie = start_position.cookie;

  /* retrieving entries */

  while(*nb_entries < max_entries)
    {

      /* processing a readdir */
      rc = GHOSTFS_Readdir(&(dir_descriptor->dir_descriptor), &entry);

      if(rc == ERR_GHOSTFS_ENDOFDIR)
        {
          /* updates ouputs and return */
          if(last_ent)
            last_ent->nextentry = NULL;
          (*end_of_dir) = TRUE;
          end_position->cookie = last_cookie;
          Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readdir);
        }
      else if(rc != 0)
        {
          Return(ghost2fsal_error(rc), rc, INDEX_FSAL_readdir);
        }

      /* we have just read an entry */
      if(last_ent)
        last_ent->nextentry = curr_ent;
      curr_ent->handle = (fsal_handle_t) entry.handle;
      strncpy(curr_ent->name.name, entry.name, FSAL_MAX_NAME_LEN);
      curr_ent->name.len = strlen(curr_ent->name.name);
      curr_ent->cookie.cookie = entry.cookie;

      /* getting attributes
       * If an error occures during getattr operation,
       * it is returned, even though the readdir operation succeeded.
       */

      curr_ent->attributes.asked_attributes = get_attr_mask;
      switch ((status = FSAL_getattrs(&curr_ent->handle,
                                      &dir_descriptor->context,
                                      &curr_ent->attributes)).major)
        {
          /* change the FAULT error to appears as an internal error.
           * indeed, parameters should not be null. */
        case ERR_FSAL_FAULT:
          Return(ERR_FSAL_SERVERFAULT, ERR_FSAL_FAULT, INDEX_FSAL_readdir);
          break;
        case ERR_FSAL_NO_ERROR:
          /* ok, continue */
          break;
        default:
          Return(status.major, status.minor, INDEX_FSAL_readdir);
        }

      last_cookie = entry.cookie;       /* the cookie for the current entry */
      last_ent = curr_ent;      /* remembers last entry */
      curr_ent++;               /* the next entry to be filled */
      (*nb_entries)++;          /* numbers of entries we've read */

    }
  /* update outputs and return */
  end_position->cookie = last_cookie;
  if(last_ent)
    last_ent->nextentry = NULL;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readdir);

}

fsal_status_t FSAL_closedir(fsal_dir_t * dir_descriptor /* IN */
    )
{

  int rc;

  /* For logging */
  SetFuncID(INDEX_FSAL_closedir);

  /* sanity checks */
  if(!dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

  /* calling GHOSTFS closedir */
  rc = GHOSTFS_Closedir(&(dir_descriptor->dir_descriptor));
  Return(ghost2fsal_error(rc), rc, INDEX_FSAL_readdir);

}
