/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Adam C. Emerson <aemerson@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * -------------
 */

/**
 *
 * \file    fsal_dirs.c
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
 * \param exthandle (input)
 *         the handle of the directory to be opened.
 * \param extcontext (input)
 *         Permission context for the operation (user, export context...).
 * \param extdescriptor (output)
 *         pointer to an allocated structure that will receive
 *         directory stream informations, on successfull completion.
 * \param attributes (optional output)
 *         On successfull completion,the structure pointed
 *         by dir_attributes receives the new directory attributes.
 *         Can be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (user does not have read permission on directory)
 *        - ERR_FSAL_STALE        (exthandle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */
fsal_status_t CEPHFSAL_opendir(fsal_handle_t * exthandle,
                               fsal_op_context_t * extcontext,
                               fsal_dir_t * extdescriptor,
                               fsal_attrib_list_t * dir_attributes)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  cephfsal_dir_t* descriptor = (cephfsal_dir_t*) extdescriptor;
  fsal_status_t status;
  int rc;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);
  struct ceph_dir_result *dh;

  /* sanity checks
   * note : dir_attributes is optional.
   */
  if(!handle || !context || !descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  TakeTokenFSCall();
  /* XXX ceph_ll_opendir has void in the interface, but Client::ll_opendir
   * is using dir_result_t. */
  rc = ceph_ll_opendir(context->export_context->cmount, VINODE(handle),
                       (void *) &dh, uid, gid);
  ReleaseTokenFSCall();

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_opendir);

  descriptor->dh = dh;
  descriptor->vi = VINODE(handle);
  descriptor->ctx = *context;

  if(dir_attributes)
    {
      status = CEPHFSAL_getattrs(exthandle, extcontext, dir_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(dir_attributes->asked_attributes);
          FSAL_SET_MASK(dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_opendir);
}

/**
 * FSAL_readdir :
 *     Read the entries of an opened directory.
 *
 * \param descriptor (input):
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
 * \param dirents (output)
 *        Adress of the buffer where the direntries are to be stored.
 * \param end_position (output)
 *        Cookie that indicates the current position in the directory.
 * \param count (output)
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
fsal_status_t CEPHFSAL_readdir(fsal_dir_t *extdescriptor,
                               fsal_cookie_t extstart,
                               fsal_attrib_mask_t attrmask,
                               fsal_mdsize_t buffersize,
                               fsal_dirent_t *dirents,
                               fsal_cookie_t *extend,
                               fsal_count_t *count,
                               fsal_boolean_t *end_of_dir)
{
  int rc = 0;
  fsal_status_t status;
  struct dirent de;
  cephfsal_dir_t* descriptor = (cephfsal_dir_t*) extdescriptor;
  struct ceph_mount_info *cmount = descriptor->ctx.export_context->cmount;

  /* XXXX gcc reports strict aliasing violation: */
  loff_t start = ((cephfsal_cookie_t) extstart).data.cookie;
  loff_t* end = &((cephfsal_cookie_t*) extend)->data.cookie;
  unsigned int max_entries = buffersize / sizeof(fsal_dirent_t);

  /* sanity checks */

  if(!descriptor || !dirents || !end || !count || !end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  *end_of_dir = FALSE;
  *count = 0;

  TakeTokenFSCall();

  (void) ceph_seekdir(cmount, DH(descriptor), start);

  while ((*count <= max_entries) && !(*end_of_dir)) {
      struct stat st;

      memset(&dirents[*count], sizeof(fsal_dirent_t), 0);
      memset(&de, sizeof(struct dirent), 0);
      memset(&st, sizeof(struct stat), 0);
      int stmask = 0;

      TakeTokenFSCall();
      rc = ceph_readdirplus_r(cmount, DH(descriptor), &de, &st, &stmask);
      if (rc < 0) /* Error */
          Return(posix2fsal_error(rc), 0, INDEX_FSAL_readdir);
      else if (rc == 1) {
          /* Got a dirent */
          cephfsal_handle_t* entryhandle
            = (cephfsal_handle_t*) &(dirents[*count].handle.data);
          cephfsal_cookie_t* entrycookie
            = (cephfsal_cookie_t*) &(dirents[*count].cookie);
          /* skip . and .. */
          if(!strcmp(de.d_name, ".") || !strcmp(de.d_name, ".."))
            continue;

          entryhandle->data.vi.ino.val = st.st_ino;
          entryhandle->data.vi.snapid.val = st.st_dev;

          status = FSAL_str2name(de.d_name, FSAL_MAX_NAME_LEN,
                                 &(dirents[*count].name));
          if(FSAL_IS_ERROR(status))
            ReturnStatus(status, INDEX_FSAL_readdir);

          entrycookie->data.cookie = ceph_telldir(cmount, DH(descriptor));
          dirents[*count].attributes.asked_attributes = attrmask;

          status =
            posix2fsal_attributes(&st,
                                  &(dirents[*count].attributes));
          if(FSAL_IS_ERROR(status)) {
              FSAL_CLEAR_MASK(dirents[*count].attributes
                              .asked_attributes);
              FSAL_SET_MASK(dirents[*count].attributes.asked_attributes,
                            FSAL_ATTR_RDATTR_ERR);
            }
          if (*count != 0) {
              dirents[(*count)-1].nextentry = &(dirents[*count]);
          }
          (*count)++;
      } else if (rc == 0) /* EOF */
          *end_of_dir = TRUE;
      else{
          /* Can't happen */ 
          abort();
      }
  } /* while */

  (*end) = ceph_telldir(cmount, DH(descriptor));

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
fsal_status_t CEPHFSAL_closedir(fsal_dir_t * extdescriptor)
{
  cephfsal_dir_t* descriptor = (cephfsal_dir_t*) extdescriptor;
  struct ceph_mount_info *cmount = descriptor->ctx.export_context->cmount;
  int rc = 0;

  /* sanity checks */
  if(!descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

  TakeTokenFSCall();
  rc = ceph_ll_releasedir(cmount, DH(descriptor));
  ReleaseTokenFSCall();

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_closedir);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);
}
