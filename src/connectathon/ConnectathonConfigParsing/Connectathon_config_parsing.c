#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "Connectathon_config_parsing.h"
#include "Connectathon_parser_yacc.h"

extern FILE *yyin;
struct testparam *param;

extern int yyparse();

/*********** For building structure from config file ***********/
void btest_init_defaults(struct btest *b)
{
  memset(b, 0, sizeof(struct btest));

  b->num = -1;
  b->num2 = -1;

  b->levels = -1;
  b->files = -1;
  b->dirs = -1;
  b->count = -1;
  b->size = -1;
  b->blocksize = -1;

  b->bigfile = "bigfile";

  b->fname = "file.";
  b->dname = "dir.";
  b->nname = "newfile.";
  b->sname = "/this/is/a/symlink";
}

void testparam_init_defaults(struct testparam *t)
{
  memset(t, 0, sizeof(struct testparam));

  t->dirtest = "/path/to/dir/test";
}

void free_btest(struct btest *b)
{
  if (b->nextbtest != NULL)
    free_btest(b->nextbtest);
  free(b);
  return;
}

void free_testparam(struct testparam *t)
{
  free_btest(t->btest);
  free(t);
  return;
}

/************************ For parsing **************************/

char *get_test_directory(struct testparam *t)
{
  return t->dirtest;
}

char *get_log_file(struct testparam *t)
{
  return t->logfile;
}

struct btest *get_btest_args(struct testparam *param, enum test_number k)
{
  struct btest *it = param->btest;

  while (it)
    {
      if (it->num == k || it->num2 == k)
	return it;
      it = it->nextbtest;
    }

  return NULL;
}

struct testparam *readin_config(char *fname)
{
  if ((yyin = fopen(fname, "r")) == NULL)
    {
      fprintf(stderr, "can't open %s: %s\n", fname, strerror(errno));
      return NULL;
    }

  if (yyparse() != 0)
    {
      fprintf(stderr, "error parsing or activating the config file: %s\n", fname);
      return NULL;
    }

  fclose(yyin);
  return param;
}
