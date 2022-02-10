// SPDX-License-Identifier: LGPL-3.0-or-later
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

#include "sys_resource.h"
#include <sys/syslimits.h>

int get_open_file_limit(struct rlimit *rlim)
{
	if (getrlimit(RLIMIT_NOFILE, rlim) != 0)
		return -1;

	/*
	 * macOS has unusual semantics for the RLIMIT_NOFILE hard limit. See
	 * the COMPATIBILITY section of the getrlimit man page.
	 * https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/getrlimit.2.html
	 */
	if (rlim->rlim_max > OPEN_MAX)
		rlim->rlim_max = OPEN_MAX;

	return 0;
}

