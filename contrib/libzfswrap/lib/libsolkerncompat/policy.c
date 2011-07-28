/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2006 Ricardo Correia
 */

#include <sys/cred_impl.h>
#include <sys/policy.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <string.h>

long pwd_buflen = 0;
long grp_buflen = 0;

cred_t st_kcred = { 0,0 };
cred_t *kcred = &st_kcred;

int ngroups_max = 0;

uid_t crgetuid(const cred_t *cr)
{
	return cr->cr_uid;
}

gid_t crgetgid(const cred_t *cr)
{
	return cr->cr_gid;
}

int crgetngroups(const cred_t *cr)
{
	return 0;
}

const gid_t *crgetgroups(const cred_t *cr)
{
	return NULL;
}

int groupmember(gid_t gid, const cred_t *cr)
{
	if(gid == cr->cr_gid)
		return 1;

#if (FUSE_MAJOR_VERSION == 2 && FUSE_MINOR_VERSION <= 7) || FUSE_MAJOR_VERSION < 2
	/* This whole thing is very expensive, FUSE should provide the list of groups the user belongs to.. */

	char *pwd_buf = NULL;
	char *grp_buf = NULL;
	int error = 0;

	pwd_buf = kmem_alloc(pwd_buflen, KM_NOSLEEP);
	if(pwd_buf == NULL) {
		fprintf(stderr, "groupmember(): pwd_buf memory allocation failed\n");
		error = 1;
		goto out;
	}

	grp_buf = kmem_alloc(grp_buflen, KM_NOSLEEP);
	if(grp_buf == NULL) {
		fprintf(stderr, "groupmember(): grp_buf memory allocation failed\n");
		error = 1;
		goto out;
	}

	struct group gbuf, *gbufp;

	error = getgrgid_r(gid, &gbuf, grp_buf, grp_buflen, &gbufp);
	if(error) {
		/* We'll reuse grp_buf */
		if(strerror_r(error, grp_buf, grp_buflen) == 0)
			fprintf(stderr, "getgrgid_r(): %s\n", grp_buf);
		else
			perror("strerror_r");

		error = 1;
		goto out;
	}

	/* gid no longer exists? */
	if(gbufp == NULL) {
		error = 1;
		goto out;
	}

	VERIFY(gbufp == &gbuf);

	struct passwd pwbuf, *pwbufp;

	error = getpwuid_r(cr->cr_uid, &pwbuf, pwd_buf, pwd_buflen, &pwbufp);
	if(error) {
		/* We'll reuse grp_buf, since we no longer need it */
		if(strerror_r(error, grp_buf, grp_buflen) == 0)
			fprintf(stderr, "getpwuid_r(): %s\n", grp_buf);
		else
			perror("strerror_r");

		error = 1;
		goto out;
	}

	/* uid no longer exists? */
	if(pwbufp == NULL) {
		error = 1;
		goto out;
	}

	VERIFY(pwbufp == &pwbuf);

	for(int i = 0; gbuf.gr_mem[i] != NULL; i++)
		if(strcmp(gbuf.gr_mem[i], pwbuf.pw_name) == 0)
			goto out;

	error = 1;

out:
	if(pwd_buf != NULL)
		kmem_free(pwd_buf, pwd_buflen);
	if(grp_buf != NULL)
		kmem_free(grp_buf, grp_buflen);

	/* If error == 0 then the user belongs to the group */
	return error ? 0 : 1;
#else // FUSE_MINOR_VERSION >= 8
#if 0
	if (!cr->req) {
	    /* This function can be called with cr=kcred if called by
	     * zfs_replay_create for example. kcred does not contain any fuse
	     * request, but it's used when you need all the privileges, so you
	     * can safety return 1 here in this case */
	    return 1;
	}
	if (!ngroups_max) { ngroups_max = sysconf(_SC_NGROUPS_MAX)+1; }
	gid_t *groups = malloc(ngroups_max * sizeof(gid_t));
	if (!groups) {
		errno = ENOMEM;
		return 0;
	}
	int nb = fuse_req_getgroups(cr->req, ngroups_max,groups);
	int found = 0;
	for (int n=0; n<nb; n++)
		if (groups[n] == gid) {
			found = 1;
			break;
		}
	free(groups);
	return found;
#endif
#endif
        return 1;
}

/*
 * Name:	secpolicy_vnode_setid_modify()
 *
 * Normal:	verify that subject can set the file setid flags.
 *
 * Output:	EPERM - if not privileged.
 */

static int
secpolicy_vnode_setid_modify(const cred_t *cr, uid_t owner)
{
	/* If changing to suid root, must have all zone privs */
	boolean_t allzone = B_TRUE;

	if (owner != 0) {
		if (owner == cr->cr_uid)
			return (0);
		allzone = B_FALSE;
	}
	return (PRIV_POLICY(cr, PRIV_FILE_SETID, allzone, EPERM, NULL));
}

void secpolicy_setid_clear(vattr_t *vap, cred_t *cr)
{
	if ((vap->va_mode & (S_ISUID | S_ISGID)) != 0 &&
	    secpolicy_vnode_setid_retain(cr,
	    (vap->va_mode & S_ISUID) != 0 &&
	    (vap->va_mask & AT_UID) != 0 && vap->va_uid == 0) != 0) {
		vap->va_mask |= AT_MODE;
		vap->va_mode &= ~(S_ISUID|S_ISGID);
	}
}

int
secpolicy_setid_setsticky_clear(vnode_t *vp, vattr_t *vap, const vattr_t *ovap,
    cred_t *cr)
{
	int error;

	if ((vap->va_mode & S_ISUID) != 0 &&
	    (error = secpolicy_vnode_setid_modify(cr,
	    ovap->va_uid)) != 0) {
		return (error);
	}

	/*
	 * Check privilege if attempting to set the
	 * sticky bit on a non-directory.
	 */
	if (vp->v_type != VDIR && (vap->va_mode & S_ISVTX) != 0 &&
	    secpolicy_vnode_stky_modify(cr) != 0) {
	    vap->va_mode &= ~S_ISVTX;
	}

	/*
	 * Check for privilege if attempting to set the
	 * group-id bit.
	 */
	if ((vap->va_mode & S_ISGID) != 0 &&
	    secpolicy_vnode_setids_setgids(cr, ovap->va_gid) != 0) {
	    vap->va_mode &= ~S_ISGID;
	}

	return (0);
}

/*
 * Are we allowed to retain the set-uid/set-gid bits when
 * changing ownership or when writing to a file?
 * "issuid" should be true when set-uid; only in that case
 * root ownership is checked (setgid is assumed).
 */
int secpolicy_vnode_setid_retain(const cred_t *cred, boolean_t issuidroot)
{
#ifndef __linux__
// In linux we always clear these bits when changing id
	if(crgetuid(cred) != 0)
#endif
		return EPERM; 
	return 0;
}

#define ATTR_FLAG_PRIV(attr, value, cr) \
        PRIV_POLICY(cr, value ? PRIV_FILE_FLAG_SET : PRIV_ALL, \
        B_FALSE, EPERM, NULL)

/*
 * Check privileges for setting xvattr attributes
 */
int
secpolicy_xvattr(xvattr_t *xvap, uid_t owner, cred_t *cr, vtype_t vtype)
{
	xoptattr_t *xoap;
	int error = 0;

	if ((xoap = xva_getxoptattr(xvap)) == NULL)
		return (EINVAL);

	/*
	 * First process the DOS bits
	 */
	if (XVA_ISSET_REQ(xvap, XAT_ARCHIVE) ||
	    XVA_ISSET_REQ(xvap, XAT_HIDDEN) ||
	    XVA_ISSET_REQ(xvap, XAT_READONLY) ||
	    XVA_ISSET_REQ(xvap, XAT_SYSTEM) ||
	    XVA_ISSET_REQ(xvap, XAT_CREATETIME)) {
		if ((error = secpolicy_vnode_owner(cr, owner)) != 0)
			return (error);
	}

	/*
	 * Now handle special attributes
	*/

	if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE))
		error = ATTR_FLAG_PRIV(XAT_IMMUTABLE,
		    xoap->xoa_immutable, cr);
	if (error == 0 && XVA_ISSET_REQ(xvap, XAT_NOUNLINK))
		error = ATTR_FLAG_PRIV(XAT_NOUNLINK,
		    xoap->xoa_nounlink, cr);
	if (error == 0 && XVA_ISSET_REQ(xvap, XAT_APPENDONLY))
		error = ATTR_FLAG_PRIV(XAT_APPENDONLY,
		    xoap->xoa_appendonly, cr);
	if (error == 0 && XVA_ISSET_REQ(xvap, XAT_NODUMP))
		error = ATTR_FLAG_PRIV(XAT_NODUMP,
		    xoap->xoa_nodump, cr);
	if (error == 0 && XVA_ISSET_REQ(xvap, XAT_OPAQUE))
		error = EPERM;
	if (error == 0 && XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
		error = ATTR_FLAG_PRIV(XAT_AV_QUARANTINED,
		    xoap->xoa_av_quarantined, cr);
		if (error == 0 && vtype != VREG)
			error = EINVAL;
	}
	if (error == 0 && XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED))
		error = ATTR_FLAG_PRIV(XAT_AV_MODIFIED,
		    xoap->xoa_av_modified, cr);
	if (error == 0 && XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP)) {
		error = ATTR_FLAG_PRIV(XAT_AV_SCANSTAMP,
		    xoap->xoa_av_scanstamp, cr);
		if (error == 0 && vtype != VREG)
			error = EINVAL;
	}
	return (error);
}


/*
 * This function checks the policy decisions surrounding the
 * vop setattr call.
 *
 * It should be called after sufficient locks have been established
 * on the underlying data structures.  No concurrent modifications
 * should be allowed.
 *
 * The caller must pass in unlocked version of its vaccess function
 * this is required because vop_access function should lock the
 * node for reading.  A three argument function should be defined
 * which accepts the following argument:
 * 	A pointer to the internal "node" type (inode *)
 *	vnode access bits (VREAD|VWRITE|VEXEC)
 *	a pointer to the credential
 *
 * This function makes the following policy decisions:
 *
 *		- change permissions
 *			- permission to change file mode if not owner
 *			- permission to add sticky bit to non-directory
 *			- permission to add set-gid bit
 *
 * The ovap argument should include AT_MODE|AT_UID|AT_GID.
 *
 * If the vap argument does not include AT_MODE, the mode will be copied from
 * ovap.  In certain situations set-uid/set-gid bits need to be removed;
 * this is done by marking vap->va_mask to include AT_MODE and va_mode
 * is updated to the newly computed mode.
 */

int
secpolicy_vnode_setattr(cred_t *cr, struct vnode *vp, struct vattr *vap,
	const struct vattr *ovap, int flags,
	int unlocked_access(void *, int, cred_t *),
	void *node)
{
	int mask = vap->va_mask;
	int error = 0;

	if (mask & AT_SIZE) {
		if (vp->v_type == VDIR) {
			error = EISDIR;
			goto out;
		}
		error = unlocked_access(node, VWRITE, cr);
		if (error)
			goto out;
	}
	if (mask & AT_MODE) {
		/*
		 * If not the owner of the file then check privilege
		 * for two things: the privilege to set the mode at all
		 * and, if we're setting setuid, we also need permissions
		 * to add the set-uid bit, if we're not the owner.
		 * In the specific case of creating a set-uid root
		 * file, we need even more permissions.
		 */
		if ((error = secpolicy_vnode_setdac(cr, ovap->va_uid)) != 0)
			goto out;

		if ((error = secpolicy_setid_setsticky_clear(vp, vap,
		    ovap, cr)) != 0)
			goto out;
	} else
		vap->va_mode = ovap->va_mode;

	if (mask & (AT_UID|AT_GID)) {
		boolean_t checkpriv = B_FALSE;
		int priv;
		boolean_t allzone = B_FALSE;

		/*
		 * Chowning files.
		 *
		 * If you are the file owner:
		 *	chown to other uid		FILE_CHOWN_SELF
		 *	chown to gid (non-member) 	FILE_CHOWN_SELF
		 *	chown to gid (member) 		<none>
		 *
		 * Instead of PRIV_FILE_CHOWN_SELF, FILE_CHOWN is also
		 * acceptable but the first one is reported when debugging.
		 *
		 * If you are not the file owner:
		 *	chown from root			PRIV_FILE_CHOWN + zone
		 *	chown from other to any		PRIV_FILE_CHOWN
		 *
		 */
		if (cr->cr_uid != ovap->va_uid) {
			checkpriv = B_TRUE;
			allzone = (ovap->va_uid == 0);
			priv = PRIV_FILE_CHOWN;
		} else {
			if (((mask & AT_UID) && vap->va_uid != ovap->va_uid) ||
			    ((mask & AT_GID) && vap->va_gid != ovap->va_gid &&
			    !groupmember(vap->va_gid, cr))) {
				checkpriv = B_TRUE;
				priv = HAS_PRIVILEGE(cr, PRIV_FILE_CHOWN) ?
				    PRIV_FILE_CHOWN : PRIV_FILE_CHOWN_SELF;
			}
		}
		/*
		 * If necessary, check privilege to see if update can be done.
		 */
		if (checkpriv &&
		    (error = PRIV_POLICY(cr, priv, allzone, EPERM, NULL))
		    != 0) {
			goto out;
		}

		/*
		 * If the file has either the set UID or set GID bits
		 * set and the caller can set the bits, then leave them.
		 */
		secpolicy_setid_clear(vap, cr);
	}
	if (mask & (AT_ATIME|AT_MTIME)) {
		/*
		 * If not the file owner and not otherwise privileged,
		 * always return an error when setting the
		 * time other than the current (ATTR_UTIME flag set).
		 * If setting the current time (ATTR_UTIME not set) then
		 * unlocked_access will check permissions according to policy.
		 */
		if (cr->cr_uid != ovap->va_uid) {
			if (flags & ATTR_UTIME)
				error = secpolicy_vnode_utime_modify(cr);
			else {
				error = unlocked_access(node, VWRITE, cr);
				if (error == EACCES &&
				    secpolicy_vnode_utime_modify(cr) == 0)
					error = 0;
			}
			if (error)
				goto out;
		}
	}
out:
	return (error);
}

/*
 * Name:	secpolicy_vnode_setids_setgids()
 *
 * Normal:	verify that subject can set the file setgid flag.
 *
 * Output:	EPERM - if not privileged
 */

int
secpolicy_vnode_setids_setgids(const cred_t *cred, gid_t gid)
{
	if (!groupmember(gid, cred))
		return (PRIV_POLICY(cred, PRIV_FILE_SETID, B_FALSE, EPERM,
		    NULL));
	return (0);
}

/*
 * Name:	secpolicy_vnode_setdac()
 *
 * Normal:	verify that subject can modify the mode of a file.
 *		allzone privilege needed when modifying root owned object.
 *
 * Output:	EPERM - if access denied.
 */

int
secpolicy_vnode_setdac(const cred_t *cred, uid_t owner)
{
	if (owner == cred->cr_uid)
		return (0);

	return (PRIV_POLICY(cred, PRIV_FILE_OWNER, owner == 0, EPERM, NULL));
}

/*
 * Name:        secpolicy_vnode_access()
 *
 * Parameters:  Process credential
 *		vnode
 *		uid of owner of vnode
 *		permission bits not granted to the caller when examining
 *		file mode bits (i.e., when a process wants to open a
 *		mode 444 file for VREAD|VWRITE, this function should be
 *		called only with a VWRITE argument).
 *
 * Normal:      Verifies that cred has the appropriate privileges to
 *              override the mode bits that were denied.
 *
 * Override:    file_dac_execute - if VEXEC bit was denied and vnode is
 *                      not a directory.
 *              file_dac_read - if VREAD bit was denied.
 *              file_dac_search - if VEXEC bit was denied and vnode is
 *                      a directory.
 *              file_dac_write - if VWRITE bit was denied.
 *
 *		Root owned files are special cased to protect system
 *		configuration files and such.
 *
 * Output:      EACCES - if privilege check fails.
 */

/* ARGSUSED */
int
secpolicy_vnode_access(const cred_t *cr, vnode_t *vp, uid_t owner, mode_t mode)
{
	if ((mode & VREAD) &&
	    PRIV_POLICY(cr, PRIV_FILE_DAC_READ, B_FALSE, EACCES, NULL) != 0)
		return (EACCES);

	if (mode & VWRITE) {
		boolean_t allzone;

		if (owner == 0 && cr->cr_uid != 0)
			allzone = B_TRUE;
		else
			allzone = B_FALSE;
		if (PRIV_POLICY(cr, PRIV_FILE_DAC_WRITE, allzone, EACCES, NULL)
		    != 0)
			return (EACCES);
	}

	if (mode & VEXEC) {
		/*
		 * Directories use file_dac_search to override the execute bit.
		 */
		vtype_t vtype = vp->v_type;

		if (vtype == VDIR)
			return (PRIV_POLICY(cr, PRIV_FILE_DAC_SEARCH, B_FALSE,
			    EACCES, NULL));
		else
			return (PRIV_POLICY(cr, PRIV_FILE_DAC_EXECUTE, B_FALSE,
			    EACCES, NULL));
	}
	return (0);
}

/*
 * Create a file with a group different than any of the groups allowed:
 * the group of the directory the file is created in, the effective
 * group or any of the supplementary groups.
 */
int
secpolicy_vnode_create_gid(const cred_t *cred)
{
	if (HAS_PRIVILEGE(cred, PRIV_FILE_CHOWN))
		return (PRIV_POLICY(cred, PRIV_FILE_CHOWN, B_FALSE, EPERM,
		    NULL));
	else
		return (PRIV_POLICY(cred, PRIV_FILE_CHOWN_SELF, B_FALSE, EPERM,
		    NULL));
}

int
secpolicy_vnode_owner(const cred_t *cr, uid_t owner)
{
	boolean_t allzone = (owner == 0);

	if (owner == cr->cr_uid)
		return (0);

	return (PRIV_POLICY(cr, PRIV_FILE_OWNER, allzone, EPERM, NULL));
}

/*
 * Name:	secpolicy_vnode_chown
 *
 * Normal:	Determine if subject can chown owner of a file.
 *
 * Output:	EPERM - if access denied
 */

int
secpolicy_vnode_chown(const cred_t *cred, boolean_t check_self)
{
	if (HAS_PRIVILEGE(cred, PRIV_FILE_CHOWN))
		return (PRIV_POLICY(cred, PRIV_FILE_CHOWN, B_FALSE, EPERM,
		    NULL));
	else if (check_self)
		return (PRIV_POLICY(cred, PRIV_FILE_CHOWN_SELF, B_FALSE, EPERM,
		    NULL));
	else
		return (EPERM);
}
