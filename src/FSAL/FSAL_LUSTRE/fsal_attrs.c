/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_attrs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/09/09 15:22:49 $
 * \version $Revision: 1.19 $
 * \brief   Attributes functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

/**
 * FSAL_getattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t LUSTREFSAL_getattrs(fsal_handle_t * p_filehandle,   /* IN */
                                  fsal_op_context_t * p_context,  /* IN */
                                  fsal_attrib_list_t * p_object_attributes      /* IN/OUT */
    )
{
  int rc;
  fsal_status_t st;
  fsal_path_t fsalpath;
  struct stat buffstat;

  /* sanity checks.
   * note : object_attributes is mandatory in FSAL_getattrs.
   */
  if(!p_filehandle || !p_context || !p_object_attributes)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_getattrs);

  /* get the path of the file */
  st = fsal_internal_Handle2FidPath(p_context, p_filehandle, &fsalpath);
  if(FSAL_IS_ERROR(st))
    ReturnStatus(st, INDEX_FSAL_getattrs);

  /* get file metadata */
  TakeTokenFSCall();
  rc = lstat(fsalpath.path, &buffstat);
  ReleaseTokenFSCall();

  if(rc != 0)
    {
      rc = errno;
      if(rc == ENOENT)
        Return(ERR_FSAL_STALE, rc, INDEX_FSAL_getattrs);
      else
        Return(posix2fsal_error(rc), rc, INDEX_FSAL_getattrs);
    }

  /* convert attributes */
  st = posix2fsal_attributes(&buffstat, p_object_attributes);
  if(FSAL_IS_ERROR(st))
    {
      FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
      FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
      ReturnStatus(st, INDEX_FSAL_getattrs);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs);

}

/**
 * FSAL_setattrs:
 * Set attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param attrib_set (mandatory input):
 *        The attributes to be set for the object.
 *        It defines the attributes that the caller
 *        wants to set and their values.
 * \param object_attributes (optionnal input/output):
 *        The post operation attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t LUSTREFSAL_setattrs(fsal_handle_t * p_filehandle,   /* IN */
                                  fsal_op_context_t * p_context,  /* IN */
                                  fsal_attrib_list_t * p_attrib_set,    /* IN */
                                  fsal_attrib_list_t * p_object_attributes      /* [ IN/OUT ] */
    )
{
  int rc, errsv;
  unsigned int i;
  fsal_status_t status;
  fsal_attrib_list_t attrs;
  int no_trunc = 0;
  fsal_path_t fsalpath;
  struct stat buffstat;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_filehandle || !p_context || !p_attrib_set)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_setattrs);

  /* local copy of attributes */
  attrs = *p_attrib_set;

  /* First, check that FSAL attributes changes are allowed. */

  /* Is it allowed to change times ? */

  if(!global_fs_info.cansettime)
    {
      if(attrs.asked_attributes
         & (FSAL_ATTR_ATIME | FSAL_ATTR_CREATION | FSAL_ATTR_CTIME | FSAL_ATTR_MTIME |
            FSAL_ATTR_ATIME_SERVER | FSAL_ATTR_MTIME_SERVER))
        {
          /* handled as an unsettable attribute. */
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_setattrs);
        }
    }

  /* apply umask, if mode attribute is to be changed */
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    {
      attrs.mode &= (~global_fs_info.umask);
    }

  /* convert handle into path */
  status = fsal_internal_Handle2FidPath(p_context, p_filehandle, &fsalpath);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_setattrs);

  /* get current attributes */
  TakeTokenFSCall();
  rc = lstat(fsalpath.path, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc != 0)
    {
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_setattrs);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_setattrs);
    }

  /**************
   *  TRUNCATE  *
   **************/

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_SIZE))
    {
#ifdef _SHOOK
      /* If the file is not online:
       * - if truncate(0) => call tuncate(0), then "shook restore_trunc"
       * - if truncate(>0) => call "shook restore", then truncate
       */
      shook_state state;
      rc = shook_get_status(fsalpath.path, &state, FALSE);
      if (rc)
      {
          LogEvent(COMPONENT_FSAL, "Error retrieving shook status of %s: %s",
                   fsalpath.path, strerror(-rc));
          if (rc)
              Return(posix2fsal_error(-rc), -rc, INDEX_FSAL_truncate);
      }
      else if (state != SS_ONLINE)
      {
          if (p_attrib_set->filesize == 0)
          {
              LogInfo(COMPONENT_FSAL, "File is offline: calling shook restore_trunc");

              /* first truncate the file, them call the shook_svr to clear
               * the 'released' flag */

              TakeTokenFSCall();
              rc = truncate(fsalpath.path, 0);
              errsv = errno;
              ReleaseTokenFSCall();

              if (rc == 0)
              {
                  /* use a short timeout of 2s */
                  rc = shook_server_call(SA_RESTORE_TRUNC,
                                         ((lustrefsal_op_context_t *)p_context)->export_context->fsname,
                                         &((lustrefsal_handle_t *)p_filehandle)->data.fid, 2);
                  if (rc)
                      Return(posix2fsal_error(-rc), -rc, INDEX_FSAL_truncate);
                  else {
                      /* check that file is online, else operation is still
                       * in progress: return err jukebox */
                      rc = shook_get_status(fsalpath.path, &state, FALSE);
                      if (rc)
                      {
                          LogEvent(COMPONENT_FSAL, "Error retrieving shook status of %s: %s",
                                   fsalpath.path, strerror(-rc));
                          if (rc)
                              Return(posix2fsal_error(-rc), -rc, INDEX_FSAL_truncate);
                      }
                      else if (state != SS_ONLINE)
                          Return(ERR_FSAL_DELAY, -rc, INDEX_FSAL_truncate);
                      /* OK */
                  }
                  /* file is already truncated, no need to truncate again */
                  no_trunc = 1;
              }
              else
              {
                    if(errsv == ENOENT)
                      Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_truncate);
                    else
                      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_truncate);
              }
          }
          else /* p_attrib_set->filesize > 0 */
          {
              /* trigger restore. Give it a chance to retrieve the file in less than a second.
               * Else, it returns ETIME that is converted in ERR_DELAY */
              rc = shook_server_call(SA_RESTORE,
                                     ((lustrefsal_op_context_t *)p_context)->export_context->fsname,
                                     &((lustrefsal_handle_t *)p_filehandle)->data.fid, 1);
              if (rc)
                  Return(posix2fsal_error(-rc), -rc, INDEX_FSAL_truncate);
              else {
                  /* check that file is online, else operation is still
                   * in progress: return err jukebox */
                  rc = shook_get_status(fsalpath.path, &state, FALSE);
                  if (rc)
                  {
                      LogEvent(COMPONENT_FSAL, "Error retrieving shook status of %s: %s",
                               fsalpath.path, strerror(-rc));
                      if (rc)
                          Return(posix2fsal_error(-rc), -rc, INDEX_FSAL_truncate);
                  }
                  else if (state != SS_ONLINE)
                      Return(ERR_FSAL_DELAY, -rc, INDEX_FSAL_truncate);
                  /* OK */
              }

              /* if rc = 0, file can be opened */
          }
      }
      /* else file is on line */
#endif

      /* Executes the POSIX truncate operation */

      if (!no_trunc)
      {
        TakeTokenFSCall();
        rc = truncate(fsalpath.path, p_attrib_set->filesize);
        errsv = errno;
        ReleaseTokenFSCall();
      }

      /* convert return code */
      if(rc)
        {
          if(errsv == ENOENT)
            Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_truncate);
          else
            Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_truncate);
        }
    }
 
  /***********
   *  CHMOD  *
   ***********/
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    {
      /* The POSIX chmod call don't affect the symlink object, but
       * the entry it points to. So we must ignore it.
       */
      if(!S_ISLNK(buffstat.st_mode))
        {
          TakeTokenFSCall();
          rc = chmod(fsalpath.path, fsal2unix_mode(attrs.mode));
          errsv = errno;
          ReleaseTokenFSCall();

          if(rc)
            {
              Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_setattrs);
            }
        }
    }

  /***********
   *  CHOWN  *
   ***********/

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER | FSAL_ATTR_GROUP))
    {
      LogFullDebug(COMPONENT_FSAL, "Performing chown(%s, %d,%d)",
                        fsalpath.path, FSAL_TEST_MASK(attrs.asked_attributes,
                                                      FSAL_ATTR_OWNER) ? (int)attrs.owner
                        : -1, FSAL_TEST_MASK(attrs.asked_attributes,
                                             FSAL_ATTR_GROUP) ? (int)attrs.group : -1);

      TakeTokenFSCall();
      rc = lchown(fsalpath.path,
                  FSAL_TEST_MASK(attrs.asked_attributes,
                                 FSAL_ATTR_OWNER) ? (int)attrs.owner : -1,
                  FSAL_TEST_MASK(attrs.asked_attributes,
                                 FSAL_ATTR_GROUP) ? (int)attrs.group : -1);
      ReleaseTokenFSCall();
      if(rc)
        Return(posix2fsal_error(errno), errno, INDEX_FSAL_setattrs);
    }

  /***********
   *  UTIME  *
   ***********/

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME | FSAL_ATTR_MTIME |
                                            FSAL_ATTR_ATIME_SERVER |
                                            FSAL_ATTR_MTIME_SERVER))
    {
      struct timeval  timebuf[2];
      struct timeval *ptimebuf = &timebuf;

      if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME_SERVER) &&
         FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME_SERVER))
        {
          /* If both times are set to server time, we can shortcut and
           * use the utimes interface to set both times to current time.
           */
          ptimebuf = NULL;
        }
      else
        {
          /* Atime */
          if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME_SERVER))
            {
              /* Since only one time is set to server time, we must
               * get time of day to set it.
               */
              gettimeofday(&timebuf[0], NULL);
            }
          else if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME))
            {
              timebuf[0].tv_sec  = attrs.atime.seconds;
              timebuf[0].tv_usec = attrs.atime.nseconds / 1000;
            }
          else
            {
              /* If we are not setting atime, must set from fetched attr. */
              timebuf[0].tv_sec  = buffstat.st_atime;
#ifdef __USE_MISC
              timebuf[0].tv_usec = buffstat.st_atim.tv_nsec / 1000;
#else
              timebuf[0].tv_usec = 0;
#endif
            }

          /* Mtime */
          if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME_SERVER))
            {
              /* Since only one time is set to server time, we must
               * get time of day to set it.
               */
              gettimeofday(&timebuf[1], NULL);
            }
          else if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME))
            {
              timebuf[1].tv_sec  = attrs.mtime.seconds;
              timebuf[1].tv_usec = attrs.mtime.nseconds / 1000;
            }
          else
            {
              /* If we are not setting mtime, must set from fetched attr. */
              timebuf[1].tv_sec  = buffstat.st_mtime;
#ifdef __USE_MISC
              timebuf[1].tv_usec = buffstat.st_mtim.tv_nsec / 1000;
#else
              timebuf[1].tv_usec = 0;
#endif
            }
        }

      TakeTokenFSCall();
      rc = utimes(fsalpath.path, ptimebuf);
      errsv = errno;
      ReleaseTokenFSCall();
      if(rc)
        Return(posix2fsal_error(errno), errno, INDEX_FSAL_setattrs);
    }

  /* Optionaly fills output attributes. */

  if(p_object_attributes)
    {
      status = LUSTREFSAL_getattrs(p_filehandle, p_context, p_object_attributes);

      /* on error, we set a special bit in the mask. */
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_setattrs);

}
