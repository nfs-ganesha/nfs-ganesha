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
