/*
 *	@(#)test8.c	1.7	2001/08/25 Connectathon Testsuite
 *	1.4 Lachman ONC Test Suite source
 *
 * Test symlink, readlink
 *
 * Uses the following important system calls against the server:
 *
 *	chdir()
 *	mkdir()		(for initial directory creation if not -m)
 *	creat()
 *	symlink()
 *	readlink()
 *	lstat()
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
	fprintf(stdout, "usage: %s [-htfn] <config_file>\n", Myname);
	fprintf(stdout, "  Flags:  h    Help - print this usage info\n");
	fprintf(stdout, "          t    Print execution time statistics\n");
	fprintf(stdout, "          f    Test function only (negate -t)\n");
	fprintf(stdout, "          n    Suppress test directory create operations\n");
}

int main( int argc, char * argv[] )
{
	int files;		/* number of files in each dir */
	int fi;
	int count;	/* times to do each file */
	int ct;
	char *fname;
	char *sname;
	struct timeval time;
	char str[MAXPATHLEN];
	char new[MAXPATHLEN];
	char buf[MAXPATHLEN];
	int ret;
	struct stat statb;
	char *opts;
	int oerrno;
  struct testparam *param;
	struct btest *b;
  char *config_file;
  char *test_dir;
  char *log_file;
  FILE *log;
  int sname_len;

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
  
	b = get_btest_args(param, EIGHT);
  if (b == NULL) {
    fprintf(stderr, "Missing basic test number 8 in the config file '%s'\n", config_file);
    free_testparam(param);
    exit(1);
  }

	if (b->files == -1) {
		fprintf(stderr,"Missing 'files' parameter in the config file '%s' for the basic test number 8\n", config_file);
    free_testparam(param);
		exit(1);
	}
	if (b->count == -1) {
		fprintf(stderr,"Missing 'count' parameter in the config file '%s' for the basic test number 8\n", config_file);
    free_testparam(param);
		exit(1);
	}
	count = b->count;
	files = b->files;
	fname = b->fname;
	sname = b->sname;
  test_dir = get_test_directory(param);
  log_file = get_log_file(param);
  sname_len = (int) strlen(sname);

  free_testparam(param);

#ifndef S_IFLNK
	fprintf(stdout, "\
%s: symlink and readlink not supported on this client\n", Myname);
#else /* S_IFLNK */
	if (!Fflag) {
		Tflag = 0;
		count = 1;
	}

	if (!Nflag)
		testdir(test_dir);
	else
		mtestdir(test_dir);

	fprintf(stdout, "%s: symlink and readlink\n", Myname);

	starttime();
	for (ct = 0; ct < count; ct++) {
		for (fi = 0; fi < files; fi++) {
			sprintf(str, "%s%d", fname, fi);
			sprintf(new, "%s%d", sname, fi);
			if (symlink(new, str) < 0) {
				oerrno = errno;
				error("can't make symlink %s", str);
				errno = oerrno;
				if (errno == EOPNOTSUPP)
					complete();
				else
					exit(1);
			}
      if (lstat(str, &statb) < 0) {
              error("can't stat %s after symlink", str);
              exit(1);
      }
			if ((statb.st_mode & S_IFMT) != S_IFLNK) {
				error("mode of %s not symlink");
				exit(1);
			}
			if ((ret = readlink(str, buf, MAXPATHLEN))
			     != (int) strlen(new)) {
				error("readlink %s ret %d, expect %d",
					str, ret, strlen(new));
				exit(1);
			}
			if (strncmp(new, buf, ret) != 0) {
				error("readlink %s returned bad linkname",
					str);
				exit(1);
			}
			if (unlink(str) < 0) {
				error("can't unlink %s", str);
				exit(1);
			}
		}
	}
	endtime(&time);

	fprintf(stdout, "\t%d symlinks and readlinks on %d files (size of symlink : %d)",
		files * count * 2, files, sname_len);
	if (Tflag) {
		fprintf(stdout, " in %ld.%02ld seconds",
		    (long)time.tv_sec, (long)time.tv_usec / 10000);
	}
	fprintf(stdout, "\n");

  if ((log = fopen( log_file, "a")) == NULL) {
    printf( "Enable to open the file '%s'\n", log_file);
	  complete();
  }
  fprintf(log, "b8\t%d\t%d\t%d\t%ld.%02ld\n", files * count * 2, files, sname_len, (long)time.tv_sec, (long)time.tv_usec / 10000);
  fclose(log);
  
#endif /* S_IFLNK */
	complete();
}
