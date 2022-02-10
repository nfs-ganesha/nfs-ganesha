/*
 * Copyright 2020 Google LLC
 * Author: Solomon Boulos <boulos@google.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

#include "nfs23.h"
#include "fsal_convert.h"

#include "proxyv3_fsal_methods.h"


/**
 * @brief Map from nfsstat3 error codes to the FSAL error codes.
 * @param status Input status as an nfsstat3.
 *
 * @return - A corresponding fsal_errors_t, if one makes sense.
 *         - ERR_FSAL_INVAL, otherwise.
 */

static fsal_errors_t nfsstat3_to_fsal(nfsstat3 status)
{
	switch (status) {
		/*
		 * Most of these have identical enum values, but do this
		 * explicitly anyway.
		 */
	case NFS3_OK:        return ERR_FSAL_NO_ERROR;
	case NFS3ERR_PERM:      return ERR_FSAL_PERM;
	case NFS3ERR_NOENT:     return ERR_FSAL_NOENT;
	case NFS3ERR_IO:     return ERR_FSAL_IO;
	case NFS3ERR_NXIO:   return ERR_FSAL_NXIO;
	case NFS3ERR_ACCES:  return ERR_FSAL_ACCESS;
	case NFS3ERR_EXIST:  return ERR_FSAL_EXIST;
	case NFS3ERR_XDEV:   return ERR_FSAL_XDEV;
		/*
		 * FSAL doesn't have NODEV, but NXIO is "No such device or
		 * address"
		 */
	case NFS3ERR_NODEV:  return ERR_FSAL_NXIO;
	case NFS3ERR_NOTDIR: return ERR_FSAL_NOTDIR;
	case NFS3ERR_ISDIR:  return ERR_FSAL_ISDIR;
	case NFS3ERR_INVAL:  return ERR_FSAL_INVAL;
	case NFS3ERR_FBIG:   return ERR_FSAL_FBIG;
	case NFS3ERR_NOSPC:  return ERR_FSAL_NOSPC;
	case NFS3ERR_ROFS:   return ERR_FSAL_ROFS;
	case NFS3ERR_MLINK:  return ERR_FSAL_MLINK;
	case NFS3ERR_NAMETOOLONG: return ERR_FSAL_NAMETOOLONG;
	case NFS3ERR_NOTEMPTY:    return ERR_FSAL_NOTEMPTY;
	case NFS3ERR_DQUOT:       return ERR_FSAL_DQUOT;
	case NFS3ERR_STALE:       return ERR_FSAL_STALE;
		/*
		 * FSAL doesn't have REMOTE (too many remotes), so just return
		 * NAMETOOLONG.
		 */
	case NFS3ERR_REMOTE:      return ERR_FSAL_NAMETOOLONG;
	case NFS3ERR_BADHANDLE:   return ERR_FSAL_BADHANDLE;
		/* FSAL doesn't have NOT_SYNC, so... INVAL? */
	case NFS3ERR_NOT_SYNC:    return ERR_FSAL_INVAL;
	case NFS3ERR_BAD_COOKIE:  return ERR_FSAL_BADCOOKIE;
	case NFS3ERR_NOTSUPP:     return ERR_FSAL_NOTSUPP;
	case NFS3ERR_TOOSMALL:    return ERR_FSAL_TOOSMALL;
	case NFS3ERR_SERVERFAULT: return ERR_FSAL_SERVERFAULT;
	case NFS3ERR_BADTYPE:     return ERR_FSAL_BADTYPE;
		/*
		 * FSAL doesn't have a single JUKEBOX error, so choose
		 * ERR_FSAL_LOCKED.
		 */
	case NFS3ERR_JUKEBOX:     return ERR_FSAL_LOCKED;
	}

	/* Shouldn't have gotten here with valid input. */
	return ERR_FSAL_INVAL;
}

/**
 * @brief Map from nlm4_stats error codes to the FSAL error codes.
 * @param status Input status as an nlm4_stats.
 *
 * @return - A corresponding fsal_errors_t, if one makes sense.
 *         - ERR_FSAL_INVAL, otherwise.
 */

static fsal_errors_t nlm4stat_to_fsal(nlm4_stats status)
{
	switch (status) {
	case NLM4_GRANTED:             return ERR_FSAL_NO_ERROR;
		/*
		 * We want NLM4_DENIED to convert to STATE_LOCK_CONFLICT in
		 * state_error_convert.
		 */
	case NLM4_DENIED:              return ERR_FSAL_DELAY;
		/* No "space" to allocate. */
	case NLM4_DENIED_NOLOCKS:      return ERR_FSAL_NOSPC;
	case NLM4_BLOCKED:             return ERR_FSAL_BLOCKED;
	case NLM4_DENIED_GRACE_PERIOD: return ERR_FSAL_IN_GRACE;
	case NLM4_DEADLCK:             return ERR_FSAL_DEADLOCK;
	case NLM4_ROFS:                return ERR_FSAL_ROFS;
	case NLM4_STALE_FH:            return ERR_FSAL_STALE;
	case NLM4_FBIG:                return ERR_FSAL_FBIG;
		/* Don't retry. */
	case NLM4_FAILED:              return ERR_FSAL_PERM;
	}

	/* Shouldn't get here. */
	return ERR_FSAL_INVAL;
}

/**
 * @brief Map from nfsstat3 error codes to fsal_status_t.
 * @param status Input nfsstat3 status.
 *
 * @return - Corresponding fsal_status_t.
 */

fsal_status_t nfsstat3_to_fsalstat(nfsstat3 status)
{
	fsal_errors_t rc = nfsstat3_to_fsal(status);

	return fsalstat(rc, (rc == ERR_FSAL_INVAL) ? (int) status : 0);
}

/**
 * @brief Map from nlm4_stats error codes to fsal_status_t.
 * @param status Input nlm4_stats status.
 *
 * @return - Corresponding fsal_status_t.
 */

fsal_status_t nlm4stat_to_fsalstat(nlm4_stats status)
{
	fsal_errors_t rc = nlm4stat_to_fsal(status);

	return fsalstat(rc, (rc == ERR_FSAL_INVAL) ? (int) status : 0);
}

/**
 * @brief Determine if an attribute mask is NFSv3 only.
 * @param mask Input attrmask_t of attributes.
 *
 * @return - True, if the attributes are representable in NFSv3.
 *         - False, otherwise.
 */

bool attrmask_is_nfs3(attrmask_t mask)
{
	/*
	 * NOTE(boulos): Consider contributing this as FSAL_ONLY_MASK or
	 * something.
	 */
	attrmask_t orig = mask;

	if (FSAL_UNSET_MASK(mask, ATTRS_NFS3 | ATTR_RDATTR_ERR) != 0) {
		LogDebug(COMPONENT_FSAL,
			 "requested = %0" PRIx64
			 "\tNFS3 = %0" PRIx64
			 "\tExtra = %0" PRIx64,
			 orig, (attrmask_t) ATTRS_NFS3, mask);
		return false;
	}

	return true;
}

/**
 * @brief Determine if an attribute mask is valid for NFSv3 SETATTR3.
 * @param mask Input attrmask_t of attributes.
 *
 * @return - True, if the attributes are suitable for SETATTR3.
 *         - False, otherwise.
 */

static bool attrmask_valid_setattr(const attrmask_t mask)
{
	attrmask_t temp = mask;
	const attrmask_t possible =
		/* mode, uid, gid, size, atime, mtime */
		ATTRS_SET_TIME | ATTRS_CREDS | ATTR_SIZE | ATTR_MODE;

	if (FSAL_UNSET_MASK(temp, possible)) {
		LogDebug(COMPONENT_FSAL,
			 "requested = %0" PRIx64
			 "\tNFS3 = %0" PRIx64
			 "\tExtra = %0" PRIx64,
			 mask, possible, temp);
		return false;
	}

	/* Make sure that only one of ATIME | ATIME_SERVER is set. */
	if (FSAL_TEST_MASK(mask, ATTR_ATIME) &&
	    FSAL_TEST_MASK(mask, ATTR_ATIME_SERVER)) {
		LogDebug(COMPONENT_FSAL,
			 "Error: mask %0" PRIx64
			 " has both ATIME and ATIME_SERVER",
			 mask);
		return false;
	}

	/* Make sure that only one of MTIME | MTIME_SERVER is set. */
	if (FSAL_TEST_MASK(mask, ATTR_MTIME) &&
	    FSAL_TEST_MASK(mask, ATTR_MTIME_SERVER)) {
		LogDebug(COMPONENT_FSAL,
			 "Error: mask %0" PRIx64
			 " has both MTIME and MTIME_SERVER",
			 mask);
		return false;
	}

	return true;
}

/**
 * @brief Convert an fattr3 to fsal_attrlist.
 * @param attrs Input attributes as fattr3.
 * @param fsal_attrs_out Output attributes in FSAL form.
 *
 * @return - True, if the attributes are suitable for NFSv3.
 *         - False, otherwise.
 */

bool fattr3_to_fsalattr(const fattr3 *attrs,
			struct fsal_attrlist *fsal_attrs_out)
{
	if (!attrmask_is_nfs3(fsal_attrs_out->request_mask)) {
		return false;
	}

	/*
	 * NOTE(boulos): Since nfs23.h typedefs fattr3 to fsal_attrlist (leaving
	 * fattr3_wire for the real fattr3 from the protocol) this is just a
	 * simple copy.
	 */
	*fsal_attrs_out = *attrs;

	/* Claim that only the NFSv3 attributes are valid. */
	FSAL_SET_MASK(fsal_attrs_out->valid_mask, ATTRS_NFS3);
	/* @todo Do we have to even do this? The CEPH FSAL does... */
	FSAL_SET_MASK(fsal_attrs_out->supported, ATTRS_NFS3);
	return true;
}

/**
 * @brief Convert an fsal_attrlist to sattr3.
 * @param fsal_attrs Input attributes as in fsal_attrlist form.
 * @param attrs_out Output attributes as sattr3.
 *
 * @return - True, if the attributes are suitable for SETATTR3.
 *         - False, otherwise.
 */

bool fsalattr_to_sattr3(const struct fsal_attrlist *fsal_attrs,
			sattr3 *attrs_out)
{
	/*
	 * Zero the struct so that all the "set_it" optionals are false by
	 * default.
	 */
	memset(attrs_out, 0, sizeof(*attrs_out));

	/* Make sure there aren't any additional options we aren't expecting. */
	if (!attrmask_valid_setattr(fsal_attrs->valid_mask)) {
		return false;
	}

	if (FSAL_TEST_MASK(fsal_attrs->valid_mask, ATTR_MODE)) {
		attrs_out->mode.set_it = true;
		attrs_out->mode.set_mode3_u.mode =
			fsal2unix_mode(fsal_attrs->mode);
	}

	if (FSAL_TEST_MASK(fsal_attrs->valid_mask, ATTR_OWNER)) {
		attrs_out->uid.set_it = true;
		attrs_out->uid.set_uid3_u.uid = fsal_attrs->owner;
	}

	if (FSAL_TEST_MASK(fsal_attrs->valid_mask, ATTR_GROUP)) {
		attrs_out->gid.set_it = true;
		attrs_out->gid.set_gid3_u.gid = fsal_attrs->group;
	}

	if (FSAL_TEST_MASK(fsal_attrs->valid_mask, ATTR_SIZE)) {
		attrs_out->size.set_it = true;
		attrs_out->size.set_size3_u.size = fsal_attrs->filesize;
	}

	if (FSAL_TEST_MASK(fsal_attrs->valid_mask, ATTR_ATIME)) {
		attrs_out->atime.set_it = SET_TO_CLIENT_TIME;
		attrs_out->atime.set_atime_u.atime.tv_sec =
			fsal_attrs->atime.tv_sec;
		attrs_out->atime.set_atime_u.atime.tv_nsec =
			fsal_attrs->atime.tv_nsec;
	} else if (FSAL_TEST_MASK(fsal_attrs->valid_mask, ATTR_ATIME_SERVER)) {
		attrs_out->atime.set_it = SET_TO_SERVER_TIME;
	}

	if (FSAL_TEST_MASK(fsal_attrs->valid_mask, ATTR_MTIME)) {
		attrs_out->mtime.set_it = SET_TO_CLIENT_TIME;
		attrs_out->mtime.set_mtime_u.mtime.tv_sec =
			fsal_attrs->mtime.tv_sec;
		attrs_out->mtime.set_mtime_u.mtime.tv_nsec =
			fsal_attrs->mtime.tv_nsec;
	} else if (FSAL_TEST_MASK(fsal_attrs->valid_mask, ATTR_MTIME_SERVER)) {
		attrs_out->mtime.set_it = SET_TO_SERVER_TIME;
	}

	return true;
}
