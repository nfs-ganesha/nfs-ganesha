/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_rcp.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.7 $
 * \brief   Transfer operations.
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
#include <fcntl.h>

/**
 * FSAL_rcp:
 * Copy an HPSS file to/from a local filesystem.
 *
 * \param filehandle (input):
 *        Handle of the HPSS file to be copied.
 * \param cred (input):
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
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */

fsal_status_t LUSTREFSAL_rcp(fsal_handle_t * filehandle,  /* IN */
                             fsal_op_context_t * p_context,       /* IN */
                             fsal_path_t * p_local_path,        /* IN */
                             fsal_rcpflag_t transfer_opt        /* IN */
    )
{

  int local_fd;
  int local_flags;
  int errsv;

  fsal_file_t fs_fd;
  fsal_openflags_t fs_flags;

  fsal_status_t st = FSAL_STATUS_NO_ERROR;

  /* default buffer size for RCP: 10MB */
#define RCP_BUFFER_SIZE 10485760
  caddr_t IObuffer;

  int to_local = FALSE;
  int to_fs = FALSE;

  int eof = FALSE;

  ssize_t local_size = -1;
  fsal_size_t fs_size;

  /* sanity checks. */

  if(!filehandle || !p_context || !p_local_path)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rcp);

  to_local = ((transfer_opt & FSAL_RCP_FS_TO_LOCAL) == FSAL_RCP_FS_TO_LOCAL);
  to_fs = ((transfer_opt & FSAL_RCP_LOCAL_TO_FS) == FSAL_RCP_LOCAL_TO_FS);

  if(to_local)
    LogFullDebug(COMPONENT_FSAL,
                      "FSAL_rcp: FSAL -> local file (%s)", p_local_path->path);

  if(to_fs)
    LogFullDebug(COMPONENT_FSAL,
                      "FSAL_rcp: local file -> FSAL (%s)", p_local_path->path);

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

  if (isFullDebug(COMPONENT_FSAL))
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

    LogFullDebug(COMPONENT_FSAL, "Openning local file %s with flags: %s",
                      p_local_path->path, msg);

  }

  local_fd = open(p_local_path->path, local_flags);
  errsv = errno;

  if(local_fd == -1)
    {
      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rcp);
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

  if (isFullDebug(COMPONENT_FSAL))
  {

    char msg[1024];

    msg[0] = '\0';

    if((fs_flags & FSAL_O_RDONLY) == FSAL_O_RDONLY)
      strcat(msg, "FSAL_O_RDONLY ");

    if((fs_flags & FSAL_O_WRONLY) == FSAL_O_WRONLY)
      strcat(msg, "FSAL_O_WRONLY ");

    if((fs_flags & FSAL_O_TRUNC) == FSAL_O_TRUNC)
      strcat(msg, "FSAL_O_TRUNC ");

    LogFullDebug(COMPONENT_FSAL, "Openning FSAL file with flags: %s", msg);

  }

  st = LUSTREFSAL_open(filehandle, p_context, fs_flags, &fs_fd, NULL);

  if(FSAL_IS_ERROR(st))
    {
      /* clean & return */
      close(local_fd);
      Return(st.major, st.minor, INDEX_FSAL_rcp);
    }

  LogFullDebug(COMPONENT_FSAL,
                    "Allocating IO buffer of size %llu",
                    (unsigned long long)RCP_BUFFER_SIZE);

  /* Allocates buffer */

  IObuffer = (caddr_t) Mem_Alloc(RCP_BUFFER_SIZE);

  if(IObuffer == NULL)
    {
      /* clean & return */
      close(local_fd);
      LUSTREFSAL_close(&fs_fd);
      Return(ERR_FSAL_NOMEM, Mem_Errno, INDEX_FSAL_rcp);
    }

  /* read/write loop */

  while(!eof)
    {
      /* initialize error code */
      st = FSAL_STATUS_NO_ERROR;

      LogFullDebug(COMPONENT_FSAL, "Read a block from source");

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
          fs_size = 0;
          st = LUSTREFSAL_read(&fs_fd, NULL, RCP_BUFFER_SIZE, IObuffer, &fs_size, &eof);

          if(FSAL_IS_ERROR(st))
            break;              /* exit loop */

          LogFullDebug(COMPONENT_FSAL, "Size read from source: %llu",
                            (unsigned long long)fs_size);

        }

      /* write (if not eof) */

      if(!eof || ((!to_fs) && (fs_size > 0)))
        {
          LogFullDebug(COMPONENT_FSAL, "Write a block to destination");

          if(to_fs)             /* to FSAL filesystem */
            {

              st = LUSTREFSAL_write(&fs_fd, NULL, local_size, IObuffer, &fs_size);

              if(FSAL_IS_ERROR(st))
                break;          /* exit loop */

            }
          else                  /* to local filesystem */
            {

              local_size = write(local_fd, IObuffer, fs_size);

              LogFullDebug(COMPONENT_FSAL, "Size written to target: %llu",
                                (unsigned long long)local_size);

              if(local_size == -1)
                {
                  st.major = ERR_FSAL_IO;
                  st.minor = errno;
                  break;        /* exit loop */
                }

            }                   /* if to_fs */

        }                       /* if eof */
      else
        LogFullDebug(COMPONENT_FSAL, "End of source file reached");

    }                           /* while !eof */

  /* Clean */

  Mem_Free(IObuffer);
  close(local_fd);
  LUSTREFSAL_close(&fs_fd);

  /* return status. */

  Return(st.major, st.minor, INDEX_FSAL_rcp);

}
