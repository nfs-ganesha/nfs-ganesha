/*
 *	@(#)test7b.c	1.7	99/08/29 Connectathon Testsuite
 *	1.3 Lachman ONC Test Suite source
 *
 * Test link
 *
 * Uses the following important system calls against the server:
 *
 *	chdir()
 *	mkdir()		(for initial directory creation if not -m)
 *	creat()
 *	stat()
 *	link()
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
#ifdef DOSorWIN32
#include <time.h>
#else
#include <sys/time.h>
#endif
#include <sys/types.h>

#include "tests.h"
#include "Connectathon_config_parsing.h"

static int Tflag = 0;           /* print timing */
static int Fflag = 0;           /* test function only;  set count to 1, negate -t */
static int Nflag = 0;           /* Suppress directory operations */

static void usage()
{
  fprintf(stdout, "usage: %s [-htfn] <config_parsing>\n", Myname);
  fprintf(stdout, "  Flags:  h    Help - print this usage info\n");
  fprintf(stdout, "          t    Print execution time statistics\n");
  fprintf(stdout, "          f    Test function only (negate -t)\n");
  fprintf(stdout, "          n    Suppress test directory create operations\n");
}

int main(int argc, char *argv[])
{
  int files;                    /* number of files in each dir */
  int fi;
  int count;                    /* times to do each file */
  int ct;
  int totfiles = 0;
  int totdirs = 0;
  char *fname;
  char *dname;
  char *nname;
  struct timeval time;
  char str[MAXPATHLEN];
  char new[MAXPATHLEN];
  struct stat statb;
  char *opts;
  int oerrno;
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
  while(argc && **argv == '-')
    {
      for(opts = &argv[0][1]; *opts; opts++)
        {
          switch (*opts)
            {
            case 'h':          /* help */
              usage();
              exit(1);
              break;

            case 't':          /* time */
              Tflag++;
              break;

            case 'f':          /* funtionality */
              Fflag++;
              break;

            case 'n':          /* No Test Directory create */
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

  if(argc)
    {
      config_file = *argv;
      argc--;
      argv++;
    }
  else
    {
      fprintf(stderr, "Missing config_file");
      exit(1);
    }

  if(argc != 0)
    {
      fprintf(stderr, "too many parameters");
      usage();
      exit(1);
    }

  param = readin_config(config_file);
  if(param == NULL)
    {
      fprintf(stderr, "Nothing built\n");
      exit(1);
    }

  b = get_btest_args(param, SEVEN);
  if(b == NULL)
    {
      fprintf(stdout, "Missing basic test number 7 in the config file '%s'\n",
              config_file);
      free_testparam(param);
      exit(1);
    }

  if(b->files == -1)
    {
      fprintf(stderr,
              "Missing 'files' parameter in the config file '%s' for the basic test number 7\n",
              config_file);
      free_testparam(param);
      exit(1);
    }
  if(b->count == -1)
    {
      fprintf(stderr,
              "Missing 'count' parameter in the config file '%s' for the basic test number 7\n",
              config_file);
      free_testparam(param);
      exit(1);
    }
  count = b->count;
  files = b->files;
  fname = b->fname;
  dname = b->dname;
  nname = b->nname;
  test_dir = get_test_directory(param);
  log_file = get_log_file(param);

  free_testparam(param);

  if(!Fflag)
    {
      Tflag = 0;
      count = 1;
    }

  fprintf(stdout, "%s: link\n", Myname);

  if(!Nflag)
    testdir(test_dir);
  else
    mtestdir(test_dir);

  dirtree(1, files, 0, fname, dname, &totfiles, &totdirs);

  starttime();
  for(ct = 0; ct < count; ct++)
    {
      for(fi = 0; fi < files; fi++)
        {
          sprintf(str, "%s%d", fname, fi);
          sprintf(new, "%s%d", nname, fi);
#ifndef DOSorWIN32
          if(link(str, new) < 0)
            {
              oerrno = errno;
              error("can't link %s to %s", str, new);
              errno = oerrno;
              if(errno == EOPNOTSUPP)
                complete();
              exit(1);
            }
          if(stat(new, &statb) < 0)
            {
              error("can't stat %s after link", new);
              exit(1);
            }
          if(statb.st_nlink != 2)
            {
              error("%s has %d links after link (expect 2)", new, statb.st_nlink);
              exit(1);
            }
          if(stat(str, &statb) < 0)
            {
              error("can't stat %s after link", str);
              exit(1);
            }
          if(statb.st_nlink != 2)
            {
              error("%s has %d links after link (expect 2)", str, statb.st_nlink);
              exit(1);
            }
          if(unlink(new) < 0)
            {
              error("can't unlink %s", new);
              exit(1);
            }
          if(stat(str, &statb) < 0)
            {
              error("can't stat %s after unlink %s", str, new);
              exit(1);
            }
          if(statb.st_nlink != 1)
            {
              error("%s has %d links after unlink (expect 1)", str, statb.st_nlink);
              exit(1);
            }
#else   /* DOSorWIN32 */                        /* just rename back to orig name */
          if(rename(str, new) < 0)
            {
              error("can't rename %s to %s", str, new);
              exit(1);
            }
          if(stat(new, &statb) < 0)
            {
              error("can't stat %s after rename %s", new, str);
              exit(1);
            }
          if(statb.st_nlink != 1)
            {
              error("%s has %d links after rename (expect 1)", new, statb.st_nlink);
              exit(1);
            }
          if(rename(new, str) < 0)
            {
              error("can't rename %s to %s", new, str);
              exit(1);
            }
          if(stat(str, &statb) < 0)
            {
              error("can't stat %s after rename %s", str, new);
              exit(1);
            }
          if(statb.st_nlink != 1)
            {
              error("%s has %d links after rename (expect 1)", str, statb.st_nlink);
              exit(1);
            }
#endif                          /* DOSorWIN32 */
        }
    }
  endtime(&time);

  fprintf(stdout, "\t%d links on %d files", files * count, files);
  if(Tflag)
    {
      fprintf(stdout, " in %ld.%02ld seconds",
              (long)time.tv_sec, (long)time.tv_usec / 10000);
    }
  fprintf(stdout, "\n");

  /* Cleanup files left around */
  rmdirtree(1, files, 0, fname, dname, &totfiles, &totdirs, 1);

  if((log = fopen(log_file, "a")) == NULL)
    {
      printf("Enable to open the file '%s'\n", log_file);
      complete();
    }
  fprintf(log, "b7b\t%d\t%d\t%ld.%02ld\n", files * count, files, (long)time.tv_sec,
          (long)time.tv_usec / 10000);
  fclose(log);

  complete();
}
