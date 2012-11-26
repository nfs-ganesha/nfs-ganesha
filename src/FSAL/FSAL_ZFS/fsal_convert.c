/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_convert.c
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.32 $
 * \brief   FS-FSAL type translation functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "fsal_convert.h"
#include "fsal_internal.h"
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_2( x, y )    ( (x) > (y) ? (x) : (y) )

/* some ideas of conversion functions...

int fs2fsal_error(int fs_errorcode);

int fsal2fs_openflags( fsal_openflags_t fsal_flags, int * p_fs_flags );

int fsal2fs_testperm(fsal_accessflags_t testperm);

fsal_status_t fs2fsal_attributes(  <your fs attribute structure (input)>,
                                   fsal_attrib_list_t * p_fsalattr_out );

fsal_accessmode_t fs2fsal_mode( <your fs object permission (input)> );

void fsal2fs_mode( fsal_accessmode_t fsal_mode, <your fs mode type (output)> );

fsal_nodetype_t  hpss2fsal_type( <your fs object type (input)> );

fsal_u64_t fs2fsal_64( <your fs 64bits type> );

<your fs 64bits type> fsal2hpss_64( fsal_u64_t fsal_size_in );

fsal_fsid_t fs2fsal_fsid( <you fs fsid type> );

fsal_status_t fsal2fs_attribset( zfsfsal_handle_t  * p_fsal_handle,
                                 fsal_attrib_list_t  * p_attrib_set ,
                                <depends on your fs way of setting attributes> );

fsal_time_t fs2fsal_time( <your fs time structure> );

<your fs time structure> fsal2fs_time(fsal_time_t in_time);
     
*/

/* THOSE FUNCTIONS CAN BE USED FROM OUTSIDE THE MODULE : */

/**
 * fsal2posix_openflags:
 * Convert FSAL open flags to Posix open flags.
 *
 * \param fsal_flags (input):
 *        The FSAL open flags to be translated.
 * \param p_posix_flags (output):
 *        Pointer to the POSIX open flags.
 *
 * \return - ERR_FSAL_NO_ERROR (no error).
 *         - ERR_FSAL_FAULT    (p_hpss_flags is a NULL pointer).
 *         - ERR_FSAL_INVAL    (invalid or incompatible input flags).
 */
int fsal2posix_openflags(fsal_openflags_t fsal_flags, int *p_posix_flags)
{
  if(!p_posix_flags)
    return ERR_FSAL_FAULT;

  /* check that all used flags exist */

  if(fsal_flags &
     ~(FSAL_O_READ | FSAL_O_RDWR | FSAL_O_WRITE | FSAL_O_SYNC))
    return ERR_FSAL_INVAL;

  /* Check for flags compatibility */

  /* O_RDONLY O_WRONLY O_RDWR cannot be used together */

  /* conversion */
  *p_posix_flags = 0;

  if(fsal_flags & FSAL_O_READ)
    *p_posix_flags |= O_RDONLY;

  if(fsal_flags & FSAL_O_RDWR)
    *p_posix_flags |= O_RDWR;

  if(fsal_flags & FSAL_O_WRITE)
    *p_posix_flags |= O_WRONLY;

  if(fsal_flags & FSAL_O_SYNC)
    *p_posix_flags |= O_SYNC;

  return ERR_FSAL_NO_ERROR;
}

int posix2fsal_error(int posix_errorcode)
{

  switch (posix_errorcode)
    {
    case 0:
        return ERR_FSAL_NO_ERROR;

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

    case ESRCH:                /* Returned by quotaclt */
      return ERR_FSAL_NO_QUOTA;

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

fsal_status_t posix2fsal_attributes(const struct stat *buffstat,
                                    struct attrlist *fsalattr)
{
  FSAL_CLEAR_MASK(fsalattr->mask);
  /* sanity checks */
  if(!buffstat || !fsalattr)
    return fsalstat(ERR_FSAL_FAULT, 0);

  FSAL_CLEAR_MASK(fsalattr->mask);

  /* Fills the output struct */
  fsalattr->type = posix2fsal_type(buffstat->st_mode);
  FSAL_SET_MASK(fsalattr->mask, ATTR_TYPE);

  fsalattr->filesize = buffstat->st_size;
  FSAL_SET_MASK(fsalattr->mask, ATTR_SIZE);

  fsalattr->fsid = posix2fsal_fsid(buffstat->st_dev);
  FSAL_SET_MASK(fsalattr->mask, ATTR_FSID);

  fsalattr->fileid = buffstat->st_ino;
  FSAL_SET_MASK(fsalattr->mask, ATTR_FILEID);

  fsalattr->mode = unix2fsal_mode(buffstat->st_mode);
  FSAL_SET_MASK(fsalattr->mask, ATTR_MODE);

  fsalattr->numlinks = buffstat->st_nlink;
  FSAL_SET_MASK(fsalattr->mask, ATTR_NUMLINKS);

  fsalattr->owner = buffstat->st_uid;
  FSAL_SET_MASK(fsalattr->mask, ATTR_OWNER);

  fsalattr->group = buffstat->st_gid;
  FSAL_SET_MASK(fsalattr->mask, ATTR_GROUP);

  fsalattr->atime = posix2fsal_time(buffstat->st_atime, 0);
  FSAL_SET_MASK(fsalattr->mask, ATTR_ATIME);

  fsalattr->ctime = posix2fsal_time(buffstat->st_ctime, 0);
  FSAL_SET_MASK(fsalattr->mask, ATTR_CTIME);

  fsalattr->mtime = posix2fsal_time(buffstat->st_mtime, 0);
  FSAL_SET_MASK(fsalattr->mask, ATTR_MTIME);

  fsalattr->chgtime
    = posix2fsal_time(MAX_2(buffstat->st_mtime,
                            buffstat->st_ctime), 0);
  fsalattr->change = fsalattr->chgtime.seconds;
  FSAL_SET_MASK(fsalattr->mask, ATTR_CHGTIME);

  fsalattr->spaceused = buffstat->st_blocks * S_BLKSIZE;
  FSAL_SET_MASK(fsalattr->mask, ATTR_SPACEUSED);

  fsalattr->rawdev = posix2fsal_devt(buffstat->st_rdev);
  FSAL_SET_MASK(fsalattr->mask, ATTR_RAWDEV);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}


