/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
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
 * Copyright CEA/DAM/DIF (2008)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
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
 therefore means  that it is reserved for developers  and  experienced
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

/**
 * \file    nfs_init.h
 * \author  $Author: leibovic $
 * \brief   NFSd initialization prototypes.
 *
 */

#ifndef _NFS_INIT_H
#define _NFS_INIT_H

#include "cache_content.h"
#include "log_functions.h"
#include "nfs_core.h"

/* setting this variable to TRUE causes datacache
 * to be flushed by garbage collector.
 */
extern unsigned int force_flush_by_signal;

typedef struct __nfs_start_info {
  int flush_datacache_mode;
  int dump_default_config;
  unsigned int nb_flush_threads;
  cache_content_flush_behaviour_t flush_behaviour;
  int lw_mark_trigger;
} nfs_start_info_t;

/**
 * nfs_prereq_init:
 * Initialize NFSd prerequisites: memory management, logging, ...
 */
int nfs_prereq_init(char *program_name, char *host_name, int debug_level, char *log_path);

/**
 * nfs_set_param_default:
 * Set p_nfs_param structure to default parameters.
 */
int nfs_set_param_default(nfs_parameter_t * p_nfs_param);

/**
 * nfs_set_param_from_conf:
 * Load parameters from config file.
 */
int nfs_set_param_from_conf(nfs_parameter_t * p_nfs_param,
			    nfs_start_info_t * p_start_info, char *config_file);

/**
 * nfs_check_param_consistency:
 * Checks parameters concistency (limits, ...)
 */
int nfs_check_param_consistency(nfs_parameter_t * p_nfs_param);

/**
 * nfs_start:
 * start NFS service
 */
int nfs_start(nfs_parameter_t * p_nfs_param, nfs_start_info_t * p_start_info);

/**
 * nfs_stop:
 * stop NFS service and exit
 */
void nfs_stop();

#endif
