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
 * \file    nfs4_Compound.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/16 16:25:44 $
 * \version $Revision: 1.24 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_Compound.c : Routines used for managing the NFS4 COMPOUND functions.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif


#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>  /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"


typedef struct nfs4_op_desc__
{
  char *         name;
  unsigned int   val ;
  int          (*funct)( struct nfs_argop4 *,  
                         compound_data_t *,
                         struct nfs_resop4 *) ;
} nfs4_op_desc_t ;

/* This array maps the operation number to the related position in array optab4 */
#ifndef _USE_NFS4_1
const int optab4index[] = { 0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,
                            24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39 };
#define POS_ILLEGAL 40
#else
const int optab4index[] = { 0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,
                            24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,
                            47,48,49,50,51,52,53,54,55,56,57,58 };
#define POS_ILLEGAL 59
#endif


static const nfs4_op_desc_t optab4v0[] = 
{
    {"OP_ACCESS",              NFS4_OP_ACCESS,              nfs4_op_access 		},
    {"OP_CLOSE",               NFS4_OP_CLOSE,               nfs4_op_close 		},
    {"OP_COMMIT",              NFS4_OP_COMMIT,              nfs4_op_commit		},
    {"OP_CREATE",              NFS4_OP_CREATE,              nfs4_op_create		},
    {"OP_DELEGPURGE",          NFS4_OP_DELEGPURGE,          nfs4_op_delegpurge	        },
    {"OP_DELEGRETURN",         NFS4_OP_DELEGRETURN,         nfs4_op_delegreturn	        },
    {"OP_GETATTR",             NFS4_OP_GETATTR,             nfs4_op_getattr		},
    {"OP_GETFH",               NFS4_OP_GETFH,               nfs4_op_getfh		}, 
    {"OP_LINK",                NFS4_OP_LINK,                nfs4_op_link		},
    {"OP_LOCK",                NFS4_OP_LOCK,                nfs4_op_lock		},
    {"OP_LOCKT",               NFS4_OP_LOCKT,               nfs4_op_lockt		},
    {"OP_LOCKU",               NFS4_OP_LOCKU,               nfs4_op_locku		},
    {"OP_LOOKUP",              NFS4_OP_LOOKUP,              nfs4_op_lookup		},
    {"OP_LOOKUPP",             NFS4_OP_LOOKUPP,             nfs4_op_lookupp		},
    {"OP_NVERIFY",             NFS4_OP_NVERIFY,             nfs4_op_nverify		},
    {"OP_OPEN",                NFS4_OP_OPEN,                nfs4_op_open		},
    {"OP_OPENATTR",            NFS4_OP_OPENATTR,            nfs4_op_openattr	        },
    {"OP_OPEN_CONFIRM",        NFS4_OP_OPEN_CONFIRM,        nfs4_op_open_confirm        },
    {"OP_OPEN_DOWNGRADE",      NFS4_OP_OPEN_DOWNGRADE,      nfs4_op_open_downgrade      },
    {"OP_PUTFH",               NFS4_OP_PUTFH,               nfs4_op_putfh		},
    {"OP_PUTPUBFH",            NFS4_OP_PUTPUBFH,            nfs4_op_putpubfh	        },
    {"OP_PUTROOTFH",           NFS4_OP_PUTROOTFH,           nfs4_op_putrootfh	        },
    {"OP_READ",                NFS4_OP_READ,                nfs4_op_read		}, 
    {"OP_READDIR",             NFS4_OP_READDIR,             nfs4_op_readdir		},
    {"OP_READLINK",            NFS4_OP_READLINK,            nfs4_op_readlink	        },
    {"OP_REMOVE",              NFS4_OP_REMOVE,              nfs4_op_remove		},
    {"OP_RENAME",              NFS4_OP_RENAME,              nfs4_op_rename		},
    {"OP_RENEW",               NFS4_OP_RENEW,               nfs4_op_renew		},
    {"OP_RESTOREFH",           NFS4_OP_RESTOREFH,           nfs4_op_restorefh	        },
    {"OP_SAVEFH",              NFS4_OP_SAVEFH,              nfs4_op_savefh		},
    {"OP_SECINFO",             NFS4_OP_SECINFO,             nfs4_op_secinfo		},
    {"OP_SETATTR",             NFS4_OP_SETATTR,             nfs4_op_setattr		},
    {"OP_SETCLIENTID",         NFS4_OP_SETCLIENTID,         nfs4_op_setclientid	        },
    {"OP_SETCLIENTID_CONFIRM", NFS4_OP_SETCLIENTID_CONFIRM, nfs4_op_setclientid_confirm	},
    {"OP_VERIFY",              NFS4_OP_VERIFY,              nfs4_op_verify		},
    {"OP_WRITE",               NFS4_OP_WRITE,               nfs4_op_write		},
    {"OP_RELEASE_LOCKOWNER",   NFS4_OP_RELEASE_LOCKOWNER,   nfs4_op_release_lockowner   },
    {"OP_ILLEGAL",             NFS4_OP_ILLEGAL,             nfs4_op_illegal		}
}; 


#ifdef _USE_NFS4_1
static const nfs4_op_desc_t optab4v1[] = 
{
    {"OP_ACCESS",               NFS4_OP_ACCESS,               nfs4_op_access 		  },
    {"OP_CLOSE",                NFS4_OP_CLOSE,                nfs4_op_close 		  },
    {"OP_COMMIT",               NFS4_OP_COMMIT,               nfs4_op_commit		  },
    {"OP_CREATE",               NFS4_OP_CREATE,               nfs4_op_create		  },
    {"OP_DELEGPURGE",           NFS4_OP_DELEGPURGE,           nfs4_op_delegpurge	  },
    {"OP_DELEGRETURN",          NFS4_OP_DELEGRETURN,          nfs4_op_delegreturn	  },
    {"OP_GETATTR",              NFS4_OP_GETATTR,              nfs4_op_getattr		  },
    {"OP_GETFH",                NFS4_OP_GETFH,                nfs4_op_getfh		  }, 
    {"OP_LINK",                 NFS4_OP_LINK,                 nfs4_op_link		  },
    {"OP_LOCK",                 NFS4_OP_LOCK,                 nfs41_op_lock		  },
    {"OP_LOCKT",                NFS4_OP_LOCKT,                nfs41_op_lockt		  },
    {"OP_LOCKU",                NFS4_OP_LOCKU,                nfs41_op_locku		  },
    {"OP_LOOKUP",               NFS4_OP_LOOKUP,               nfs4_op_lookup		  },
    {"OP_LOOKUPP",              NFS4_OP_LOOKUPP,              nfs4_op_lookupp		  },
    {"OP_NVERIFY",              NFS4_OP_NVERIFY,              nfs4_op_nverify		  },
    {"OP_OPEN",                 NFS4_OP_OPEN,                 nfs41_op_open		  },
    {"OP_OPENATTR",             NFS4_OP_OPENATTR,             nfs4_op_openattr	          },
    {"OP_OPEN_CONFIRM",         NFS4_OP_OPEN_CONFIRM,         nfs4_op_open_confirm        },
    {"OP_OPEN_DOWNGRADE",       NFS4_OP_OPEN_DOWNGRADE,       nfs4_op_open_downgrade      },
    {"OP_PUTFH",                NFS4_OP_PUTFH,                nfs4_op_putfh		  },
    {"OP_PUTPUBFH",             NFS4_OP_PUTPUBFH,             nfs4_op_putpubfh	          },
    {"OP_PUTROOTFH",            NFS4_OP_PUTROOTFH,            nfs4_op_putrootfh	          },
    {"OP_READ",                 NFS4_OP_READ,                 nfs4_op_read		  }, 
    {"OP_READDIR",              NFS4_OP_READDIR,              nfs4_op_readdir		  },
    {"OP_READLINK",             NFS4_OP_READLINK,             nfs4_op_readlink	          },
    {"OP_REMOVE",               NFS4_OP_REMOVE,               nfs4_op_remove		  },
    {"OP_RENAME",               NFS4_OP_RENAME,               nfs4_op_rename		  },
    {"OP_RENEW",                NFS4_OP_RENEW,                nfs4_op_renew		  },
    {"OP_RESTOREFH",            NFS4_OP_RESTOREFH,            nfs4_op_restorefh	          },
    {"OP_SAVEFH",               NFS4_OP_SAVEFH,               nfs4_op_savefh		  },
    {"OP_SECINFO",              NFS4_OP_SECINFO,              nfs4_op_secinfo		  },
    {"OP_SETATTR",              NFS4_OP_SETATTR,              nfs4_op_setattr	  	  },
    {"OP_SETCLIENTID",          NFS4_OP_SETCLIENTID,          nfs4_op_setclientid	  },
    {"OP_SETCLIENTID_CONFIRM",  NFS4_OP_SETCLIENTID_CONFIRM,  nfs4_op_setclientid_confirm },
    {"OP_VERIFY",               NFS4_OP_VERIFY,               nfs4_op_verify		  },
    {"OP_WRITE",                NFS4_OP_WRITE,                nfs4_op_write		  },
    {"OP_RELEASE_LOCKOWNER",    NFS4_OP_RELEASE_LOCKOWNER,    nfs4_op_release_lockowner   },
    {"OP_BACKCHANNEL_CTL",      NFS4_OP_BACKCHANNEL_CTL,      nfs4_op_illegal             },  /* tbd */
    {"OP_BIND_CONN_TO_SESSION", NFS4_OP_BIND_CONN_TO_SESSION, nfs4_op_illegal             },  /* tbd */
    {"OP_EXCHANGE_ID",          NFS4_OP_EXCHANGE_ID,          nfs41_op_exchange_id        },  
    {"OP_CREATE_SESSION",       NFS4_OP_CREATE_SESSION,       nfs41_op_create_session     },  
    {"OP_DESTROY_SESSION",      NFS4_OP_DESTROY_SESSION,      nfs41_op_destroy_session    },  
    {"OP_FREE_STATEID",         NFS4_OP_FREE_STATEID,         nfs4_op_illegal             },  /* tbd */
    {"OP_GET_DIR_DELEGATION",   NFS4_OP_GET_DIR_DELEGATION,   nfs4_op_illegal             },  /* tbd */
    {"OP_GETDEVICEINFO",        NFS4_OP_GETDEVICEINFO,        nfs4_op_illegal             },  /* tbd */
    {"OP_GETDEVICELIST",        NFS4_OP_GETDEVICELIST,        nfs4_op_illegal             },  /* tbd */
    {"OP_LAYOUTCOMMIT",         NFS4_OP_LAYOUTCOMMIT,         nfs4_op_illegal             },  /* tbd */
    {"OP_LAYOUTGET",            NFS4_OP_LAYOUTGET,            nfs4_op_illegal             },  /* tbd */
    {"OP_LAYOUTRETURN",         NFS4_OP_LAYOUTRETURN,         nfs4_op_illegal             },  /* tbd */
    {"OP_SECINFO_NO_NAME",      NFS4_OP_SECINFO_NO_NAME,      nfs4_op_illegal             },  /* tbd */
    {"OP_SEQUENCE",             NFS4_OP_SEQUENCE,             nfs41_op_sequence           },  
    {"OP_SET_SSV",              NFS4_OP_SET_SSV,              nfs41_op_set_ssv            },  
    {"OP_TEST_STATEID",         NFS4_OP_TEST_STATEID,         nfs4_op_illegal             },  /* tbd */
    {"OP_WANT_DELEGATION",      NFS4_OP_WANT_DELEGATION,      nfs4_op_illegal             },  /* tbd */
    {"OP_DESTROY_CLIENTID",     NFS4_OP_DESTROY_CLIENTID,     nfs4_op_illegal             },  /* tbd */
    {"OP_RECLAIM_COMPLETE",     NFS4_OP_RECLAIM_COMPLETE,     nfs4_op_illegal             },  /* tbd */
    {"OP_ILLEGAL",              NFS4_OP_ILLEGAL,              nfs4_op_illegal		  }
}; 
#endif /* _USE_NFS4_1 */

#ifdef _USE_NFS4_1
nfs4_op_desc_t * optabvers[] = { (nfs4_op_desc_t * )optab4v0, (nfs4_op_desc_t * )optab4v1 } ;
#else
nfs4_op_desc_t * optabvers[] = { (nfs4_op_desc_t * )optab4v0 } ;
#endif

/**
 * nfs4_COMPOUND: The NFS PROC4 COMPOUND
 * 
 * Implements the NFS PROC4 COMPOUND.
 * This routine processes the content of the nfsv4 operation list and composes the result. 
 * On this aspect it is a little similar to a dispatch routine.
 * Operation and functions necessary to process them are defined in array optab4 .
 *
 * 
 *  @param parg        [IN]  generic nfs arguments
 *  @param pexportlist [IN]  the full export list 
 *  @param pcontex     [IN]  context for the FSAL (unused but kept for nfs functions prototype homogeneity)
 *  @param pclient     [INOUT] client resource for request management
 *  @param ht          [INOUT] cache inode hash table
 *  @param preq        [IN]  RPC svc request
 *  @param pres        [OUT] generic nfs reply
 *
 *  @see   nfs4_op_<*> functions
 *  @see   nfs4_GetPseudoFs
 * 
 */

int nfs4_Compound( nfs_arg_t               * parg     /* IN     */,
                   exportlist_t            * pexport  /* IN     */,
                   fsal_op_context_t       * pcontext /* IN     */,
                   cache_inode_client_t    * pclient  /* INOUT  */,
                   hash_table_t            * ht       /* INOUT */,
                   struct svc_req          * preq     /* IN     */,
                   nfs_res_t               * pres     /* OUT    */ ) 
{
  unsigned int i = 0 ;
  int status = NFS4_OK;
  struct nfs_resop4 res ; 
  char __attribute__(( __unused__ )) funcname[] = "nfs4_Compound" ;
  compound_data_t data;
  int opindex ;
 
 
  /* A "local" #define to avoid typo with nfs (too) long structure names */
#define COMPOUND4_ARRAY parg->arg_compound4.argarray

  DisplayLogJdLevel( pclient->log_outputs, NIV_FULL_DEBUG, "NFS v4 COMPOUND REQUEST: %d operation(s)", 
                   COMPOUND4_ARRAY.argarray_len ) ;

#ifdef _USE_NFS4_1
  if( parg->arg_compound4.minorversion > 1 )
#else
  if( parg->arg_compound4.minorversion != 0 )
#endif
    {
      DisplayLog( "NFS V4 COMPOUND: Bad Minor Version %d", 
                  parg->arg_compound4.minorversion );
      
      pres->res_compound4.status = NFS4ERR_MINOR_VERS_MISMATCH ;
      pres->res_compound4.resarray.resarray_len = 0 ;
      return NFS_REQ_OK ;
    }

  /* Check for empty COMPOUND request */
  if( COMPOUND4_ARRAY.argarray_len == 0 )
    {
      DisplayLogJdLevel( pclient->log_outputs, NIV_MAJOR, 
                         "NFS V4 COMPOUND: an empty COMPOUND (no operation in it) was received !!" ) ;

      pres->res_compound4.status = NFS4_OK ;
      pres->res_compound4.resarray.resarray_len = 0 ;
      return NFS_REQ_OK ;
    }

  /* Check for too long request */
  if( COMPOUND4_ARRAY.argarray_len > 30 ) 
    {
      DisplayLogJdLevel( pclient->log_outputs, NIV_MAJOR, 
                         "NFS V4 COMPOUND: an empty COMPOUND (no operation in it) was received !!" ) ;

      pres->res_compound4.status = NFS4ERR_RESOURCE ;
      pres->res_compound4.resarray.resarray_len = 0 ;
      return NFS_REQ_OK ;
    }
 
  /* Minor version related stuff */
  data.minorversion = parg->arg_compound4.minorversion ;
  /** @todo BUGAZOMEU: Reminder: Stats on NFSv4 operations are to be set here */
 
  /* Initialisation of the compound request internal's data */
  data.currentFH.nfs_fh4_len     = 0 ;
  data.currentFH.nfs_fh4_val     = NULL ;
  data.rootFH.nfs_fh4_len        = 0 ;
  data.rootFH.nfs_fh4_val        = NULL ;
  data.publicFH.nfs_fh4_len      = 0 ; 
  data.publicFH.nfs_fh4_val      = NULL ; 
  data.savedFH.nfs_fh4_len       = 0 ;
  data.savedFH.nfs_fh4_val       = NULL ;
  data.mounted_on_FH.nfs_fh4_len = 0 ;
  data.mounted_on_FH.nfs_fh4_val = NULL ;

  data.current_entry             = NULL ;
  data.saved_entry               = NULL ;
  data.pexport                   = NULL ;
  data.pfullexportlist           = pexport ; /* Full export list is provided in input */
  data.pcontext                  = pcontext ; /* Get the fsal credentials from the worker thread */
  data.pseudofs                  = nfs4_GetPseudoFs() ;
  data.reqp                      = preq ;
  data.ht                        = ht ;
  data.pclient                   = pclient ;
  strcpy( data.MntPath, "/" ) ;

  /* Building the client credential field */
  if(  nfs_rpc_req2client_cred( preq, &(data.credential) ) == -1 )
     return NFS_REQ_DROP ; /* Malformed credential */

  /* Keeping the same tag as in the arguments */
  memcpy( &(pres->res_compound4.tag),  &(parg->arg_compound4.tag), sizeof( parg->arg_compound4.tag ) ) ; 
  
  /* Allocating the reply nfs_resop4 */
  if( ( pres->res_compound4.resarray.resarray_val = 
        (struct nfs_resop4 *)Mem_Alloc( (COMPOUND4_ARRAY.argarray_len)*sizeof( struct nfs_resop4 ) )) == NULL )
    {
      /* nfs_Log(CS_ALARM, funcname, SOFTWARE_ERROR, CRITICAL, HPSS_ENOMEM, 0, NULL, NULL); */
      return NFS_REQ_DROP;
    }
  
  /* Managing the operation list */
#ifdef _DEBUG_NFS_V4
  DisplayLogJdLevel( pclient->log_outputs, NIV_FULL_DEBUG, "NFS V4 COMPOUND: There are %d operations", COMPOUND4_ARRAY.argarray_len ) ;
#endif

#ifdef _DEBUG_NFS_V4                   
  for( i = 0 ; i < COMPOUND4_ARRAY.argarray_len ; i++ )
    printf( "%s ", optabvers[parg->arg_compound4.minorversion][optab4index[COMPOUND4_ARRAY.argarray_val[i].argop]].name ) ;
  printf( "\n" ) ;
#endif
 
  pres->res_compound4.resarray.resarray_len = COMPOUND4_ARRAY.argarray_len  ; 
  for( i = 0 ; i <  COMPOUND4_ARRAY.argarray_len ; i++ )
    {
      /* Use optab4index to reference the operation */
#ifdef _USE_NFS4_1
      if( ( COMPOUND4_ARRAY.argarray_val[i].argop <= NFS4_OP_RELEASE_LOCKOWNER  && parg->arg_compound4.minorversion == 0 ) ||
	  ( COMPOUND4_ARRAY.argarray_val[i].argop <= NFS4_OP_RECLAIM_COMPLETE  && parg->arg_compound4.minorversion == 1 ) ) 
#else
      if( COMPOUND4_ARRAY.argarray_val[i].argop <= NFS4_OP_RELEASE_LOCKOWNER )
#endif
        opindex = optab4index[COMPOUND4_ARRAY.argarray_val[i].argop];
      else
        opindex = optab4index[POS_ILLEGAL] ; /* = NFS4_OP_ILLEGAL a value to big for argop means an illegal value */
          
      
      DisplayLog( "NFS V4 COMPOUND: Request #%d is %d = %s, entry #%d in the op array",
                       i, 
                       optabvers[parg->arg_compound4.minorversion][opindex].val,
                       optabvers[parg->arg_compound4.minorversion][opindex].name,
                       opindex ) ;
      
#ifdef _DEBUG_NFS_V4                   
      DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "NFS V4 COMPOUND: Request #%d is %d = %s, entry #%d in the op array",
                       i, 
                       optabvers[parg->arg_compound4.minorversion][opindex].val,
                       optabvers[parg->arg_compound4.minorversion][opindex].name,
                       opindex ) ;
#endif
      
      memset( &res, 0, sizeof( res ) ) ;
      status = (optabvers[parg->arg_compound4.minorversion][opindex].funct)( &(COMPOUND4_ARRAY.argarray_val[i]),
                                        &data,
                                        &res ) ;

      memcpy( &(pres->res_compound4.resarray.resarray_val[i]), &res, sizeof( res ) ) ;

#ifdef _DEBUG_NFS_V4
      utf82str( tmpstr, &(pres->res_compound4.tag) ) ;
      printf( "--> COMPOUND REQUEST TAG is #%s#\n", tmpstr )  ;
      
      print_compound_fh( &data ) ; 

      DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "NFS V4 COMPOUND:Status of %s in position %d = %d", 
                         optabvers[parg->arg_compound4.minorversion][opindex].name, i, status ) ;
#endif

      /* All the operation, like NFS4_OP_ACESS, have a first replyied field called .status */
      pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opaccess.status = status ; 

      if( status != NFS4_OK )
        {
          /* An error occured, we do not manage the other requests in the COMPOUND, this may be a regular behaviour */
#ifdef _DEBUG_NFS_V4
          DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "NFS V4 COMPOUND: Error met, stop request with status =%d", status ) ;
#endif

          pres->res_compound4.resarray.resarray_len =  i+1 ; 
          
          break ;
        }
    }

  /* Complete the reply, in particular, tell where you stopped if unsuccessfull COMPOUD */
  pres->res_compound4.status = status ;

#ifdef _DEBUG_NFS_V4
  DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "NFS V4 COMPOUND: end status = %d|%d  lastindex = %d  last status = %d",
                   status, pres->res_compound4.status,
                   i,  pres->res_compound4.resarray.resarray_val[i-1].nfs_resop4_u.opaccess.status );
  DisplayLogJdLevel( pclient->log_outputs, NIV_DEBUG, "===============================================================" ) ;
#endif
 
  compound_data_Free( &data ) ;

  return NFS_REQ_OK ;
} /* nfs4_Compound */

/**
 * 
 * nfs4_Compound_Free: Mem_Free the result for NFS4PROC_COMPOUND
 *
 * Mem_Free the result for NFS4PROC_COMPOUND.
 *
 * @param resp pointer to be Mem_Freed
 * 
 * @return nothing (void function).
 *
 * @see nfs4_op_getfh
 *
 */
void nfs4_Compound_Free( nfs_res_t * pres)
{
  unsigned int i = 0 ;
#ifdef _DEBUG_NFS_V4
  fprintf(stderr, "nfs4_Compound_Free de %p (resarraylen : %i)\n", pres, pres->res_compound4.resarray.resarray_len ); 
#endif

  for( i = 0 ; i < pres->res_compound4.resarray.resarray_len ; i ++ )
    {
#ifdef _DEBUG_NFS_V4
      fprintf( stderr, "nfs4_Compound_Free sur op=%s\n",  optabvers[parg->arg_compound4.minorversion][optab4index[pres->res_compound4.resarray.resarray_val[i].resop]].name ) ;
#endif

      switch( pres->res_compound4.resarray.resarray_val[i].resop )
        {
        case NFS4_OP_ACCESS:
          nfs4_op_access_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opaccess ) ) ;
          break ;
          
        case NFS4_OP_CLOSE:
          nfs4_op_close_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opclose ) ) ;
          break ;

        case NFS4_OP_COMMIT:
          nfs4_op_commit_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opcommit ) ) ;
          break ;
          
        case NFS4_OP_CREATE:
          nfs4_op_create_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opcreate ) ) ;
          break ;
          
        case NFS4_OP_DELEGPURGE:
          nfs4_op_delegpurge_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opdelegpurge ) ) ;
          break ;
          
        case NFS4_OP_DELEGRETURN:
          nfs4_op_delegreturn_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opdelegreturn ) ) ;
          break ;
      
        case NFS4_OP_GETATTR:
          nfs4_op_getattr_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u. opgetattr ) ) ;
          break ;
          
        case NFS4_OP_GETFH:    
          nfs4_op_getfh_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opgetfh ) ) ;
          break ;
    
        case NFS4_OP_LINK:
          nfs4_op_link_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u. oplink ) ) ;
          break ;
          
        case NFS4_OP_LOCK:
          nfs4_op_lock_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.oplock ) ) ;
          break ;
          
        case NFS4_OP_LOCKT:
          nfs4_op_lockt_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.oplockt ) ) ;
          break ;
          
        case NFS4_OP_LOCKU:
          nfs4_op_locku_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.oplocku ) ) ;
          break ;
          
        case NFS4_OP_LOOKUP:
          nfs4_op_lookup_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.oplookup ) ) ;
          break ;

        case NFS4_OP_LOOKUPP:
          nfs4_op_lookupp_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.oplookupp ) ) ;
          break ;
          
        case NFS4_OP_NVERIFY:
          nfs4_op_nverify_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opnverify ) ) ;
          break ;
               
        case NFS4_OP_OPEN:
          nfs4_op_open_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opopen ) ) ;
          break ;
          
        case NFS4_OP_OPENATTR:
          nfs4_op_openattr_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opopenattr ) ) ;
          break ;
          
        case NFS4_OP_OPEN_CONFIRM:
          nfs4_op_open_confirm_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opopen_confirm ) ) ;
          break ;
          
        case NFS4_OP_OPEN_DOWNGRADE:
          nfs4_op_open_downgrade_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opopen_downgrade ) ) ;
          break ;
          
        case NFS4_OP_PUTFH:
          nfs4_op_putfh_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opputfh ) ) ;
          break ;
        
        case NFS4_OP_PUTPUBFH:
          nfs4_op_putpubfh_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opputpubfh ) ) ;
          break ;
        
        case NFS4_OP_PUTROOTFH:
          nfs4_op_putrootfh_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opputrootfh ) ) ;
          break ;
          
        case NFS4_OP_READ:
          nfs4_op_read_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opread ) ) ;
          break ;
          
        case NFS4_OP_READDIR:
          nfs4_op_readdir_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opreaddir ) ) ;
          break ;
          
        case NFS4_OP_REMOVE: 
          nfs4_op_remove_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opremove ) ) ;
          break ;
          
        case NFS4_OP_RENAME:
          nfs4_op_rename_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.oprename ) ) ;
          break ;
          
        case NFS4_OP_RENEW:
          nfs4_op_renew_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.oprenew ) ) ;
          break ;
          
        case NFS4_OP_RESTOREFH:
          nfs4_op_restorefh_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.oprestorefh ) ) ;
          break ;
          
        case NFS4_OP_SAVEFH:
          nfs4_op_savefh_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opsavefh ) ) ;
          break ;
          
        case NFS4_OP_SECINFO:
          nfs4_op_secinfo_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opsecinfo ) ) ;
          break ;
          
        case NFS4_OP_SETATTR:
          nfs4_op_setattr_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opsetattr ) ) ;
          break ;
          
        case NFS4_OP_SETCLIENTID:
          nfs4_op_setclientid_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opsetclientid ) ) ;
          break ;
          
        case NFS4_OP_SETCLIENTID_CONFIRM:
          nfs4_op_setclientid_confirm_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opsetclientid_confirm ) ) ;
          break ;
          
        case NFS4_OP_VERIFY:
          nfs4_op_verify_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opverify ) ) ;
          break ;
          
        case NFS4_OP_WRITE:
          nfs4_op_write_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opwrite ) ) ;
          break ;
          
        case NFS4_OP_RELEASE_LOCKOWNER:
          nfs4_op_release_lockowner_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.oprelease_lockowner ) ) ;
          break ;

#ifdef _USE_NFS4_1
        case NFS4_OP_EXCHANGE_ID: 
	  nfs41_op_exchange_id_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opexchange_id ) ) ;
          break ;
	    
        case NFS4_OP_CREATE_SESSION:
	  nfs41_op_create_session_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opcreate_session ) ) ;
          break ;

        case NFS4_OP_SEQUENCE: 
	  nfs41_op_sequence_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opsequence ) ) ;
          break ;

        case NFS4_OP_BACKCHANNEL_CTL: 
        case NFS4_OP_BIND_CONN_TO_SESSION: 
        case NFS4_OP_DESTROY_SESSION:
        case NFS4_OP_FREE_STATEID:
        case NFS4_OP_GET_DIR_DELEGATION:
        case NFS4_OP_GETDEVICEINFO: 
        case NFS4_OP_GETDEVICELIST:
        case NFS4_OP_LAYOUTCOMMIT:
        case NFS4_OP_LAYOUTGET: 
        case NFS4_OP_LAYOUTRETURN: 
        case NFS4_OP_SECINFO_NO_NAME: 
        case NFS4_OP_SET_SSV: 
        case NFS4_OP_TEST_STATEID:
        case NFS4_OP_WANT_DELEGATION: 
        case NFS4_OP_DESTROY_CLIENTID:
        case NFS4_OP_RECLAIM_COMPLETE:
#endif
          
        case NFS4_OP_ILLEGAL:
          nfs4_op_illegal_Free( &(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opillegal ) ) ;
          break ;
          
        default:
          /* Should not happen */
          /* BUGAZOMEU : Un nouveau message d'erreur */
          break ;
        } /* switch */

    } /* for i */
  Mem_Free( (char *)pres->res_compound4.resarray.resarray_val );
  if( pres->res_compound4.tag.utf8string_len != 0 )
    Mem_Free( pres->res_compound4.tag.utf8string_val ) ;

  return ;
} /* nfs4_Compound_Free */

/**
 * 
 * compound_data_Free: Mem_Frees the compound data structure.
 *
 * Mem_Frees the compound data structure..
 *
 * @param data pointer to be Mem_Freed
 *
 * @return nothing (void function).
 *
 * @see nfs4_op_getfh
 *
 */
void compound_data_Free( compound_data_t * data )
{

  if( data->currentFH.nfs_fh4_val != NULL ) 
     Mem_Free( (char *)data->currentFH.nfs_fh4_val ) ;

  if( data->rootFH.nfs_fh4_val  != NULL ) 
     Mem_Free( (char *)data->rootFH.nfs_fh4_val ) ;

  if( data->publicFH.nfs_fh4_val != NULL ) 
     Mem_Free( (char *)data->publicFH.nfs_fh4_val ) ;

  if( data->savedFH.nfs_fh4_val != NULL ) 
     Mem_Free( (char *)data->savedFH.nfs_fh4_val ); 

  if( data->mounted_on_FH.nfs_fh4_val != NULL )
     Mem_Free( (char *)data->mounted_on_FH.nfs_fh4_val ) ;

} /* compound_data_Free */
