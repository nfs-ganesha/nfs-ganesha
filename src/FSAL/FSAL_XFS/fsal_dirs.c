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
fsal_status_t XFSFSAL_opendir(fsal_handle_t * p_dir_handle,  /* IN */
                              fsal_op_context_t * p_context, /* IN */
                              fsal_dir_t *dir_desc, /* OUT */
                              fsal_attrib_list_t * p_dir_attributes     /* [ IN/OUT ] */
    )
{
  xfsfsal_dir_t * p_dir_descriptor = (xfsfsal_dir_t *) dir_desc;
  int rc = 0 , errsv = 0 ;
  fsal_status_t status;

  struct stat buffstat;

  /* sanity checks
   * note : dir_attributes is optionnal.
   */
  if(!p_dir_handle || !p_context || !p_dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  /* get the path of the directory */
  TakeTokenFSCall();
  status =
      fsal_internal_handle2fd(p_context, p_dir_handle, &p_dir_descriptor->fd,
                              O_RDONLY | O_DIRECTORY);
  ReleaseTokenFSCall();

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_opendir);

  /* get directory metadata */
  TakeTokenFSCall();
  rc = fstat(p_dir_descriptor->fd, &buffstat);
  ReleaseTokenFSCall();

  if(rc != 0)
    {
      close(p_dir_descriptor->fd);
      if(rc == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_opendir);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_opendir);
    }

  /* Test access rights for this directory */
  status = fsal_internal_testAccess(p_context, FSAL_R_OK, &buffstat, NULL);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_opendir);

  /* if everything is OK, fills the dir_desc structure : */

  memcpy(&(p_dir_descriptor->context), p_context, sizeof(xfsfsal_op_context_t));
  memcpy(&(p_dir_descriptor->handle), p_dir_handle, sizeof(xfsfsal_handle_t));

  if(p_dir_attributes)
    {
      status = posix2fsal_attributes(&buffstat, p_dir_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_dir_attributes->asked_attributes);
          FSAL_SET_MASK(p_dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  p_dir_descriptor->dir_offset = 0;

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

struct linux_dirent
{
  long d_ino;
  long d_off;                   /* Be careful, SYS_getdents is a 32 bits call */
  unsigned short d_reclen;
  char d_name[];
};

#define BUF_SIZE 1024

fsal_status_t XFSFSAL_readdir(fsal_dir_t * dir_descriptor, /* IN */
                              fsal_cookie_t startposition,  /* IN */
                              fsal_attrib_mask_t get_attr_mask, /* IN */
                              fsal_mdsize_t buffersize, /* IN */
                              fsal_dirent_t * p_pdirent,        /* OUT */
                              fsal_cookie_t * end_position,        /* OUT */
                              fsal_count_t * p_nb_entries,      /* OUT */
                              fsal_boolean_t * p_end_of_dir     /* OUT */
    )
{
  xfsfsal_dir_t * p_dir_descriptor = (xfsfsal_dir_t * ) dir_descriptor;
  xfsfsal_cookie_t start_position;
  xfsfsal_cookie_t * p_end_position = (xfsfsal_cookie_t *) end_position;
  fsal_status_t st;
  fsal_count_t max_dir_entries;
  char buff[BUF_SIZE];
  struct linux_dirent *dp = NULL;
  int bpos = 0;
  int tmpfd = 0;

  char d_type;
  struct stat buffstat;

  int errsv = 0, rc = 0;

  memset(buff, 0, BUF_SIZE);

  /*****************/
  /* sanity checks */
  /*****************/

  if(!p_dir_descriptor || !p_pdirent || !p_end_position || !p_nb_entries || !p_end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  max_dir_entries = (buffersize / sizeof(fsal_dirent_t));

  /***************************/
  /* seek into the directory */
  /***************************/
  memcpy( (char *)&start_position.data.cookie, startposition.data, sizeof( off_t ) ) ;
  rc = errno = 0;
  lseek(p_dir_descriptor->fd, start_position.data.cookie, SEEK_SET);
  rc = errno;

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
      rc = syscall(SYS_getdents, p_dir_descriptor->fd, buff, BUF_SIZE);
      ReleaseTokenFSCall();
      if(rc < 0)
        {
          rc = errno;
          Return(posix2fsal_error(rc), rc, INDEX_FSAL_readdir);
        }
      /* End of directory */
      if(rc == 0)
        {
          *p_end_of_dir = 1;
          break;
        }

    /***********************************/
      /* Get information about the entry */
    /***********************************/

      for(bpos = 0; bpos < rc;)
        {
          dp = (struct linux_dirent *)(buff + bpos);
          d_type = *(buff + bpos + dp->d_reclen - 1);
                                                    /** @todo not used for the moment. Waiting for information on symlink management */
          bpos += dp->d_reclen;

          /* LogFullDebug(COMPONENT_FSAL, "\tino=%8ld|%8lx off=%d|%x reclen=%d|%x name=%s|%d", dp->d_ino, dp->d_ino, (int)dp->d_off, (int)dp->d_off, 
             dp->d_reclen, dp->d_reclen, dp->d_name, (int)dp->d_name[0]  ) ; */

          if(!(*p_nb_entries < max_dir_entries))
            break;

          /* skip . and .. */
          if(!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
            continue;

          /* build the full path of the file into "fsalpath */
          if(FSAL_IS_ERROR
             (st =
              FSAL_str2name(dp->d_name, FSAL_MAX_NAME_LEN,
                            &(p_pdirent[*p_nb_entries].name))))
            ReturnStatus(st, INDEX_FSAL_readdir);

          d_type = DT_UNKNOWN;
          if((tmpfd =
              openat(p_dir_descriptor->fd, dp->d_name, O_RDONLY | O_NOFOLLOW, 0600)) < 0)
            {
              if(errno != ELOOP)        /* ( p_dir_descriptor->fd, dp->d_name) is not a symlink */
                Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_readdir);
              else
                d_type = DT_LNK;
            }

          /* get object handle */
          TakeTokenFSCall();
          if(d_type != DT_LNK)
            {
              st = fsal_internal_fd2handle((fsal_op_context_t *)&(p_dir_descriptor->context),
					   tmpfd, &(p_pdirent[*p_nb_entries].handle));
              close(tmpfd);
            }
          else
            {
              if(fstatat(p_dir_descriptor->fd, dp->d_name, &buffstat, AT_SYMLINK_NOFOLLOW)
                 < 0)
                {
                  ReleaseTokenFSCall();
                  Return(posix2fsal_error(errno), errno, INDEX_FSAL_readdir);
                }

              st = fsal_internal_inum2handle(&p_dir_descriptor->context,
                                             buffstat.st_ino, &(p_pdirent[*p_nb_entries].handle));

              if(FSAL_IS_ERROR(st))
                {
                  ReleaseTokenFSCall();
                  ReturnStatus(st, INDEX_FSAL_readdir);
                }
              p_pdirent[*p_nb_entries].attributes.asked_attributes = get_attr_mask;

              st = posix2fsal_attributes(&buffstat, &p_pdirent[*p_nb_entries].attributes);
              if(FSAL_IS_ERROR(st))
                {
                  ReleaseTokenFSCall();
                  FSAL_CLEAR_MASK(p_pdirent[*p_nb_entries].attributes.asked_attributes);
                  FSAL_SET_MASK(p_pdirent[*p_nb_entries].attributes.asked_attributes,
                                FSAL_ATTR_RDATTR_ERR);
                  ReturnStatus(st, INDEX_FSAL_getattrs);
                }

            }
          ReleaseTokenFSCall();

          if(FSAL_IS_ERROR(st))
            ReturnStatus(st, INDEX_FSAL_readdir);

    /************************
     * Fills the attributes *
     ************************/
          if(d_type != DT_LNK)
            {
              p_pdirent[*p_nb_entries].attributes.asked_attributes = get_attr_mask;

              st = XFSFSAL_getattrs((xfsfsal_handle_t
                                     *) (&(p_pdirent[*p_nb_entries].handle)),
                                    (xfsfsal_op_context_t *) & p_dir_descriptor->context,
                                    &p_pdirent[*p_nb_entries].attributes);
              if(FSAL_IS_ERROR(st))
                {
                  FSAL_CLEAR_MASK(p_pdirent[*p_nb_entries].attributes.asked_attributes);
                  FSAL_SET_MASK(p_pdirent[*p_nb_entries].attributes.asked_attributes,
                                FSAL_ATTR_RDATTR_ERR);
                }
            }
          //p_pdirent[*p_nb_entries].cookie.cookie = dp->d_off;
          ((xfsfsal_cookie_t *) (&p_pdirent[*p_nb_entries].cookie))->data.cookie = dp->d_off;
          p_pdirent[*p_nb_entries].nextentry = NULL;
          if(*p_nb_entries)
            p_pdirent[*p_nb_entries - 1].nextentry = &(p_pdirent[*p_nb_entries]);

          //(*p_end_position) = p_pdirent[*p_nb_entries].cookie;
          memcpy((char *)p_end_position, (char *)&p_pdirent[*p_nb_entries].cookie,
                 sizeof(xfsfsal_cookie_t));

          (*p_nb_entries)++;

        }                       /* for */
    }                           /* While */

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
fsal_status_t XFSFSAL_closedir(fsal_dir_t * p_dir_descriptor /* IN */
    )
{

  int rc;

  /* sanity checks */
  if(!p_dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

  rc = close(((xfsfsal_dir_t *)p_dir_descriptor)->fd);
  if(rc != 0)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_closedir);

  /* fill dir_descriptor with zeros */
  memset(p_dir_descriptor, 0, sizeof(xfsfsal_dir_t));

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

}
