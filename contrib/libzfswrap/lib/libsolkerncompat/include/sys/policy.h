/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2006 Ricardo Correia.
 * Use is subject to license terms.
 */

#ifndef _SYS_POLICY_H
#define _SYS_POLICY_H

#include <sys/cred.h>
#include <sys/vnode.h>

enum {
	PRIV_FILE_CHOWN,
	PRIV_FILE_CHOWN_SELF
};

extern long pwd_buflen;
extern long grp_buflen;

#define HAS_PRIVILEGE(cr, pr) (crgetuid(cr) == 0)
#define PRIV_POLICY(cred, priv, all, err, reason) (crgetuid(cred) == 0 ? 0 : err)

#define secpolicy_sys_config(c, co) (0)
#define secpolicy_zfs(c) (0)
#define secpolicy_zinject(c) (0)
#define secpolicy_fs_mount(c,vnode,vfs) (0)
#define secpolicy_fs_mount_clearopts(cr,vfsp) ((void) 0)
#define secpolicy_fs_unmount(c,vfs) (0)

/* In Linux, anyone can set sticky bit in their files/directories */
#define secpolicy_vnode_stky_modify(c) (0)

#define secpolicy_basic_link(cr) (PRIV_POLICY(cr, PRIV_FILE_LINK_ANY, B_FALSE, EPERM, NULL))
#define secpolicy_vnode_utime_modify(cr) (PRIV_POLICY(cr, PRIV_FILE_OWNER, B_FALSE, EPERM, "modify file times"))
#define secpolicy_vnode_remove(cr) (PRIV_POLICY(cr, PRIV_FILE_OWNER, B_FALSE, EACCES, "sticky directory"))
#define secpolicy_nfs(cr) (PRIV_POLICY(cr, PRIV_SYS_NFS, B_FALSE, EPERM, NULL))

extern int secpolicy_vnode_setid_retain(const cred_t *cred, boolean_t issuidroot);
extern void secpolicy_setid_clear(vattr_t *vap, cred_t *cr);
extern int secpolicy_setid_setsticky_clear(vnode_t *vp, vattr_t *vap, const vattr_t *ovap, cred_t *cr);
extern int secpolicy_vnode_setattr(cred_t *cr, struct vnode *vp, struct vattr *vap, const struct vattr *ovap, int flags, int unlocked_access(void *, int, cred_t *), void *node);
extern int secpolicy_vnode_setids_setgids(const cred_t *cred, gid_t gid);
extern int secpolicy_vnode_setdac(const cred_t *cred, uid_t owner);
extern int secpolicy_vnode_access(const cred_t *cr, vnode_t *vp, uid_t owner, mode_t mode);
extern int secpolicy_vnode_create_gid(const cred_t *cred);
extern int secpolicy_vnode_owner(const cred_t *cr, uid_t owner);
extern int secpolicy_xvattr(xvattr_t *, uid_t, cred_t *, vtype_t);
extern int secpolicy_vnode_chown(const cred_t *, boolean_t);

#endif
