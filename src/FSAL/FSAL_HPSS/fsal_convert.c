/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * DISCLAIMER
 * ----------
 * This file is part of FSAL_HPSS.
 * FSAL HPSS provides the glue in-between FSAL API and HPSS CLAPI
 * You need to have HPSS installed to properly compile this file.
 *
 * Linkage/compilation/binding/loading/etc of HPSS licensed Software
 * must occur at the HPSS partner's or licensee's location.
 * It is not allowed to distribute this software as compiled or linked
 * binaries or libraries, as they include HPSS licensed material.
 * -------------
 */

/**
 *
 * \file    fsal_convert.c
 * \date    $Date: 2006/02/08 12:46:59 $
 * \brief   HPSS-FSAL type translation functions.
 *
 */
#include "config.h"
#include "fsal_convert.h"
#include "fsal_internal.h"
#include <sys/types.h>
#include <errno.h>

#include <hpss_errno.h>

#define MAX_2(x, y)    ((x) > (y) ? (x) : (y))
#define MAX_3(x, y, z) ((x) > (y) ? MAX_2((x), (z)) : MAX_2((y), (z)))



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
	switch (hpss_errorcode) {
	case HPSS_E_NOERROR:
		return ERR_FSAL_NO_ERROR;

	case EPERM:
	case HPSS_EPERM:
		return ERR_FSAL_PERM;

	case ENOENT:
	case HPSS_ENOENT:
		return ERR_FSAL_NOENT;

	/* connection error */
	case ECONNREFUSED:
	case ECONNABORTED:
	case ECONNRESET:
	/* IO error */
	case HPSS_ECONN:
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
	 *        is opened for reading, and we try writing in it.
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

	case ENOTEMPTY:
	case -ENOTEMPTY:
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
		/* hsec error code regarding security (-11000...) */
	if ((hpss_errorcode <= HPSS_SEC_ENOT_AUTHORIZED)
	     && (hpss_errorcode >= HPSS_SEC_LDAP_RETRY))
		return ERR_FSAL_SEC;

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

	if (testperm & FSAL_R_OK)
		hpss_testperm |= R_OK;

	if (testperm & FSAL_W_OK)
		hpss_testperm |= W_OK;

	if (testperm & FSAL_X_OK)
		hpss_testperm |= X_OK;

	return hpss_testperm;
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
object_file_type_t  hpss2fsal_type(unsigned32 hpss_type_in)
{
	switch (hpss_type_in) {
	case NS_OBJECT_TYPE_DIRECTORY:
		return DIRECTORY;

	case NS_OBJECT_TYPE_HARD_LINK:
	case NS_OBJECT_TYPE_FILE:
		return REGULAR_FILE;

	case NS_OBJECT_TYPE_SYM_LINK:
		return SYMBOLIC_LINK;

	/*
	 * case NS_OBJECT_TYPE_JUNCTION:
	 * return FS_JUNCTION;
	 */

	default:
		LogEvent(COMPONENT_FSAL,
			 "Unknown object type: %d",
			 hpss_type_in);
		return -1;
	}
}

/**
 * hpss2fsal_time:
 * Convert HPSS time structure (timestamp_sec_t)
 * to FSAL time type (struct timespec).
 */
struct timespec hpss2fsal_time(timestamp_sec_t tsec)
{
	struct timespec ts = { .tv_sec = tsec, .tv_nsec = 0 };
	return ts;
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
uint64_t hpss2fsal_64(u_signed64 hpss_size_in)
{
	long long output_buff;

	CONVERT_U64_TO_LONGLONG(hpss_size_in, output_buff);
	return (uint64_t) (output_buff);
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
u_signed64 fsal2hpss_64(uint64_t fsal_size_in)
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
uint32_t hpss2fsal_mode(unsigned32 uid_bit,
			unsigned32 gid_bit,
			unsigned32 sticky_bit,
			unsigned32 user_perms,
			unsigned32 group_perms,
			unsigned32 other_perms)
{
	uint32_t out_mode = 0;

	/* special bits */
	if (uid_bit)
		out_mode |= S_ISUID;

	if (gid_bit)
		out_mode |= S_ISGID;

	if (sticky_bit)
		out_mode |= S_ISVTX;

	/* user perms */
	if (user_perms & NS_PERMS_RD)
		out_mode |= S_IRUSR;

	if (user_perms & NS_PERMS_WR)
		out_mode |= S_IWUSR;

	if (user_perms & NS_PERMS_XS)
		out_mode |= S_IXUSR;

	/* group perms */
	if (group_perms & NS_PERMS_RD)
		out_mode |= S_IRGRP;

	if (group_perms & NS_PERMS_WR)
		out_mode |= S_IWGRP;

	if (group_perms & NS_PERMS_XS)
		out_mode |= S_IXGRP;

	/* other perms */
	if (other_perms & NS_PERMS_RD)
		out_mode |= S_IROTH;

	if (other_perms & NS_PERMS_WR)
		out_mode |= S_IWOTH;

	if (other_perms & NS_PERMS_XS)
		out_mode |= S_IXOTH;

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
void fsal2hpss_mode(uint32_t fsal_mode,
		    unsigned32 *mode_perms,
		    unsigned32 *user_perms,
		    unsigned32 *group_perms,
		    unsigned32 *other_perms)
{
	/* init outputs */
	*mode_perms = 0;
	*user_perms = 0;
	*group_perms = 0;
	*other_perms = 0;

	/* special bits */

	if (fsal_mode & S_ISUID)
		(*mode_perms) |= NS_PERMS_RD;

	if (fsal_mode & S_ISGID)
		(*mode_perms) |= NS_PERMS_WR;

	if (fsal_mode & S_ISVTX)
		(*mode_perms) |= NS_PERMS_XS;

	/* user perms */
	if (fsal_mode & S_IRUSR)
		*user_perms |= NS_PERMS_RD;

	if (fsal_mode & S_IWUSR)
		*user_perms |= NS_PERMS_WR;

	if (fsal_mode & S_IXUSR)
		*user_perms |= NS_PERMS_XS;

	/* group perms */
	if (fsal_mode & S_IRGRP)
		*group_perms |= NS_PERMS_RD;

	if (fsal_mode & S_IWGRP)
		*group_perms |= NS_PERMS_WR;

	if (fsal_mode & S_IXGRP)
		*group_perms |= NS_PERMS_XS;

	/* other perms */
	if (fsal_mode & S_IROTH)
		*other_perms |= NS_PERMS_RD;

	if (fsal_mode & S_IWOTH)
		*other_perms |= NS_PERMS_WR;

	if (fsal_mode & S_IXOTH)
		*other_perms |= NS_PERMS_XS;
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
fsal_status_t hpss2fsal_attributes(ns_ObjHandle_t *p_hpss_handle_in,
				   hpss_Attrs_t *p_hpss_attr_in,
				   struct attrlist *p_fsalattr_out)
{
	attrmask_t supp_attr, unsupp_attr;

	/* sanity checks */
	if (!p_hpss_handle_in || !p_hpss_attr_in || !p_fsalattr_out)
		return fsalstat(ERR_FSAL_FAULT, 0);

	memset(p_fsalattr_out, 0, sizeof(struct attrlist));

	if (p_fsalattr_out->mask == 0)
		p_fsalattr_out->mask = HPSS_SUPPORTED_ATTRIBUTES;

	/* check that asked attributes are supported */
	/** @todo: FIXME: add an argument for the export
	 *  so we get the actual supported attrs. same in fsal2hpss */
	supp_attr = HPSS_SUPPORTED_ATTRIBUTES;

	unsupp_attr = (p_fsalattr_out->mask) & (~supp_attr);

	if (unsupp_attr) {
		LogFullDebug(COMPONENT_FSAL,
		  "Unsupported attr: %"PRIx64" removing it from asked attr",
		  unsupp_attr);

	p_fsalattr_out->mask =
		p_fsalattr_out->mask & (~unsupp_attr);

	/* return fsalstat( ERR_FSAL_ATTRNOTSUPP, 0); */
	}

	/* Initialize ACL regardless of whether ACL was asked or not.
	 * This is needed to make sure ACL attribute is initialized. */
	p_fsalattr_out->acl = NULL;

	/* Fills the output struct */
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_TYPE))
		p_fsalattr_out->type = hpss2fsal_type(p_hpss_handle_in->Type);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SIZE))
		p_fsalattr_out->filesize =
			 hpss2fsal_64(p_hpss_attr_in->DataLength);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FSID))
		p_fsalattr_out->fsid =
			 hpss2fsal_fsid(p_hpss_attr_in->FilesetId);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ACL))
		if (p_hpss_attr_in->ExtendedACLs == 0)
			p_fsalattr_out->acl = NULL;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FILEID))
		p_fsalattr_out->fileid =
			 (uint64_t) hpss_GetObjId(p_hpss_handle_in);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MODE))
		p_fsalattr_out->mode = hpss2fsal_mode(
				p_hpss_attr_in->ModePerms & NS_PERMS_RD,
				p_hpss_attr_in->ModePerms & NS_PERMS_WR,
				p_hpss_attr_in->ModePerms & NS_PERMS_XS,
				p_hpss_attr_in->UserPerms,
				p_hpss_attr_in->GroupPerms,
				p_hpss_attr_in->OtherPerms);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_NUMLINKS))
		p_fsalattr_out->numlinks = p_hpss_attr_in->LinkCount;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_OWNER))
		p_fsalattr_out->owner = p_hpss_attr_in->UID;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_GROUP))
		p_fsalattr_out->group = p_hpss_attr_in->GID;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ATIME)) {
		LogFullDebug(COMPONENT_FSAL, "Getting ATIME:");
		LogFullDebug(COMPONENT_FSAL, "\tTimeLastRead = %d",
			     p_hpss_attr_in->TimeLastRead);
		LogFullDebug(COMPONENT_FSAL, "\tTimeCreated = %d",
			     p_hpss_attr_in->TimeCreated);

	if (p_hpss_attr_in->TimeLastRead != 0)
		p_fsalattr_out->atime =
			 hpss2fsal_time(p_hpss_attr_in->TimeLastRead);
	else
		p_fsalattr_out->atime =
			 hpss2fsal_time(p_hpss_attr_in->TimeCreated);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CREATION))
		p_fsalattr_out->creation =
			 hpss2fsal_time(p_hpss_attr_in->TimeCreated);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CTIME))
		p_fsalattr_out->ctime =
			 hpss2fsal_time(p_hpss_attr_in->TimeModified);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MTIME)) {
		LogFullDebug(COMPONENT_FSAL, "Getting MTIME:");
		LogFullDebug(COMPONENT_FSAL, "\tType = %d",
			     hpss2fsal_type(p_hpss_handle_in->Type));
		LogFullDebug(COMPONENT_FSAL, "\tTimeLastWritten = %d",
			     p_hpss_attr_in->TimeLastWritten);
		LogFullDebug(COMPONENT_FSAL, "\tTimeModified = %d",
			     p_hpss_attr_in->TimeModified);
		LogFullDebug(COMPONENT_FSAL, "\tTimeCreated = %d",
			     p_hpss_attr_in->TimeCreated);

		switch (hpss2fsal_type(p_hpss_handle_in->Type)) {
		case REGULAR_FILE:
		case SYMBOLIC_LINK:
			if (p_hpss_attr_in->TimeLastWritten != 0)
				p_fsalattr_out->mtime =
					hpss2fsal_time(p_hpss_attr_in->
							TimeLastWritten);
			else
				p_fsalattr_out->mtime =
				   hpss2fsal_time(p_hpss_attr_in->TimeCreated);
			break;

		case DIRECTORY:
		/* case FS_JUNCTION: */
			if (p_hpss_attr_in->TimeModified != 0)
				p_fsalattr_out->mtime =
					hpss2fsal_time(p_hpss_attr_in->
							TimeModified);
			else
				p_fsalattr_out->mtime =
					hpss2fsal_time(p_hpss_attr_in->
							TimeCreated);
			break;

		default:
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CHGTIME)) {
		p_fsalattr_out->chgtime =
			hpss2fsal_time(MAX_3(
				p_hpss_attr_in->TimeModified,
				p_hpss_attr_in->TimeCreated,
				p_hpss_attr_in->TimeLastWritten));
		p_fsalattr_out->change =
			 (uint64_t) p_fsalattr_out->chgtime.tv_sec;
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SPACEUSED))
		p_fsalattr_out->spaceused =
			hpss2fsal_64(p_hpss_attr_in->DataLength);

	/* everything has been copied ! */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * hpss2fsal_vattributes:
 * Fills an FSAL attributes structure with the info
 * provided by the hpss_vattr_t of an object.
 *
 * \param p_hpss_vattr_in (input):
 *        Pointer to the HPSS vattr.
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
fsal_status_t hpss2fsal_vattributes(hpss_vattr_t *p_hpss_vattr_in,
				    struct attrlist *p_fsalattr_out)
{
	attrmask_t supp_attr, unsupp_attr;

	/* sanity checks */
	if (!p_hpss_vattr_in || !p_fsalattr_out)
		return fsalstat(ERR_FSAL_FAULT, 0);

	memset(p_fsalattr_out, 0, sizeof(struct attrlist));

	if (p_fsalattr_out->mask == 0)
		p_fsalattr_out->mask = HPSS_SUPPORTED_ATTRIBUTES;

	/* check that asked attributes are supported */
	/** @todo: FIXME: add an argument for the export so
	 * we get the actual supported attrs. same in fsal2hpss */
	supp_attr = HPSS_SUPPORTED_ATTRIBUTES;

	unsupp_attr = (p_fsalattr_out->mask) & (~supp_attr);

	if (unsupp_attr) {
		LogFullDebug(COMPONENT_FSAL,
		  "Unsupported attr: %"PRIx64"removing it from asked attr",
		  unsupp_attr);

	p_fsalattr_out->mask = p_fsalattr_out->mask & (~unsupp_attr);

	/* return fsalstat( ERR_FSAL_ATTRNOTSUPP, 0); */
	}

	/* Initialize ACL regardless of whether ACL was asked or not.
	 * This is needed to make sure ACL attribute is initialized. */
	p_fsalattr_out->acl = NULL;

	/* Fills the output struct */
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_TYPE))
		p_fsalattr_out->type =
			 hpss2fsal_type(p_hpss_vattr_in->va_objhandle.Type);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SIZE))
		p_fsalattr_out->filesize =
			 hpss2fsal_64(p_hpss_vattr_in->va_size);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FSID))
		p_fsalattr_out->fsid =
			 hpss2fsal_fsid(p_hpss_vattr_in->va_ftid);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ACL))
		if (p_hpss_vattr_in->va_acl == NULL)
			p_fsalattr_out->acl = NULL;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FILEID))
		p_fsalattr_out->fileid =
			 (uint64_t)hpss_GetObjId(
				&p_hpss_vattr_in->va_objhandle);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MODE))
		p_fsalattr_out->mode = p_hpss_vattr_in->va_mode;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_NUMLINKS))
		p_fsalattr_out->numlinks = p_hpss_vattr_in->va_nlink;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_OWNER))
		p_fsalattr_out->owner = p_hpss_vattr_in->va_uid;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_GROUP))
		p_fsalattr_out->group = p_hpss_vattr_in->va_gid;

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_ATIME)) {
		LogFullDebug(COMPONENT_FSAL, "Getting ATIME:");
		LogFullDebug(COMPONENT_FSAL, "\tTimeLastRead = %d",
			     p_hpss_vattr_in->va_atime);

	if (p_hpss_vattr_in->va_atime != 0)
		p_fsalattr_out->atime =
			hpss2fsal_time(p_hpss_vattr_in->va_atime);
	else
		p_fsalattr_out->atime =
			hpss2fsal_time(p_hpss_vattr_in->va_ctime);
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CTIME))
		p_fsalattr_out->ctime =
			hpss2fsal_time(p_hpss_vattr_in->va_ctime);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_MTIME))
		p_fsalattr_out->mtime =
			hpss2fsal_time(p_hpss_vattr_in->va_mtime);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_CHGTIME)) {
		p_fsalattr_out->chgtime =
			hpss2fsal_time(MAX_2(
				p_hpss_vattr_in->va_mtime,
				p_hpss_vattr_in->va_ctime));
		p_fsalattr_out->change =
			(uint64_t) p_fsalattr_out->chgtime.tv_sec;
	}

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_SPACEUSED))
		p_fsalattr_out->spaceused =
			 hpss2fsal_64(p_hpss_vattr_in->va_size);

	/* everything has been copied ! */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
fsal_status_t hpssHandle2fsalAttributes(ns_ObjHandle_t *p_hpsshandle_in,
					struct attrlist *p_fsalattr_out)
{
	attrmask_t avail_attr, unavail_attr;

	/* sanity check */
	if (!p_hpsshandle_in || !p_fsalattr_out)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* check that asked attributes are available */
	avail_attr = (ATTR_TYPE | ATTR_FILEID);

	unavail_attr = (p_fsalattr_out->mask) & (~avail_attr);
	if (unavail_attr) {
		LogFullDebug(COMPONENT_FSAL,
			"Attributes not available: %"PRIx64"", unavail_attr);
		return fsalstat(ERR_FSAL_ATTRNOTSUPP, 0);
	}

	/* Fills the output struct */
	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_TYPE))
		p_fsalattr_out->type =
			 hpss2fsal_type(p_hpsshandle_in->Type);

	if (FSAL_TEST_MASK(p_fsalattr_out->mask, ATTR_FILEID))
		p_fsalattr_out->fileid =
			(uint64_t) hpss_GetObjId(p_hpsshandle_in);

	/* everything has been copied ! */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
 *        the FSAL mask.
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
fsal_status_t fsal2hpss_attribset(struct fsal_obj_handle *p_fsal_handle,
				  struct attrlist *p_attrib_set,
				  hpss_fileattrbits_t *p_hpss_attrmask,
				  hpss_Attrs_t *p_hpss_attrs)
{
	attrmask_t settable_attrs, supp_attrs, unavail_attrs, unsettable_attrs;

	/* sanity check */
	if (!p_attrib_set || !p_hpss_attrmask || !p_hpss_attrs)
		return fsalstat(ERR_FSAL_FAULT, 0);

	/* init output values */
	memset(p_hpss_attrmask, 0, sizeof(hpss_fileattrbits_t));
	memset(p_hpss_attrs, 0, sizeof(hpss_Attrs_t));

	/** @todo : Define some constants for settable and supported attrs */
	/* Supported attributes */

	supp_attrs = HPSS_SUPPORTED_ATTRIBUTES;

	/* Settable attrs. */
	settable_attrs = (ATTR_SIZE | ATTR_SPACEUSED | ATTR_ACL |
			  ATTR_MODE | ATTR_OWNER |
			  ATTR_GROUP | ATTR_ATIME |
			  ATTR_CTIME | ATTR_MTIME |
			  ATTR_ATIME_SERVER | ATTR_MTIME_SERVER);

	/* If there are unsupported attributes, return ERR_FSAL_ATTRNOTSUPP */
	unavail_attrs = (p_attrib_set->mask) & (~supp_attrs);

	if (unavail_attrs) {
		LogFullDebug(COMPONENT_FSAL,
			"Attributes not supported: %"PRIx64"", unavail_attrs);
		return fsalstat(ERR_FSAL_ATTRNOTSUPP, 0);
	}

	/* If there are read-only attributes, return. */
	unsettable_attrs = (p_attrib_set->mask) & (~settable_attrs);

	if (unsettable_attrs) {
		LogFullDebug(COMPONENT_FSAL,
			"Read-Only Attributes: %"PRIx64"", unsettable_attrs);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	/* convert settable attributes */
	if (FSAL_TEST_MASK(p_attrib_set->mask, ATTR_SIZE)) {
		(*p_hpss_attrmask) =
			API_AddRegisterValues(*p_hpss_attrmask,
					      CORE_ATTR_DATA_LENGTH,
					      -1);

		p_hpss_attrs->DataLength =
			 fsal2hpss_64(p_attrib_set->filesize);
	}

	/** @todo  ACL management */
	if (FSAL_TEST_MASK(p_attrib_set->mask, ATTR_MODE)) {
		(*p_hpss_attrmask) =
			API_AddRegisterValues(*p_hpss_attrmask,
					      CORE_ATTR_USER_PERMS,
					      CORE_ATTR_GROUP_PERMS,
					      CORE_ATTR_OTHER_PERMS,
					      CORE_ATTR_MODE_PERMS,
					      -1);

		/* convert mode and set output structure. */
		fsal2hpss_mode(p_attrib_set->mode,
			       &(p_hpss_attrs->ModePerms),
			       &(p_hpss_attrs->UserPerms),
			       &(p_hpss_attrs->GroupPerms),
			       &(p_hpss_attrs->OtherPerms));
	}

	if (FSAL_TEST_MASK(p_attrib_set->mask, ATTR_OWNER)) {
		(*p_hpss_attrmask) =
			 API_AddRegisterValues(*p_hpss_attrmask,
					       CORE_ATTR_UID,
					       -1);

		p_hpss_attrs->UID = p_attrib_set->owner;

		LogFullDebug(COMPONENT_FSAL, "Setting Owner = : %"PRIu64" ",
			     p_attrib_set->owner);
	}

	if (FSAL_TEST_MASK(p_attrib_set->mask, ATTR_GROUP)) {
		(*p_hpss_attrmask) =
			API_AddRegisterValues(*p_hpss_attrmask,
					      CORE_ATTR_GID,
					      -1);

		p_hpss_attrs->GID = p_attrib_set->group;
	}

	/* if *TIME_SERVER, just fill the regular *TIME with the right value */
	if (FSAL_TEST_MASK(p_attrib_set->mask, ATTR_ATIME_SERVER)) {
		p_attrib_set->mask |= ATTR_ATIME;
		clock_gettime(CLOCK_REALTIME, &p_attrib_set->atime);
	}

	if (FSAL_TEST_MASK(p_attrib_set->mask, ATTR_MTIME_SERVER)) {
		p_attrib_set->mask |= ATTR_MTIME;
		clock_gettime(CLOCK_REALTIME, &p_attrib_set->mtime);
	}


	if (FSAL_TEST_MASK(p_attrib_set->mask, ATTR_ATIME)) {
		(*p_hpss_attrmask) =
			API_AddRegisterValues(*p_hpss_attrmask,
					      CORE_ATTR_TIME_LAST_READ,
					      -1);

		p_hpss_attrs->TimeLastRead =
			fsal2hpss_time(p_attrib_set->atime);

		LogFullDebug(COMPONENT_FSAL, "Setting ATIME:");
		LogFullDebug(COMPONENT_FSAL, "\tTimeLastRead = %d",
			p_hpss_attrs->TimeLastRead);
	}

	if (FSAL_TEST_MASK(p_attrib_set->mask, ATTR_MTIME)) {
		LogFullDebug(COMPONENT_FSAL, "Setting MTIME:");
		LogFullDebug(COMPONENT_FSAL, "\tType = %d",
			     p_fsal_handle->type);

		switch (p_fsal_handle->type) {
		case REGULAR_FILE:
		case SYMBOLIC_LINK:
			(*p_hpss_attrmask) =
				API_AddRegisterValues(*p_hpss_attrmask,
					      CORE_ATTR_TIME_LAST_WRITTEN,
					      -1);
				p_hpss_attrs->TimeLastWritten =
					 fsal2hpss_time(p_attrib_set->mtime);

			LogFullDebug(COMPONENT_FSAL, "\tTimeLastWritten = %d",
				     p_hpss_attrs->TimeLastWritten);

			break;

		case DIRECTORY:
		/*  case FS_JUNCTION: */
			(*p_hpss_attrmask) =
				API_AddRegisterValues(*p_hpss_attrmask,
						      CORE_ATTR_TIME_MODIFIED,
						      -1);
			p_hpss_attrs->TimeModified =
				fsal2hpss_time(p_attrib_set->mtime);

			LogFullDebug(COMPONENT_FSAL, "\tTimeModified = %d",
				     p_hpss_attrs->TimeModified);

			break;

		default:
			return fsalstat(ERR_FSAL_SERVERFAULT, 0);
		}

	} /* end testmask ATTR_MTIME */

	if (FSAL_TEST_MASK(p_attrib_set->mask, ATTR_CTIME)) {
		(*p_hpss_attrmask) =
			API_AddRegisterValues(*p_hpss_attrmask,
					      CORE_ATTR_TIME_MODIFIED,
					      -1);
		p_hpss_attrs->TimeModified =
			fsal2hpss_time(p_attrib_set->ctime);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
