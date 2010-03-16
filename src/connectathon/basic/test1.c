/*
 *	@(#)test1.c	1.5 99/08/29 Connectathon Testsuite
 *	1.4 Lachman ONC Test Suite source
 *
 * Test file and directory creation.
 * Builds a tree on the server.
 *
 * Uses the following important system calls against the server:
 *
 *	chdir()
 *	mkdir()		(if creating directories, level > 1)
 *	creat()
 */

#if defined (DOS) || defined (WIN32)
/* If Dos, Windows or Win32 */
#define DOSorWIN32
#endif

#ifndef DOSorWIN32
#include <sys/param.h>
#endif

#include <stdlib.h>
#include <sys/types.h>
#ifdef DOSorWIN32
#include <time.h>
#else
#include <sys/time.h>
#endif
#include <sys/stat.h>
#include <stdio.h>

#include "tests.h"
#include "Connectathon_config_parsing.h"

static int Tflag = 0;		/* print timing */
static int Sflag = 0;		/* don't print non-error messages */
static int Fflag = 0;		/* test function only;  set count to 1, negate -t */
static int Nflag = 0;		/* Suppress directory operations */

static void usage()
{
  fprintf(stdout, "usage: %s [-htfn] <config_file>\n", Myname);
  /* -s is a hidden option used by test2 */
  fprintf(stdout, "  Flags:  h    Help - print this usage info\n");
  fprintf(stdout, "          t    Print execution time statistics\n");
  fprintf(stdout, "          f    Test function only (negate -t)\n");
  fprintf(stdout, "          n    Suppress test directory create operations\n");
}

int main(int argc, char *argv[])
{
  int files;			/* number of files in each dir */
  int totfiles = 0;
  int dirs;			/* directories in each dir */
  int totdirs = 0;
  int levels;			/* levels deep */
  char *fname;
  char *dname;
  struct timeval time;
  char *opts;
  struct testparam *param;
  struct btest *b;
  char *config_file;
  char *test_dir;
  char *log_file;
  FILE *log;

  setbuf(stdout, NULL);
  Myname = *argv++;
  argc--;
  while (argc && **argv == '-')
    {
      for (opts = &argv[0][1]; *opts; opts++)
	{
	  switch (*opts)
	    {
	    case 'h':		/* help */
	      usage();
	      exit(1);
	      break;

	    case 's':		/* silent */
	      Sflag++;
	      break;

	    case 't':		/* time */
	      Tflag++;
	      break;

	    case 'f':		/* funtionality */
	      Fflag++;
	      break;

	    case 'n':		/* No Test Directory create */
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

  if (argc)
    {
      config_file = *argv;
      argc--;
      argv++;
    } else
    {
      fprintf(stderr, "Missing config_file");
      exit(1);
    }

  if (argc != 0)
    {
      fprintf(stderr, "too many parameters");
      usage();
      exit(1);
    }

  param = readin_config(config_file);
  if (param == NULL)
    {
      fprintf(stderr, "Nothing built\n");
      exit(1);
    }

  b = get_btest_args(param, ONE);
  if (b == NULL)
    {
      fprintf(stderr, "Missing basic test number 1 in the config file '%s'\n",
	      config_file);
      free_testparam(param);
      exit(1);
    }

  if (b->levels == -1)
    {
      fprintf(stderr,
	      "Missing 'levels' parameter in the config file '%s' for the basic test number 1\n",
	      config_file);
      free_testparam(param);
      exit(1);
    }
  if (b->files == -1)
    {
      fprintf(stderr,
	      "Missing 'files' parameter in the config file '%s' for the basic test number 1\n",
	      config_file);
      free_testparam(param);
      exit(1);
    }
  if (b->dirs == -1)
    {
      fprintf(stderr,
	      "Missing 'dirs' parameter in the config file '%s' for the basic test number 1\n",
	      config_file);
      free_testparam(param);
      exit(1);
    }
  levels = b->levels;
  files = b->files;
  dirs = b->dirs;
  fname = b->fname;
  dname = b->dname;
  test_dir = get_test_directory(param);
  log_file = get_log_file(param);

  free_testparam(param);

  if (!Fflag)
    {
      Tflag = 0;
      levels = 2;
      files = 2;
      dirs = 2;
    }

  if (!Sflag)
    {
      fprintf(stdout, "%s: File and directory creation test\n", Myname);
    }

  if (!Nflag)
    testdir(test_dir);
    else
    mtestdir(test_dir);

  starttime();
  dirtree(levels, files, dirs, fname, dname, &totfiles, &totdirs);
  endtime(&time);

  if (!Sflag)
    {
      fprintf(stdout,
	      "\tcreated %d files %d directories %d levels deep",
	      totfiles, totdirs, levels);
    }
  if (Tflag && !Sflag)
    {
      fprintf(stdout, " in %ld.%02ld seconds",
	      (long)time.tv_sec, (long)time.tv_usec / 10000);
    }
  if (!Sflag)
    {
      fprintf(stdout, "\n");
    }

  if ((log = fopen(log_file, "a")) == NULL)
    {
      printf("Enable to open the file '%s'\n", log_file);
      complete();
    }
  fprintf(log, "b1\t%d\t%d\t%d\t%ld.%02ld\n", totfiles, totdirs, levels,
	  (long)time.tv_sec, (long)time.tv_usec / 10000);
  fclose(log);

  complete();
}
