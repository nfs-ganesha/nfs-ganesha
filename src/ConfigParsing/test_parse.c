#include "config_parsing.h"
#include <errno.h>

int main(int argc, char **argv)
{

  config_file_t config;
  char *fichier;
  char *errtxt;

  if ((argc > 1) && (argv[1]))
    {
      fichier = argv[1];
    }
  else
    {
      fprintf(stderr, "Usage %s <config_file>\n", argv[0]);
      exit(EINVAL);
    }

  /* Exemple de parsing */
  config = config_ParseFile(fichier);

  printf("config_pointer = %p\n", config);

  if (config == NULL)
    {
      errtxt = config_GetErrorMsg();
      fprintf(stderr, "Erreur de parsing de %s : %s\n", argv[1], errtxt);
      exit(EINVAL);
    }

  config_Print(stdout, config);

  {
    int i;
    char *val_a;
    config_item_t block, item;

    for (i = 0; i < config_GetNbBlocks(config); i++)
      {

        int j;
        char *nom;
        char *val;

        /* affichage du nom de l'item : */
        block = config_GetBlockByIndex(config, i);

        printf("bloc %s\n", config_GetBlockName(block));

        if ((val_a = config_GetKeyValueByName(block, "b")))
          {
            printf("%s.b est defini et vaut %s\n", config_GetBlockName(block), val_a);
          }
        else
          {
            printf("%s.b n'est pas defini\n", config_GetBlockName(block));
          }

        /* parcours des variables du block */
        for (j = 0; j < config_GetNbItems(block); j++)
          {

            item = config_GetItemByIndex(block, j);

            if (config_ItemType(item) == CONFIG_ITEM_VAR)
              {
                config_GetKeyValue(item, &nom, &val);
                printf("\t%s = %s\n", nom, val);
              }
            else
              printf("\tsub-block = %s\n", config_GetBlockName(item));
          }
        printf("\n");

      }

  }

/* free and reload the file */
  config_Free(config);

  config = config_ParseFile(fichier);

  printf("config_pointer = %p\n", config);

  if (config == NULL)
    {
      errtxt = config_GetErrorMsg();
      fprintf(stderr, "Erreur de 2e parsing de %s : %s\n", argv[1], errtxt);
      exit(EINVAL);
    }

  config_Print(stdout, config);

  exit(0);

}
