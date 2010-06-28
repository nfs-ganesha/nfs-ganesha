/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_convert.c
 * \author  $Author: leibovic $
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

fsal_status_t fsal2fs_attribset( fsal_handle_t  * p_fsal_handle,
                                 fsal_attrib_list_t  * p_attrib_set ,
                                <depends on your fs way of setting attributes> );

fsal_time_t fs2fsal_time( <your fs time structure> );

<your fs time structure> fsal2fs_time(fsal_time_t in_time);
     
*/

/* THOSE FUNCTIONS CAN BE USED FROM OUTSIDE THE MODULE : */

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

fsal_fsid_t posix2fsal_fsid(dev_t posix_devid)
{

  fsal_fsid_t fsid;

  fsid.major = (fsal_u64_t) posix_devid;
  fsid.minor = 0;

  return fsid;

}

fsal_time_t posix2fsal_time(time_t tsec)
{
  fsal_time_t fsaltime;

  fsaltime.seconds = (fsal_uint_t) tsec;
  fsaltime.nseconds = 0;

  return fsaltime;
}

fsal_dev_t posix2fsal_devt(dev_t posix_devid)
{

  fsal_dev_t dev;

  dev.major = posix_devid >> 8;
  dev.minor = posix_devid & 0xFF;

  return dev;
}

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

fsal_status_t posix2fsal_attributes(struct stat *p_buffstat,
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
