/*
 *	@(#)test4.c	1.7	99/12/10 Connectathon Testsuite
 *	1.4 Lachman ONC Test Suite source
 *
 * Test setattr, getattr and lookup
 *
 * Creates the files in the test directory - does not create a directory
 * tree.
 *
 * Uses the following important system calls against the server:
 *
 *	chdir()
 *	mkdir()		(for initial directory creation if not -m)
 *	creat()
 *	chmod()
 *	stat()
 */

#if defined (DOS) || defined (WIN32)
/* If Dos, Windows or Win32 */
#define	DOSorWIN32
#endif

#ifndef DOSorWIN32
#include <sys/param.h>
#endif

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef DOSorWIN32
#include <time.h>
#else
#include <sys/time.h>
#endif

#include "tests.h"
#include "Connectathon_config_parsing.h"

static int Tflag = 0;	/* print timing */
static int Fflag = 0;	/* test function only;  set count to 1, negate -t */
static int Nflag = 0;	/* Suppress directory operations */

static void
usage()
{
	fprintf(stdout, "usage: %s [-hstfn] <config_file>\n", Myname);
	fprintf(stdout, "  Flags:  h    Help - print this usage info\n");
	fprintf(stdout, "          t    Print execution time statistics\n");
	fprintf(stdout, "          f    Test function only (negate -t)\n");
	fprintf(stdout,
		"          n    Suppress test directory create operations\n");
}

int main( int argc, char *argv[] )
{
	int files;		/* number of files in each dir */
	int fi;
	int count;	/* times to do each file */
	int ct;
	int totfiles = 0;
	int totdirs = 0;
	char *fname;
	char *dname;
	struct timeval time;
	char str[MAXPATHLEN];
	struct stat statb;
	char *opts;
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

				case 'n':	/* suppress initial directory */
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
  if (param == NULL){
    fprintf(stderr, "Nothing built\n");
    exit(1);
  }

	b = get_btest_args(param, FOUR);
  if (b == NULL) {
    fprintf(stderr, "Missing basic test number 4 in the config file '%s'\n", config_file);
    free_testparam(param);
    exit(1);
  }

	if (b->files == -1) {
		fprintf(stderr, "Missing 'files' parameter in the config file '%s' for the basic test number 4\n", config_file);
    free_testparam(param);
		exit(1);
	}
	if (b->count == -1) {
		fprintf(stderr,"Missing 'count' parameter in the config file '%s' for the basic test number 4\n", config_file);
    free_testparam(param);
		exit(1);
	}
	count = b->count;
	files = b->files;
	fname = b->fname;
	dname = b->dname;
  test_dir = get_test_directory(param);
  log_file = get_log_file(param);

  free_testparam(param);
  
	if (Fflag) {
		Tflag = 0;
		count = 1;
	}

 	fprintf(stdout, "%s: setattr, getattr, and lookup\n", Myname);

	if (!Nflag)
		testdir(test_dir);
	else
		mtestdir(test_dir);

	dirtree(1, files, 0, fname, dname, &totfiles, &totdirs);

	starttime();
	for (ct = 0; ct < count; ct++) {
		for (fi = 0; fi < files; fi++) {
			sprintf(str, "%s%d", fname, fi);
			if (chmod(str, CHMOD_NONE) < 0) {
				error("can't chmod %o %s", CHMOD_NONE, str);
				exit(0);
			}
			if (stat(str, &statb) < 0) {
				error("can't stat %s after CMOD_NONE", str);
				exit(1);
			}
			if ((statb.st_mode & CHMOD_MASK) != CHMOD_NONE) {
				error("%s has mode %o after chmod 0",
				    str, (statb.st_mode & 0777));
				exit(1);
			}
			if (chmod(str, CHMOD_RW) < 0) {
				error("can't chmod %o %s", CHMOD_RW, str);
				exit(0);
			}
			if (stat(str, &statb) < 0) {
				error("can't stat %s after CHMOD_RW", str);
				exit(1);
			}
			if ((statb.st_mode & CHMOD_MASK) != CHMOD_RW) {
				error("%s has mode %o after chmod 0666",
				    str, (statb.st_mode & 0777));
				exit(1);
			}
		}
	}
	endtime(&time);

	fprintf(stdout, "\t%d chmods and stats on %d files",
	  files * count * 2, files);
	if (Tflag) {
		fprintf(stdout, " in %ld.%02ld seconds",
			(long)time.tv_sec, (long)time.tv_usec / 10000);
	}
 	fprintf(stdout, "\n");

  if ((log = fopen( log_file, "a")) == NULL) {
    printf( "Enable to open the file '%s'\n", log_file);
	  complete();
  }
  fprintf(log, "b4\t%d\t%d\t%ld.%02ld\n", files * count * 2, files, (long)time.tv_sec, (long)time.tv_usec / 10000);
  fclose(log);
  
	/* XXX REMOVE DIRECTORY TREE? */
	complete();
}
