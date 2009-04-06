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

#ifndef CONFPARSER_H
#define CONFPARSER_H

#include <stdio.h>


#define MAXSTRLEN   1024


/* A program consists of several blocks,
 * each block consists of variables definitions
 * and subblocks.
 */

/* forward declaration of generic item */
struct _generic_item_;

typedef enum {
                TYPE_BLOCK,
                TYPE_AFFECT
             } type_item;
                
                
typedef struct _type_affect_ {

    char varname[MAXSTRLEN];
    char varvalue[MAXSTRLEN];
    
} type_affect;


typedef struct _type_block_ {

    char                    block_name[MAXSTRLEN];
    struct _generic_item_ * block_content;
    
} type_block;


typedef struct _generic_item_ {
    
    type_item   type;
    union
    {
       type_block  block;
       type_affect affect;
    }item;
    
    /* next item in the list */
    struct _generic_item_ * next;    
        
} generic_item;


typedef generic_item * list_items;

/**
 *  create a list of items
 */
list_items * config_CreateItemsList();

/**
 *  Create a block item with the given content
 */
generic_item * config_CreateBlock(char * blockname, list_items * list);

/**
 *  Create a key=value peer (assignment)
 */
generic_item * config_CreateAffect(char * varname, char * varval);


/**
 *  Add an item to a list
 */
void config_AddItem( list_items * list, generic_item * item );


/**
 *  Displays the content of a list of blocks.
 */
void config_print_list(FILE * output, list_items * list);

/**
 * config_free_list:
 * Free ressources for a list
 */
void config_free_list(list_items * list);




#endif
