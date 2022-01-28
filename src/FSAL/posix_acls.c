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


#include "posix_acls.h"

/* Checks whether ACE belongs to effective acl (ACCESS TYPE) */
bool is_ace_valid_for_effective_acl_entry(fsal_ace_t *ace)
{
	bool ret;

	if (IS_FSAL_ACE_HAS_INHERITANCE_FLAGS(*ace)) {
		if (IS_FSAL_ACE_APPLICABLE_FOR_BOTH_ACL(*ace))
			ret = true;
		else
			ret = false;
	} else
		ret = true;

	return ret;
}

/* Checks whether ACE belongs to inherited acl (DEFAULT TYPE) */
bool is_ace_valid_for_inherited_acl_entry(fsal_ace_t *ace)
{

	if (IS_FSAL_ACE_APPLICABLE_FOR_BOTH_ACL(*ace)
	    || IS_FSAL_ACE_APPLICABLE_ONLY_FOR_INHERITED_ACL(*ace))
		return  true;
	else
		return false;

}

/*
 * Check whether given perm(ACL_READ, ACL_WRITE or ACL_EXECUTE) is allowed for
 * a permset, it depends on given ace and permset of @EVERYONE entry.
 */
bool isallow(fsal_ace_t *ace, acl_permset_t everyone, acl_perm_t perm)
{

	bool ret = acl_get_perm(everyone, perm);

	switch (perm) {
	case ACL_READ:
		ret |= IS_FSAL_ACE_READ_DATA(*ace);
		break;
	case ACL_WRITE:
		ret |= IS_FSAL_ACE_WRITE_DATA(*ace);
		break;
	case ACL_EXECUTE:
		ret |= IS_FSAL_ACE_EXECUTE(*ace);
		break;
	}

	return ret;
}

/*
 * Check whether given perm(ACL_READ, ACL_WRITE or ACL_EXECUTE) is denied for a
 * permset, it depends on permsets of deny entry of the acl and @EVERYONE entry.
 */
bool isdeny(acl_permset_t deny, acl_permset_t everyone, acl_perm_t perm)
{

	return acl_get_perm(deny, perm) || acl_get_perm(everyone, perm);
}

/* Returns no of possible fsal_ace entries from a given posix_acl */
int ace_count(acl_t acl)
{
	int ret;

	ret = acl_entries(acl);
	if (ret < 0)
		return 0;

	/* Mask is not converted to an ace entry */
	if (find_entry(acl, ACL_MASK, 0))
		ret--;

	return ret;
}

/*
 * It traverse entire list of entries for a posix acl and finds ACL entry which
 * corresponds to a given tag and id
 *
 * On success , it returns acl entry otherwise it returns NULL
 */
acl_entry_t find_entry(acl_t acl, acl_tag_t tag,  unsigned int id)
{
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
				if (id != *(unsigned int *)
						acl_get_qualifier(entry))
					continue;
			break;
		}
	}

	return entry;
}

/*
 * It tries to find out whether an entry is present in posix acl for the given
 * (tag, id) tuple and returns it. If not , it will create a new entry for
 * given (tag, id).
 *
 * On success , it returns acl entry otherwise it returns NULL
 */
acl_entry_t get_entry(acl_t acl, acl_tag_t tag, unsigned int id)
{
	acl_entry_t entry;
	int ret;

	if (!acl)
		return NULL;
	entry = find_entry(acl, tag, id);

	if (!entry) {
		ret = acl_create_entry(&acl, &entry);
		if (ret) {
			LogMajor(COMPONENT_FSAL, "Cannot create entry");
			return NULL;
		}
		ret = acl_set_tag_type(entry, tag);
		if (ret)
			LogWarn(COMPONENT_FSAL, "Cannot set tag for Entry");
		ret = acl_set_qualifier(entry, &id);
		if (ret) {
			LogWarn(COMPONENT_FSAL, "Failed to set id");
			return NULL;
		}
	}

	return entry;
}

/*
 *  @brief convert POSIX ACL into an equivalent FSAL ACL
 *
 * @param[in]  p_posixacl	POSIX ACL
 * @param[in]  is_dir		Represents file/directory
 * @param[in]  is_inherit	Represents type of ace entry
 * @param[in]  ace		Stores the starting of fsal_acl_t
 * @param[out] ace		Stores last ace entry in fsal_acl_t
 *
 * @returns no of entries on success and -1 on failure
 */

int posix_acl_2_fsal_acl(acl_t p_posixacl, bool is_dir, bool is_inherit,
			 fsal_ace_t **ace)
{
	int ret = 0, ent, d_ent, total = 0;
	fsal_ace_t *pace_deny = NULL, *pace_allow = NULL;
	acl_t dup_acl;
	acl_entry_t entry, mask, other, d_entry, dup_mask;
	acl_tag_t tag;
	acl_permset_t p_permset;
	bool readmask = true, readother = false, readcurrent = true;
	bool writemask = true, writeother = false, writecurrent = true;
	bool executemask = true, executeother = false, executecurrent = true;

	if (!p_posixacl)
		return -1;

	pace_deny = *ace;
	pace_allow = (pace_deny + 1);
	/* Store the mask entry values */
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
	}

	other = find_entry(p_posixacl, ACL_OTHER, 0);
	if (other) {
		ret = acl_get_permset(other, &p_permset);
		if (ret)
			LogWarn(COMPONENT_FSAL,
			"Cannot retrieve permission set for the Mask Entry");
		if (acl_get_perm(p_permset, ACL_READ) == 1)
			readother = true;
		if (acl_get_perm(p_permset, ACL_WRITE) == 1)
			writeother = true;
		if (acl_get_perm(p_permset, ACL_EXECUTE) == 1)
			executeother = true;
	}

	/* *
	 * Converts each entry in posix acl into fsal_ace by filling type, flag,
	 * perm, iflag, flag and who(uid, gid) appropiately
	 *
	 * Corresponding to each posix acl entry, there is a possiblity of two
	 * fsal_aces, it can either be ALLOW or DENY. The DENY added to list
	 * depending on the permission set set of other entries.
	 *
	 * Here both entries are created for a posix acl entry and filled up
	 * correspondingly. Then at the end unnecessary DENY entries are removed
	 * from the list.
	 */
	for (ent = ACL_FIRST_ENTRY; ; ent = ACL_NEXT_ENTRY) {

		ret = acl_get_entry(p_posixacl, ent, &entry);
		if (ret == 0 || ret == -1) {
			LogDebug(COMPONENT_FSAL,
					"No more ACL entries remaining");
			break;
		}
		if (acl_get_tag_type(entry, &tag) == -1) {
			LogWarn(COMPONENT_FSAL, "No entry tag for ACL Entry");
			continue;
		}

		pace_deny->type = FSAL_ACE_TYPE_DENY;
		pace_allow->type = FSAL_ACE_TYPE_ALLOW;

		if (is_inherit)
			pace_allow->flag = pace_deny->flag =
				FSAL_ACE_FLAG_INHERIT;
		else
			pace_allow->flag = pace_deny->flag = 0;

		/* Finding uid for the fsal_acl entry */
		switch (tag) {
		case  ACL_USER_OBJ:
			pace_allow->who.uid = pace_deny->who.uid =
						FSAL_ACE_SPECIAL_OWNER;
			pace_allow->iflag = pace_deny->iflag =
						FSAL_ACE_IFLAG_SPECIAL_ID;
			break;
		case  ACL_GROUP_OBJ:
			pace_allow->who.uid = pace_deny->who.uid =
						FSAL_ACE_SPECIAL_GROUP;
			pace_allow->iflag = pace_deny->iflag =
						FSAL_ACE_IFLAG_SPECIAL_ID;
			break;
		case  ACL_OTHER:
			pace_allow->who.uid = pace_deny->who.uid =
						FSAL_ACE_SPECIAL_EVERYONE;
			pace_allow->iflag = pace_deny->iflag =
						FSAL_ACE_IFLAG_SPECIAL_ID;
			break;
		case  ACL_USER:
			pace_allow->who.uid = pace_deny->who.uid =
					*(uid_t *)acl_get_qualifier(entry);
			break;
		case  ACL_GROUP:
			pace_allow->who.gid = pace_deny->who.gid =
					*(gid_t *)acl_get_qualifier(entry);
			pace_allow->flag = pace_deny->flag |=
						FSAL_ACE_FLAG_GROUP_ID;
			break;
		case  ACL_MASK:
			pace_allow->who.uid = pace_deny->who.uid =
						FSAL_ACE_SPECIAL_MASK;
			pace_allow->iflag = pace_deny->iflag =
						FSAL_ACE_IFLAG_SPECIAL_ID;
			break;
		default:
			LogWarn(COMPONENT_FSAL, "Invalid tag for the acl");
		}

		/* *
		 * Finding permission set for the fsal_acl ALLOW entry.
		 * Conversion purely is based on
		 * http://tools.ietf.org/html/draft-ietf-nfsv4-acl-mapping-05
		 * */

		/* *
		 * Unconditionally all ALLOW ACL Entry should have these
		 * permissions
		 * */

		pace_allow->perm = FSAL_ACE_PERM_SET_DEFAULT;
		pace_deny->perm = 0;

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
			if (tag == ACL_USER_OBJ || tag == ACL_OTHER || readmask)
				pace_allow->perm |= FSAL_ACE_PERM_READ_DATA;
			if ((tag == ACL_USER || tag == ACL_GROUP ||
					tag == ACL_GROUP_OBJ) && (!readmask))
				pace_allow->iflag |=
						FSAL_ACE_FLAG_MASK_READ_DENY;
		} else
			readcurrent = false;

		if (acl_get_perm(p_permset, ACL_WRITE)) {
			if (tag == ACL_USER_OBJ || tag == ACL_OTHER ||
						writemask)
				pace_allow->perm |=
					FSAL_ACE_PERM_SET_DEFAULT_WRITE;
			if (tag == ACL_USER_OBJ)
				pace_allow->perm |=
					FSAL_ACE_PERM_SET_OWNER_WRITE;
			if (is_dir)
				pace_allow->perm |=
					FSAL_ACE_PERM_DELETE_CHILD;
			if ((tag == ACL_USER || tag == ACL_GROUP ||
					tag == ACL_GROUP_OBJ) && (!writemask))
				pace_allow->iflag |=
					FSAL_ACE_FLAG_MASK_WRITE_DENY;
		} else
			writecurrent = false;

		if (acl_get_perm(p_permset, ACL_EXECUTE)) {
			if (tag == ACL_USER_OBJ || tag == ACL_OTHER ||
						executemask)
				pace_allow->perm |= FSAL_ACE_PERM_EXECUTE;
			if ((tag == ACL_USER || tag == ACL_GROUP ||
					tag == ACL_GROUP_OBJ) && (!executemask))
				pace_allow->iflag |=
					FSAL_ACE_FLAG_MASK_EXECUTE_DENY;
		} else
			executecurrent = false;

		/* *
		 * Filling up permission set for DENY entries based on ALLOW
		 * entries , if it is applicable.
		 * If the tag is ACL_USER_OBJ or ACL_USER then all later posix
		 * acl entries should be considered.
		 * If the tag is either ACL_GROUP_OBJ or ACL_GROUP then consider
		 * only ACL_OTHER.
		 */
		if (tag == ACL_USER_OBJ || tag == ACL_USER) {
			dup_acl = acl_dup(p_posixacl);

			/*
			 * Do not consider ACL_MASK entry in the following loop
			 */
			if (mask) {
				dup_mask = find_entry(dup_acl, ACL_MASK, 0);
				if (dup_mask)
					acl_delete_entry(dup_acl, dup_mask);
			}

			if (tag == ACL_USER_OBJ) {
				d_entry = find_entry(dup_acl, ACL_USER_OBJ, 0);
				ret = acl_get_entry(dup_acl, ACL_NEXT_ENTRY,
						&d_entry);
				if (ret == 0 || ret == -1) {
					LogDebug(COMPONENT_FSAL,
					"No more ACL entries remaining");
					break;
				}
			} else
				d_entry = find_entry(dup_acl, ACL_GROUP_OBJ, 0);

			for (d_ent = ACL_NEXT_ENTRY; ; d_ent = ACL_NEXT_ENTRY) {
				ret = acl_get_permset(d_entry, &p_permset);
				if (ret) {
					LogWarn(COMPONENT_FSAL,
					"Cannot retrieve permission set");
					continue;
				}

				if (!readcurrent &&
					acl_get_perm(p_permset, ACL_READ))
					pace_deny->perm |=
						FSAL_ACE_PERM_READ_DATA;
				if (!writecurrent &&
					acl_get_perm(p_permset, ACL_WRITE)) {
					pace_deny->perm |=
						FSAL_ACE_PERM_SET_DEFAULT_WRITE;
					if (tag == ACL_USER_OBJ)
						pace_deny->perm |=
						FSAL_ACE_PERM_SET_OWNER_WRITE;
					if (is_dir)
						pace_deny->perm |=
						FSAL_ACE_PERM_DELETE_CHILD;
				}
				if (!executecurrent &&
					acl_get_perm(p_permset, ACL_EXECUTE))
					pace_deny->perm |=
						FSAL_ACE_PERM_EXECUTE;
				ret = acl_get_entry(dup_acl, d_ent, &d_entry);
				if (ret == 0 || ret == -1) {
					LogDebug(COMPONENT_FSAL,
					"No more ACL entries remaining");
					break;
				}
			}
			acl_free(dup_acl);

		} else if (tag == ACL_GROUP_OBJ || tag == ACL_GROUP) {
			if (!readcurrent && readother)
				pace_deny->perm |= FSAL_ACE_PERM_READ_DATA;
			if (!writecurrent && writeother) {
				pace_deny->perm |=
					FSAL_ACE_PERM_SET_DEFAULT_WRITE;
				if (is_dir)
					pace_deny->perm |=
						FSAL_ACE_PERM_DELETE_CHILD;
			}
			if (!executecurrent && executeother)
				pace_deny->perm |= FSAL_ACE_PERM_EXECUTE;
		}
		readcurrent = writecurrent = executecurrent = true;

		/* Removing DENY entries if it is not present */
		if (pace_deny->perm == 0) {
			*pace_deny = *pace_allow;
			memset(pace_allow, 0, sizeof(fsal_ace_t));
			total += 1;
			pace_deny += 1;
			pace_allow += 1;
		} else {
			total += 2;
			pace_deny += 2;
			pace_allow += 2;
		}


	}

	*ace = pace_allow - 1;/* Returns last entry in the list */
	return total; /* Returning no of entries in the list */
}

/*
 * @brief convert FSAL ACL into an equivalent POSIX ACL
 *
 * @param[in]  p_fsalacl	FSAL ACL
 * @param[in]  type		Represents type of posix acl( ACCESS/DEFAULT )
 *
 * @return acl_t structure
 */
acl_t fsal_acl_2_posix_acl(fsal_acl_t *p_fsalacl, acl_type_t type)
{
	int ret = 0, i;
	fsal_ace_t *f_ace;
	acl_t allow_acl, deny_acl;
	acl_entry_t a_entry, d_entry;
	acl_permset_t a_permset, e_a_permset, d_permset, e_d_permset;
	acl_tag_t tag = -1;
	unsigned int id;
	bool mask = false, mask_set = false;
	bool deny_e_r = false, deny_e_w = false, deny_e_x = false;


	if (p_fsalacl == NULL)
		return NULL;

	/* *
	 * Check whether ace list contains any inherited entries, if not then
	 * returns NULL.
	 * */
	if (type == ACL_TYPE_DEFAULT) {
		for (f_ace = p_fsalacl->aces;
			f_ace < p_fsalacl->aces + p_fsalacl->naces; f_ace++) {
			if (is_ace_valid_for_inherited_acl_entry(f_ace))
				ret++;
		}
		if (ret == 0)
			return NULL;
	}

	/*
	 * FIXME: Always allocating with maximum possible value of acl entries,
	 * there is a possibility of memory leak
	 */
	allow_acl = acl_init(p_fsalacl->naces + 1);
	deny_acl = acl_init(p_fsalacl->naces + 1);

	/* first convert ACE EVERYONE@ to ACL_OTHER */
	ret = acl_create_entry(&allow_acl, &a_entry);
	if (ret) {
		LogMajor(COMPONENT_FSAL, "Cannot create entry for other");
		return NULL;
	}
	ret = acl_set_tag_type(a_entry, ACL_OTHER);
	if (ret)
		LogWarn(COMPONENT_FSAL, "Cannot set tag for ACL Entry");

	ret = acl_get_permset(a_entry, &e_a_permset);
	if (ret) {
		LogWarn(COMPONENT_FSAL,
			"Cannot retrieve permission set");
	}

	/*
	 * Deny entry for @EVERYONE created only because it will ease the
	 * manipulation of other acl entries. It will be updated only when deny
	 * entry for @EVERYONE is encountered
	 */
	ret = acl_create_entry(&deny_acl, &d_entry);
	if (ret)
		LogMajor(COMPONENT_FSAL, "Cannot create entry for other");

	ret = acl_set_tag_type(d_entry, ACL_OTHER);
	if (ret)
		LogWarn(COMPONENT_FSAL, "Cannot set tag for ACL Entry");

	ret = acl_get_permset(d_entry, &e_d_permset);
	if (ret) {
		LogWarn(COMPONENT_FSAL,
			"Cannot retrieve permission set");
	}

	for (f_ace = p_fsalacl->aces;
		f_ace < p_fsalacl->aces + p_fsalacl->naces; f_ace++) {
		if (IS_FSAL_ACE_SPECIAL_EVERYONE(*f_ace)) {
			if ((type == ACL_TYPE_ACCESS &&
			!is_ace_valid_for_effective_acl_entry(f_ace))
			|| (type == ACL_TYPE_DEFAULT &&
			!is_ace_valid_for_inherited_acl_entry(f_ace)))
				continue;

			if (IS_FSAL_ACE_DENY(*f_ace)) {
				if (IS_FSAL_ACE_READ_DATA(*f_ace))
					deny_e_r = true;
				if (IS_FSAL_ACE_WRITE_DATA(*f_ace))
					deny_e_w = true;
				if (IS_FSAL_ACE_EXECUTE(*f_ace))
					deny_e_x = true;
			} else if (IS_FSAL_ACE_ALLOW(*f_ace)) {
				if (IS_FSAL_ACE_READ_DATA(*f_ace) && !deny_e_r)
					acl_add_perm(e_a_permset, ACL_READ);
				if (IS_FSAL_ACE_WRITE_DATA(*f_ace) && !deny_e_w)
					acl_add_perm(e_a_permset, ACL_WRITE);
				if (IS_FSAL_ACE_EXECUTE(*f_ace) && !deny_e_x)
					acl_add_perm(e_a_permset, ACL_EXECUTE);
			}
		}
	}

	/*
	 * It is mandatory to have acl entries for ACL_USER_OBJ and
	 * ACL_GROUP_OBJ
	 */
	ret = acl_create_entry(&allow_acl, &a_entry);
	if (ret) {
		LogMajor(COMPONENT_FSAL, "Cannot create entry for other");
		return NULL;
	}
	ret = acl_set_tag_type(a_entry, ACL_USER_OBJ);
	if (ret)
		LogWarn(COMPONENT_FSAL, "Cannot set tag for ACL Entry");

	ret = acl_create_entry(&allow_acl, &a_entry);
	if (ret) {
		LogMajor(COMPONENT_FSAL, "Cannot create entry for other");
		return NULL;
	}

	ret = acl_set_tag_type(a_entry, ACL_GROUP_OBJ);
	if (ret)
		LogWarn(COMPONENT_FSAL, "Cannot set tag for ACL Entry");

	/** @todo: Annoymous users/groups (id = -1) should handle properly
	 */

	/*
	 * It uses two posix acl - allow_acl and deny_acl which represents ALLOW
	 * and DENY aces respectively. They are filled according to the order in
	 * fsal_acl list. The allow acl is build based on ALLOW ace, @EVERYONE
	 * ace and deny acl. The permset for allow acl entry is constructed in
	 * such a way that it will contain all permissions(READ,WRITE,EXECUTE)
	 * of ALLOW aces plus EVERYONE which is not denied by the corresponding
	 * deny acl entry
	 *
	 * At last allow_acl is returned and deny_acl is ignored.
	 */
	for (f_ace = p_fsalacl->aces;
	     f_ace < p_fsalacl->aces + p_fsalacl->naces;
	     f_ace++) {
		if ((type == ACL_TYPE_ACCESS &&
		    !is_ace_valid_for_effective_acl_entry(f_ace))
		    || (type == ACL_TYPE_DEFAULT &&
		    !is_ace_valid_for_inherited_acl_entry(f_ace)))
			continue;
		if (IS_FSAL_ACE_SPECIAL_ID(*f_ace)) {
			id = 0;
			if (IS_FSAL_ACE_SPECIAL_OWNER(*f_ace))
				tag = ACL_USER_OBJ;
			if (IS_FSAL_ACE_SPECIAL_GROUP(*f_ace))
				tag = ACL_GROUP_OBJ;
			if (IS_FSAL_ACE_SPECIAL_MASK(*f_ace))
				tag = ACL_MASK;
		} else {
			id = GET_FSAL_ACE_WHO(*f_ace);
			if (IS_FSAL_ACE_GROUP_ID(*f_ace))
				tag = ACL_GROUP;
			else
				tag = ACL_USER;
			/*
			 * Mask entry will be created only if it
			 * contains user or group entry
			 */
			mask = true;
		}

		if (IS_FSAL_ACE_SPECIAL_EVERYONE(*f_ace)) {
			if (IS_FSAL_ACE_DENY(*f_ace)) {
				if (deny_e_r)
					acl_add_perm(e_d_permset, ACL_READ);
				if (deny_e_w)
					acl_add_perm(e_d_permset, ACL_WRITE);
				if (deny_e_x)
					acl_add_perm(e_d_permset, ACL_EXECUTE);
			}
			continue;
		}

		a_entry = get_entry(allow_acl, tag, id);
		d_entry = get_entry(deny_acl, tag, id);
		ret = acl_get_permset(d_entry, &d_permset);
		if (ret) {
			LogWarn(COMPONENT_FSAL,
				"Cannot retrieve permission set");
		}

		if (IS_FSAL_ACE_DENY(*f_ace)) {
			if (IS_FSAL_ACE_READ_DATA(*f_ace))
				acl_add_perm(d_permset, ACL_READ);
			if (IS_FSAL_ACE_WRITE_DATA(*f_ace))
				acl_add_perm(d_permset, ACL_WRITE);
			if (IS_FSAL_ACE_EXECUTE(*f_ace))
				acl_add_perm(d_permset, ACL_EXECUTE);
		}
		ret = acl_get_permset(a_entry, &a_permset);
		if (ret) {
			LogWarn(COMPONENT_FSAL,
				"Cannot retrieve permission set");
		}

		if (IS_FSAL_ACE_SPECIAL_MASK(*f_ace)) {
			if (IS_FSAL_ACE_ALLOW(*f_ace)) {
				if IS_FSAL_ACE_READ_DATA(*f_ace)
					acl_add_perm(a_permset, ACL_READ);
				if IS_FSAL_ACE_WRITE_DATA(*f_ace)
					acl_add_perm(a_permset, ACL_WRITE);
				if IS_FSAL_ACE_EXECUTE(*f_ace)
					acl_add_perm(a_permset, ACL_EXECUTE);
			}
			mask_set = true;
			continue;
		}

		if ((isallow(f_ace, e_a_permset, ACL_READ)
		    && !isdeny(d_permset, e_d_permset, ACL_READ))
		    || IS_FSAL_ACE_IFLAG(*f_ace, FSAL_ACE_FLAG_MASK_READ_DENY))
			acl_add_perm(a_permset, ACL_READ);

		if ((isallow(f_ace, e_a_permset, ACL_WRITE)
		    && !isdeny(d_permset, e_d_permset, ACL_WRITE))
		    || IS_FSAL_ACE_IFLAG(*f_ace, FSAL_ACE_FLAG_MASK_WRITE_DENY))
			acl_add_perm(a_permset, ACL_WRITE);

		if ((isallow(f_ace, e_a_permset, ACL_EXECUTE)
		    && !isdeny(d_permset, e_d_permset, ACL_EXECUTE))
		    || IS_FSAL_ACE_IFLAG(*f_ace,
				FSAL_ACE_FLAG_MASK_EXECUTE_DENY))
			acl_add_perm(a_permset, ACL_EXECUTE);
	}
	if (!mask_set && mask) {
		ret = acl_calc_mask(&allow_acl);
		if (ret)
			LogWarn(COMPONENT_FSAL,
			"Cannot calculate mask for posix");
	}

	/* A valid acl_t should have only one entry for ACL_USER_OBJ,
	 * ACL_GROUP_OBJ, ACL_OTHER and ACL_MASK is required only if
	 * ACL_USER or ACL_GROUP exists
	 */
	ret = acl_check(allow_acl, &i);
	if (ret) {
		if (ret > 0) {
			LogWarn(COMPONENT_FSAL,
				"Error converting ACL: %s at entry no %d",
				acl_error(ret), i);
		}

	}

	if (isDebug(COMPONENT_FSAL)) {
		char *acl_str;

		acl_str = acl_to_any_text(allow_acl, NULL, ',',
				TEXT_ABBREVIATE | TEXT_NUMERIC_IDS);
		LogDebug(COMPONENT_FSAL, "posix acl = %s ", acl_str);
		acl_free(acl_str);
	}

	if (deny_acl)
		acl_free(deny_acl);

	return allow_acl;
}

/*
 * @brief Calculate the ACL xattr size by quantity of ACL entries
 *
 * @param[in]  count Quantity of ACL entries
 *
 * @return ACL xattr size
 */

size_t posix_acl_xattr_size(int count)
{
	return sizeof(struct acl_ea_header) +
	    count * sizeof(struct acl_ea_entry);
}

/*
 * @brief Calculate the quantity of ACL entries by xattr size
 *
 * @param[in]  size Length of extented attribute
 *
 * @return Quantity of ACL entries on success, -1 on failure.
 */

int posix_acl_entries_count(size_t size)
{
	if (size < sizeof(struct acl_ea_header))
		return -1;
	size -= sizeof(struct acl_ea_header);
	if (size % sizeof(struct acl_ea_entry))
		return -1;
	return size / sizeof(struct acl_ea_entry);
}

/*
 * @brief Convert extented attribute to POSIX ACL
 *
 * @param[in]  value	Extented attribute
 * @param[in]  size	Size of the extented attribute
 *
 * @return Posix ACL on success, NULL on failure.
 */

acl_t xattr_2_posix_acl(const struct acl_ea_header *ea_header, size_t size)
{
	const struct acl_ea_entry *ea_entry = &ea_header->a_entries[0], *end;

	int count;
	int ret = 0;
	acl_t acl = NULL;
	acl_entry_t acl_entry;
	acl_tag_t tag;
	acl_permset_t permset;
	uid_t uid;
	gid_t gid;

	count = posix_acl_entries_count(size);
	if (count < 0) {
		LogMajor(COMPONENT_FSAL,
			"Invalid parameter: size = %d", (int)size);
		return NULL;
	}

	if (count == 0)
		return NULL;

	if (ea_header->a_version != htole32(ACL_EA_VERSION)) {
		LogMajor(COMPONENT_FSAL, "ACL ea version is inconsistent");
		return NULL;
	}

	acl = acl_init(count);
	if (!acl) {
		LogMajor(COMPONENT_FSAL,
			"Failed to ACL INIT: count = %d", count);
		return NULL;
	}

	for (end = ea_entry + count; ea_entry != end; ea_entry++) {
		ret = acl_create_entry(&acl, &acl_entry);
		if (ret) {
			LogMajor(COMPONENT_FSAL, "Failed to create acl entry");
			goto out;
		}

		tag = le16toh(ea_entry->e_tag);
		ret = acl_set_tag_type(acl_entry, tag);
		if (ret) {
			LogMajor(COMPONENT_FSAL, "Failed to set acl tag type");
			goto out;
		}

		ret = acl_get_permset(acl_entry, &permset);
		if (ret) {
			LogWarn(COMPONENT_FSAL, "Failed to get acl permset");
			goto out;
		}

		ret = acl_add_perm(permset, le16toh(ea_entry->e_perm));
		if (ret) {
			LogWarn(COMPONENT_FSAL, "Failed to add acl permission");
			goto out;
		}

		switch (tag) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			break;

		case ACL_USER:
			uid = le32toh(ea_entry->e_id);
			ret = acl_set_qualifier(acl_entry, &uid);
			if (ret) {
				LogMajor(COMPONENT_FSAL, "Failed to set uid");
				goto out;
			}

			break;

		case ACL_GROUP:
			gid = le32toh(ea_entry->e_id);
			ret = acl_set_qualifier(acl_entry, &gid);
			if (ret) {
				LogMajor(COMPONENT_FSAL, "Failed to set gid");
				goto out;
			}

			break;

		default:
			goto out;
		}
	}

	if (isDebug(COMPONENT_FSAL)) {
		char *acl_str;

		acl_str = acl_to_any_text(acl, NULL, ',',
				TEXT_ABBREVIATE | TEXT_NUMERIC_IDS);
		LogDebug(COMPONENT_FSAL, "posix acl = %s ", acl_str);
		acl_free(acl_str);
	}

	return acl;

out:
	if (acl) {
		acl_free((void *)acl);
	}

	return NULL;
}

/*
 * @brief Convert POSIX ACL to extented attribute
 *
 * @param[in]  acl	Posix ACL
 * @param[in]  size	Size of the extented attribute buffer
 * @param[out] buf	Buffer of the extented attribute
 *
 * @return Real size of extented attribute on success, -1 on failure.
 */

int posix_acl_2_xattr(acl_t acl, void *buf, size_t size)
{
	struct acl_ea_header *ea_header = buf;
	struct acl_ea_entry *ea_entry;
	acl_entry_t acl_entry;
	acl_tag_t tag;
	acl_permset_t permset;
	int real_size, count, entry_id;
	int ret = 0;

	if (isDebug(COMPONENT_FSAL)) {
		char *acl_str;

		acl_str = acl_to_any_text(acl, NULL, ',',
				TEXT_ABBREVIATE | TEXT_NUMERIC_IDS);
		LogDebug(COMPONENT_FSAL, "posix acl = %s ", acl_str);
		acl_free(acl_str);
	}

	count = acl_entries(acl);
	real_size = sizeof(*ea_header) + count * sizeof(*ea_entry);

	if (!buf)
		return real_size;
	if (real_size > size)
		return -1;

	ea_entry = (void *)(ea_header + 1);
	ea_header->a_version = htole32(ACL_EA_VERSION);

	for (entry_id = ACL_FIRST_ENTRY; ; entry_id = ACL_NEXT_ENTRY,
	     ea_entry++) {
		ret = acl_get_entry(acl, entry_id, &acl_entry);
		if (ret == 0 || ret == -1) {
			LogDebug(COMPONENT_FSAL,
				"No more ACL entries remaining");
			break;
		}
		if (acl_get_tag_type(acl_entry, &tag) == -1) {
			LogWarn(COMPONENT_FSAL, "No entry tag for ACL Entry");
			continue;
		}

		ret = acl_get_permset(acl_entry, &permset);
		if (ret) {
			LogWarn(COMPONENT_FSAL,
			"Cannot retrieve permission set for the ACL Entry");
			continue;
		}

		ea_entry->e_tag = htole16(tag);
		ea_entry->e_perm = 0;
		if (acl_get_perm(permset, ACL_READ))
			ea_entry->e_perm |= htole16(ACL_READ);
		if (acl_get_perm(permset, ACL_WRITE))
			ea_entry->e_perm |= htole16(ACL_WRITE);
		if (acl_get_perm(permset, ACL_EXECUTE))
			ea_entry->e_perm |= htole16(ACL_EXECUTE);

		switch (tag) {
		case ACL_USER:
			ea_entry->e_id =
				htole32(*(uid_t *)acl_get_qualifier(acl_entry));
			break;
		case ACL_GROUP:
			ea_entry->e_id =
				htole32(*(gid_t *)acl_get_qualifier(acl_entry));
			break;
		default:
			ea_entry->e_id = htole32(ACL_UNDEFINED_ID);
			break;
		}
	}

	return real_size;
}
