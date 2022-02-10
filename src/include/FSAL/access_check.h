/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
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

#ifndef _ACCESS_CHECK_H
#define _ACCESS_CHECK_H

/* A few headers required to have "struct stat" */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"
#include "fsal_api.h"

/* fsal_test_access
 * common (default) access check method for fsal_obj_handle objects.
 */

fsal_status_t fsal_test_access(struct fsal_obj_handle *obj_hdl,
			       fsal_accessflags_t access_type,
			       fsal_accessflags_t *allowed,
			       fsal_accessflags_t *denied,
			       bool owner_skip);

int display_fsal_v4mask(struct display_buffer *dspbuf, fsal_aceperm_t v4mask,
			bool is_dir);

#if GSH_CAN_HOST_LOCAL_FS
void fsal_set_credentials(const struct user_cred *creds);
void fsal_restore_ganesha_credentials(void);
#endif

bool fsal_set_credentials_only_one_user(const struct user_cred *creds);
void fsal_save_ganesha_credentials(void);

void fsal_print_ace_int(log_components_t component, log_levels_t debug,
			fsal_ace_t *ace, char *file, int line,
			char *function);
#define fsal_print_ace(component, debug, ace) \
	fsal_print_ace_int((component), (debug), (ace), \
		 (char *) __FILE__, __LINE__, (char *) __func__)
void fsal_print_acl_int(log_components_t component, log_levels_t debug,
			fsal_acl_t *acl, char *file, int line,
			char *function);
#define fsal_print_acl(component, debug, acl) \
	fsal_print_acl_int((component), (debug), (acl), \
		 (char *) __FILE__, __LINE__, (char *) __func__)
#endif
