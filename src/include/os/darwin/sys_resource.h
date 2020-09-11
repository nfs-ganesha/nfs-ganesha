#ifndef DARWIN_SYS_RESOURCE_H_
#define DARWIN_SYS_RESOURCE_H_

#include <sys/resource.h>

/* A portable wrapper for getrlimit(RLIMIT_NOFILE, rlim). */
int get_open_file_limit(struct rlimit *rlim);

#endif		/* DARWIN_SYS_RESOURCE_H_ */
