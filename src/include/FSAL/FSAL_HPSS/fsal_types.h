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
 * \brief   File System Abstraction Layer types and constants.
 *
 *
 *
 */

#ifndef _FSAL_TYPES_SPECIFIC_H
#define _FSAL_TYPES_SPECIFIC_H

/*
 * FS relative includes
 */

#if defined(_USE_HPSS_51)

#include <u_signed64.h> /* for cast64 function */
#include <hpss_api.h>

#elif defined (_USE_HPSS_62) || defined ( _USE_HPSS_622 )

#include <u_signed64.h> /* for cast64 function */
#include <hpss_api.h>
#include <acct_hpss.h>

#endif


/*
 * labels in the config file
 */


# define CONF_LABEL_FS_SPECIFIC   "HPSS"

/* -------------------------------------------
 *      HPSS dependant definitions
 * ------------------------------------------- */

#define FSAL_MAX_NAME_LEN   HPSS_MAX_FILE_NAME
#define FSAL_MAX_PATH_LEN   HPSS_MAX_PATH_NAME

/* prefered readdir size */
#define FSAL_READDIR_SIZE 2048

/** object name.  */

typedef struct fsal_name__ {
  char          name[FSAL_MAX_NAME_LEN];
  unsigned int  len;
} fsal_name_t;


/** object path.  */

typedef struct fsal_path__ {
  char          path[FSAL_MAX_PATH_LEN];
  unsigned int  len;
} fsal_path_t;



#define FSAL_NAME_INITIALIZER {"",0}
#define FSAL_PATH_INITIALIZER {"",0}

static fsal_name_t FSAL_DOT = {".",1};
static fsal_name_t FSAL_DOT_DOT = {"..",2};


/* Filesystem handle */

typedef struct fsal_handle__ {

    /* The object type */
    fsal_nodetype_t obj_type;
        
    /* The hpss handle */
    ns_ObjHandle_t  ns_handle;
    
} fsal_handle_t;

  
/** FSAL security context */

#ifdef _USE_HPSS_51

typedef struct {
  
  time_t            last_update;
  hsec_UserCred_t   hpss_usercred;
      
} fsal_cred_t;


#elif defined( _USE_HPSS_62 ) || defined ( _USE_HPSS_622 )

typedef struct {
  
  time_t            last_update;
  sec_cred_t        hpss_usercred;
      
} fsal_cred_t;

#endif


typedef struct {
  
  ns_ObjHandle_t  fileset_root_handle;
  unsigned int    default_cos;
      
} fsal_export_context_t;


#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(pexport_context->default_cos)

typedef struct {
  
  fsal_cred_t               credential;
  fsal_export_context_t *   export_context;
      
} fsal_op_context_t;


/** directory stream descriptor */
typedef struct fsal_dir__ {
    fsal_handle_t       dir_handle; /* directory handle */
    fsal_op_context_t   context;       /* credential for readdir operations */
    int                 reserved;   /* not used */
} fsal_dir_t;


/** FSAL file descriptor */

#ifdef _USE_HPSS_51

typedef struct fsal_file__ {
    int              filedes;   /* file descriptor. */
    gss_token_t      fileauthz; /* data access credential. */
} fsal_file_t;

#define FSAL_FILENO( p_fsal_file )  ( (p_fsal_file)->filedes )

#elif defined ( _USE_HPSS_62 ) || defined ( _USE_HPSS_622 )

typedef struct fsal_file__ {
    int                 filedes;   /* file descriptor. */
    hpss_authz_token_t  fileauthz; /* data access credential. */
} fsal_file_t;

#define FSAL_FILENO( p_fsal_file )  ( (p_fsal_file)->filedes )

#endif
    

/** HPSS specific init info */

#ifdef _USE_HPSS_51

typedef struct fs_specific_initinfo__{

    /* specifies the behavior for each init value : */
  struct behaviors {    
    fsal_initflag_t
        
        /* client API configuration */
        PrincipalName,
        KeytabPath,
        
        /* Other specific configuration */
        CredentialLifetime,
        ReturnInconsistentDirent ;
        
      } behaviors;
    
    /* client API configuration info : */
    api_config_t  hpss_config;
    
    /* other configuration info */    
    fsal_uint_t   CredentialLifetime;
    fsal_uint_t   ReturnInconsistentDirent;
    
} fs_specific_initinfo_t;

#elif defined ( _USE_HPSS_62 ) || defined ( _USE_HPSS_622 )

typedef struct fs_specific_initinfo__{

    /* specifies the behavior for each init value : */
  struct behaviors {    
    fsal_initflag_t
        
        /* client API configuration */
        AuthnMech,
        NumRetries,
        BusyDelay,
        BusyRetries,
        MaxConnections,                
        DebugPath,
        
        /* Other specific configuration */
        Principal,
        KeytabPath,

        CredentialLifetime,
        ReturnInconsistentDirent ;
        
      } behaviors;
    
    /* client API configuration info : */
    api_config_t  hpss_config;
    
    /* other configuration info */
    char          Principal[FSAL_MAX_NAME_LEN];
    char          KeytabPath[FSAL_MAX_PATH_LEN];
    
    fsal_uint_t   CredentialLifetime;
    fsal_uint_t   ReturnInconsistentDirent;

} fs_specific_initinfo_t;

#endif

/** directory cookie : OffsetOut parameter of hpss_ReadRawAttrsHandle. */
typedef u_signed64 fsal_cookie_t;

#define FSAL_READDIR_FROM_BEGINNING  (cast64(0))

typedef void * fsal_lockdesc_t;  /**< not implemented in hpss */


#endif /* _FSAL_TYPES_SPECIFIC_H */

