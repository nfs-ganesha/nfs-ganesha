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

