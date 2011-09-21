/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_convert.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 15:53:39 $
 * \version $Revision: 1.31 $
 * \brief   HPSS-FSAL type translation functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "fsal_convert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define MAX_2( x, y )    ( (x) > (y) ? (x) : (y) )
#define MAX_3( x, y, z ) ( (x) > (y) ? MAX_2((x),(z)) : MAX_2((y),(z)) )

/**
 * fsal2posix_testperm:
 * Convert FSAL permission flags to Posix permission flags.
 *
 * \param testperm (input):
 *        The FSAL permission flags to be tested.
 *
 * \return The POSIX permission flags to be tested.
 */
int fsal2posix_testperm(fsal_accessflags_t testperm)
{

  int posix_testperm = 0;

  if(testperm & FSAL_R_OK)
    posix_testperm |= R_OK;
  if(testperm & FSAL_W_OK)
    posix_testperm |= W_OK;
  if(testperm & FSAL_X_OK)
    posix_testperm |= X_OK;
  if(testperm & FSAL_F_OK)
    posix_testperm |= F_OK;

  return posix_testperm;

}

/**
 * fsal2unix_mode:
 * Convert FSAL mode to posix mode.
 *
 * \param fsal_mode (input):
 *        The FSAL mode to be translated.
 *
 * \return The posix mode associated to fsal_mode.
 */
mode_t fsal2unix_mode(fsal_accessmode_t fsal_mode)
{

  mode_t out_mode = 0;

  if((fsal_mode & FSAL_MODE_SUID))
    out_mode |= S_ISUID;
  if((fsal_mode & FSAL_MODE_SGID))
    out_mode |= S_ISGID;
  if((fsal_mode & FSAL_MODE_SVTX))
    out_mode |= S_ISVTX;

  if((fsal_mode & FSAL_MODE_RUSR))
    out_mode |= S_IRUSR;
  if((fsal_mode & FSAL_MODE_WUSR))
    out_mode |= S_IWUSR;
  if((fsal_mode & FSAL_MODE_XUSR))
    out_mode |= S_IXUSR;
  if((fsal_mode & FSAL_MODE_RGRP))
    out_mode |= S_IRGRP;
  if((fsal_mode & FSAL_MODE_WGRP))
    out_mode |= S_IWGRP;
  if((fsal_mode & FSAL_MODE_XGRP))
    out_mode |= S_IXGRP;
  if((fsal_mode & FSAL_MODE_ROTH))
    out_mode |= S_IROTH;
  if((fsal_mode & FSAL_MODE_WOTH))
    out_mode |= S_IWOTH;
  if((fsal_mode & FSAL_MODE_XOTH))
    out_mode |= S_IXOTH;

  return out_mode;

}

/**
 * unix2fsal_mode:
 * Convert posix mode to FSAL mode.
 *
 * \param unix_mode (input):
 *        The posix mode to be translated.
 *
 * \return The FSAL mode associated to unix_mode.
 */
fsal_accessmode_t unix2fsal_mode(mode_t unix_mode)
{

  fsal_accessmode_t fsalmode = 0;

  if(unix_mode & S_ISUID)
    fsalmode |= FSAL_MODE_SUID;
  if(unix_mode & S_ISGID)
    fsalmode |= FSAL_MODE_SGID;
  if(unix_mode & S_ISVTX)
    fsalmode |= FSAL_MODE_SVTX;

  if(unix_mode & S_IRUSR)
    fsalmode |= FSAL_MODE_RUSR;
  if(unix_mode & S_IWUSR)
    fsalmode |= FSAL_MODE_WUSR;
  if(unix_mode & S_IXUSR)
    fsalmode |= FSAL_MODE_XUSR;

  if(unix_mode & S_IRGRP)
    fsalmode |= FSAL_MODE_RGRP;
  if(unix_mode & S_IWGRP)
    fsalmode |= FSAL_MODE_WGRP;
  if(unix_mode & S_IXGRP)
    fsalmode |= FSAL_MODE_XGRP;

  if(unix_mode & S_IROTH)
    fsalmode |= FSAL_MODE_ROTH;
  if(unix_mode & S_IWOTH)
    fsalmode |= FSAL_MODE_WOTH;
  if(unix_mode & S_IXOTH)
    fsalmode |= FSAL_MODE_XOTH;

  return fsalmode;

}

/**
 * posix2fsal_type:
 * Convert posix object type to FSAL node type.
 *
 * \param posix_type_in (input):
 *        The POSIX object type.
 *
 * \return - The FSAL node type associated to posix_type_in.
 *         - -1 if the input type is unknown.
 */
fsal_nodetype_t posix2fsal_type(mode_t posix_type_in)
{

  switch (posix_type_in & S_IFMT)
    {
    case S_IFIFO:
      return FSAL_TYPE_FIFO;

    case S_IFCHR:
      return FSAL_TYPE_CHR;

    case S_IFDIR:
      return FSAL_TYPE_DIR;

    case S_IFBLK:
      return FSAL_TYPE_BLK;

    case S_IFREG:
    case S_IFMT:
      return FSAL_TYPE_FILE;

    case S_IFLNK:
      return FSAL_TYPE_LNK;

    case S_IFSOCK:
      return FSAL_TYPE_SOCK;

    default:
      LogWarn(COMPONENT_FSAL, "Unknown object type: %d", posix_type_in);
      return -1;
    }

}

fsal_time_t posix2fsal_time(time_t tsec, time_t nsec)
{
  fsal_time_t fsaltime;

  fsaltime.seconds = (fsal_uint_t) tsec;
  fsaltime.nseconds = (fsal_uint_t) nsec;

  return fsaltime;
}

fsal_fsid_t posix2fsal_fsid(dev_t posix_devid)
{

  fsal_fsid_t fsid;

  fsid.major = (fsal_u64_t) posix_devid;
  fsid.minor = 0;

  return fsid;

}

fsal_dev_t posix2fsal_devt(dev_t posix_devid)
{

  fsal_dev_t dev;

  dev.major = posix_devid >> 8;
  dev.minor = posix_devid & 0xFF;

  return dev;
}
