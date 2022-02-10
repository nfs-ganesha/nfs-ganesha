// SPDX-License-Identifier: LGPL-3.0-or-later
/* ----------------------------------------------------------------------------
 * Copyright CEA/DAM/DIF  (2007)
 * contributeur : Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

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
