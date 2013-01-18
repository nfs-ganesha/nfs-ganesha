#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>
#include "log.h"
#include "ganesha_types.h"

void print_time(char *heading, struct timespec *time)
{
	printf("%s: %s and %ld nsecs\n",
	       heading,
	       ctime((const time_t *)&time->tv_sec), time->tv_nsec);
}

int main(int argc, char *argv[])
{
	struct timespec ts, start;
	struct timespec epoch = {0L, 0L};
	nsecs_elapsed_t start_time, elapsed;

	now(&start);
	printf("Now: %lu.%lu\n", start.tv_sec, start.tv_nsec);
	start_time = timespec_diff(&epoch, &start);
	printf("nsecs elapsed since epoch: %lu\n", start_time);
	nsecs_to_timespec(start_time, &ts);
	printf("reconstructed Now: %lu.%lu\n", ts.tv_sec, ts.tv_nsec);
	elapsed = start_time - 1 * NS_PER_SEC;
	nsecs_to_timespec(elapsed, &ts);
	printf("one second before: %lu.%lu\n", ts.tv_sec, ts.tv_nsec);
	elapsed = start_time + 1 * NS_PER_SEC;
	nsecs_to_timespec(elapsed, &ts);
	printf("one second after: %lu.%lu\n", ts.tv_sec, ts.tv_nsec);

	ts = start;
	print_time("start time", &ts);
	elapsed = start_time - 86400L * NS_PER_SEC;
	nsecs_to_timespec(elapsed, &ts);
	print_time("yesterday", &ts);

	elapsed = timespec_diff(&ts, &start);
	printf("difference between yesterday and today in nsecs: %lu\n",
	       elapsed);
	timespec_add_nsecs(elapsed, &ts);
	print_time("today by difference", &ts);

	ts = start;
	elapsed = start_time + 86400L * NS_PER_SEC;
	nsecs_to_timespec(elapsed, &ts);
	print_time("tomorrow", &ts);

	ts = start;
	elapsed = 86400L * NS_PER_SEC;
	timespec_add_nsecs(elapsed, &ts);
	print_time("tomorrow by adding", &ts);

	elapsed = timespec_diff(&start, &ts);
	printf("difference between today and yesterday in nsecs: %lu\n",
	       elapsed);
	ts = start;
	timespec_sub_nsecs(elapsed, &ts);
	print_time("yesterday by subtracting", &ts);

	exit(0);
}
