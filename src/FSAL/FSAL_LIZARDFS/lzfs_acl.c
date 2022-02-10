/*
 * Copyright (C) 2019 Skytechnology sp. z o.o.
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "context_wrap.h"
#include "lzfs_internal.h"

void lzfs_int_apply_masks(liz_acl_t *lzfs_acl, uint32_t owner);

liz_acl_t *lzfs_int_convert_fsal_acl(const fsal_acl_t *fsal_acl)
{
	liz_acl_t *lzfs_acl = NULL;

	if (!fsal_acl || (!fsal_acl->aces && fsal_acl->naces > 0)) {
		return NULL;
	}

	int count = 0;

	for (unsigned int i = 0; i < fsal_acl->naces; ++i) {
		fsal_ace_t *fsal_ace = fsal_acl->aces + i;

		count += (IS_FSAL_ACE_ALLOW(*fsal_ace) ||
				IS_FSAL_ACE_DENY(*fsal_ace)) ? 1 : 0;
	}

	lzfs_acl = liz_create_acl();
	if (!lzfs_acl) {
		return NULL;
	}

	for (unsigned int i = 0; i < fsal_acl->naces; ++i) {
		fsal_ace_t *fsal_ace = fsal_acl->aces + i;

		if (!(IS_FSAL_ACE_ALLOW(*fsal_ace) ||
		      IS_FSAL_ACE_DENY(*fsal_ace))) {
			continue;
		}

		liz_acl_ace_t ace;

		ace.flags = fsal_ace->flag & 0xFF;
		ace.mask = fsal_ace->perm;
		ace.type = fsal_ace->type;
		if (IS_FSAL_ACE_GROUP_ID(*fsal_ace)) {
			ace.id = GET_FSAL_ACE_GROUP(*fsal_ace);
		} else {
			ace.id = GET_FSAL_ACE_USER(*fsal_ace);
		}
		if (IS_FSAL_ACE_SPECIAL_ID(*fsal_ace)) {
			ace.flags |= LIZ_ACL_SPECIAL_WHO;
			switch (GET_FSAL_ACE_USER(*fsal_ace)) {
			case FSAL_ACE_SPECIAL_OWNER:
				ace.id = LIZ_ACL_OWNER_SPECIAL_ID;
				break;
			case FSAL_ACE_SPECIAL_GROUP:
				ace.id = LIZ_ACL_GROUP_SPECIAL_ID;
				break;
			case FSAL_ACE_SPECIAL_EVERYONE:
				ace.id = LIZ_ACL_EVERYONE_SPECIAL_ID;
				break;
			default:
				LogFullDebug(
					COMPONENT_FSAL,
					"Invalid FSAL ACE special id type (%d)",
					(int)GET_FSAL_ACE_USER(*fsal_ace));
				continue;
			}
		}

		liz_add_acl_entry(lzfs_acl, &ace);
	}

	return lzfs_acl;
}

fsal_acl_t *lzfs_int_convert_lzfs_acl(const liz_acl_t *lzfs_acl)
{
	fsal_acl_data_t acl_data;
	fsal_acl_status_t acl_status;
	fsal_acl_t *fsal_acl = NULL;

	if (!lzfs_acl) {
		return NULL;
	}

	acl_data.naces = liz_get_acl_size(lzfs_acl);
	acl_data.aces = (fsal_ace_t *)nfs4_ace_alloc(acl_data.naces);

	if (!acl_data.aces) {
		return NULL;
	}

	for (unsigned int i = 0; i < acl_data.naces; ++i) {
		fsal_ace_t *fsal_ace = acl_data.aces + i;
		liz_acl_ace_t lzfs_ace;

		int rc = liz_get_acl_entry(lzfs_acl, i, &lzfs_ace);
		(void)rc;
		assert(rc == 0);

		fsal_ace->type = lzfs_ace.type;
		fsal_ace->flag = lzfs_ace.flags & 0xFF;
		fsal_ace->iflag = (lzfs_ace.flags & LIZ_ACL_SPECIAL_WHO) ?
						FSAL_ACE_IFLAG_SPECIAL_ID : 0;

		if (IS_FSAL_ACE_GROUP_ID(*fsal_ace)) {
			fsal_ace->who.gid = lzfs_ace.id;
		} else {
			fsal_ace->who.uid = lzfs_ace.id;
		}

		if (IS_FSAL_ACE_SPECIAL_ID(*fsal_ace)) {
			switch (lzfs_ace.id) {
			case LIZ_ACL_OWNER_SPECIAL_ID:
				fsal_ace->who.uid = FSAL_ACE_SPECIAL_OWNER;
				break;
			case LIZ_ACL_GROUP_SPECIAL_ID:
				fsal_ace->who.uid = FSAL_ACE_SPECIAL_GROUP;
				break;
			case LIZ_ACL_EVERYONE_SPECIAL_ID:
				fsal_ace->who.uid = FSAL_ACE_SPECIAL_EVERYONE;
				break;
			default:
				fsal_ace->who.uid = FSAL_ACE_NORMAL_WHO;
				LogWarn(COMPONENT_FSAL,
					"Invalid LizardFS ACE special id type (%u)",
					(unsigned int)lzfs_ace.id);
			}
		}
	}

	fsal_acl = nfs4_acl_new_entry(&acl_data, &acl_status);
	LogDebug(COMPONENT_FSAL, "fsal acl = %p, fsal_acl_status = %u",
		 fsal_acl, acl_status);
	return fsal_acl;
}

fsal_status_t lzfs_int_getacl(struct lzfs_fsal_export *lzfs_export,
			      uint32_t inode, uint32_t owner_id,
			      fsal_acl_t **fsal_acl)
{
	if (*fsal_acl) {
		int acl_status = nfs4_acl_release_entry(*fsal_acl);

		if (acl_status != NFS_V4_ACL_SUCCESS) {
			LogCrit(COMPONENT_FSAL,
				"Failed to release old acl, status=%d",
				acl_status);
		}
		*fsal_acl = NULL;
	}

	liz_acl_t *acl = NULL;
	int rc = liz_cred_getacl(lzfs_export->lzfs_instance, &op_ctx->creds,
				 inode, &acl);
	if (rc < 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "getacl status=%s export=%" PRIu16
			     " inode=%" PRIu32,
			     liz_error_string(liz_last_err()),
			     lzfs_export->export.export_id, inode);
		return lzfs_fsal_last_err();
	}

	lzfs_int_apply_masks(acl, owner_id);

	*fsal_acl = lzfs_int_convert_lzfs_acl(acl);
	liz_destroy_acl(acl);

	if (*fsal_acl == NULL) {
		LogFullDebug(COMPONENT_FSAL,
			     "Failed to convert lzfs acl to nfs4 acl, export=%"
			     PRIu16 " inode=%" PRIu32,
			     lzfs_export->export.export_id, inode);
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t lzfs_int_setacl(struct lzfs_fsal_export *lzfs_export,
			      uint32_t inode, const fsal_acl_t *fsal_acl)
{
	if (!fsal_acl) {
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	liz_acl_t *lzfs_acl = lzfs_int_convert_fsal_acl(fsal_acl);

	if (!lzfs_acl) {
		LogFullDebug(COMPONENT_FSAL, "failed to convert acl");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}
	int rc = liz_cred_setacl(lzfs_export->lzfs_instance, &op_ctx->creds,
				 inode, lzfs_acl);
	liz_destroy_acl(lzfs_acl);

	if (rc < 0) {
		LogFullDebug(COMPONENT_FSAL, "setacl returned %s (%d)",
			     liz_error_string(liz_last_err()),
			     (int)liz_last_err());
		return lzfs_fsal_last_err();
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
