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
#include "lustre_extended_types.h"

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
