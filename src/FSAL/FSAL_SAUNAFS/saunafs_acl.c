// SPDX-License-Identifier: LGPL-3.0-or-later
/*
   Copyright 2017 Skytechnology sp. z o.o.
   Copyright 2023 Leil Storage OÜ

   This file is part of SaunaFS.

   SaunaFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   SaunaFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with SaunaFS. If not, see <http://www.gnu.org/licenses/>.
 */

#include "context_wrap.h"
#include "saunafs_internal.h"

const uint ByteMaxValue = 0xFF;

/**
 * @brief Convert a given ACL in FSAL format to the corresponding SaunaFS ACL.
 * The mode is used to create a default ACL and set POSIX permission flags.
 * The new ACL is filled with the ACEs from the original ACL to have the same
 * permissions and flags.
 *
 * @param[in] fsalACL     FSAL ACL
 * @param[in] mode        Mode used to create the acl and set POSIX permission
 *                        flags
 *
 * @returns: SaunaFS ACL
 */
sau_acl_t *convertFsalACLToSaunafsACL(const fsal_acl_t *fsalACL,
				      unsigned int mode)
{
	sau_acl_t *saunafsACL = NULL;

	if (!fsalACL || (!fsalACL->aces && fsalACL->naces > 0))
		return NULL;

	saunafsACL = sau_create_acl_from_mode(mode);
	if (!saunafsACL)
		return NULL;

	for (unsigned int i = 0; i < fsalACL->naces; ++i) {
		fsal_ace_t *fsalACE = fsalACL->aces + i;

		if (!(IS_FSAL_ACE_ALLOW(*fsalACE) ||
		      IS_FSAL_ACE_DENY(*fsalACE))) {
			continue;
		}

		sau_acl_ace_t ace;

		ace.flags = fsalACE->flag & ByteMaxValue;
		ace.mask = fsalACE->perm;
		ace.type = fsalACE->type;

		if (IS_FSAL_ACE_GROUP_ID(*fsalACE))
			ace.id = GET_FSAL_ACE_GROUP(*fsalACE);
		else
			ace.id = GET_FSAL_ACE_USER(*fsalACE);

		if (IS_FSAL_ACE_SPECIAL_ID(*fsalACE)) {
			ace.flags |= (uint)SAU_ACL_SPECIAL_WHO;
			switch (GET_FSAL_ACE_USER(*fsalACE)) {
			case FSAL_ACE_SPECIAL_OWNER:
				ace.id = SAU_ACL_OWNER_SPECIAL_ID;
				break;
			case FSAL_ACE_SPECIAL_GROUP:
				ace.id = SAU_ACL_GROUP_SPECIAL_ID;
				break;
			case FSAL_ACE_SPECIAL_EVERYONE:
				ace.id = SAU_ACL_EVERYONE_SPECIAL_ID;
				break;
			default:
				LogFullDebug(
					COMPONENT_FSAL,
					"Invalid FSAL ACE special id type (%d)",
					(int)GET_FSAL_ACE_USER(*fsalACE));
				continue;
			}
		}

		sau_add_acl_entry(saunafsACL, &ace);
	}

	return saunafsACL;
}

/**
 * @brief Convert a given SaunaFS ACL to the corresponding ACL in FSAL format.
 * This function copies the ACEs from the original SaunaFS ACL to have the same
 * permissions and flags in the new ACL.
 *
 * @param[in] saunafsACL     SaunaFS ACL
 *
 * @returns: ACL in FSAL format
 */
fsal_acl_t *convertSaunafsACLToFsalACL(const sau_acl_t *saunafsACL)
{
	fsal_acl_data_t aclData;
	fsal_acl_status_t status = 0;

	if (!saunafsACL)
		return NULL;

	aclData.naces = sau_get_acl_size(saunafsACL);
	aclData.aces = nfs4_ace_alloc(aclData.naces);

	if (!aclData.aces)
		return NULL;

	for (size_t aceEntry = 0; aceEntry < aclData.naces; ++aceEntry) {
		fsal_ace_t *fsalACE = aclData.aces + aceEntry;
		sau_acl_ace_t saunafsACE;

		int retvalue =
			sau_get_acl_entry(saunafsACL, aceEntry, &saunafsACE);

		(void)retvalue;
		assert(retvalue == 0);

		fsalACE->type = saunafsACE.type;
		fsalACE->flag = saunafsACE.flags & ByteMaxValue;
		fsalACE->iflag =
			(saunafsACE.flags & (unsigned int)SAU_ACL_SPECIAL_WHO) ?
				FSAL_ACE_IFLAG_SPECIAL_ID :
				0;
		fsalACE->perm = saunafsACE.mask;

		if (IS_FSAL_ACE_GROUP_ID(*fsalACE))
			fsalACE->who.gid = saunafsACE.id;
		else
			fsalACE->who.uid = saunafsACE.id;

		if (IS_FSAL_ACE_SPECIAL_ID(*fsalACE)) {
			switch (saunafsACE.id) {
			case SAU_ACL_OWNER_SPECIAL_ID:
				fsalACE->who.uid = FSAL_ACE_SPECIAL_OWNER;
				break;
			case SAU_ACL_GROUP_SPECIAL_ID:
				fsalACE->who.uid = FSAL_ACE_SPECIAL_GROUP;
				break;
			case SAU_ACL_EVERYONE_SPECIAL_ID:
				fsalACE->who.uid = FSAL_ACE_SPECIAL_EVERYONE;
				break;
			default:
				fsalACE->who.uid = FSAL_ACE_NORMAL_WHO;
				LogWarn(COMPONENT_FSAL,
					"Invalid SaunaFS ACE special id type (%u)",
					(unsigned int)saunafsACE.id);
			}
		}
	}

	return nfs4_acl_new_entry(&aclData, &status);
}

/**
 * @brief Get ACL from a file.
 *
 * This function returns the ACL of the file in FSAL format.
 *
 * @param[in] export       SaunaFS export instance
 * @param[in] inode        Inode of the file
 * @param[in] ownerId      Owner id of the file
 * @param[in] acl          Buffer to fill with information
 *
 * @returns: FSAL status.
 */
fsal_status_t getACL(struct SaunaFSExport *export, uint32_t inode,
		     uint32_t ownerId, fsal_acl_t **acl)
{
	if (*acl) {
		nfs4_acl_release_entry(*acl);
		*acl = NULL;
	}

	sau_acl_t *saunafsACL = NULL;
	int status = saunafs_getacl(export->fsInstance, &op_ctx->creds, inode,
				    &saunafsACL);

	if (status < 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "getacl status = %s export=%" PRIu16
			     " inode=%" PRIu32,
			     sau_error_string(sau_last_err()),
			     export->export.export_id, inode);

		return fsalLastError();
	}

	sau_acl_apply_masks(saunafsACL, ownerId);

	*acl = convertSaunafsACLToFsalACL(saunafsACL);
	sau_destroy_acl(saunafsACL);

	if (*acl == NULL) {
		LogFullDebug(
			COMPONENT_FSAL,
			"Failed to convert saunafs acl to nfs4 acl, export=%"
			PRIu16 " inode=%" PRIu32,
			export->export.export_id, inode);
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Set ACL to a file.
 *
 * This function receives an ACL in FSAL format, transform it to a SaunaFS ACL
 * using the given mode and then set it to the file with the given inode.
 *
 * @param[in] export      SaunaFS export instance
 * @param[in] inode       Inode of the file
 * @param[in] acl         FSAL ACL to set
 * @param[in] mode        Mode used to create the acl and set POSIX permission
 *                        flags
 *
 * @returns: FSAL status.
 */
fsal_status_t setACL(struct SaunaFSExport *export, uint32_t inode,
		     const fsal_acl_t *acl, unsigned int mode)
{
	if (!acl)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	sau_acl_t *saunafsACL = convertFsalACLToSaunafsACL(acl, mode);

	if (!saunafsACL) {
		LogFullDebug(COMPONENT_FSAL, "Failed to convert acl");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	int status = saunafs_setacl(export->fsInstance, &op_ctx->creds, inode,
				    saunafsACL);
	sau_destroy_acl(saunafsACL);

	if (status < 0)
		return fsalLastError();

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
