/**
 *
 * \file    fsal_convertions.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:37:24 $
 * \version $Revision: 1.3 $
 * \brief   GHOSTFS-FSAL type converting functions.
 *
 */

/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal_internal.h"
#include "fsal_convertions.h"
#include <sys/stat.h>

/* converts GHOST_FS type to FSAL type */
fsal_nodetype_t ghost2fsal_type(GHOSTFS_typeitem_t type)
{

  switch (type)
    {
    case GHOSTFS_DIR:
      return FSAL_TYPE_DIR;
      break;
    case GHOSTFS_FILE:
      return FSAL_TYPE_FILE;
      break;
    case GHOSTFS_LNK:
      return FSAL_TYPE_LNK;
      break;
    }

}

/* converts GHOST_FS mode to FSAL mode */
fsal_accessmode_t ghost2fsal_mode(GHOSTFS_perm_t mode)
{

  fsal_accessmode_t out_mode = 0;

  if((mode & GHOSTFS_UR))
    out_mode |= FSAL_MODE_RUSR;
  if((mode & GHOSTFS_UW))
    out_mode |= FSAL_MODE_WUSR;
  if((mode & GHOSTFS_UX))
    out_mode |= FSAL_MODE_XUSR;
  if((mode & GHOSTFS_GR))
    out_mode |= FSAL_MODE_RGRP;
  if((mode & GHOSTFS_GW))
    out_mode |= FSAL_MODE_WGRP;
  if((mode & GHOSTFS_GX))
    out_mode |= FSAL_MODE_XGRP;
  if((mode & GHOSTFS_OR))
    out_mode |= FSAL_MODE_ROTH;
  if((mode & GHOSTFS_OW))
    out_mode |= FSAL_MODE_WOTH;
  if((mode & GHOSTFS_OX))
    out_mode |= FSAL_MODE_XOTH;

  return out_mode;

}

/* converts GHOST_FS mode to FSAL mode */
GHOSTFS_perm_t fsal2ghost_mode(fsal_accessmode_t mode)
{

  GHOSTFS_perm_t out_mode = 0;

  if((mode & FSAL_MODE_RUSR))
    out_mode |= GHOSTFS_UR;
  if((mode & FSAL_MODE_WUSR))
    out_mode |= GHOSTFS_UW;
  if((mode & FSAL_MODE_XUSR))
    out_mode |= GHOSTFS_UX;
  if((mode & FSAL_MODE_RGRP))
    out_mode |= GHOSTFS_GR;
  if((mode & FSAL_MODE_WGRP))
    out_mode |= GHOSTFS_GW;
  if((mode & FSAL_MODE_XGRP))
    out_mode |= GHOSTFS_GX;
  if((mode & FSAL_MODE_ROTH))
    out_mode |= GHOSTFS_OR;
  if((mode & FSAL_MODE_WOTH))
    out_mode |= GHOSTFS_OW;
  if((mode & FSAL_MODE_XOTH))
    out_mode |= GHOSTFS_OX;

  return out_mode;

}

/*
#define GHOSTFS_TEST_READ    4
#define GHOSTFS_TEST_WRITE   2
#define GHOSTFS_TEST_EXEC    1

#define	FSAL_R_OK	4
#define	FSAL_W_OK	2
#define	FSAL_X_OK	1
#define	FSAL_F_OK	0
*/

/** converts an FSAL permission test to a GHOSTFS permission test. */
GHOSTFS_testperm_t fsal2ghost_testperm(fsal_accessflags_t testperm)
{

  GHOSTFS_testperm_t outtest = 0;
  if(testperm & FSAL_R_OK)
    outtest |= GHOSTFS_TEST_READ;
  if(testperm & FSAL_W_OK)
    outtest |= GHOSTFS_TEST_WRITE;
  if(testperm & FSAL_X_OK)
    outtest |= GHOSTFS_TEST_EXEC;

  return outtest;

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

/* convert a gost fs error code to an FSAL error code */
int ghost2fsal_error(int code)
{
  switch (code)
    {
    case ERR_GHOSTFS_NO_ERROR:
      return ERR_FSAL_NO_ERROR;
    case ERR_GHOSTFS_NOENT:
      return ERR_FSAL_NOENT;
    case ERR_GHOSTFS_STALE:
      return ERR_FSAL_STALE;
    case ERR_GHOSTFS_NOTINIT:
      return ERR_FSAL_NOT_INIT;
    case ERR_GHOSTFS_NOTDIR:
      return ERR_FSAL_NOTDIR;
    case ERR_GHOSTFS_ISDIR:
      return ERR_FSAL_ISDIR;
    case ERR_GHOSTFS_EXIST:
      return ERR_FSAL_EXIST;
    case ERR_GHOSTFS_NOTEMPTY:
      return ERR_FSAL_NOTEMPTY;

    case ERR_GHOSTFS_ACCES:
      return ERR_FSAL_ACCESS;
    case ERR_GHOSTFS_NOTLNK:
      return ERR_FSAL_INVAL;
    case ERR_GHOSTFS_TOOSMALL:
      return ERR_FSAL_TOOSMALL;
    case ERR_GHOSTFS_MALLOC:
      return ERR_FSAL_NOMEM;
    case ERR_GHOSTFS_NOTOPENED:
      return ERR_FSAL_NOT_OPENED;
    case ERR_GHOSTFS_ATTR_NOT_SUPP:
      return ERR_FSAL_ATTRNOTSUPP;
    case ERR_GHOSTFS_ARGS:
      return ERR_FSAL_INVAL;

    case ERR_GHOSTFS_CORRUPT:
    case ERR_GHOSTFS_INTERNAL:
    default:
      return ERR_FSAL_SERVERFAULT;
    }
}

/* convert ghostfs attributes to FSAL attributes */
int ghost2fsal_attrs(fsal_attrib_list_t * p_fsal_attrs, GHOSTFS_Attrs_t * p_ghost_attrs)
{
  /* Fills the output struct */
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_SUPPATTR))
    {
      p_fsal_attrs->supported_attributes = GHOSTFS_SUPPORTED_ATTRIBUTES;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_TYPE))
    {
      p_fsal_attrs->type = ghost2fsal_type(p_ghost_attrs->type);
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_SIZE))
    {
      p_fsal_attrs->filesize = p_ghost_attrs->size;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_FSID))
    {
      /* constant FSID for ghostFS */
      p_fsal_attrs->fsid.major = 1;
      p_fsal_attrs->fsid.minor = 1;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_FILEID))
    {
      p_fsal_attrs->fileid = (unsigned int)p_ghost_attrs->inode;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_MODE))
    {
      p_fsal_attrs->mode = ghost2fsal_mode(p_ghost_attrs->mode);
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_NUMLINKS))
    {
      p_fsal_attrs->numlinks = p_ghost_attrs->linkcount;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_OWNER))
    {
      p_fsal_attrs->owner = (fsal_uid_t) p_ghost_attrs->uid;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_GROUP))
    {
      p_fsal_attrs->group = (fsal_gid_t) p_ghost_attrs->gid;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_ATIME))
    {
      p_fsal_attrs->atime.seconds = p_ghost_attrs->atime;
      p_fsal_attrs->atime.nseconds = 0;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_CTIME))
    {
      p_fsal_attrs->ctime.seconds = p_ghost_attrs->ctime;
      p_fsal_attrs->ctime.nseconds = 0;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_MTIME))
    {
      p_fsal_attrs->mtime.seconds = p_ghost_attrs->mtime;
      p_fsal_attrs->mtime.nseconds = 0;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_CREATION))
    {
      p_fsal_attrs->creation.seconds = p_ghost_attrs->creationTime;
      p_fsal_attrs->creation.nseconds = 0;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_SPACEUSED))
    {
      p_fsal_attrs->spaceused = p_ghost_attrs->size;
    }
  if(FSAL_TEST_MASK(p_fsal_attrs->asked_attributes, FSAL_ATTR_CHGTIME))
    {
      p_fsal_attrs->chgtime.seconds = p_ghost_attrs->ctime;
      p_fsal_attrs->chgtime.nseconds = 0;
    }

  return 0;

}
