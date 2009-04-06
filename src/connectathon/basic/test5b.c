/*
 *	@(#)test5b.c	1.7	03/12/01 Connectathon Testsuite
 *	1.3 Lachman ONC Test Suite source
 *
 * Test read - will read a file of specified size, contents not looked at
 *
 * Uses the following important system calls against the server:
 *
 *	chdir()
 *	mkdir()		(for initial directory creation if not -m)
 *	open()
 *	read()
 *	unlink()
 */

#if defined (DOS) || defined (WIN32)
/* If Dos, Windows or Win32 */
#define DOSorWIN32
#endif

#ifndef DOSorWIN32
#include <sys/param.h>
#include <unistd.h>
#endif

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#ifdef DOSorWIN32
#include <time.h>
#else
#include <sys/time.h>
#endif
#ifdef MMAP
#include <sys/mman.h>
#endif

#include "tests.h"
#include "Connectathon_config_parsing.h"

#ifndef MIN
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#endif

#define	BUFSZ	8192

static int Tflag = 0;	/* print timing */
static int Fflag = 0;	/* test function only;  set count to 1, negate -t */
static int Nflag = 0;	/* Suppress directory operations */

static void
usage()
{
	fprintf(stdout, "usage: %s [-htfn] <config_file>\n", Myname);
	fprintf(stdout, "  Flags:  h    Help - print this usage info\n");
	fprintf(stdout, "          t    Print execution time statistics\n");
	fprintf(stdout, "          f    Test function only (negate -t)\n");
	fprintf(stdout, "          n    Suppress test directory create operations\n");
}

int main( int argc, char * argv[] )
{
	int count;	/* times to do each file */
	int ct;
	off_t size;
	off_t si;
	int fd;
	off_t bytes = 0;
	int roflags;			/* open read-only flags */
	char *bigfile;
	struct timeval time;
	char *opts;
	char buf[BUFSZ];
	double etime;
#ifdef MMAP
	caddr_t maddr;
#endif
  struct testparam *param;
	struct btest *b;
  char *config_file;
  char *test_dir;
  char *log_file;
  FILE *log;

	umask(0);
	setbuf(stdout, NULL);
	Myname = *argv++;
	argc--;
	while (argc && **argv == '-') {
		for (opts = &argv[0][1]; *opts; opts++) {
			switch (*opts) {
				case 'h':	/* help */
					usage();
					exit(1);
					break;

				case 't':	/* time */
					Tflag++;
					break;

				case 'f':	/* funtionality */
					Fflag++;
					break;

				case 'n':	/* No Test Directory create */
					Nflag++;
					break;

				default:
					error("unknown option '%c'", *opts);
					usage();
					exit(1);
			}
		}
		argc--;
		argv++;
	}

  if (argc) {
    config_file = *argv;
    argc--;
    argv++;
  } else {
    fprintf(stderr,"Missing config_file");
    exit(1);
  }

  if (argc != 0) {
		fprintf(stderr,"too many parameters");
		usage();
		exit(1);
	}
  
	param = readin_config(config_file);
  if (param == NULL) {
    fprintf(stderr, "Nothing built\n");
    exit(1);
  }

  b = get_btest_args(param, FIVE);
  if (b == NULL) {
    fprintf(stderr, "Missing basic test number 5 in the config file '%s'\n", config_file);
    free_testparam(param);
    exit(1);
  }

	if (b->count == -1) {
		fprintf(stderr,"Missing 'count' parameter in the config file '%s' for the basic test number 5\n", config_file);
    free_testparam(param);
		exit(1);
	}
	if (b->size == -1) {
		fprintf(stderr,"Missing 'size' parameter in the config file '%s' for the basic test number 5\n", config_file);
    free_testparam(param);
		exit(1);
	}
	count = b->count;
	size = b->size;
	bigfile = b->bigfile;
  test_dir = get_test_directory(param);
  log_file = get_log_file(param);

  free_testparam(param);

	if (Fflag) {
		Tflag = 0;
		count = 1;
	}

	roflags = O_RDONLY;
#ifdef DOSorWIN32
	roflags |= O_BINARY;
#endif

	fprintf(stdout, "%s: read\n", Myname);

	mtestdir(test_dir);

	starttime();
	for (ct = 0; ct < count; ct++) {
		if ((fd = open(bigfile, roflags)) < 0) {
			error("can't open '%s'", bigfile);
			exit(1);
		}
#ifdef MMAP
		maddr = mmap((caddr_t)0, (size_t)size, PROT_READ,
				MAP_PRIVATE, fd, (off_t)0);
		if (maddr == MAP_FAILED) {
			error("can't mmap '%s'", bigfile);
			exit(1);
		}
		if (msync(maddr, (size_t)size, MS_INVALIDATE) < 0) {
			error("can't invalidate pages for '%s'", bigfile);
			exit(1);
		}
		if (munmap(maddr, (size_t)size) < 0) {
			error("can't munmap '%s'", bigfile);
			exit(1);
		}
#endif
		for (si = size; si > 0; si -= bytes) {
			bytes = MIN(BUFSZ, si);
			if (read(fd, buf, bytes) != bytes) {
				error("'%s' read failed", bigfile);
				exit(1);
			}
		}
		close(fd);
	}
	endtime(&time);

	fprintf(stdout, "\tread %ld byte file %d times", (long)size, count);

	if (Tflag) {
		etime = (double)time.tv_sec + (double)time.tv_usec / 1000000.0;
		if (etime != 0.0) {
			fprintf(stdout, " in %ld.%02ld seconds (%ld bytes/sec)",
				(long)time.tv_sec, (long)time.tv_usec / 10000,
				(long)((double)size * ((double)count / etime)));
		} else {
			fprintf(stdout, " in %ld.%02ld seconds (> %ld bytes/sec)",
				(long)time.tv_sec, (long)time.tv_usec / 10000,
				(long)size * count);
		}
	}
	fprintf(stdout, "\n");

	if (unlink(bigfile) < 0) {
		error("can't unlink '%s'", bigfile);
		exit(1);
	}

  if ((log = fopen( log_file, "a")) == NULL) {
    printf( "Enable to open the file '%s'\n", log_file);
	  complete();
  }
#ifdef _TOTO
  if (etime != 0.0) {
    fprintf(log, "b5b\t%d\t%d\t%ld.%02ld\t%ld\n", (long)size, count, (long)time.tv_sec, (long)time.tv_usec / 10000, (long)((double)size * ((double)count / etime)));
  } else {
    fprintf(log, "b5b\t%d\t%d\t%ld.%02ld\t%ld\n", (long)size, count, (long)time.tv_sec, (long)time.tv_usec / 10000, (long)size * count);
  }
#endif
  fclose(log);

	complete();
}
