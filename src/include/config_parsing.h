/* ----------------------------------------------------------------------------
 * Copyright CEA/DAM/DIF  (2007)
 * contributeur : Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2007)
 * Contributor: Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 * therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
 * ---------------------------------------
 */
#ifndef _CONFIG_PARSING_H
#define _CONFIG_PARSING_H

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>

/* opaque type */
typedef caddr_t config_file_t;
typedef caddr_t config_item_t;

typedef enum { CONFIG_ITEM_BLOCK = 1, CONFIG_ITEM_VAR} config_item_type;

/* config_ParseFile:
 * Reads the content of a configuration file and
 * stores it in a memory structure.
 * \return NULL on error.
 */   
config_file_t config_ParseFile(  char * file_path );


/* If config_ParseFile returns a NULL pointer,
 * config_GetErrorMsg returns a detailled message
 * to indicate the reason for this error.
 */
char * config_GetErrorMsg();

/**
 * config_Print:
 * Print the content of the syntax tree
 * to a file.
 */
void config_Print( FILE * output, config_file_t config );


/* Free the memory structure that store the configuration. */
void config_Free( config_file_t config );


/* Indicates how many main blocks are defined into the config file.
 * \return A positive value if no error.
 *         Else return a negative error code.
 */
int config_GetNbBlocks( config_file_t config );


/* retrieves a given block from the config file, from its index */
config_item_t config_GetBlockByIndex( config_file_t config , unsigned int block_no );


/* Return the name of a block */
char * config_GetBlockName( config_item_t block );


/* Indicates how many items are defines in a block */
int config_GetNbItems( config_item_t block );


/* Retrieves an item from a given block and the subitem index. */
config_item_t config_GetItemByIndex( config_item_t block, unsigned int item_no );

/* indicates which type of item it is */
config_item_type    config_ItemType( config_item_t item );

/* Retrieves a key-value peer from a CONFIG_ITEM_VAR */
int config_GetKeyValue(
                        config_item_t item,
                        char ** var_name,
                        char ** var_value
                       );


/* Returns a block or variable with the specified name. This name can be "BLOCK::SUBBLOCK::SUBBLOCK" */
config_item_t config_FindItemByName( config_file_t config,
                                     const char *  name );

/* Directly returns the value of the key with the specified name.
 * This name can be "BLOCK::SUBBLOCK::SUBBLOCK::VARNAME"
 */
char * config_FindKeyValueByName( config_file_t config,
                                  const char * key_name );


/* Returns a block or variable with the specified name from the given block" */
config_item_t config_GetItemByName( config_item_t block,
                                    const char *  name );

/* Directly returns the value of the key with the specified name
 * relative to the given block.
 */
char * config_GetKeyValueByName( config_item_t block,
                                 const char * key_name );



#endif
