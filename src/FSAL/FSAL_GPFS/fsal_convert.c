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
#include "nfs4_acls.h"
#include "gpfs.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/resource.h>

#define MAX_2( x, y )    ( (x) > (y) ? (x) : (y) )
#define MAX_3( x, y, z ) ( (x) > (y) ? MAX_2((x),(z)) : MAX_2((y),(z)) )
extern uint32_t open_fd_count;

#ifdef _USE_NFS4_ACL
static int gpfs_acl_2_fsal_acl(fsal_attrib_list_t * p_object_attributes,
                               gpfs_acl_t *p_gpfsacl);
#endif                          /* _USE_NFS4_ACL */

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
  struct rlimit rlim = {
    .rlim_cur = RLIM_INFINITY,
    .rlim_max = RLIM_INFINITY
  };

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
      if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
         LogInfo(COMPONENT_FSAL, "Mapping %d to ERR_FSAL_IO, open_fd_count=%d getrlimit failed",
                        posix_errorcode, open_fd_count);
      }
      else {
         LogInfo(COMPONENT_FSAL, "Mapping %d to ERR_FSAL_IO, open_fd_count=%d rlim_cur=%ld rlim_max=%ld",
                        posix_errorcode, open_fd_count, rlim.rlim_cur, rlim.rlim_max);
      }
      return ERR_FSAL_IO;

      /* no such device */
    case ENODEV:
    case ENXIO:
      LogInfo(COMPONENT_FSAL, "Mapping %d to ERR_FSAL_NXIO", posix_errorcode);
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
    case ENOLCK:
      LogInfo(COMPONENT_FSAL, "Mapping %d to ERR_FSAL_NOMEM", posix_errorcode);
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

    case EROFS:
      return ERR_FSAL_ROFS;

    case ETXTBSY:
      return ERR_FSAL_SHARE_DENIED;

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
      LogInfo(COMPONENT_FSAL, "Mapping %d to ERR_FSAL_DELAY", posix_errorcode);
      return ERR_FSAL_DELAY;

    case ENOTSUP:
      return ERR_FSAL_NOTSUPP;

    case EOVERFLOW:
      return ERR_FSAL_OVERFLOW;

    case EDEADLK:
      return ERR_FSAL_DEADLOCK;

    case EINTR:
      return ERR_FSAL_INTERRUPT;

    case EUNATCH:
      LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
      return ERR_FSAL_SERVERFAULT;

    default:
      LogCrit(COMPONENT_FSAL, "Mapping %d(default) to ERR_FSAL_SERVERFAULT",
                        posix_errorcode);
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
     ~(FSAL_O_RDONLY | FSAL_O_RDWR | FSAL_O_WRONLY | FSAL_O_APPEND |
       FSAL_O_TRUNC | FSAL_O_SYNC))
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
  if(fsal_flags & FSAL_O_SYNC)
    *p_posix_flags |= O_SYNC;

  return ERR_FSAL_NO_ERROR;

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
      LogFullDebug(COMPONENT_FSAL, "Unsupported attributes: %#llX",
                        unsupp_attr);
      ReturnCode(ERR_FSAL_ATTRNOTSUPP, 0);
    }

  /* Initialize ACL regardless of whether ACL was asked or not.
   * This is needed to make sure ACL attribute is initialized. */
  p_fsalattr_out->acl = NULL;

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
      p_fsalattr_out->acl = NULL;
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
      p_fsalattr_out->atime = posix2fsal_time(p_buffstat->st_atime, p_buffstat->st_atim.tv_nsec);
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CTIME))
    {
      p_fsalattr_out->ctime = posix2fsal_time(p_buffstat->st_ctime, p_buffstat->st_ctim.tv_nsec);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_MTIME))
    {
      p_fsalattr_out->mtime = posix2fsal_time(p_buffstat->st_mtime, p_buffstat->st_mtim.tv_nsec);
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CHGTIME))
    {
      if(p_buffstat->st_mtime == p_buffstat->st_ctime)
        {
          if(p_buffstat->st_mtim.tv_nsec > p_buffstat->st_ctim.tv_nsec)
            p_fsalattr_out->chgtime
              = posix2fsal_time(p_buffstat->st_mtime, p_buffstat->st_mtim.tv_nsec);
          else
            p_fsalattr_out->chgtime
              = posix2fsal_time(p_buffstat->st_ctime, p_buffstat->st_ctim.tv_nsec);
        }
      else if(p_buffstat->st_mtime > p_buffstat->st_ctime)
        {
          p_fsalattr_out->chgtime
            = posix2fsal_time(p_buffstat->st_mtime, p_buffstat->st_mtim.tv_nsec);
        }
      else
        {
          p_fsalattr_out->chgtime
            = posix2fsal_time(p_buffstat->st_ctime, p_buffstat->st_ctim.tv_nsec);
        }
      p_fsalattr_out->change = (uint64_t) p_fsalattr_out->chgtime.seconds +
                               (uint64_t) p_fsalattr_out->chgtime.nseconds;
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

/* Same function as posixstat64_2_fsal_attributes. When NFS4 ACL support
 * is enabled, this will replace posixstat64_2_fsal_attributes. */
fsal_status_t gpfsfsal_xstat_2_fsal_attributes(gpfsfsal_xstat_t *p_buffxstat,
                                               fsal_attrib_list_t *p_fsalattr_out)
{

    fsal_attrib_mask_t supp_attr, unsupp_attr;
    struct stat *p_buffstat;

    /* sanity checks */
    if(!p_buffxstat || !p_fsalattr_out)
        ReturnCode(ERR_FSAL_FAULT, 0);

    /* check that asked attributes are supported */
    supp_attr = global_fs_info.supported_attrs;

    unsupp_attr = (p_fsalattr_out->asked_attributes) & (~supp_attr);
    if(unsupp_attr)
        {
            LogFullDebug(COMPONENT_FSAL, "Unsupported attributes: %#llX",
                         unsupp_attr);
            ReturnCode(ERR_FSAL_ATTRNOTSUPP, 0);
        }

    p_buffstat = &p_buffxstat->buffstat;

    /* Fills the output struct */
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SUPPATTR))
        {
            p_fsalattr_out->supported_attributes = supp_attr;
            LogFullDebug(COMPONENT_FSAL, "supported_attributes = %llu", p_fsalattr_out->supported_attributes);
        }
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_TYPE))
        {
            p_fsalattr_out->type = posix2fsal_type(p_buffstat->st_mode);
            LogFullDebug(COMPONENT_FSAL, "type = 0x%x", p_fsalattr_out->type);
        }
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SIZE))
        {
            p_fsalattr_out->filesize = p_buffstat->st_size;
            LogFullDebug(COMPONENT_FSAL, "filesize = %llu",
                         (unsigned long long)p_fsalattr_out->filesize);
        }
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_FSID))
        {
            p_fsalattr_out->fsid = posix2fsal_fsid(p_buffstat->st_dev);
            LogFullDebug(COMPONENT_FSAL, "fsid major = %llu, minor = %llu", p_fsalattr_out->fsid.major, p_fsalattr_out->fsid.minor);
        }
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_ACL))
        {
#ifndef _USE_NFS4_ACL
            p_fsalattr_out->acl = NULL;
#else
            if(p_buffxstat->attr_valid & XATTR_ACL)
              {
                /* ACL is valid, so try to convert fsal acl. */
                if(gpfs_acl_2_fsal_acl(p_fsalattr_out,
                   (gpfs_acl_t *)p_buffxstat->buffacl) != ERR_FSAL_NO_ERROR)
                  p_fsalattr_out->acl = NULL;
              } else
                  p_fsalattr_out->acl = NULL;
#endif                          /* _USE_NFS4_ACL */
            LogFullDebug(COMPONENT_FSAL, "acl = %p", p_fsalattr_out->acl);
        }
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_FILEID))
        {
            p_fsalattr_out->fileid = (fsal_u64_t) (p_buffstat->st_ino);
            LogFullDebug(COMPONENT_FSAL, "fileid = %llu", p_fsalattr_out->fileid);
        }

    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_MODE))
        {
            p_fsalattr_out->mode = unix2fsal_mode(p_buffstat->st_mode);
            LogFullDebug(COMPONENT_FSAL, "mode = %llu", (long long unsigned int) p_fsalattr_out->mode);
        }
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_NUMLINKS))
        {
            p_fsalattr_out->numlinks = p_buffstat->st_nlink;
            LogFullDebug(COMPONENT_FSAL, "numlinks = %lu", p_fsalattr_out->numlinks);
        }
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_OWNER))
        {
            p_fsalattr_out->owner = p_buffstat->st_uid;
            LogFullDebug(COMPONENT_FSAL, "owner = %u", p_fsalattr_out->owner);
        }
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_GROUP))
        {
            p_fsalattr_out->group = p_buffstat->st_gid;
            LogFullDebug(COMPONENT_FSAL, "group = %u", p_fsalattr_out->group);
        }
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_ATIME))
        {
          p_fsalattr_out->atime = posix2fsal_time(p_buffstat->st_atime, p_buffstat->st_atim.tv_nsec);
            LogFullDebug(COMPONENT_FSAL, "atime = %u", p_fsalattr_out->atime.seconds);
        }

    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CTIME))
        {
          p_fsalattr_out->ctime = posix2fsal_time(p_buffstat->st_ctime, p_buffstat->st_ctim.tv_nsec);
            LogFullDebug(COMPONENT_FSAL, "ctime = %u", p_fsalattr_out->ctime.seconds);
        }
    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_MTIME))
        {
          p_fsalattr_out->mtime = posix2fsal_time(p_buffstat->st_mtime, p_buffstat->st_mtim.tv_nsec);
            LogFullDebug(COMPONENT_FSAL, "mtime = %u", p_fsalattr_out->mtime.seconds);
        }

    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CHGTIME))
        {
          if(p_buffstat->st_mtime == p_buffstat->st_ctime)
            {
              if(p_buffstat->st_mtim.tv_nsec > p_buffstat->st_ctim.tv_nsec)
                p_fsalattr_out->chgtime
                  = posix2fsal_time(p_buffstat->st_mtime, p_buffstat->st_mtim.tv_nsec);
              else
                p_fsalattr_out->chgtime
                  = posix2fsal_time(p_buffstat->st_ctime, p_buffstat->st_ctim.tv_nsec);
            }
          else if(p_buffstat->st_mtime > p_buffstat->st_ctime)
            {
              p_fsalattr_out->chgtime
                = posix2fsal_time(p_buffstat->st_mtime, p_buffstat->st_mtim.tv_nsec);
            }
          else
            {
              p_fsalattr_out->chgtime
                = posix2fsal_time(p_buffstat->st_ctime, p_buffstat->st_ctim.tv_nsec);
            }
          p_fsalattr_out->change = (uint64_t) p_fsalattr_out->chgtime.seconds +
                                   (uint64_t) p_fsalattr_out->chgtime.nseconds;
          LogFullDebug(COMPONENT_FSAL, "chgtime = %u", p_fsalattr_out->chgtime.seconds);

        }

    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SPACEUSED))
        {
            p_fsalattr_out->spaceused = p_buffstat->st_blocks * S_BLKSIZE;
            LogFullDebug(COMPONENT_FSAL, "spaceused = %llu",
                         (unsigned long long)p_fsalattr_out->spaceused);
        }

    if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_RAWDEV))
        {
            p_fsalattr_out->rawdev = posix2fsal_devt(p_buffstat->st_rdev);    /* XXX: convert ? */
            LogFullDebug(COMPONENT_FSAL,
                         "rawdev major = %u, minor = %u",
                         (unsigned int) p_fsalattr_out->rawdev.major,
                         (unsigned int) p_fsalattr_out->rawdev.minor);
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

#ifdef _USE_NFS4_ACL
/* Covert GPFS NFS4 ACLs to FSAL ACLs, and set the ACL
 * pointer of attribute. */
static int gpfs_acl_2_fsal_acl(fsal_attrib_list_t * p_object_attributes,
	                           gpfs_acl_t *p_gpfsacl)
{
  fsal_acl_status_t status;
  fsal_acl_data_t acldata;
  fsal_ace_t *pace;
  fsal_acl_t *pacl;
  gpfs_ace_v4_t *pace_gpfs;

  /* sanity checks */
  if(!p_object_attributes || !p_gpfsacl)
    return ERR_FSAL_FAULT;

  /* Create fsal acl data. */
  acldata.naces = p_gpfsacl->acl_nace;
  acldata.aces = (fsal_ace_t *)nfs4_ace_alloc(acldata.naces);

  /* Fill fsal acl data from gpfs acl. */
  for(pace = acldata.aces, pace_gpfs = p_gpfsacl->ace_v4;
      pace < acldata.aces + acldata.naces; pace++, pace_gpfs++)
    {
      pace->type = pace_gpfs->aceType;
      pace->flag = pace_gpfs->aceFlags;
      pace->iflag = pace_gpfs->aceIFlags;
      pace->perm = pace_gpfs->aceMask;

      if(IS_FSAL_ACE_SPECIAL_ID(*pace))  /* Record special user. */
        {
          pace->who.uid = pace_gpfs->aceWho;
        }
        else
        {
          if(IS_FSAL_ACE_GROUP_ID(*pace))  /* Record group. */
            pace->who.gid = pace_gpfs->aceWho;
          else  /* Record user. */
            pace->who.uid = pace_gpfs->aceWho;
        }

        LogDebug(COMPONENT_FSAL,
                 "gpfs_acl_2_fsal_acl: fsal ace: type = 0x%x, flag = 0x%x, perm = 0x%x, special = %d, %s = 0x%x",
                 pace->type, pace->flag, pace->perm, IS_FSAL_ACE_SPECIAL_ID(*pace),
                 GET_FSAL_ACE_WHO_TYPE(*pace), GET_FSAL_ACE_WHO(*pace));
    }

  /* Create a new hash table entry for fsal acl. */
  pacl = nfs4_acl_new_entry(&acldata, &status);
  LogDebug(COMPONENT_FSAL, "fsal acl = %p, fsal_acl_status = %u", pacl, status);

  if(pacl == NULL)
    {
      LogCrit(COMPONENT_FSAL, "gpfs_acl_2_fsal_acl: failed to create a new acl entry");
      return ERR_FSAL_FAULT;
    }

  /* Add fsal acl to attribute. */
  p_object_attributes->acl = pacl;

  return ERR_FSAL_NO_ERROR;
}

/* Covert FSAL ACLs to GPFS NFS4 ACLs. */
fsal_status_t fsal_acl_2_gpfs_acl(fsal_acl_t *p_fsalacl, gpfsfsal_xstat_t *p_buffxstat)
{
  int i;
  fsal_ace_t *pace;
  gpfs_acl_t *p_gpfsacl;

  p_gpfsacl = (gpfs_acl_t *) p_buffxstat->buffacl;

  p_gpfsacl->acl_level   =  0;
  p_gpfsacl->acl_version =  GPFS_ACL_VERSION_NFS4;
  p_gpfsacl->acl_type    =  GPFS_ACL_TYPE_NFS4;
  p_gpfsacl->acl_nace = p_fsalacl->naces;
  p_gpfsacl->acl_len = ((int)(signed long)&(((gpfs_acl_t *) 0)->ace_v1)) + p_gpfsacl->acl_nace * sizeof(gpfs_ace_v4_t);

  for(pace = p_fsalacl->aces, i = 0; pace < p_fsalacl->aces + p_fsalacl->naces; pace++, i++)
    {
      p_gpfsacl->ace_v4[i].aceType = pace->type;
      p_gpfsacl->ace_v4[i].aceFlags = pace->flag;
      p_gpfsacl->ace_v4[i].aceIFlags = pace->iflag;
      p_gpfsacl->ace_v4[i].aceMask = pace->perm;

      if(IS_FSAL_ACE_SPECIAL_ID(*pace))
        p_gpfsacl->ace_v4[i].aceWho = pace->who.uid;
      else
        {
          if(IS_FSAL_ACE_GROUP_ID(*pace))
            p_gpfsacl->ace_v4[i].aceWho = pace->who.gid;
          else
            p_gpfsacl->ace_v4[i].aceWho = pace->who.uid;
        }

      LogDebug(COMPONENT_FSAL, "fsal_acl_2_gpfs_acl: gpfs ace: type = 0x%x, flag = 0x%x, perm = 0x%x, special = %d, %s = 0x%x",
               p_gpfsacl->ace_v4[i].aceType, p_gpfsacl->ace_v4[i].aceFlags, p_gpfsacl->ace_v4[i].aceMask,
               (p_gpfsacl->ace_v4[i].aceIFlags & FSAL_ACE_IFLAG_SPECIAL_ID) ? 1 : 0,
               (p_gpfsacl->ace_v4[i].aceFlags & FSAL_ACE_FLAG_GROUP_ID) ? "gid" : "uid",
               p_gpfsacl->ace_v4[i].aceWho);

    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
#endif                          /* _USE_NFS4_ACL */
