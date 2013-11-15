
#ifndef HAVE_STRNLEN

#include <sys/types.h>
#include <stdlib.h>

size_t gsh_strnlen(const char *s, size_t max)
{
	register const char *p;
	for (p = s; *p && max--; ++p)
		/* do nothing */;
	return p - s;
}

#endif
