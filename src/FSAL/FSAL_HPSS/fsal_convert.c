/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_convert.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.32 $
 * \brief   HPSS-FSAL type translation functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "fsal_convert.h"
#include "fsal_internal.h"
#include <sys/types.h>
#include <errno.h>

#include <hpss_errno.h>

#define MAX_2( x, y )    ( (x) > (y) ? (x) : (y) )
#define MAX_3( x, y, z ) ( (x) > (y) ? MAX_2((x),(z)) : MAX_2((y),(z)) )

/**
 * hpss2fsal_error :
 * Convert HPSS error codes to FSAL error codes.
 *
 * \param hpss_errorcode (input):
 *        The error code returned from HPSS.
 *
 * \return The FSAL error code associated
 *         to hpss_errorcode.
 *
 */
int hpss2fsal_error(int hpss_errorcode)
{

  switch (hpss_errorcode)
    {

    case HPSS_E_NOERROR:
      return ERR_FSAL_NO_ERROR;

    case EPERM:
    case HPSS_EPERM:
      return ERR_FSAL_PERM;

    case ENOENT:
    case HPSS_ENOENT:
      return ERR_FSAL_NOENT;

      /* connection error */
#ifdef _AIX_5
    case ENOCONNECT:
#elif defined _LINUX
    case ECONNREFUSED:
    case ECONNABORTED:
    case ECONNRESET:
#endif
    case HPSS_ECONN:

      /* IO error */
    case EIO:
    case HPSS_EIO:

      /* too many open files */
    case ENFILE:
    case HPSS_ENFILE:
    case EMFILE:
    case HPSS_EMFILE:

      /* broken pipe */
    case EPIPE:
    case HPSS_EPIPE:

      /* all shown as IO errors */
      return ERR_FSAL_IO;

      /* no such device */
    case ENODEV:
    case HPSS_ENODEV:
    case ENXIO:
    case HPSS_ENXIO:
      return ERR_FSAL_NXIO;

      /* invalid file descriptor : */
    case EBADF:
    case HPSS_EBADF:
      /* we suppose it was not opened... */

      /**
       * @todo: The EBADF error also happens when file
       *        is opened for reading, and we try writting in it.
       *        In this case, we return ERR_FSAL_NOT_OPENED,
       *        but it doesn't seems to be a correct error translation.
       */

      return ERR_FSAL_NOT_OPENED;

    case ENOMEM:
    case HPSS_ENOMEM:
      return ERR_FSAL_NOMEM;

    case EACCES:
    case HPSS_EACCES:
      return ERR_FSAL_ACCESS;

    case EFAULT:
    case HPSS_EFAULT:
      return ERR_FSAL_FAULT;

    case EEXIST:
    case HPSS_EEXIST:
      return ERR_FSAL_EXIST;

    case EXDEV:
    case HPSS_EXDEV:
      return ERR_FSAL_XDEV;

    case ENOTDIR:
    case HPSS_ENOTDIR:
      return ERR_FSAL_NOTDIR;

    case EISDIR:
    case HPSS_EISDIR:
      return ERR_FSAL_ISDIR;

    case EINVAL:
    case HPSS_EINVAL:
      return ERR_FSAL_INVAL;

    case EFBIG:
    case HPSS_EFBIG:
      return ERR_FSAL_FBIG;

    case ENOSPC:
    case HPSS_ENOSPACE:
      return ERR_FSAL_NOSPC;

    case EMLINK:
    case HPSS_EMLINK:
      return ERR_FSAL_MLINK;

    case EDQUOT:
    case HPSS_EDQUOT:
      return ERR_FSAL_DQUOT;

    case ENAMETOOLONG:
    case HPSS_ENAMETOOLONG:
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
    case HPSS_ENOTEMPTY:
      return ERR_FSAL_NOTEMPTY;

    case ESTALE:
    case HPSS_ESTALE:
      return ERR_FSAL_STALE;

      /* Error code that needs a retry */
    case EAGAIN:
    case HPSS_EAGAIN:
    case EBUSY:
    case HPSS_EBUSY:

      return ERR_FSAL_DELAY;

    default:

#if HPSS_MAJOR_VERSION == 5

      /* hsec error code regarding security (-3000...) */
      if((hpss_errorcode <= -3000) && (hpss_errorcode > -4000))
        return ERR_FSAL_SEC;

#elif HPSS_MAJOR_VERSION == 6

      /* hsec error code regarding security (-11000...) */
      if((hpss_errorcode <= HPSS_SEC_ENOT_AUTHORIZED)
         && (hpss_errorcode >= HPSS_SEC_LDAP_ERROR))
        return ERR_FSAL_SEC;
#elif HPSS_MAJOR_VERSION == 7

      /* hsec error code regarding security (-11000...) */
      if((hpss_errorcode <= HPSS_SEC_ENOT_AUTHORIZED)
         && (hpss_errorcode >= HPSS_SEC_LDAP_RETRY))
        return ERR_FSAL_SEC;

#endif

      /* other unexpected errors */
      return ERR_FSAL_SERVERFAULT;

    }

}

/**
 * fsal2hpss_testperm:
 * Convert FSAL permission flags to (HPSS) Posix permission flags.
 *
 * \param testperm (input):
 *        The FSAL permission flags to be tested.
 *
 * \return The HPSS permission flags to be tested.
 */
int fsal2hpss_testperm(fsal_accessflags_t testperm)
{

  int hpss_testperm = 0;

  if(testperm & FSAL_R_OK)
    hpss_testperm |= R_OK;
  if(testperm & FSAL_W_OK)
    hpss_testperm |= W_OK;
  if(testperm & FSAL_X_OK)
    hpss_testperm |= X_OK;
  if(testperm & FSAL_F_OK)
    hpss_testperm |= F_OK;

  return hpss_testperm;

}

/**
 * fsal2hpss_openflags:
 * Convert FSAL open flags to (HPSS) Posix open flags.
 *
 * \param fsal_flags (input):
 *        The FSAL open flags to be translated.
 * \param p_hpss_flags (output):
 *        Pointer to the HPSS open flags.
 *
 * \return - ERR_FSAL_NO_ERROR (no error).
 *         - ERR_FSAL_FAULT    (p_hpss_flags is a NULL pointer).
 *         - ERR_FSAL_INVAL    (invalid or incompatible input flags).
 */
int fsal2hpss_openflags(fsal_openflags_t fsal_flags, int *p_hpss_flags)
{
  int cpt;

  if(!p_hpss_flags)
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

  *p_hpss_flags = 0;

  if(fsal_flags & FSAL_O_RDONLY)
    *p_hpss_flags |= O_RDONLY;
  if(fsal_flags & FSAL_O_RDWR)
    *p_hpss_flags |= O_RDWR;
  if(fsal_flags & FSAL_O_WRONLY)
    *p_hpss_flags |= O_WRONLY;
  if(fsal_flags & FSAL_O_APPEND)
    *p_hpss_flags |= O_APPEND;
  if(fsal_flags & FSAL_O_TRUNC)
    *p_hpss_flags |= O_TRUNC;

  return ERR_FSAL_NO_ERROR;

}

/**
 * hpss2fsal_type:
 * Convert HPSS NS object type to FSAL node type.
 *
 * \param hpss_type_in (input):
 *        The HPSS NS object type from NSObjHandle.Type.
 *
 * \return - The FSAL node type associated to hpss_type_in.
 *         - -1 if the input type is unknown.
 */
fsal_nodetype_t hpss2fsal_type(unsigned32 hpss_type_in)
{

  switch (hpss_type_in)
    {

    case NS_OBJECT_TYPE_DIRECTORY:
      return FSAL_TYPE_DIR;

    case NS_OBJECT_TYPE_HARD_LINK:
    case NS_OBJECT_TYPE_FILE:
      return FSAL_TYPE_FILE;

    case NS_OBJECT_TYPE_SYM_LINK:
      return FSAL_TYPE_LNK;

    case NS_OBJECT_TYPE_JUNCTION:
      return FSAL_TYPE_JUNCTION;

    default:
      DisplayLogJdLevel(fsal_log, NIV_EVENT, "Unknown object type: %d", hpss_type_in);
      return -1;
    }

}

/**
 * hpss2fsal_time:
 * Convert HPSS time structure (timestamp_sec_t)
 * to FSAL time type (fsal_time_t).
 */
fsal_time_t hpss2fsal_time(timestamp_sec_t tsec)
{
  fsal_time_t fsaltime;

  fsaltime.seconds = (fsal_uint_t) tsec;
  fsaltime.nseconds = 0;

  return fsaltime;
}

/* fsal2hpss_time is a macro */

/**
 * hpss2fsal_64:
 * Convert HPSS u_signed64 type to fsal_u64_t type.
 *
 * \param hpss_size_in (input):
 *        The HPSS 64 bits number.
 *
 * \return - The FSAL 64 bits number.
 */
fsal_u64_t hpss2fsal_64(u_signed64 hpss_size_in)
{

  long long output_buff;
  CONVERT_U64_TO_LONGLONG(hpss_size_in, output_buff);
  return (fsal_u64_t) (output_buff);

}

/**
 * fsal2hpss_64:
 * Convert fsal_u64_t type to HPSS u_signed64 type.
 *
 * \param fsal_size_in (input):
 *        The FSAL 64 bits number.
 *
 * \return - The HPSS 64 bits number.
 */
u_signed64 fsal2hpss_64(fsal_u64_t fsal_size_in)
{

  u_signed64 output_buff;
  CONVERT_LONGLONG_TO_U64(fsal_size_in, output_buff);

  return output_buff;

}

/**
 * hpss2fsal_fsid:
 * Convert HPSS fsid type to FSAL fsid type.
 *
 * \param hpss_fsid_in (input):
 *        The HPSS fsid to be translated.
 *
 * \return - The FSAL fsid associated to hpss_fsid_in.
 */
fsal_fsid_t hpss2fsal_fsid(u_signed64 hpss_fsid_in)
{

  fsal_fsid_t fsid;

  fsid.major = high32m(hpss_fsid_in);
  fsid.minor = low32m(hpss_fsid_in);

  return fsid;

}

/**
 * hpss2fsal_mode:
 * Convert HPSS mode to FSAL mode.
 *
 * \param uid_bit (input):
 *        The uid_bit field from HPSS object attributes.
 * \param gid_bit (input):
 *        The gid_bit field from HPSS object attributes.
 * \param sticky_bit (input):
 *        The sticky_bit field from HPSS object attributes.
 * \param user_perms (input):
 *        The user_perms field from HPSS object attributes.
 * \param group_perms (input):
 *        The group_perms field from HPSS object attributes.
 * \param other_perms (input):
 *        The other_perms field from HPSS object attributes.
 *
 * \return The FSAL mode associated to input parameters.
 */
fsal_accessmode_t hpss2fsal_mode(unsigned32 uid_bit,
                                 unsigned32 gid_bit,
                                 unsigned32 sticky_bit,
                                 unsigned32 user_perms,
                                 unsigned32 group_perms, unsigned32 other_perms)
{

  fsal_accessmode_t out_mode = 0;

  /* special bits */
  if(uid_bit)
    out_mode |= FSAL_MODE_SUID;
  if(gid_bit)
    out_mode |= FSAL_MODE_SGID;
  if(sticky_bit)
    out_mode |= FSAL_MODE_SVTX;

  /* user perms */
  if(user_perms & NS_PERMS_RD)
    out_mode |= FSAL_MODE_RUSR;
  if(user_perms & NS_PERMS_WR)
    out_mode |= FSAL_MODE_WUSR;
  if(user_perms & NS_PERMS_XS)
    out_mode |= FSAL_MODE_XUSR;

  /* group perms */
  if(group_perms & NS_PERMS_RD)
    out_mode |= FSAL_MODE_RGRP;
  if(group_perms & NS_PERMS_WR)
    out_mode |= FSAL_MODE_WGRP;
  if(group_perms & NS_PERMS_XS)
    out_mode |= FSAL_MODE_XGRP;

  /* other perms */
  if(other_perms & NS_PERMS_RD)
    out_mode |= FSAL_MODE_ROTH;
  if(other_perms & NS_PERMS_WR)
    out_mode |= FSAL_MODE_WOTH;
  if(other_perms & NS_PERMS_XS)
    out_mode |= FSAL_MODE_XOTH;

  return out_mode;

}

/**
 * fsal2hpss_mode:
 * converts FSAL mode to HPSS mode.
 *
 * \param fsal_mode (input):
 *        The fsal mode to be translated.
 * \param uid_bit (output):
 *        The uid_bit field to be set in HPSS object attributes.
 * \param gid_bit (output):
 *        The gid_bit field to be set in HPSS object attributes.
 * \param sticky_bit (output):
 *        The sticky_bit field to be set in HPSS object attributes.
 * \param user_perms (output):
 *        The user_perms field to be set in HPSS object attributes.
 * \param group_perms (output):
 *        The group_perms field to be set in HPSS object attributes.
 * \param other_perms (output):
 *        The other_perms field to be set in HPSS object attributes.
 *
 * \return Nothing.
 */
void fsal2hpss_mode(fsal_accessmode_t fsal_mode,
#if HPSS_MAJOR_VERSION < 7
                    unsigned32 * uid_bit, unsigned32 * gid_bit, unsigned32 * sticky_bit,
#else
                    unsigned32 * mode_perms,
#endif
                    unsigned32 * user_perms,
                    unsigned32 * group_perms, unsigned32 * other_perms)
{

  /* init outputs */

#if HPSS_MAJOR_VERSION < 7
  *uid_bit = 0;
  *gid_bit = 0;
  *sticky_bit = 0;
#else
  *mode_perms = 0;
#endif
  *user_perms = 0;
  *group_perms = 0;
  *other_perms = 0;

  /* special bits */

#if HPSS_MAJOR_VERSION < 7
  if(fsal_mode & FSAL_MODE_SUID)
    *uid_bit = 1;
  if(fsal_mode & FSAL_MODE_SGID)
    *gid_bit = 1;
  if(fsal_mode & FSAL_MODE_SVTX)
    *sticky_bit = 1;
#else
  if(fsal_mode & FSAL_MODE_SUID)
    (*mode_perms) |= NS_PERMS_RD;
  if(fsal_mode & FSAL_MODE_SGID)
    (*mode_perms) |= NS_PERMS_WR;
  if(fsal_mode & FSAL_MODE_SVTX)
    (*mode_perms) |= NS_PERMS_XS;
#endif

  /* user perms */

  if(fsal_mode & FSAL_MODE_RUSR)
    *user_perms |= NS_PERMS_RD;
  if(fsal_mode & FSAL_MODE_WUSR)
    *user_perms |= NS_PERMS_WR;
  if(fsal_mode & FSAL_MODE_XUSR)
    *user_perms |= NS_PERMS_XS;

  /* group perms */

  if(fsal_mode & FSAL_MODE_RGRP)
    *group_perms |= NS_PERMS_RD;
  if(fsal_mode & FSAL_MODE_WGRP)
    *group_perms |= NS_PERMS_WR;
  if(fsal_mode & FSAL_MODE_XGRP)
    *group_perms |= NS_PERMS_XS;

  /* other perms */

  if(fsal_mode & FSAL_MODE_ROTH)
    *other_perms |= NS_PERMS_RD;
  if(fsal_mode & FSAL_MODE_WOTH)
    *other_perms |= NS_PERMS_WR;
  if(fsal_mode & FSAL_MODE_XOTH)
    *other_perms |= NS_PERMS_XS;

  return;

}

/**
 * hpss2fsal_attributes:
 * Fills an FSAL attributes structure with the info
 * provided by the hpss handle and the hpss attributes
 * of an object.
 *
 * \param p_hpss_handle_in (input):
 *        Pointer to the HPSS NS object handle.
 * \param p_hpss_attr_in (input):
 *        Pointer to the HPSS attributes.
 * \param p_fsalattr_out (input/output):
 *        Pointer to the FSAL attributes.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 * \param p_cred (input)
 *        HPSS Credential.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - ERR_FSAL_FAULT: NULL pointer passed as input parameter.
 *      - ERR_FSAL_ATTRNOTSUPP: One of the asked attributes is not supported.
 *      - ERR_FSAL_SERVERFAULT: Unexpected error.
 */
fsal_status_t hpss2fsal_attributes(ns_ObjHandle_t * p_hpss_handle_in,
                                   hpss_Attrs_t * p_hpss_attr_in,
                                   fsal_attrib_list_t * p_fsalattr_out)
{

  fsal_attrib_mask_t supp_attr, unsupp_attr;

  /* sanity checks */
  if(!p_hpss_handle_in || !p_hpss_attr_in || !p_fsalattr_out)
    ReturnCode(ERR_FSAL_FAULT, 0);

  if(p_fsalattr_out->asked_attributes == 0)
    {
      p_fsalattr_out->asked_attributes = global_fs_info.supported_attrs;

      DisplayLog
          ("Error: p_fsalattr_out->asked_attributes  valait 0 dans hpss2fsal_attributes line %d, fichier %s",
           __LINE__, __FILE__);
    }

  /* check that asked attributes are supported */
  supp_attr = global_fs_info.supported_attrs;

  unsupp_attr = (p_fsalattr_out->asked_attributes) & (~supp_attr);

  if(unsupp_attr)
    {
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG,
                        "Unsupported attributes: %#llX    removing it from asked attributes ",
                        unsupp_attr);

      p_fsalattr_out->asked_attributes =
          p_fsalattr_out->asked_attributes & (~unsupp_attr);

      /* ReturnCode( ERR_FSAL_ATTRNOTSUPP, 0 ); */
    }

  /* Fills the output struct */
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SUPPATTR))
    {
      p_fsalattr_out->supported_attributes = supp_attr;
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_TYPE))
    {
      p_fsalattr_out->type = hpss2fsal_type(p_hpss_handle_in->Type);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SIZE))
    {
      p_fsalattr_out->filesize = hpss2fsal_64(p_hpss_attr_in->DataLength);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_FSID))
    {
      p_fsalattr_out->fsid = hpss2fsal_fsid(p_hpss_attr_in->FilesetId);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_ACL))
    {

      if(p_hpss_attr_in->ExtendedACLs == 0)
        {
          int i;
          for(i = 0; i < FSAL_MAX_ACL; i++)
            {
              p_fsalattr_out->acls[i].type = FSAL_ACL_EMPTY;    /* empty ACL slot */
            }
        }
      else
        {

      /** @todo : This doesn't convert ACLs for the moment. */

        }

    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_FILEID))
    {
      p_fsalattr_out->fileid = (fsal_u64_t) hpss_GetObjId(p_hpss_handle_in);
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_MODE))
    {
      p_fsalattr_out->mode = hpss2fsal_mode(
#if HPSS_MAJOR_VERSION < 7
                                             p_hpss_attr_in->SetUIDBit,
                                             p_hpss_attr_in->SetGIDBit,
                                             p_hpss_attr_in->SetStickyBit,
#else
                                             p_hpss_attr_in->ModePerms & NS_PERMS_RD,
                                             p_hpss_attr_in->ModePerms & NS_PERMS_WR,
                                             p_hpss_attr_in->ModePerms & NS_PERMS_XS,
#endif
                                             p_hpss_attr_in->UserPerms,
                                             p_hpss_attr_in->GroupPerms,
                                             p_hpss_attr_in->OtherPerms);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_NUMLINKS))
    {
      p_fsalattr_out->numlinks = p_hpss_attr_in->LinkCount;
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_OWNER))
    {
      p_fsalattr_out->owner = p_hpss_attr_in->UID;
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_GROUP))
    {
      p_fsalattr_out->group = p_hpss_attr_in->GID;
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_ATIME))
    {

#ifdef _DEBUG_FSAL
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "Getting ATIME:");
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tTimeLastRead = %d",
                        p_hpss_attr_in->TimeLastRead);
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tTimeCreated = %d",
                        p_hpss_attr_in->TimeCreated);
#endif

      if(p_hpss_attr_in->TimeLastRead != 0)
        p_fsalattr_out->atime = hpss2fsal_time(p_hpss_attr_in->TimeLastRead);
      else
        p_fsalattr_out->atime = hpss2fsal_time(p_hpss_attr_in->TimeCreated);

    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CREATION))
    {
      p_fsalattr_out->creation = hpss2fsal_time(p_hpss_attr_in->TimeCreated);
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CTIME))
    {
      p_fsalattr_out->ctime = hpss2fsal_time(p_hpss_attr_in->TimeModified);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_MTIME))
    {

#ifdef _DEBUG_FSAL
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "Getting MTIME:");
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tType = %d",
                        hpss2fsal_type(p_hpss_handle_in->Type));
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tTimeLastWritten = %d",
                        p_hpss_attr_in->TimeLastWritten);
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tTimeModified = %d",
                        p_hpss_attr_in->TimeModified);
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tTimeCreated = %d",
                        p_hpss_attr_in->TimeCreated);
#endif

      switch (hpss2fsal_type(p_hpss_handle_in->Type))
        {

        case FSAL_TYPE_FILE:
        case FSAL_TYPE_LNK:

          if(p_hpss_attr_in->TimeLastWritten != 0)
            p_fsalattr_out->mtime = hpss2fsal_time(p_hpss_attr_in->TimeLastWritten);
          else
            p_fsalattr_out->mtime = hpss2fsal_time(p_hpss_attr_in->TimeCreated);
          break;

        case FSAL_TYPE_DIR:
        case FSAL_TYPE_JUNCTION:

          if(p_hpss_attr_in->TimeModified != 0)
            p_fsalattr_out->mtime = hpss2fsal_time(p_hpss_attr_in->TimeModified);
          else
            p_fsalattr_out->mtime = hpss2fsal_time(p_hpss_attr_in->TimeCreated);
          break;

        default:
          ReturnCode(ERR_FSAL_SERVERFAULT, 0);

        }
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_CHGTIME))
    {
      p_fsalattr_out->chgtime
          =
          hpss2fsal_time(MAX_3
                         (p_hpss_attr_in->TimeModified, p_hpss_attr_in->TimeCreated,
                          p_hpss_attr_in->TimeLastWritten));
    }

  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SPACEUSED))
    {
      p_fsalattr_out->spaceused = hpss2fsal_64(p_hpss_attr_in->DataLength);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_MOUNTFILEID))
    {
      p_fsalattr_out->mounted_on_fileid = hpss2fsal_64(p_hpss_attr_in->FilesetRootId);
    }

  /* everything has been copied ! */

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * hpssHandle2fsalAttributes:
 * Fills an FSAL attributes structure with the info
 * provided (only) by the hpss handle of an object.
 *
 * \param p_hpsshandle_in (input):
 *        Pointer to the HPSS NS object handle.
 * \param p_fsalattr_out (input/output):
 *        Pointer to the FSAL attributes.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - ERR_FSAL_FAULT: NULL pointer passed as input parameter.
 *      - ERR_FSAL_ATTRNOTSUPP: One of the asked attributes is not supported.
 *      - ERR_FSAL_SERVERFAULT: Unexpected error.
 */
fsal_status_t hpssHandle2fsalAttributes(ns_ObjHandle_t * p_hpsshandle_in,
                                        fsal_attrib_list_t * p_fsalattr_out)
{

  fsal_attrib_mask_t avail_attr, unavail_attr;

  /* sanity check */
  if(!p_hpsshandle_in || !p_fsalattr_out)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* check that asked attributes are available */
  avail_attr = (FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE | FSAL_ATTR_FILEID);

  unavail_attr = (p_fsalattr_out->asked_attributes) & (~avail_attr);
  if(unavail_attr)
    {
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG,
                        "Attributes not available: %#llX", unavail_attr);
      ReturnCode(ERR_FSAL_ATTRNOTSUPP, 0);
    }

  /* Fills the output struct */
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_SUPPATTR))
    {
      p_fsalattr_out->supported_attributes = global_fs_info.supported_attrs;
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_TYPE))
    {
      p_fsalattr_out->type = hpss2fsal_type(p_hpsshandle_in->Type);
    }
  if(FSAL_TEST_MASK(p_fsalattr_out->asked_attributes, FSAL_ATTR_FILEID))
    {
      p_fsalattr_out->fileid = (fsal_u64_t) hpss_GetObjId(p_hpsshandle_in);
    }

  /* everything has been copied ! */

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * fsal2hpss_attribset:
 * Converts an fsal attrib list to a hpss attrib list and values
 * to be used in Setattr.
 *
 * \param p_fsal_handle (input):
 *        Pointer to the FSAL object handle.
 * \param p_attrib_set (input):
 *        Pointer to the FSAL attributes to be set.
 * \param p_hpss_attrmask (output):
 *        Pointer to the HPSS attribute list associated to
 *        the FSAL asked_attributes.
 * \param p_hpss_attrs (output):
 *        Pointer to the HPSS attribute values associated
 *        to input attributes.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - ERR_FSAL_FAULT: NULL pointer passed as parameter.
 *      - ERR_FSAL_ATTRNOTSUPP:
 *          Some of the asked attributes are not supported.
 *      - ERR_FSAL_INVAL:
 *          Some of the asked attributes are read-only.
 *      - ERR_FSAL_SERVERFAULT: Unexpected error.
 */
fsal_status_t fsal2hpss_attribset(hpssfsal_handle_t * p_fsal_handle,
                                  fsal_attrib_list_t * p_attrib_set,
                                  hpss_fileattrbits_t * p_hpss_attrmask,
                                  hpss_Attrs_t * p_hpss_attrs)
{

  fsal_attrib_mask_t settable_attrs, supp_attrs, unavail_attrs, unsettable_attrs;

  /* sanity check */

  if(!p_attrib_set || !p_hpss_attrmask || !p_hpss_attrs)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* init output values */

  memset(p_hpss_attrmask, 0, sizeof(hpss_fileattrbits_t));
  memset(p_hpss_attrs, 0, sizeof(hpss_Attrs_t));

  /** @todo : Define some constants for settable and supported attributes. */

  /* Supported attributes */

  supp_attrs = global_fs_info.supported_attrs;

  /* Settable attrs. */

  settable_attrs = (FSAL_ATTR_SIZE | FSAL_ATTR_ACL |
                    FSAL_ATTR_MODE | FSAL_ATTR_OWNER |
                    FSAL_ATTR_GROUP | FSAL_ATTR_ATIME |
                    FSAL_ATTR_CTIME | FSAL_ATTR_MTIME);

  /* If there are unsupported attributes, return ERR_FSAL_ATTRNOTSUPP */

  unavail_attrs = (p_attrib_set->asked_attributes) & (~supp_attrs);

  if(unavail_attrs)
    {
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG,
                        "Attributes not supported: %#llX", unavail_attrs);

      /* Error : unsupported attribute. */

      ReturnCode(ERR_FSAL_ATTRNOTSUPP, 0);
    }

  /* If there are read-only attributes, return. */

  unsettable_attrs = (p_attrib_set->asked_attributes) & (~settable_attrs);

  if(unsettable_attrs)
    {
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG,
                        "Read-Only Attributes: %#llX", unsettable_attrs);

      /* Error : unsettable attribute. */

      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  /* convert settable attributes */

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_SIZE))
    {

      (*p_hpss_attrmask) =
          API_AddRegisterValues(*p_hpss_attrmask, CORE_ATTR_DATA_LENGTH, -1);

      p_hpss_attrs->DataLength = fsal2hpss_64(p_attrib_set->filesize);

    }

  /** @todo  ACL management */

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_MODE))
    {

      (*p_hpss_attrmask) =
          API_AddRegisterValues(*p_hpss_attrmask,
                                CORE_ATTR_USER_PERMS,
                                CORE_ATTR_GROUP_PERMS, CORE_ATTR_OTHER_PERMS,
#if HPSS_MAJOR_VERSION < 7
                                CORE_ATTR_SET_GID,
                                CORE_ATTR_SET_UID, CORE_ATTR_SET_STICKY,
#else
                                CORE_ATTR_MODE_PERMS,
#endif
                                -1);

      /* convert mode and set output structure. */
      fsal2hpss_mode(p_attrib_set->mode,
#if HPSS_MAJOR_VERSION < 7
                     &(p_hpss_attrs->SetUIDBit),
                     &(p_hpss_attrs->SetGIDBit), &(p_hpss_attrs->SetStickyBit),
#else
                     &(p_hpss_attrs->ModePerms),
#endif
                     &(p_hpss_attrs->UserPerms),
                     &(p_hpss_attrs->GroupPerms), &(p_hpss_attrs->OtherPerms));

    }

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_OWNER))
    {

      (*p_hpss_attrmask) = API_AddRegisterValues(*p_hpss_attrmask, CORE_ATTR_UID, -1);

      p_hpss_attrs->UID = p_attrib_set->owner;

#ifdef _DEBUG_FSAL
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "Setting Owner = : %d ",
                        p_attrib_set->owner);
#endif
    }

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_GROUP))
    {

      (*p_hpss_attrmask) = API_AddRegisterValues(*p_hpss_attrmask, CORE_ATTR_GID, -1);

      p_hpss_attrs->GID = p_attrib_set->group;

    }

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_ATIME))
    {

      (*p_hpss_attrmask) =
          API_AddRegisterValues(*p_hpss_attrmask, CORE_ATTR_TIME_LAST_READ, -1);

      p_hpss_attrs->TimeLastRead = fsal2hpss_time(p_attrib_set->atime);

#ifdef _DEBUG_FSAL
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "Setting ATIME:");
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tTimeLastRead = %d",
                        p_hpss_attrs->TimeLastRead);
#endif

    }

  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_MTIME))
    {

#ifdef _DEBUG_FSAL
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "Setting MTIME:");
      DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tType = %d", p_fsal_handle->data.obj_type);
#endif

      switch (p_fsal_handle->data.obj_type)
        {
        case FSAL_TYPE_FILE:
        case FSAL_TYPE_LNK:

          (*p_hpss_attrmask) =
              API_AddRegisterValues(*p_hpss_attrmask, CORE_ATTR_TIME_LAST_WRITTEN, -1);
          p_hpss_attrs->TimeLastWritten = fsal2hpss_time(p_attrib_set->mtime);

#ifdef _DEBUG_FSAL
          DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tTimeLastWritten = %d",
                            p_hpss_attrs->TimeLastWritten);
#endif

          break;

        case FSAL_TYPE_DIR:
        case FSAL_TYPE_JUNCTION:

          (*p_hpss_attrmask) =
              API_AddRegisterValues(*p_hpss_attrmask, CORE_ATTR_TIME_MODIFIED, -1);
          p_hpss_attrs->TimeModified = fsal2hpss_time(p_attrib_set->mtime);

#ifdef _DEBUG_FSAL
          DisplayLogJdLevel(fsal_log, NIV_FULL_DEBUG, "\tTimeModified = %d",
                            p_hpss_attrs->TimeModified);
#endif

          break;

        default:
          ReturnCode(ERR_FSAL_SERVERFAULT, 0);

        }                       /* end switch */

    }
  /* end testmask FSAL_ATTR_MTIME */
  if(FSAL_TEST_MASK(p_attrib_set->asked_attributes, FSAL_ATTR_CTIME))
    {

      (*p_hpss_attrmask) =
          API_AddRegisterValues(*p_hpss_attrmask, CORE_ATTR_TIME_MODIFIED, -1);

      p_hpss_attrs->TimeModified = fsal2hpss_time(p_attrib_set->ctime);

    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}
