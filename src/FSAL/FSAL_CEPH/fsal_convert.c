/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

/**
 *
 * \file    fsal_convert.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 15:53:39 $
 * \version $Revision: 1.31 $
 * \brief   VFS-FSAL type translation functions.
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
  switch (-posix_errorcode)
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
 * fsal2posix_openflags:
 * Convert FSAL open flags to Posix open flags.
 *
 * \param fsal_flags (input):
 *        The FSAL open flags to be translated.
 * \param posix_flags (output):
 *        Pointer to the POSIX open flags.
 *
 * \return - ERR_FSAL_NO_ERROR (no error).
 *         - ERR_FSAL_FAULT    (posix_flags is a NULL pointer).
 *         - ERR_FSAL_INVAL    (invalid or incompatible input flags).
 */
int fsal2posix_openflags(fsal_openflags_t fsal_flags, int *posix_flags)
{
  int cpt;

  if(!posix_flags)
    return ERR_FSAL_FAULT;

  /* check that all used flags exist */

  if(fsal_flags &
     ~(FSAL_O_RDONLY | FSAL_O_RDWR | FSAL_O_WRONLY | FSAL_O_APPEND |
       FSAL_O_TRUNC))
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

  if((fsal_flags & FSAL_O_TRUNC) && !(fsal_flags & (FSAL_O_WRONLY |
                                                    FSAL_O_RDWR)))
    return ERR_FSAL_INVAL;

  /* conversion */
  *posix_flags = 0;

  if(fsal_flags & FSAL_O_RDONLY)
    *posix_flags |= O_RDONLY;
  if(fsal_flags & FSAL_O_WRONLY)
    *posix_flags |= O_WRONLY;
  if(fsal_flags & FSAL_O_RDWR)
    *posix_flags |= O_RDWR;

  return ERR_FSAL_NO_ERROR;
}


fsal_status_t posix2fsal_attributes(struct stat* st,
                                    fsal_attrib_list_t * attrs)
{
  fsal_attrib_mask_t supp_attr, unsupp_attr;

  /* sanity checks */
  if(!st || !attrs)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* check that asked attributes are supported */
  supp_attr = global_fs_info.supported_attrs;

  unsupp_attr = (attrs->asked_attributes) & (~supp_attr);
  if(unsupp_attr)
    {
      LogFullDebug(COMPONENT_FSAL, "Unsupported attributes: %#llX",
                        unsupp_attr);
      ReturnCode(ERR_FSAL_ATTRNOTSUPP, 0);
    }

  /* Initialize ACL regardless of whether ACL was asked or not.
   * This is needed to make sure ACL attribute is initialized. */
  attrs->acl = NULL;

  /* Fills the output struct */
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_SUPPATTR))
    {
      attrs->supported_attributes = supp_attr;
    }
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_TYPE))
    {
      attrs->type = posix2fsal_type(st->st_mode);
    }
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_SIZE))
    {
      attrs->filesize = st->st_size;
    }
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_FSID))
    {
      attrs->fsid = posix2fsal_fsid(st->st_dev);
    }
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_ACL))
    {
      attrs->acl = NULL;
    }
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_FILEID))
    {
      attrs->fileid = (fsal_u64_t) (st->st_ino);
    }

  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_MODE))
    {
      attrs->mode = unix2fsal_mode(st->st_mode);
    }
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_NUMLINKS))
    {
      attrs->numlinks = st->st_nlink;
    }
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_OWNER))
    {
      attrs->owner = st->st_uid;
    }
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_GROUP))
    {
      attrs->group = st->st_gid;
    }
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_ATIME))
    {
      attrs->atime = posix2fsal_time(st->st_atime);

    }

  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_CTIME))
    {
      attrs->ctime = posix2fsal_time(st->st_ctime);
    }
  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_MTIME))
    {
      attrs->mtime = posix2fsal_time(st->st_mtime);
    }

  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_CHGTIME))
    {
      attrs->chgtime
          = posix2fsal_time(MAX_2(st->st_mtime, st->st_ctime));
      attrs->change = (uint64_t) attrs->chgtime.seconds ;
    }

  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_SPACEUSED))
    {
      attrs->spaceused = st->st_blocks * S_BLKSIZE;
    }

  if(FSAL_TEST_MASK(attrs->asked_attributes, FSAL_ATTR_RAWDEV))
    {
      attrs->rawdev = posix2fsal_devt(st->st_rdev);    /* XXX: convert ? */
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

void stat2fsal_fh(struct stat *st, cephfsal_handle_t* fh)
{
  VINODE(fh).ino.val = st->st_ino;
  VINODE(fh).snapid.val = st->st_dev;
}
