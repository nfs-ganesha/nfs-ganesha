
#include <stdlib.h>
#include <sys/time.h>

int portable_clock_gettime(struct timespec *ts)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * 1000UL;
	return 0;
}
