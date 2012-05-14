/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010 The Linux Box, Inc.
 * Contributor : Adam C. Emerson <aemerson@linuxbox.com>
 *
 * Portions copyright CEA/DAM/DIF  (2008)
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
 * \file    fsal_rcp.c
 * \brief   Transfer operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/**
 * FSAL_rcp:
 * Copy an HPSS file to/from a local filesystem.
 *
 * \param filehandle (input):
 *        Handle of the HPSS file to be copied.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 * \param p_local_path (input):
 *        Path of the file in the local filesystem.
 * \param transfer_opt (input):
 *        Flags that indicate transfer direction and options.
 *        This consists of an inclusive OR between the following values :
 *        - FSAL_RCP_FS_TO_LOCAL: Copy the file from the filesystem
 *          to a local path.
 *        - FSAL_RCP_LOCAL_TO_FS: Copy the file from local path
 *          to the filesystem.
 *        - FSAL_RCP_LOCAL_CREAT: Create the target local file
 *          if it doesn't exist.
 *        - FSAL_RCP_LOCAL_EXCL: Produce an error if the target local file
 *          already exists.
 *
 * \return Major error codes :
 *      - ERR_FSAL_NO_ERROR     (no error)
 *      - ERR_FSAL_ACCESS       (user doesn't have the permissions for opening the file)
 *      - ERR_FSAL_STALE        (filehandle does not address an existing object) 
 *      - ERR_FSAL_INVAL        (filehandle does not address a regular file,
 *                               or tranfert options are conflicting)
 *      - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *      - Other error codes can be returned :
 *        ERR_FSAL_IO, ERR_FSAL_NOSPC, ERR_FSAL_DQUOT...
 */

fsal_status_t CEPHFSAL_rcp(fsal_handle_t * filehandle,
                           fsal_op_context_t * p_context,
                           fsal_path_t * p_local_path,
                           fsal_rcpflag_t transfer_opt)
{
  int local_fd;
  int local_flags;

  fsal_file_t fs_fd;
  fsal_openflags_t fs_flags;

  fsal_status_t st = FSAL_STATUS_NO_ERROR;

/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
 * This is a template implementation of rcp based on FSAL_read and FSAL_write
 * function. You may chose keeping it or doing your own implementation
 * that is optimal for your filesystems.
 * <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 */

  /* default buffer size for RCP: 1MB */
#define RCP_BUFFER_SIZE 1048576
  caddr_t IObuffer;

  int to_local = FALSE;
  int to_fs = FALSE;

  int eof = FALSE;

  ssize_t local_size = 0;
  fsal_size_t fs_size = 0;

  /* sanity checks. */

  if(!filehandle || !p_context || !p_local_path)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rcp);

  to_local = ((transfer_opt & FSAL_RCP_FS_TO_LOCAL) == FSAL_RCP_FS_TO_LOCAL);
  to_fs = ((transfer_opt & FSAL_RCP_LOCAL_TO_FS) == FSAL_RCP_LOCAL_TO_FS);

#ifdef  _DEBUG_FSAL
  if(to_local)
    DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG,
                      "FSAL_rcp: FSAL -> local file (%s)", p_local_path->path);

  if(to_fs)
    DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG,
                      "FSAL_rcp: local file -> FSAL (%s)", p_local_path->path);
#endif

  /* must give the sens of transfert (exactly one) */

  if((!to_local && !to_fs) || (to_local && to_fs))
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_rcp);

  /* first, open local file with the correct flags */

  if(to_fs)
    {
      local_flags = O_RDONLY;
    }
  else
    {
      local_flags = O_WRONLY | O_TRUNC;

      if((transfer_opt & FSAL_RCP_LOCAL_CREAT) == FSAL_RCP_LOCAL_CREAT)
        local_flags |= O_CREAT;

      if((transfer_opt & FSAL_RCP_LOCAL_EXCL) == FSAL_RCP_LOCAL_EXCL)
        local_flags |= O_EXCL;

    }

#ifdef  _DEBUG_FSAL
  {

    char msg[1024];

    msg[0] = '\0';

    if((local_flags & O_RDONLY) == O_RDONLY)
      strcat(msg, "O_RDONLY ");

    if((local_flags & O_WRONLY) == O_WRONLY)
      strcat(msg, "O_WRONLY ");

    if((local_flags & O_TRUNC) == O_TRUNC)
      strcat(msg, "O_TRUNC ");

    if((local_flags & O_CREAT) == O_CREAT)
      strcat(msg, "O_CREAT ");

    if((local_flags & O_EXCL) == O_EXCL)
      strcat(msg, "O_EXCL ");

    DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "Openning local file %s with flags: %s",
                      p_local_path->path, msg);

  }
#endif

  local_fd = open(p_local_path->path, local_flags, 0644);

  if(local_fd == -1)
    {
      /* todo : put a function in fsal_convert.c that convert your local
       * filesystem errors to an FSAL error code.
       * So you will have a call like :
       * Return( unix2fsal_error(errno) , errno , INDEX_FSAL_rcp );
       */
    }

  /* call FSAL_open with the correct flags */

  if(to_fs)
    {
      fs_flags = FSAL_O_WRONLY | FSAL_O_TRUNC;

      /* invalid flags for local to filesystem */

      if(((transfer_opt & FSAL_RCP_LOCAL_CREAT) == FSAL_RCP_LOCAL_CREAT)
         || ((transfer_opt & FSAL_RCP_LOCAL_EXCL) == FSAL_RCP_LOCAL_EXCL))
        {
          /* clean & return */
          close(local_fd);
          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_rcp);
        }
    }
  else
    {
      fs_flags = FSAL_O_RDONLY;
    }

#ifdef  _DEBUG_FSAL
  {

    char msg[1024];

    msg[0] = '\0';

    if((fs_flags & FSAL_O_RDONLY) == FSAL_O_RDONLY)
      strcat(msg, "FSAL_O_RDONLY ");

    if((fs_flags & FSAL_O_WRONLY) == FSAL_O_WRONLY)
      strcat(msg, "FSAL_O_WRONLY ");

    if((fs_flags & FSAL_O_TRUNC) == FSAL_O_TRUNC)
      strcat(msg, "FSAL_O_TRUNC ");

    DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "Openning FSAL file with flags: %s", msg);

  }
#endif

  st = CEPHFSAL_open(filehandle, p_context, fs_flags, &fs_fd, NULL);

  if(FSAL_IS_ERROR(st))
    {
      /* clean & return */
      close(local_fd);
      Return(st.major, st.minor, INDEX_FSAL_rcp);
    }
#ifdef  _DEBUG_FSAL
  DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG,
                    "Allocating IO buffer of size %llu",
                    (unsigned long long)RCP_BUFFER_SIZE);
#endif

  /* Allocates buffer */

  IObuffer = gsh_malloc(RCP_BUFFER_SIZE);

  if(IObuffer == NULL)
    {
      /* clean & return */
      close(local_fd);
      CEPHFSAL_close(&fs_fd);
      Return(ERR_FSAL_NOMEM, ENOMEM, INDEX_FSAL_rcp);
    }

  /* read/write loop */

  while(!eof)
    {
      /* initialize error code */
      st = FSAL_STATUS_NO_ERROR;

#ifdef  _DEBUG_FSAL
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "Read a block from source");
#endif

      /* read */

      if(to_fs)                 /* from local filesystem */
        {
          local_size = read(local_fd, IObuffer, RCP_BUFFER_SIZE);

          if(local_size == -1)
            {
              st.major = ERR_FSAL_IO;
              st.minor = errno;
              break;            /* exit loop */
            }

          eof = (local_size == 0);

        }
      else                      /* from FSAL filesystem */
        {
          st=CEPHFSAL_read(&fs_fd, NULL, RCP_BUFFER_SIZE, IObuffer, &fs_size, &eof);

          if(FSAL_IS_ERROR(st))
            break;              /* exit loop */

        }

      /* write (if not eof) */

      if(!eof)
        {

#ifdef  _DEBUG_FSAL
          DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "Write a block to destination");
#endif

          if(to_fs)             /* to FSAL filesystem */
            {

              st = CEPHFSAL_write(&fs_fd, p_context, NULL, local_size, IObuffer, &fs_size);

              if(FSAL_IS_ERROR(st))
                break;          /* exit loop */

            }
          else                  /* to local filesystem */
            {

              local_size = write(local_fd, IObuffer, fs_size);

              if(local_size == -1)
                {
                  st.major = ERR_FSAL_IO;
                  st.minor = errno;
                  break;        /* exit loop */
                }

            }                   /* if to_fs */

        }                       /* if eof */
#ifdef  _DEBUG_FSAL
      else
        DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "End of source file reached");
#endif

    }                           /* while !eof */

  /* Clean */

  gsh_free(IObuffer);
  close(local_fd);
  CEPHFSAL_close(&fs_fd);

  /* return status. */

  Return(st.major, st.minor, INDEX_FSAL_rcp);

}
