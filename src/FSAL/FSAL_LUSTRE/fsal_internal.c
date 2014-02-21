/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 *
 * \file    fsal_internal.c
 * \date    Date: 2006/01/17 14:20:07
 * \version Revision: 1.24
 * \brief   Defines the datas that are to be
 *          accessed as extern by the fsal modules
 *
 */
#define FSAL_INTERNAL_C
#include "config.h"

#include <libgen.h> /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <mntent.h>
#include <unistd.h> /* glibc uses <sys/fsuid.h> */
#include <netdb.h> /* fgor gethostbyname() */

#include "abstract_mem.h"
#include  "fsal.h"
#include "fsal_handle.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "config_parsing.h"
#include "lustre_extended_types.h"

#if 0
#define CONF_LUSTRE_PNFS "LUSTRE_pNFS"

struct lustre_pnfs_parameter pnfs_param;

int lustre_pnfs_read_ds_conf(config_item_t subblock,
			     struct lustre_pnfs_ds_parameter *pds_conf)
{
	unsigned int nb_subitem = config_GetNbItems(subblock);
	unsigned int i;
	int err;
	int var_index = 0;
	struct hostent *hp = NULL;

	pds_conf->ipport = 2049; /* Default value */

	for (i = 0; i < nb_subitem; i++) {
		char *key_name;
		char *key_value;
		config_item_t item;
		item = config_GetItemByIndex(subblock, i);

		/* Here, we are managing a configuration sub block,
		 * it has nothing but CONFIG_ITEM_VAR in it */
		if (config_ItemType(item) != CONFIG_ITEM_VAR) {
			LogCrit(COMPONENT_CONFIG,
				"No sub-block expected ");
			return -EINVAL;
		}

		/* recuperation du couple clef-valeur */
		err = config_GetKeyValue(item, &key_name, &key_value);
		if (err != 0) {
			LogCrit(COMPONENT_CONFIG,
			"Error reading key[%d] from section \"%s\" of configuration file.",
				var_index, CONF_LUSTRE_PNFS);
			return -EINVAL;
		} else if (!strcasecmp(key_name, "DS_Addr")) {
			if (isdigit(key_value[0])) {
				/* Address begin with a digit,
				 * it is a address in the dotted form,
				 * translate it */
				pds_conf->ipaddr = ntohl(inet_addr(key_value));

				/* Keep this address in the ascii format
				 * as well (for GETDEVICEINFO) */
				strncpy(pds_conf->ipaddr_ascii,
					key_value, MAXNAMLEN);
			} else {
				/* This is a server name to be
				* resolved. Use gethostbyname */
				hp = gethostbyname(key_value);
				if (hp == NULL) {
					LogCrit(COMPONENT_CONFIG,
					"PNFS LOAD PARAMETER: ERROR: Unexpected value for %s",
						key_name);
					return -1;
				}
				memcpy(&pds_conf->ipaddr,
					(char *)hp->h_addr,
					hp->h_length);
				pds_conf->ipaddr = ntohl(pds_conf->ipaddr);
				snprintf(pds_conf->ipaddr_ascii, MAXNAMLEN,
					"%u.%u.%u.%u",
					((unsigned int)(pds_conf->ipaddr) &
					0xFF000000) >> 24,
					((unsigned int)(pds_conf->ipaddr) &
					0x00FF0000) >> 16,
					((unsigned int)(pds_conf->ipaddr) &
					0x0000FF00) >> 8,
					(unsigned int)(pds_conf->ipaddr)
					& 0x000000FF);
			}
		} else if (!strcasecmp(key_name, "DS_Port"))
			pds_conf->ipport = atoi(key_value);
		else if (!strcasecmp(key_name, "DS_Id"))
			pds_conf->id = atoi(key_value);
		else {
			LogCrit(COMPONENT_CONFIG,
				"Unknown or unsettable key: %s (item %s)",
				key_name, CONF_LUSTRE_PNFS);
			return -1;
		}
	}                           /* for */

	return 0;
}                               /* nfs_read_pnfs_ds_conf */

int lustre_pnfs_read_conf(config_file_t in_config,
			  struct lustre_pnfs_parameter *pparam)
{
	int var_max;
	int var_index;
	int err;
	char *key_name;
	char *key_value;
	char *block_name;
	config_item_t block;
	int unique;

	unsigned int ds_count = 0;

	/* Is the config tree initialized ? */
	if (in_config == NULL || pparam == NULL)
		return -1;

	/* Get the config BLOCK */
	block = config_FindItemByName_CheckUnique(in_config,
						  CONF_LUSTRE_PNFS,
						  &unique);
	if (block == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"Cannot read item \"%s\" from configuration file: %s",
			CONF_LUSTRE_PNFS, config_GetErrorMsg());
		return 1;
	} else if (!unique) {
		LogCrit(COMPONENT_CONFIG,
			"Only a single \"%s\" block is expected in config file: %s",
			CONF_LUSTRE_PNFS, config_GetErrorMsg());
		return -1;
	} else if (config_ItemType(block) != CONFIG_ITEM_BLOCK) {
		/* Expected to be a block */
		return -1;
	}

	var_max = config_GetNbItems(block);

	for (var_index = 0; var_index < var_max; var_index++) {
		config_item_t item;
		config_item_type item_type;

		item = config_GetItemByIndex(block, var_index);
		item_type = config_ItemType(item);

		switch (item_type) {
		case CONFIG_ITEM_VAR:
			/* Get key's name */
			err = config_GetKeyValue(item, &key_name, &key_value);
			if (err != 0) {
				LogCrit(COMPONENT_CONFIG,
			"Error reading key[%d] from section \"%s\" of configuration file.",
				var_index, CONF_LUSTRE_PNFS);
				return -1;
			} else if (!strcasecmp(key_name, "Stripe_Size"))
				pparam->stripe_size = atoi(key_value);
			else if (!strcasecmp(key_name, "Stripe_Width"))
				pparam->stripe_width = atoi(key_value);
			else {
				LogCrit(COMPONENT_CONFIG,
					"Unknown or unsettable key: %s (item %s)",
					key_name,
					CONF_LUSTRE_PNFS);
				return -1;
			}
			break;

		case CONFIG_ITEM_BLOCK:
				block_name = config_GetBlockName(item);

			if (!strcasecmp(block_name, "DataServer"))
				if (lustre_pnfs_read_ds_conf(item,
				    &pparam->ds_param[ds_count]) != 0) {
					LogCrit(COMPONENT_CONFIG,
						"Unknown or unsettable key: %s (item %s)",
						key_name,
						CONF_LUSTRE_PNFS);
					return -1;
				}

			ds_count += 1;
			break;

		default:
				LogCrit(COMPONENT_CONFIG,
					"Error reading key[%d] from section \"%s\" of configuration file.",
					var_index, CONF_LUSTRE_PNFS);
				return -1;
				break;
		}
	}                           /* for */

	/* Remeber how many DSs have been configured */
	pparam->ds_count = ds_count;

	return 0;
}                               /* pnfs_read_ds_conf */
#endif

/** get (name+parent_id) for an entry
 * \param linkno hardlink index
 * \retval -ENODATA after last link
 * \retval -ERANGE if namelen is too small
 */
int Lustre_GetNameParent(const char *path, int linkno,
			 lustre_fid *pfid, char *name,
			 int namelen)
{
	int rc, i, len;
	char buf[4096];
	struct linkea_data     ldata      = { 0 };
	struct lu_buf          lb = { 0 };

	rc = lgetxattr(path, XATTR_NAME_LINK, buf, sizeof(buf));
	if (rc < 0)
		return -errno;

	lb.lb_buf = buf;
	lb.lb_len = sizeof(buf);
	ldata.ld_buf = &lb;
	ldata.ld_leh = (struct link_ea_header *)buf;

	ldata.ld_lee = LINKEA_FIRST_ENTRY(ldata);
	ldata.ld_reclen = (ldata.ld_lee->lee_reclen[0] << 8)
		| ldata.ld_lee->lee_reclen[1];

	if (linkno >= ldata.ld_leh->leh_reccount)
		/* beyond last link */
		return -ENODATA;

	for (i = 0; i < linkno; i++) {
		ldata.ld_lee = LINKEA_NEXT_ENTRY(ldata);
		ldata.ld_reclen = (ldata.ld_lee->lee_reclen[0] << 8)
			| ldata.ld_lee->lee_reclen[1];
	}

	memcpy(pfid, &ldata.ld_lee->lee_parent_fid, sizeof(*pfid));
	fid_be_to_cpu(pfid, pfid);

	len = ldata.ld_reclen - sizeof(struct link_ea_entry);
	if (len >= namelen)
		return -ERANGE;

	strncpy(name, ldata.ld_lee->lee_name, len);
	name[len] = '\0';
	return 0;
}

/* credential lifetime (1h) */
uint32_t CredentialLifetime = 3600;

/* static filesystem info.
 * The access is thread-safe because
 * it is read-only, except during initialization.
 */
struct fsal_staticfsinfo_t global_fs_info;
