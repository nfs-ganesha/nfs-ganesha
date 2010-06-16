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
#include "fsal_internal.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define MAX_2( x, y )    ( (x) > (y) ? (x) : (y) )
#define MAX_3( x, y, z ) ( (x) > (y) ? MAX_2((x),(z)) : MAX_2((y),(z)) )

/**
 * posix2fsal_error :
 * Convert POSIX error codes to FSAL error codes.
 *
 * \param posix_errorcode (input):
 *        The error code returned from POSIX.
 *
 * \return The FSAL error code associated
 *         to posix_errorcode.
 *
 */
int posix2fsal_error(int posix_errorcode)
{

  switch (posix_errorcode)
    {

    case EPERM:
      return ERR_FSAL_PERM;

    case ENOENT:
      return ERR_FSAL_NOENT;

      /* connection error */
#ifdef _AIX_5
    case ENOCONNECT:
#elif defined _LINUX
    case ECONNREFUSED:
    case ECONNABORTED:
    case ECONNRESET:
#endif

      /* IO error */
    case EIO:

      /* too many open files */
    case ENFILE:
    case EMFILE:

      /* broken pipe */
    case EPIPE:

      /* all shown as IO errors */
      return ERR_FSAL_IO;

      /* no such device */
    case ENODEV:
    case ENXIO:
      return ERR_FSAL_NXIO;

      /* invalid file descriptor : */
    case EBADF:
      /* we suppose it was not opened... */

      /**
       * @todo: The EBADF error also happens when file
       *        is opened for reading, and we try writting in it.
       *        In this case, we return ERR_FSAL_NOT_OPENED,
       *        but it doesn't seems to be a correct error translation.
       */

      return ERR_FSAL_NOT_OPENED;

    case ENOMEM:
      return ERR_FSAL_NOMEM;

    case EACCES:
      return ERR_FSAL_ACCESS;

    case EFAULT:
      return ERR_FSAL_FAULT;

    case EEXIST:
      return ERR_FSAL_EXIST;

    case EXDEV:
      return ERR_FSAL_XDEV;

    case ENOTDIR:
      return ERR_FSAL_NOTDIR;

    case EISDIR:
      return ERR_FSAL_ISDIR;

    case EINVAL:
      return ERR_FSAL_INVAL;

    case EFBIG:
      return ERR_FSAL_FBIG;

    case ENOSPC:
      return ERR_FSAL_NOSPC;

    case EMLINK:
      return ERR_FSAL_MLINK;

    case EDQUOT:
      return ERR_FSAL_DQUOT;

    case ESRCH: /* Returned by quotaclt */
      return ERR_FSAL_NO_QUOTA ;

    case ENAMETOOLONG:
      return ERR_FSAL_NAMETOOLONG;

/**
 * @warning
 * AIX returns EEXIST where BSD uses ENOTEMPTY;
 * We want ENOTEMPTY to be interpreted anyway on AIX plateforms.
 * Thus, we explicitely write its value (87).
 */
#ifdef _AIX
    case 87:
#else
    case ENOTEMPTY:
    case -ENOTEMPTY:
#endif
      return ERR_FSAL_NOTEMPTY;

    case ESTALE:
      return ERR_FSAL_STALE;

      /* Error code that needs a retry */
    case EAGAIN:
    case EBUSY:

      return ERR_FSAL_DELAY;

    case ENOTSUP:
      return ERR_FSAL_NOTSUPP;

    default:

      /* other unexpected errors */
      return ERR_FSAL_SERVERFAULT;

    }

}

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
 * fsal2posix_openflags:
 * Convert FSAL open flags to Posix open flags.
 *
 * \param fsal_flags (input):
 *        The FSAL open flags to be translated.
 * \param p_hpss_flags (output):
 *        Pointer to the POSIX open flags.
 *
 * \return - ERR_FSAL_NO_ERROR (no error).
 *         - ERR_FSAL_FAULT    (p_hpss_flags is a NULL pointer).
 *         - ERR_FSAL_INVAL    (invalid or incompatible input flags).
 */
int fsal2posix_openflags(fsal_openflags_t fsal_flags, int *p_posix_flags)
{
  int cpt;

  if(!p_posix_flags)
    return ERR_FSAL_FAULT;

  /* check that all used flags exist */

  if(fsal_flags &
     ~(FSAL_O_RDONLY | FSAL_O_RDWR | FSAL_O_WRONLY | FSAL_O_APPEND | FSAL_O_TRUNC))
    return ERR_FSAL_INVAL;

  /* Check for flags compatibility */

  /* O_RDONLY O_WRONLY O_RDWR cannot be used together */

  cpt = 0;
  if(fsal_flags & FSAL_O_RDONLY)
    cpt++;
  if(fsal_flags & FSAL_O_RDWR)
    cpt++;
  if(fsal_flags & FSAL_O_WRONLY)
    cpt++;

  if(cpt > 1)
    return ERR_FSAL_INVAL;

  /* FSAL_O_APPEND et FSAL_O_TRUNC cannot be used together */

  if((fsal_flags & FSAL_O_APPEND) && (fsal_flags & FSAL_O_TRUNC))
    return ERR_FSAL_INVAL;

  /* FSAL_O_TRUNC without FSAL_O_WRONLY or FSAL_O_RDWR */

  if((fsal_flags & FSAL_O_TRUNC) && !(fsal_flags & (FSAL_O_WRONLY | FSAL_O_RDWR)))
    return ERR_FSAL_INVAL;

  /* conversion */
  *p_posix_flags = 0;

  if(fsal_flags & FSAL_O_RDONLY)
    *p_posix_flags |= O_RDONLY;
  if(fsal_flags & FSAL_O_WRONLY)
    *p_posix_flags |= O_WRONLY;
  if(fsal_flags & FSAL_O_RDWR)
    *p_posix_flags |= O_RDWR;

  return ERR_FSAL_NO_ERROR;

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
      DisplayLogJdLevel(fsal_log, NIV_EVENT, "Unknown object type: %d", posix_type_in);
      return -1;
    }

}

fsal_time_t posix2fsal_time(time_t tsec)
{
  fsal_time_t fsaltime;

  fsaltime.seconds = (fsal_uint_t) tsec;
  fsaltime.nseconds = 0;

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

fsal_status_t posix2fsal_attributes(struct stat * p_buffstat,
                                    fsal_attrib_list_t * p_fsalattr_out)
{

  fsal_attrib_mask_t supp_attr, unsupp_attr;

  /* sanity checks */
  if(!p_buffstat || !p_fsalattr_out)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* check that asked attributes are supported */
  supp_attr = global_fs_info.supported_attrs;

  unsupp_attr = (p_fsalattr_out->asked_attributes) & (~supp_attr);
  if(unsupp_attr)
    {
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "Unsupported attributes: %#llX",
                        unsupp_attr);
      ReturnCode(ERR_FSAL_ATTRNOTSUPP, 0);
    }

  /* Fills the output struct */
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SUPPATTR))
    {
      p_fsalattr_out->supported_attributes = supp_attr;
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_TYPE))
    {
      p_fsalattr_out->type = posix2fsal_type(p_buffstat->st_mode);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SIZE))
    {
      p_fsalattr_out->filesize = p_buffstat->st_size;
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_FSID))
    {
      p_fsalattr_out->fsid = posix2fsal_fsid(p_buffstat->st_dev);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_ACL))
    {

      /* XXX : manage ACL */
      int i;
      for(i = 0; i < FSAL_MAX_ACL; i++)
        {
          p_fsalattr_out->acls[i].type = FSAL_ACL_EMPTY;        /* empty ACL slot */
        }

    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_FILEID))
    {
      p_fsalattr_out->fileid = (fsal_u64_t) (p_buffstat->st_ino);
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_MODE))
    {
      p_fsalattr_out->mode = unix2fsal_mode(p_buffstat->st_mode);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_NUMLINKS))
    {
      p_fsalattr_out->numlinks = p_buffstat->st_nlink;
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_OWNER))
    {
      p_fsalattr_out->owner = p_buffstat->st_uid;
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_GROUP))
    {
      p_fsalattr_out->group = p_buffstat->st_gid;
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_ATIME))
    {
      p_fsalattr_out->atime = posix2fsal_time(p_buffstat->st_atime);

    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CTIME))
    {
      p_fsalattr_out->ctime = posix2fsal_time(p_buffstat->st_ctime);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_MTIME))
    {
      p_fsalattr_out->mtime = posix2fsal_time(p_buffstat->st_mtime);
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CHGTIME))
    {
      p_fsalattr_out->chgtime
          = posix2fsal_time(MAX_2(p_buffstat->st_mtime, p_buffstat->st_ctime));
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SPACEUSED))
    {
      p_fsalattr_out->spaceused = p_buffstat->st_blocks * S_BLKSIZE;
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_RAWDEV))
    {
      p_fsalattr_out->rawdev = posix2fsal_devt(p_buffstat->st_rdev);    /* XXX: convert ? */
    }
  /* mounted_on_fileid :
     if ( FSAL_TEST_MASK(p_fsalattr_out->asked_attributes,
     FSAL_ATTR_MOUNTFILEID )){
     p_fsalattr_out->mounted_on_fileid = 
     hpss2fsal_64( p_hpss_attr_in->FilesetRootId );
     }
   */

  /* everything has been copied ! */

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
