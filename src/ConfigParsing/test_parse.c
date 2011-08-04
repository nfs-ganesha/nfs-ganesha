#include "config_parsing.h"
#include "log_macros.h"
#include <errno.h>

int main(int argc, char **argv)
{
  SetDefaultLogging("TEST");
  SetNamePgm("test_parse");

  config_file_t config;
  char *fichier;
  char *errtxt;

  if((argc > 1) && (argv[1]))
    {
      fichier = argv[1];
    }
  else
    {
      LogTest("Usage %s <config_file>", argv[0]);
      exit(EINVAL);
    }

  /* Exemple de parsing */
  config = config_ParseFile(fichier);

  LogTest("config_pointer = %p", config);

  if(config == NULL)
    {
      errtxt = config_GetErrorMsg();
      LogTest("Error in parsing %s : %s", argv[1], errtxt);
      exit(EINVAL);
    }

  config_Print(stdout, config);

  {
    int i;
    char *val_a;
    config_item_t block, item;

    for(i = 0; i < config_GetNbBlocks(config); i++)
      {

        int j;
        char *nom;
        char *val;

        /* affichage du nom de l'item : */
        block = config_GetBlockByIndex(config, i);

        LogTest("bloc %s", config_GetBlockName(block));

        if((val_a = config_GetKeyValueByName(block, "b")))
          {
            LogTest("%s.b is defined as %s", config_GetBlockName(block), val_a);
          }
        else
          {
            LogTest("%s.b not defined", config_GetBlockName(block));
          }

        /* parcours des variables du block */
        for(j = 0; j < config_GetNbItems(block); j++)
          {

            item = config_GetItemByIndex(block, j);

            if(config_ItemType(item) == CONFIG_ITEM_VAR)
              {
                config_GetKeyValue(item, &nom, &val);
                LogTest("\t%s = %s", nom, val);
              }
            else
              LogTest("\tsub-block = %s", config_GetBlockName(item));
          }
        LogTest(" ");

      }

  }

/* free and reload the file */
  config_Free(config);

  config = config_ParseFile(fichier);

  LogTest("config_pointer = %p", config);

  if(config == NULL)
    {
      errtxt = config_GetErrorMsg();
      LogTest("Parsing error for %s : %s", argv[1], errtxt);
      exit(EINVAL);
    }

  config_Print(stdout, config);

  exit(0);

}
