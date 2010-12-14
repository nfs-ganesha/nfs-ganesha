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
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "stuff_alloc.h"
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "HPSSclapiExt/hpssclapiext.h"
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
fsal_status_t HPSSFSAL_opendir(hpssfsal_handle_t * dir_handle,  /* IN */
                               hpssfsal_op_context_t * p_context,       /* IN */
                               hpssfsal_dir_t * dir_descriptor, /* OUT */
                               fsal_attrib_list_t * dir_attributes      /* [ IN/OUT ] */
    )
{
  int rc;
  fsal_status_t st;

  /* sanity checks
   * note : dir_attributes is optionnal.
   */
  if(!dir_handle || !p_context || !dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  /* Test access rights for this directory
   * and retrieve asked attributes */

  st = HPSSFSAL_access(dir_handle, p_context, FSAL_R_OK, dir_attributes);

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_opendir);

  /* if everything is OK, fills the dir_desc structure */
  memcpy(&dir_descriptor->dir_handle, dir_handle, sizeof(hpssfsal_handle_t));
  memcpy(&dir_descriptor->context, p_context, sizeof(hpssfsal_op_context_t));

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
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument) 
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */

fsal_status_t HPSSFSAL_readdir(hpssfsal_dir_t * dir_descriptor, /* IN */
                               hpssfsal_cookie_t start_position,        /* IN */
                               fsal_attrib_mask_t get_attr_mask,        /* IN */
                               fsal_mdsize_t buffersize,        /* IN */
                               fsal_dirent_t * pdirent, /* OUT */
                               hpssfsal_cookie_t * end_position,        /* OUT */
                               fsal_count_t * nb_entries,       /* OUT */
                               fsal_boolean_t * end_of_dir      /* OUT */ )
{
  int rc, returned, i;
  fsal_status_t st;

  fsal_attrib_mask_t handle_attr_mask;
  fsal_count_t current_nb_entries, missing_entries, max_dir_entries;

  /* hpss_ReadRawAttrsHandle arguments. */

  u_signed64 curr_start_position;
  unsigned32 buff_size_in;
  unsigned32 bool_getattr_in;
  unsigned32 bool_eod_out;
  u_signed64 last_offset_out;
  //ns_DirEntry_t outbuff[FSAL_READDIR_SIZE];
  ns_DirEntry_t * outbuff = NULL ;

  /* sanity checks */

  if(!dir_descriptor || !pdirent || !end_position || !nb_entries || !end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  if( ( outbuff = ( ns_DirEntry_t * )Mem_Alloc( sizeof(  ns_DirEntry_t ) * FSAL_READDIR_SIZE ) ) == NULL )
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  /* handle provides : suppattr, type, fileid */
  /** @todo : does handle provide mounted_on_fileid ? */

  handle_attr_mask = FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE | FSAL_ATTR_FILEID;

  /* if the handle cannot provide the requested attributes,
   * we have to retrieve file attributes. */

  if(get_attr_mask & (~handle_attr_mask))
    bool_getattr_in = TRUE;
  else
    bool_getattr_in = FALSE;

  /* init values */

  curr_start_position = start_position.data;
  bool_eod_out = 0;
  current_nb_entries = 0;
  max_dir_entries = (buffersize / sizeof(fsal_dirent_t));

  /* while we haven't filled the output buffer
   * and the end of dir has not been reached :
   */
  while((current_nb_entries < max_dir_entries) && (!bool_eod_out))
    {

      missing_entries = max_dir_entries - current_nb_entries;

      /* If the requested count is smaller than the default FSAL_READDIR_SIZE,
       * we use a smaller output buffer.
       */
      if(missing_entries < FSAL_READDIR_SIZE)
        buff_size_in = missing_entries * sizeof(ns_DirEntry_t);
      else
        buff_size_in = FSAL_READDIR_SIZE * sizeof(ns_DirEntry_t);

      /* call to hpss clapi */

      TakeTokenFSCall();

      rc = HPSSFSAL_ReadRawAttrsHandle(&(dir_descriptor->dir_handle.data.ns_handle),
                                       curr_start_position,
                                       &dir_descriptor->context.credential.hpss_usercred,
                                       buff_size_in,
                                       bool_getattr_in,
                                       ReturnInconsistentDirent,
                                       &bool_eod_out, &last_offset_out, outbuff);

      ReleaseTokenFSCall();

      if(rc < 0)
       {
         Mem_Free( outbuff ) ;
         Return(hpss2fsal_error(rc), -rc, INDEX_FSAL_readdir);
       }
      else
        returned = rc;

      /* Fills the fsal dirent list. */

      for(i = 0; i < returned; i++)
        {

          pdirent[current_nb_entries].handle.data.ns_handle = outbuff[i].ObjHandle;

          pdirent[current_nb_entries].handle.data.obj_type =
              hpss2fsal_type(outbuff[i].ObjHandle.Type);

          st = FSAL_str2name((char *)outbuff[i].Name, HPSS_MAX_FILE_NAME,
                             &pdirent[current_nb_entries].name);

      /** @todo : test returned status */

          pdirent[current_nb_entries].cookie.data = outbuff[i].ObjOffset;

          /* set asked attributes */
          pdirent[current_nb_entries].attributes.asked_attributes = get_attr_mask;

          if(bool_getattr_in)
            {

              /* convert HPSS attributes to fsal attributes */
              st = hpss2fsal_attributes(&outbuff[i].ObjHandle,
                                        &outbuff[i].Attrs,
                                        &pdirent[current_nb_entries].attributes);

              /* on error, we set a special bit in the mask. */
              if(FSAL_IS_ERROR(st))
                {
                  FSAL_CLEAR_MASK(pdirent[current_nb_entries].attributes.
                                  asked_attributes);
                  FSAL_SET_MASK(pdirent[current_nb_entries].attributes.asked_attributes,
                                FSAL_ATTR_RDATTR_ERR);
                }

            }
          else if(get_attr_mask)
            {

              /* extract asked attributes from file handle */
              st = hpssHandle2fsalAttributes(&outbuff[i].ObjHandle,
                                             &pdirent[current_nb_entries].attributes);

              /* on error, we set a special bit in the mask. */
              if(FSAL_IS_ERROR(st))
                {
                  FSAL_CLEAR_MASK(pdirent[current_nb_entries].attributes.
                                  asked_attributes);
                  FSAL_SET_MASK(pdirent[current_nb_entries].attributes.asked_attributes,
                                FSAL_ATTR_RDATTR_ERR);
                }

            }

          /* set the previous' next */
          if(current_nb_entries)
            pdirent[current_nb_entries - 1].nextentry = &(pdirent[current_nb_entries]);

          /* current's next */
          pdirent[current_nb_entries].nextentry = NULL;

          /* increment entries count */
          current_nb_entries++;
          curr_start_position = last_offset_out;
        }

    }

  /* At this point, 2 cases :
   * - the requested count is reached
   * - the end of dir is reached.
   * However, the treatment is the same.
   */

  /* setting output vars. */

  /* if no item was read, the offset keeps the same. */
  end_position->data = (current_nb_entries == 0 ? start_position.data : last_offset_out);

  *nb_entries = current_nb_entries;
  *end_of_dir = (bool_eod_out ? TRUE : FALSE);

  Mem_Free( outbuff ) ;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readdir); /* @todo badly set fsal_log ? */
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
fsal_status_t HPSSFSAL_closedir(hpssfsal_dir_t * dir_descriptor /* IN */
    )
{

  int rc;

  /* sanity checks */
  if(!dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

  /* fill dir_descriptor with zeros */
  memset(dir_descriptor, 0, sizeof(hpssfsal_dir_t));

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

}
