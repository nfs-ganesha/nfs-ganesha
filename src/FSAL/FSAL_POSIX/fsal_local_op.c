/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */


/**
 *
 * \file    fsal_local_check.c
 * \author  $Author: deniel $
 * \date    $Date: 2008/03/13 14:20:07 $
 * \version $Revision: 1.0 $
 * \brief   Check for FSAL authentication locally
 *
 */

/*
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"


/**
 * FSAL_test_access :
 * Tests whether the user or entity identified by its cred
 * can access the object as indicated by the access_type parameter.
 * This function tests access rights using cached attributes
 * given as parameter.
 * Thus, it cannot test FSAL_F_OK flag, and asking such a flag
 * will result in a ERR_FSAL_INVAL error.
 *
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param access_type (input):
 *        Indicates the permissions to test.
 *        This is an inclusive OR of the permissions
 *        to be checked for the user identified by cred.
 *        Permissions constants are :
 *        - FSAL_R_OK : test for read permission
 *        - FSAL_W_OK : test for write permission
 *        - FSAL_X_OK : test for exec permission
 *        - FSAL_F_OK : test for file existence
 * \param object_attributes (mandatory input):
 *        The cached attributes for the object to test rights on.
 *        The following attributes MUST be filled :
 *        owner, group, mode, ACLs.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t FSAL_test_access(
    fsal_op_context_t          * p_context,          /* IN */
    fsal_accessflags_t         access_type,          /* IN */
    fsal_attrib_list_t         * p_object_attributes   /* IN */
)
{
  fsal_status_t status;
  status = fsal_internal_testAccess(p_context, access_type, NULL, p_object_attributes);
  Return(status.major, status.minor, INDEX_FSAL_test_access);
}


/**
 * FSAL_test_setattr_access :
 * test if a client identified by cred can access setattr on the object
 * knowing its attributes and parent's attributes.
 * The following fields of the object_attributes structures MUST be filled :
 * acls (if supported), mode, owner, group.
 * This doesn't make any call to the filesystem,
 * as a result, this doesn't ensure that the file exists, nor that
 * the permissions given as parameters are the actual file permissions :
 * this must be ensured by the cache_inode layer, using FSAL_getattrs,
 * for example.
 *
 * \param cred (in fsal_cred_t *) user's identifier.
 * \param candidate_attrbutes the attributes we want to set on the object
 * \param object_attributes (in fsal_attrib_list_t *) the cached attributes
 *        for the object.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t FSAL_setattr_access(
    fsal_op_context_t          * p_context,             /* IN */
    fsal_attrib_list_t         * candidate_attributes,  /* IN */
    fsal_attrib_list_t         * object_attributes      /* IN */
)
{
  Return( ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_setattr_access );
} /* FSAL_test_setattr_access */

/**
 * FSAL_rename_access :
 * test if a client identified by cred can be renamed on the object
 * knowing the parents attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattrsrc      source directory attributes
 * \param pattrdest     destination directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */

fsal_status_t FSAL_rename_access(
        fsal_op_context_t  * pcontext,             /* IN */
        fsal_attrib_list_t * pattrsrc,             /* IN */
        fsal_attrib_list_t * pattrdest )           /* IN */
{
  fsal_status_t fsal_status ;

  fsal_status =  FSAL_test_access( pcontext, FSAL_W_OK, pattrsrc ) ;
  if( FSAL_IS_ERROR( fsal_status ) ) 
    Return( fsal_status.major, fsal_status.minor, INDEX_FSAL_rename_access ) ;

  fsal_status =  FSAL_test_access( pcontext, FSAL_W_OK, pattrdest ) ;
  if( FSAL_IS_ERROR( fsal_status ) ) 
    Return( fsal_status.major, fsal_status.minor, INDEX_FSAL_rename_access ) ;

 /* If this point is reached, then access is granted */
  Return( ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_rename_access );
} /* FSAL_rename_access */

/**
 * FSAL_create_access :
 * test if a client identified by cred can create an object within a directory knowing its attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattr      source directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t FSAL_create_access(
        fsal_op_context_t  * pcontext,             /* IN */
        fsal_attrib_list_t * pattr )           /* IN */
{
  fsal_status_t fsal_status ;

  fsal_status =  FSAL_test_access( pcontext, FSAL_W_OK, pattr ) ;
  if( FSAL_IS_ERROR( fsal_status ) )
    Return( fsal_status.major, fsal_status.minor, INDEX_FSAL_create_access ) ;

 /* If this point is reached, then access is granted */
  Return( ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_create_access );
} /* FSAL_create_access */


/**
 * FSAL_unlink_access :
 * test if a client identified by cred can unlink on a directory knowing its attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattr      source directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t FSAL_unlink_access(
        fsal_op_context_t  * pcontext,             /* IN */
        fsal_attrib_list_t * pattr )           /* IN */
{
  fsal_status_t fsal_status ;

  fsal_status =  FSAL_test_access( pcontext, FSAL_W_OK, pattr ) ;
  if( FSAL_IS_ERROR( fsal_status ) )
    Return( fsal_status.major, fsal_status.minor, INDEX_FSAL_unlink_access ) ;

 /* If this point is reached, then access is granted */
  Return( ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlink_access );

} /* FSAL_unlink_access */

/**
 * FSAL_link_access :
 * test if a client identified by cred can link to a directory knowing its attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattr      destination directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */

fsal_status_t FSAL_link_access(
        fsal_op_context_t  * pcontext,             /* IN */
        fsal_attrib_list_t * pattr )           /* IN */
{
  fsal_status_t fsal_status ;

  fsal_status =  FSAL_test_access( pcontext, FSAL_W_OK, pattr ) ;
  if( FSAL_IS_ERROR( fsal_status ) )
    Return( fsal_status.major, fsal_status.minor, INDEX_FSAL_unlink_access ) ;

 /* If this point is reached, then access is granted */
  Return( ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_link_access );
} /* FSAL_link_access */


/**
 * FSAL_merge_attrs: merge to attributes structure.
 *
 * This functions merge the second attributes list into the first argument. 
 * Results in returned in the last argument.
 *
 * @param pinit_attr   [IN] attributes to be changed
 * @param pnew_attr    [IN] attributes to be added
 * @param presult_attr [IN] resulting attributes
 * 
 * @return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_INVAL        Invalid argument(s)
 */

fsal_status_t FSAL_merge_attrs( fsal_attrib_list_t * pinit_attr, 
                                fsal_attrib_list_t * pnew_attr, 
                                fsal_attrib_list_t * presult_attr ) 
{
  if( pinit_attr == NULL || pnew_attr == NULL || presult_attr == NULL )
      Return( ERR_FSAL_INVAL, 0, INDEX_FSAL_merge_attrs );

  /* The basis for the result attr is the fist argument */
  *presult_attr = *pinit_attr ;

  /* Now deal with the attributes to be merged in this set of attributes */
  if( FSAL_TEST_MASK( pnew_attr->asked_attributes, FSAL_ATTR_MODE ) )
     presult_attr->mode = pnew_attr->mode ;

  if( FSAL_TEST_MASK( pnew_attr->asked_attributes, FSAL_ATTR_OWNER ) ) 
      presult_attr->owner = pnew_attr->owner ;

  if( FSAL_TEST_MASK( pnew_attr->asked_attributes, FSAL_ATTR_GROUP ) ) 
      presult_attr->group = pnew_attr->group ;

  if( FSAL_TEST_MASK( pnew_attr->asked_attributes, FSAL_ATTR_SIZE) )
      presult_attr->filesize = pnew_attr->filesize ;

  if( FSAL_TEST_MASK( pnew_attr->asked_attributes, FSAL_ATTR_SPACEUSED ) )
      presult_attr->spaceused = pnew_attr->spaceused ; 

  if( FSAL_TEST_MASK( pnew_attr->asked_attributes, FSAL_ATTR_ATIME ) )
    {
        presult_attr->atime.seconds  = pnew_attr->atime.seconds ;
        presult_attr->atime.nseconds = pnew_attr->atime.nseconds ;
    }
  
  if( FSAL_TEST_MASK( pnew_attr->asked_attributes, FSAL_ATTR_MTIME ) )
    {
        presult_attr->mtime.seconds  = pnew_attr->mtime.seconds ;
        presult_attr->mtime.nseconds = pnew_attr->mtime.nseconds ;
    }

  /* Do not forget the ctime */
  FSAL_SET_MASK( presult_attr->asked_attributes, FSAL_ATTR_CTIME ) ;
  presult_attr->ctime.seconds  = pnew_attr->ctime.seconds ;
  presult_attr->ctime.nseconds = pnew_attr->ctime.nseconds ;

  /* Regular exit */
  Return( ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_merge_attrs );
} /* FSAL_merge_attrs */

