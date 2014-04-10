/**
 * @file  verif_syntax.c
 * @brief Test the syntax of the configuration file.
 */

#include "config_parsing.h"
#include "log.h"
#include <errno.h>

int main(int argc, char **argv)
{

	SetDefaultLogging("TEST");
	SetNamePgm("verif_syntax");
	char *errtxt;
	char *fichier;

	config_file_t config;

	if ((argc > 1) && (argv[1])) {
		fichier = argv[1];
	} else {
		LogTest("Usage %s <config_file>", argv[0]);
		exit(EINVAL);
	}

	/* test de la syntaxe du fichier */
	config = config_ParseFile(fichier);
	if (config == NULL) {
		LogTest("Error parsing %s", argv[1]);
		exit(EINVAL);
	} else {
		LogTest("The syntax of the file %s is correct!", argv[1]);
		exit(0);
	}
	config_Free(config);

	return 0;

}
