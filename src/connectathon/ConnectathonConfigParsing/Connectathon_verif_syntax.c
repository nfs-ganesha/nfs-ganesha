#include "Connectathon_config_parsing.h"
#include <errno.h>
#include <stdio.h>

int main(int argc, char **argv)
{
  char *filename;
  struct testparam *param;

  if((argc > 1) && (argv[1]))
    {
      filename = argv[1];
    }
  else
    {
      fprintf(stderr, "Usage %s <config_file>\n", argv[0]);
      return -1;
    }

  param = readin_config(filename);
  if(param == NULL)
    return -1;

  free_testparam(param);

  return 0;
}
