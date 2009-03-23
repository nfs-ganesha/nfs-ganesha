/*
 *	@(#)test3.c	1.7	00/12/30 Connectathon Testsuite
 *	1.5 Lachman ONC Test Suite source
 *
 * Test lookup up and down across mount points
 *
 * Uses the following important system calls against the server:
 *
 *	chdir()
 *	getcwd()
 *	stat()
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef DOSorWIN32
#include <time.h>
#else
#include <sys/time.h>
#endif
#include <sys/types.h>

#include "tests.h"
#include "Connectathon_config_parsing.h"

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
	int count;		/* times to do test */
	int ct;
	struct timeval time;
	struct stat statb;
	char *opts;
	char path[MAXPATHLEN];
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
    fprintf(stderr, "Missing config_file");
    exit(1);
  }

  if (argc != 0) {
		fprintf(stderr, "too many parameters");
		usage();
		exit(1);
	}

	param = readin_config(config_file);
  if (param == NULL) {
    fprintf(stderr, "Nothing built\n");
    exit(1);
  }

	b = get_btest_args(param, THREE);
  if (b == NULL) {
    fprintf(stderr, "Missing basic test number 3 in the config file '%s'\n", config_file);
    free_testparam(param);
    exit(1);
  }

	if (b->count == -1) {
		fprintf(stderr, "Missing 'count' parameter in the config file '%s' for the basic test number 3\n", config_file);
    free_testparam(param);
		exit(1);
	}
	count = b->count;
  test_dir = get_test_directory(param);
  log_file = get_log_file(param);

  free_testparam(param);

	if (Fflag) {
		Tflag = 0;
		count = 1;
	}

	fprintf(stdout, "%s: lookups across mount point\n", Myname);

	if (!Nflag)
		testdir(test_dir);
	else
		mtestdir(test_dir);

	starttime();
	for (ct = 0; ct < count; ct++) {
		if (getcwd(path, sizeof(path)) == NULL) {
			fprintf(stderr, "%s: getcwd failed\n", Myname);
			exit(1);
		}
		if (stat(path, &statb) < 0) {
			error("can't stat %s after getcwd", path);
			exit(1);
		}
	}
	endtime(&time);
  
	fprintf(stdout, "\t%d getcwd and stat calls", count * 2);
	if (Tflag) {
		fprintf(stdout, " in %ld.%02ld seconds",
		    (long)time.tv_sec, (long)time.tv_usec / 10000);
	}
	fprintf(stdout, "\n");

  if ((log = fopen( log_file, "a")) == NULL) {
    printf( "Enable to open the file '%s'\n", log_file);
	  complete();
  }
  fprintf(log, "b3\t%d\t%ld.%02ld\n", count * 2, (long)time.tv_sec, (long)time.tv_usec / 10000);
  fclose(log);
  
	complete();
}
