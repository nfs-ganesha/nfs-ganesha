/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * posix_acls.c
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat  Inc., 2015
 * Author: Niels de Vos <ndevos@redhat.com>
 *	   Jiffin Tony Thottan <jthottan@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * Conversion routines for fsal_acl <-> POSIX ACl
 *
 * Routines based on the description from an Internet Draft that has also been
 * used for the implementation of the conversion in the Linux kernel
 * NFS-server.
 *
 *     Title: Mapping Between NFSv4 and Posix Draft ACLs
 *   Authors: Marius Aamodt Eriksen & J. Bruce Fields
 *       URL: http://tools.ietf.org/html/draft-ietf-nfsv4-acl-mapping-05
 */

#ifndef _POSIX_ACLS_H
#define _POSIX_ACLS_H

#include <sys/acl.h>
#include "nfs4_acls.h"
#include <acl/libacl.h>
#include "fsal_types.h"

/* inheritance flags checks */
#define IS_FSAL_ACE_HAS_INHERITANCE_FLAGS(ACE) \
	(IS_FSAL_ACE_FILE_INHERIT(ACE) | IS_FSAL_ACE_DIR_INHERIT(ACE) | \
	IS_FSAL_ACE_NO_PROPAGATE(ACE) | IS_FSAL_ACE_INHERIT_ONLY(ACE))

#define IS_FSAL_ACE_APPLICABLE_FOR_BOTH_ACL(ACE) \
	((IS_FSAL_ACE_FILE_INHERIT(ACE) | IS_FSAL_ACE_DIR_INHERIT(ACE)) & \
	!IS_FSAL_ACE_APPLICABLE_ONLY_FOR_INHERITED_ACL(ACE))

#define IS_FSAL_ACE_APPLICABLE_ONLY_FOR_INHERITED_ACL(ACE) \
	((IS_FSAL_ACE_FILE_INHERIT(ACE) | IS_FSAL_ACE_DIR_INHERIT(ACE)) & \
	IS_FSAL_ACE_INHERIT_ONLY(ACE))

/* permission set for ACE's */
#define FSAL_ACE_PERM_SET_DEFAULT \
	(FSAL_ACE_PERM_READ_ACL	| FSAL_ACE_PERM_READ_ATTR \
	| FSAL_ACE_PERM_SYNCHRONIZE)
#define FSAL_ACE_PERM_SET_DEFAULT_WRITE \
	(FSAL_ACE_PERM_WRITE_DATA | FSAL_ACE_PERM_APPEND_DATA)
#define FSAL_ACE_PERM_SET_OWNER_WRITE \
	(FSAL_ACE_PERM_WRITE_ACL | FSAL_ACE_PERM_WRITE_ATTR)

#define ACL_EA_VERSION        0x0002
#define ACL_EA_ACCESS         "system.posix_acl_access"
#define ACL_EA_DEFAULT        "system.posix_acl_default"

struct acl_ea_entry {
	u_int16_t	e_tag;
	u_int16_t	e_perm;
	u_int32_t	e_id;
};

struct acl_ea_header {
	u_int32_t	a_version;
	struct acl_ea_entry	a_entries[0];
};

int
posix_acl_2_fsal_acl(acl_t p_posixacl, bool is_dir, bool is_inherit,
			fsal_ace_t **p_falacl);

acl_t
fsal_acl_2_posix_acl(fsal_acl_t *p_fsalacl, acl_type_t type);

acl_entry_t
find_entry(acl_t acl, acl_tag_t tag, unsigned int id);

acl_entry_t
get_entry(acl_t acl, acl_tag_t tag, unsigned int id);

int
ace_count(acl_t acl);

size_t posix_acl_xattr_size(int count);

int posix_acl_entries_count(size_t size);

acl_t
xattr_2_posix_acl(const struct acl_ea_header *ea_header, size_t size);

int
posix_acl_2_xattr(acl_t acl, void *buf, size_t size);

static inline uid_t posix_acl_get_uid(acl_entry_t entry_d)
{
	void *q = acl_get_qualifier(entry_d);
	uid_t u = *(uid_t *)q;

	acl_free(q);
	return u;
}

static inline gid_t posix_acl_get_gid(acl_entry_t entry_d)
{
	void *q = acl_get_qualifier(entry_d);
	gid_t g = *(gid_t *)q;

	acl_free(q);
	return g;
}

#endif /* _POSIX_ACLS_H */
