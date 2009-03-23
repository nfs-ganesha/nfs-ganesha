/*
 *
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
 * Copyright CEA/DAM/DIF (2005)
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
 * \file    fsal_types.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:45:27 $
 * \version $Revision: 1.19 $
 * \brief   File System specific types and constants.
 */

#ifndef _FSAL_TYPES_SPECIFIC_H
#define _FSAL_TYPES_SPECIFIC_H

#include "config_parsing.h"
#include "err_fsal.h"

#include <sys/types.h>
#include <sys/param.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#ifdef _APPLE
#define HOST_NAME_MAX          64
#endif

  /* Change bellow the label of your filesystem configuration
   * section in the GANESHA's configuration file.
   */
# define CONF_LABEL_FS_SPECIFIC   "SNMP"


  /* In this section, you must define your own FSAL internal types.
   * Here are some template types :
   */

/* prefered readdir size */
#define FSAL_READDIR_SIZE 64 


# define FSAL_MAX_NAME_LEN  MAXLABEL 
# define FSAL_MAX_PATH_LEN  SNMP_MAXPATH 

#define FSAL_MAX_PROTO_LEN  16
#define FSAL_MAX_USERNAME_LEN   256
#define FSAL_MAX_PHRASE_LEN   USM_AUTH_KU_LEN
  /* object name */

  typedef struct fsal_name__ {
    char         name[FSAL_MAX_NAME_LEN];
    unsigned int len;
  } fsal_name_t;

  /* object path */

  typedef struct fsal_path__ {
    char         path[FSAL_MAX_PATH_LEN];
    unsigned int len;
  } fsal_path_t;
  
# define FSAL_NAME_INITIALIZER {"",0}
# define FSAL_PATH_INITIALIZER {"",0}

  static fsal_name_t FSAL_DOT = {".",1};
  static fsal_name_t FSAL_DOT_DOT = {"..",2};

  typedef enum
  {
    FSAL_NODETYPE_ROOT = 1,
    FSAL_NODETYPE_NODE = 2,
    FSAL_NODETYPE_LEAF = 3
  } nodetype_t;
  
  /* The handle consists in an oid table.  */
  
  typedef struct fsal_handle__
  {
    oid 	    oid_tab[MAX_OID_LEN];
    size_t          oid_len;
    nodetype_t      object_type_reminder;

  } fsal_handle_t;

  typedef struct fsal_cred__
  {
    fsal_uid_t    user;
    fsal_gid_t    group;
    /*
    int    ticket_handle;
    time_t ticket_renewal_time;
    */
  } fsal_cred_t;
  
  typedef struct fsal_export_context__
  {
    fsal_handle_t  root_handle;
    struct tree *  root_mib_tree;
    fsal_path_t    root_path;

  } fsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(FSAL_Handle_to_RBTIndex( &(pexport_context->root_handle), 0 ) )

  typedef struct fsal_op_context__
  {
    /* user authentication info */ 
    fsal_cred_t     user_credential;

    /* SNMP session and the associated info  */
    netsnmp_session *snmp_session;
    netsnmp_pdu     *snmp_request;
    netsnmp_pdu     *snmp_response;
    netsnmp_variable_list *current_response;

    /* the export context for the next request */
    fsal_export_context_t *export_context;

  } fsal_op_context_t;
  
  typedef struct fsal_dir__
  {
	fsal_handle_t       node_handle; 
	fsal_op_context_t * p_context;
  } fsal_dir_t;

  typedef struct fsal_file__
  {
	fsal_handle_t 		file_handle;
	fsal_op_context_t 	* p_context;

	enum
	{
 	  FSAL_MODE_READ  = 1,
          FSAL_MODE_WRITE = 2
	} rw_mode;

  } fsal_file_t;
  
# define FSAL_FILENO(_f) (0)
  
  typedef struct fsal_cookie__
  {
    /* in SNMP the cookie is the last listed entry */
    oid 	    oid_tab[MAX_OID_LEN];
    unsigned int    oid_len; 
  } fsal_cookie_t;
  
static fsal_cookie_t FSAL_READDIR_FROM_BEGINNING = { {0,}, 0 }; 
    
  typedef struct fs_specific_initinfo__
  {
    long snmp_version;
    char snmp_server [HOST_NAME_MAX];
    char community [COMMUNITY_MAX_LEN];
    int  nb_retries;       /* Number of retries before timeout */ 
    int  microsec_timeout; /* Number of uS until first timeout, then exponential backoff */
    int  enable_descriptions;
    char client_name[HOST_NAME_MAX];
    unsigned int getbulk_count;
    char auth_proto[FSAL_MAX_PROTO_LEN];
    char enc_proto[FSAL_MAX_PROTO_LEN];
    char username[FSAL_MAX_NAME_LEN];
    char auth_phrase[FSAL_MAX_PHRASE_LEN];
    char enc_phrase[FSAL_MAX_PHRASE_LEN];
  } fs_specific_initinfo_t;
  
  typedef void * fsal_lockdesc_t;
    

#endif /* _FSAL_TYPES_SPECIFIC_H */

