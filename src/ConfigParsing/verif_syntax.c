/**
 *
 * \file    verif_syntaxe.c
 * \author  $Author: leibovic $
 * \date    $Date: 2008/07/04 08:15:29 $
 * \version	$Revision: 1.2 $ 
 * \brief   Construction de l'arbre syntaxique.
 *
 * Teste la syntaxe d'un fichier de configuration.
 *
 * Historique CVS :
 *
 * $Log: verif_syntax.c,v $
 * Revision 1.2  2008/07/04 08:15:29  leibovic
 * Added missing "include <config.h>"
 *
 * Revision 1.1  2008/07/03 11:32:31  leibovic
 * New config parsing with sub blocks.
 *
 * Revision 1.2  2004/08/18 09:53:38  leibovic
 * Ajout de fonctionnalites de parsing en perl.
 *
 * Revision 1.1  2004/08/18 06:55:08  leibovic
 * programme de verification de syntaxe.
 *
 *
 */

#include "config_parsing.h"
#include "log_macros.h"
#include <errno.h>

int main(int argc, char **argv)
{

  SetDefaultLogging("TEST");
  SetNamePgm("verif_syntax");
  char *errtxt;
  char *fichier;

  config_file_t config;

  if((argc > 1) && (argv[1]))
    {
      fichier = argv[1];
    }
  else
    {
      LogTest("Usage %s <config_file>", argv[0]);
      exit(EINVAL);
    }

  /* test de la syntaxe du fichier */
  config = config_ParseFile(fichier);
  if(config == NULL)
    {
      errtxt = config_GetErrorMsg();
      LogTest("Error parsing %s : %s", argv[1], errtxt);
      exit(EINVAL);
    }
  else
    {
      LogTest("The syntax of the file %s is correct!", argv[1]);
      exit(0);
    }

  return 0;

}
