#ifndef FREEBSD_SYS_RESOURCE_H_
#define FREEBSD_SYS_RESOURCE_H_

#include <sys/resource.h>

#define get_open_file_limit(rlim) getrlimit(RLIMIT_NOFILE, (rlim))

#endif		/* FREEBSD_SYS_RESOURCE_H_ */
