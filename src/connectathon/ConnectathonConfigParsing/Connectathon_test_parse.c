#include "Connectathon_config_parsing.h"
#include <errno.h>
#include <stdio.h>

int main(int argc, char **argv)
{
  char *filename;
  struct testparam *param;
  struct btest *b;

  if ((argc > 1) && (argv[1]))
    {
      filename = argv[1];
    }
  else
    {
      fprintf(stderr, "Usage %s <config_file>\n", argv[0]);
      return -1;
    }

  param = readin_config(filename);
  if (param == NULL)
    {
      fprintf(stdout, "Nothing built\n");
      return -1;
    }

  /* test directory */
  fprintf(stdout, "####### TEST DIRECTORY : %s\n", get_test_directory(param));

  /* log file */
  fprintf(stdout, "\n####### LOG FILE : %s\n", get_log_file(param));

  /* basic test 1 */
  b = get_btest_args(param, ONE);
  if (b == NULL)
    {
      fprintf(stdout, "Missing basic test number 1\n");
      free_testparam(param);
      return -1;
    }

  fprintf(stdout, "\n####### BASIC TEST 1 :\n");
  fprintf(stdout, "\tLEVELS : %d\n", b->levels);
  fprintf(stdout, "\tFILES : %d\n", b->files);
  fprintf(stdout, "\tDIRS : %d\n", b->dirs);
  fprintf(stdout, "\tFNAME : %s\n", b->fname);
  fprintf(stdout, "\tDNAME : %s\n", b->dname);

  /* basic test 2 */
  b = get_btest_args(param, TWO);
  if (b == NULL)
    {
      fprintf(stdout, "Missing basic test number 2\n");
      free_testparam(param);
      return -1;
    }

  fprintf(stdout, "\n####### BASIC TEST 2 :\n");
  fprintf(stdout, "\tLEVELS : %d\n", b->levels);
  fprintf(stdout, "\tFILES : %d\n", b->files);
  fprintf(stdout, "\tDIRS : %d\n", b->dirs);
  fprintf(stdout, "\tFNAME : %s\n", b->fname);
  fprintf(stdout, "\tDNAME : %s\n", b->dname);

  /* basic test 3 */
  b = get_btest_args(param, THREE);
  if (b == NULL)
    {
      fprintf(stdout, "Missing basic test number 3\n");
      free_testparam(param);
      return -1;
    }

  fprintf(stdout, "\n####### BASIC TEST 3 :\n");
  fprintf(stdout, "\tCOUNT : %d\n", b->count);

  /* basic test 4 */
  b = get_btest_args(param, FOUR);
  if (b == NULL)
    {
      fprintf(stdout, "Missing basic test number 4\n");
      free_testparam(param);
      return -1;
    }

  fprintf(stdout, "\n####### BASIC TEST 4 :\n");
  fprintf(stdout, "\tFILES : %d\n", b->files);
  fprintf(stdout, "\tCOUNT : %d\n", b->count);
  fprintf(stdout, "\tFNAME : %s\n", b->fname);
  fprintf(stdout, "\tDNAME : %s\n", b->dname);

  /* basic test 5 */
  b = get_btest_args(param, FIVE);
  if (b == NULL)
    {
      fprintf(stdout, "Missing basic test number 5\n");
      free_testparam(param);
      return -1;
    }

  fprintf(stdout, "\n####### BASIC TEST 5 :\n");
  fprintf(stdout, "\tCOUNT : %d\n", b->count);
  fprintf(stdout, "\tSIZE : %d\n", b->size);
  fprintf(stdout, "\tBIGFILE : %s\n", b->bigfile);

  /* basic test 6 */
  b = get_btest_args(param, SIX);
  if (b == NULL)
    {
      fprintf(stdout, "Missing basic test number 6\n");
      free_testparam(param);
      return -1;
    }

  fprintf(stdout, "\n####### BASIC TEST 6 :\n");
  fprintf(stdout, "\tFILES : %d\n", b->files);
  fprintf(stdout, "\tCOUNT : %d\n", b->count);
  fprintf(stdout, "\tFNAME : %s\n", b->fname);
  fprintf(stdout, "\tDNAME : %s\n", b->dname);

  /* basic test 7 */
  b = get_btest_args(param, SEVEN);
  if (b == NULL)
    {
      fprintf(stdout, "Missing basic test number 7\n");
      free_testparam(param);
      return -1;
    }

  fprintf(stdout, "\n####### BASIC TEST 7 :\n");
  fprintf(stdout, "\tFILES : %d\n", b->files);
  fprintf(stdout, "\tCOUNT : %d\n", b->count);
  fprintf(stdout, "\tFNAME : %s\n", b->fname);
  fprintf(stdout, "\tDNAME : %s\n", b->dname);
  fprintf(stdout, "\tNNAME : %s\n", b->nname);

  /* basic test 8 */
  b = get_btest_args(param, EIGHT);
  if (b == NULL)
    {
      fprintf(stdout, "Missing basic test number 8\n");
      free_testparam(param);
      return -1;
    }

  fprintf(stdout, "\n####### BASIC TEST 8 :\n");
  fprintf(stdout, "\tCOUNT : %d\n", b->count);
  fprintf(stdout, "\tFILES : %d\n", b->files);
  fprintf(stdout, "\tFNAME : %s\n", b->fname);
  fprintf(stdout, "\tSNAME : %s\n", b->sname);

  /* basic test 9 */
  b = get_btest_args(param, NINE);
  if (b == NULL)
    {
      fprintf(stdout, "Missing basic test number 9\n");
      free_testparam(param);
      return -1;
    }

  fprintf(stdout, "\n####### BASIC TEST 9 :\n");
  fprintf(stdout, "\tCOUNT : %d\n", b->count);

  return 0;
}
