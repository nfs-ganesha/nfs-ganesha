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


#include <acl/libacl.h>
#include "nfs4_acls.h"
#include "fsal_types.h"
#include <sys/acl.h>
#include "posix_acls.h"

/* add permissions in posix acl entry according to allow ace */
void
convert_allow_entry_to_posix(fsal_ace_t *ace, acl_permset_t *p_permset) {

	if (IS_FSAL_ACE_READ_DATA(*ace))
		acl_add_perm(*p_permset, ACL_READ);
	if (IS_FSAL_ACE_WRITE_DATA(*ace))
		acl_add_perm(*p_permset, ACL_WRITE);
	if (IS_FSAL_ACE_EXECUTE(*ace))
		acl_add_perm(*p_permset, ACL_EXECUTE);
}

/* delete permissions in posix acl entry according to deny ace */
void
convert_deny_entry_to_posix(fsal_ace_t *ace, acl_permset_t *p_permset) {

	if (IS_FSAL_ACE_READ_DATA(*ace))
		acl_delete_perm(*p_permset, ACL_READ);
	if (IS_FSAL_ACE_WRITE_DATA(*ace))
		acl_delete_perm(*p_permset, ACL_WRITE);
	if (IS_FSAL_ACE_EXECUTE(*ace))
		acl_delete_perm(*p_permset, ACL_EXECUTE);
}

/* Finds ACL entry with help of tag and id */
acl_entry_t
find_entry(acl_t acl, acl_tag_t tag, int id) {
	acl_entry_t entry;
	acl_tag_t entryTag;
	int ent, ret;

	if (!acl)
		return NULL;

	for (ent = ACL_FIRST_ENTRY; ; ent = ACL_NEXT_ENTRY) {
		ret = acl_get_entry(acl, ent, &entry);
		if (ret == -1) {
			LogWarn(COMPONENT_FSAL, "acl_get entry failed errno %d",
					errno);
		}
		if (ret == 0 || ret == -1)
			return NULL;

		if (acl_get_tag_type(entry, &entryTag) == -1) {
			LogWarn(COMPONENT_FSAL, "No entry tag for ACL Entry");
			continue;
		}
		if (tag == entryTag) {
			if (tag == ACL_USER || tag == ACL_GROUP)
				if (id != *(int *)acl_get_qualifier(entry))
					continue;
			break;
		}
	}

	return entry;
}

/*
 *  Given a POSIX ACL convert it into an equivalent FSAL ACL
 */
fsal_status_t
posix_acl_2_fsal_acl(acl_t p_posixacl, fsal_acl_t **p_falacl)
{
	int ret = 0, ent, i = 0;
	fsal_acl_status_t status;
	fsal_acl_data_t acldata;
	fsal_ace_t *pace = NULL;
	fsal_acl_t *pacl = NULL;
	acl_entry_t entry, mask;
	acl_tag_t tag;
	acl_permset_t p_permset;
	bool readmask = true;
	bool writemask = true;
	bool executemask = true;

	if (!p_posixacl)
		return fsalstat(ERR_FSAL_FAULT, ret);

	acldata.naces = acl_entries(p_posixacl);

	if (!acldata.naces)
		return fsalstat(ERR_FSAL_FAULT, ret);

	mask = find_entry(p_posixacl, ACL_MASK, 0);
	if (mask) {
		ret = acl_get_permset(mask, &p_permset);
		if (ret)
			LogWarn(COMPONENT_FSAL,
			"Cannot retrieve permission set for the Mask Entry");
		if (acl_get_perm(p_permset, ACL_READ) == 0)
			readmask = false;
		if (acl_get_perm(p_permset, ACL_WRITE) == 0)
			writemask = false;
		if (acl_get_perm(p_permset, ACL_EXECUTE) == 0)
			executemask = false;
		acldata.naces--;
	}
	/* *
	 * Only ALLOW ACL Entries considered right now
	 * TODO : How to display DENY ACL Entries
	 * */
	acldata.aces = (fsal_ace_t *) nfs4_ace_alloc(acldata.naces);

	for (pace = acldata.aces, ent = ACL_FIRST_ENTRY;
		i < acldata.naces; ent = ACL_NEXT_ENTRY) {

		ret = acl_get_entry(p_posixacl, ent, &entry);
		if (ret == 0 || ret == -1) {
			LogWarn(COMPONENT_FSAL,
					"No more ACL entires remaining");
			break;
		}
		if (acl_get_tag_type(entry, &tag) == -1) {
			LogWarn(COMPONENT_FSAL, "No entry tag for ACL Entry");
			continue;
		}
		/* Mask is not converted to a fsal_acl entry , skipping */
		if (tag == ACL_MASK)
			continue;

		pace->type = FSAL_ACE_TYPE_ALLOW;
		pace->flag = 0;

		/* Finding uid for the fsal_acl entry */
		switch (tag) {
		case  ACL_USER_OBJ:
			pace->who.uid =  FSAL_ACE_SPECIAL_OWNER;
			pace->iflag = FSAL_ACE_IFLAG_SPECIAL_ID;
			break;
		case  ACL_GROUP_OBJ:
			pace->who.uid =  FSAL_ACE_SPECIAL_GROUP;
			pace->iflag = FSAL_ACE_IFLAG_SPECIAL_ID;
			break;
		case  ACL_OTHER:
			pace->who.uid =  FSAL_ACE_SPECIAL_EVERYONE;
			pace->iflag = FSAL_ACE_IFLAG_SPECIAL_ID;
			break;
		case  ACL_USER:
			pace->who.uid =
				*(uid_t *)acl_get_qualifier(entry);
			break;
		case  ACL_GROUP:
			pace->who.gid =
				*(gid_t *)acl_get_qualifier(entry);
			pace->flag = FSAL_ACE_FLAG_GROUP_ID;
			break;
		default:
			LogWarn(COMPONENT_FSAL, "Invalid tag for the acl");
		}

		/* *
		 * Finding permission set for the fsal_acl entry.
		 * Conversion purely is based on
		 * http://tools.ietf.org/html/draft-ietf-nfsv4-acl-mapping-05
		 * */

		/* *
		 * Unconditionally all ALLOW ACL Entry should
		 * have these permissions
		 * */

		pace->perm = FSAL_ACE_PERM_SET_DEFAULT;
		ret = acl_get_permset(entry, &p_permset);
		if (ret) {
			LogWarn(COMPONENT_FSAL,
			"Cannot retrieve permission set for the ACL Entry");
			continue;
		}
		/* *
		 * Consider Mask bits only for ACL_USER, ACL_GROUP,
		 * ACL_GROUP_OBJ entries
		 * */
		if (acl_get_perm(p_permset, ACL_READ)) {
			if (tag == ACL_USER_OBJ || tag == ACL_OTHER ||
						readmask)
				pace->perm = pace->perm
						| FSAL_ACE_PERM_READ_DATA;
		}
		if (acl_get_perm(p_permset, ACL_WRITE)) {
			if (tag == ACL_USER_OBJ || tag == ACL_OTHER ||
						writemask)
				pace->perm = pace->perm
					| FSAL_ACE_PERM_SET_DEFAULT_WRITE;
			if (tag == ACL_USER_OBJ)
				pace->perm = pace->perm
						| FSAL_ACE_PERM_SET_OWNER_WRITE;
		}
		if (acl_get_perm(p_permset, ACL_EXECUTE)) {
			if (tag == ACL_USER_OBJ || tag == ACL_OTHER ||
						executemask)
				pace->perm = pace->perm
						| FSAL_ACE_PERM_EXECUTE;
		}
		i++;
		pace++;
	}
	pacl = nfs4_acl_new_entry(&acldata, &status);
	LogMidDebug(COMPONENT_FSAL, "fsal acl = %p, fsal_acl_status = %u", pacl,
		    status);
	if (pacl == NULL) {
		LogCrit(COMPONENT_FSAL,
		"posix_acl_2_fsal_acl: failed to create a new acl entry");
		return fsalstat(ERR_FSAL_FAULT, ret);
	} else {
		*p_falacl = pacl;
		return fsalstat(ERR_FSAL_NO_ERROR, ret);
	}
}

/*
 *  Given a FSAL ACL convert it into an equivalent POSIX ACL
 */
acl_t
fsal_acl_2_posix_acl(fsal_acl_t *p_fsalacl)
{
	int ret = 0, i, u_count = 0, g_count = 0;
	fsal_ace_t *f_ace, *deny_ace = NULL;
	acl_t p_acl;
	acl_entry_t p_entry;
	acl_permset_t p_permset;
	/* FIXME : The no of users/groups is limited to hundred here */
	uid_t uid[100], tmp_uid;
	gid_t gid[100], tmp_gid;
	bool user = false;
	bool group = false;
	bool dup_entry = false;

	if (p_fsalacl == NULL)
		return NULL;
	/*
	 * Populating list of users and group which are not special.
	 * Mulitple entries for same users and group is possible, so
	 * avoid duplicate entries in the array.
	 * TODO: Use a separate api for performing this function
	 * Annoymous users/groups (id = -1) should handle properly
	 */
	for (f_ace = p_fsalacl->aces;
	f_ace < p_fsalacl->aces + p_fsalacl->naces; f_ace++) {
		if (!IS_FSAL_ACE_SPECIAL_ID(*f_ace)) {
			if (IS_FSAL_ACE_GROUP_ID(*f_ace)) {
				tmp_gid = GET_FSAL_ACE_WHO(*f_ace);
				for (i = 0; i < g_count ; i++) {
					if (gid[i] == tmp_gid) {
						dup_entry = true;
						break;
					}
				}
				if (dup_entry) {
					dup_entry = false;
					continue;
				}
				gid[g_count++] = tmp_gid;
			} else {
				tmp_uid = GET_FSAL_ACE_WHO(*f_ace);
				for (i = 0; i < u_count ; i++) {
					if (uid[i] == tmp_uid) {
						dup_entry = true;
						break;
					}
				}
				if (dup_entry) {
					dup_entry = false;
					continue;
				}
				uid[u_count++] = tmp_uid;
			}
		}
	}
	if (u_count > 0)
		user = true;
	if (g_count > 0)
		group = true;

	LogDebug(COMPONENT_FSAL, "u_count = %d g_count = %d entries = %d",
		       u_count, g_count, p_fsalacl->naces);
	/*
	 * The fsal_acl list is unordered, but we need to keep posix_acl in
	 * a specific order such that users, groups, other.So we will fetch
	 * ACE's from the fsal_acl in that order and convert it into a
	 * corresponding posix_acl entry
	 *
	 * TODO: Currently there are lots of code duplication in this api
	 *       So its better implement different api's for :
	 *             1.) fetching a required ACE from fsal_acl
	 *             2.) Convert a ACE into corresponding posix_acl_entry
	 *	and use them
	 */
	p_acl = acl_init(p_fsalacl->naces);

	ret = acl_create_entry(&p_acl, &p_entry);
	if (ret) {
		LogMajor(COMPONENT_FSAL, "Cannot create entry for user");
		return NULL;
	}

	ret = acl_set_tag_type(p_entry, ACL_USER_OBJ);
	if (ret)
		LogWarn(COMPONENT_FSAL, "Cannot set tag for Entry");

	ret = acl_get_permset(p_entry, &p_permset);

	/* Deny entry is handled at the end */
	for (f_ace = p_fsalacl->aces;
		f_ace < p_fsalacl->aces + p_fsalacl->naces; f_ace++) {
			if (IS_FSAL_ACE_SPECIAL_OWNER(*f_ace)) {
				if (IS_FSAL_ACE_DENY(*f_ace))
					deny_ace = f_ace;
				else if (IS_FSAL_ACE_ALLOW(*f_ace))
					convert_allow_entry_to_posix(f_ace,
								&p_permset);
			}
	}

	if (deny_ace) {
		convert_deny_entry_to_posix(deny_ace, &p_permset);
		deny_ace = NULL;
	}

	if (user) {
		for (i = 0; i < u_count; i++) {
			ret = acl_create_entry(&p_acl, &p_entry);
			if (ret)
				LogWarn(COMPONENT_FSAL,
					"Cannot create entry for user id %d",
					uid[i]);
			ret = acl_set_tag_type(p_entry, ACL_USER);
			if (ret)
				LogWarn(COMPONENT_FSAL,
					"Cannot set tag for ACL Entry");

			ret = acl_set_qualifier(p_entry, &uid[i]);

			ret = acl_get_permset(p_entry, &p_permset);
			for (f_ace = p_fsalacl->aces;
			f_ace < p_fsalacl->aces + p_fsalacl->naces; f_ace++) {
				if (IS_FSAL_ACE_USER(*f_ace, uid[i])) {
					if (IS_FSAL_ACE_DENY(*f_ace))
						deny_ace = f_ace;
					else if (IS_FSAL_ACE_ALLOW(*f_ace))
						convert_allow_entry_to_posix(
							f_ace, &p_permset);
				}
			}

			if (deny_ace) {
				convert_deny_entry_to_posix(deny_ace,
								 &p_permset);
				deny_ace = NULL;
			}
			if (!acl_get_perm(p_permset, ACL_READ)
				&& !acl_get_perm(p_permset, ACL_WRITE)
				&& !acl_get_perm(p_permset, ACL_EXECUTE))
				acl_delete_entry(p_acl, p_entry);
		}
	}
	ret = acl_create_entry(&p_acl, &p_entry);
	if (ret) {
		LogMajor(COMPONENT_FSAL, "Cannot create entry for group");
		return NULL;
	}

	ret = acl_set_tag_type(p_entry, ACL_GROUP_OBJ);
	if (ret)
		LogWarn(COMPONENT_FSAL, "Cannot set tag for ACL Entry");
	ret = acl_get_permset(p_entry, &p_permset);

	for (f_ace = p_fsalacl->aces;
		f_ace < p_fsalacl->aces + p_fsalacl->naces; f_ace++) {
			if (IS_FSAL_ACE_SPECIAL_GROUP(*f_ace)) {
				if (IS_FSAL_ACE_DENY(*f_ace))
					deny_ace = f_ace;
				else if (IS_FSAL_ACE_ALLOW(*f_ace))
					convert_allow_entry_to_posix(f_ace,
								&p_permset);
			}
	}

	if (deny_ace) {
		convert_deny_entry_to_posix(deny_ace, &p_permset);
		deny_ace = NULL;
	}

	if (group) {
		for (i = 0; i < g_count; i++) {
			ret = acl_create_entry(&p_acl, &p_entry);
			if (ret)
				LogWarn(COMPONENT_FSAL,
				"Cannot create entry for group id %d",
				gid[i]);

			ret = acl_set_tag_type(p_entry, ACL_GROUP);
			if (ret)
				LogWarn(COMPONENT_FSAL,
				      "Cannot set tag for ACL Entry");

			ret = acl_set_qualifier(p_entry, &gid[i]);

			ret = acl_get_permset(p_entry, &p_permset);
			for (f_ace = p_fsalacl->aces;
			f_ace < p_fsalacl->aces + p_fsalacl->naces; f_ace++) {
				if (IS_FSAL_ACE_GROUP(*f_ace, gid[i])) {
					if (IS_FSAL_ACE_DENY(*f_ace))
						deny_ace = f_ace;
					else if (IS_FSAL_ACE_ALLOW(*f_ace))
						convert_allow_entry_to_posix(
							f_ace, &p_permset);
				}
			}

			if (deny_ace) {
				convert_deny_entry_to_posix(deny_ace,
								 &p_permset);
				deny_ace = NULL;
			}
			if (!acl_get_perm(p_permset, ACL_READ)
				&& !acl_get_perm(p_permset, ACL_WRITE)
				     && !acl_get_perm(p_permset, ACL_EXECUTE))
					acl_delete_entry(p_acl, p_entry);
		}
	}
	ret = acl_create_entry(&p_acl, &p_entry);
	if (ret) {
		LogMajor(COMPONENT_FSAL, "Cannot create entry for other");
		return NULL;
	}
	ret = acl_set_tag_type(p_entry, ACL_OTHER);
	if (ret)
		LogWarn(COMPONENT_FSAL, "Cannot set tag for ACL Entry");

	ret = acl_get_permset(p_entry, &p_permset);
	for (f_ace = p_fsalacl->aces;
		f_ace < p_fsalacl->aces + p_fsalacl->naces; f_ace++) {
			if (IS_FSAL_ACE_SPECIAL_EVERYONE(*f_ace)) {
				if (IS_FSAL_ACE_DENY(*f_ace))
					deny_ace = f_ace;
				else if (IS_FSAL_ACE_ALLOW(*f_ace))
					convert_allow_entry_to_posix(f_ace,
								&p_permset);
			}
	}

	if (deny_ace) {
		convert_deny_entry_to_posix(deny_ace, &p_permset);
		deny_ace = NULL;
	}

	/* calculate appropriate mask if it is needed*/
	if (user || group) {
		ret = acl_calc_mask(&p_acl);
		if (ret)
			LogWarn(COMPONENT_FSAL,
			"Cannot calculate mask for posix");
	}

	/* A valid acl_t should have only one entry for
	 * ACL_USER_OBJ, ACL_GROUP_OBJ, ACL_OTHER and
	 * ACL_MASK is required only if ACL_USER or
	 * ACL_GROUP exists
	 */
	ret = acl_check(p_acl, &i);
	if (ret) {
		if (ret > 0) {
			LogWarn(COMPONENT_FSAL,
			"Error converting ACL: %s at entry no %d",
			acl_error(ret), i);
		}
		return NULL;

	}
	LogDebug(COMPONENT_FSAL, "posix acl = %s ",
				acl_to_any_text(p_acl, NULL, ',',
					TEXT_ABBREVIATE |
					TEXT_NUMERIC_IDS));
	return p_acl;
}
