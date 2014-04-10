#include "config_parsing.h"
#include "log.h"
#include <errno.h>

int main(int argc, char **argv)
{
	SetDefaultLogging("TEST");
	SetNamePgm("test_parse");

	config_file_t config;
	char *fichier;
	char *errtxt;

	if ((argc > 1) && (argv[1])) {
		fichier = argv[1];
	} else {
		LogTest("Usage %s <config_file>", argv[0]);
		exit(EINVAL);
	}

	/* Exemple de parsing */
	config = config_ParseFile(fichier);

	LogTest("config_pointer = %p", config);

	if (config == NULL) {
		errtxt = config_GetErrorMsg();
		LogTest("Error in parsing %s : %s", argv[1], errtxt);
		exit(EINVAL);
	}

	config_Print(stdout, config);


/* free and reload the file */
	config_Free(config);

	config = config_ParseFile(fichier);

	LogTest("config_pointer = %p", config);

	if (config == NULL) {
		LogTest("Parsing error for %s", argv[1], errtxt);
		exit(EINVAL);
	}

	config_Print(stdout, config);
	config_Free(config);

	exit(0);

}
