#ifndef LINUX_SYS_RESOURCE_H_
#define LINUX_SYS_RESOURCE_H_

#include <sys/resource.h>

#define get_open_file_limit(rlim) getrlimit(RLIMIT_NOFILE, (rlim))

#endif		/* LINUX_SYS_RESOURCE_H_ */
