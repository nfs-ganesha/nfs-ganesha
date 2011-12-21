/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_dirs.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/29 09:39:04 $
 * \version $Revision: 1.10 $
 * \brief   Directory browsing operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <string.h>
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/xdr.h>
#else
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#endif
#include "nfs4.h"

#include "stuff_alloc.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"

#include "nfs_proto_functions.h"
#include "fsal_nfsv4_macros.h"

/**
 * FSAL_opendir :
 *     Opens a directory for reading its content.
 *     
 * \param dir_handle (input)
 *         the handle of the directory to be opened.
 * \param p_context (input)
 *         Permission context for the operation (user, export context...).
 * \param dir_descriptor (output)
 *         pointer to an allocated structure that will receive
 *         directory stream informations, on successfull completion.
 * \param dir_attributes (optional output)
 *         On successfull completion,the structure pointed
 *         by dir_attributes receives the new directory attributes.
 *         Can be NULL.
 * 
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (user does not have read permission on directory)
 *        - ERR_FSAL_STALE        (dir_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */
fsal_status_t PROXYFSAL_opendir(fsal_handle_t * dir_handle,        /* IN */
                                fsal_op_context_t * p_context,     /* IN */
                                fsal_dir_t * dir_desc,       /* OUT */
                                fsal_attrib_list_t * dir_attributes     /* [ IN/OUT ] */
    )
{
  proxyfsal_dir_t * dir_descriptor = (proxyfsal_dir_t *)dir_desc;

  /* sanity checks
   * note : dir_attributes is optional.
   */
  if(!dir_handle || !p_context || !dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_opendir);

  PRINT_HANDLE("FSAL_opendir", dir_handle);

  /* Set the context */
  memcpy(&dir_descriptor->fhandle, dir_handle, sizeof(dir_descriptor->fhandle));
  memset(dir_descriptor->verifier, 0, NFS4_VERIFIER_SIZE);
  dir_descriptor->pcontext = (proxyfsal_op_context_t *)p_context;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_opendir);

}

/**
 * FSAL_readdir :
 *     Read the entries of an opened directory.
 *     
 * \param dir_descriptor (input):
 *        Pointer to the directory descriptor filled by FSAL_opendir.
 * \param start_position (input):
 *        Cookie that indicates the first object to be read during
 *        this readdir operation.
 *        This should be :
 *        - FSAL_READDIR_FROM_BEGINNING for reading the content
 *          of the directory from the beginning.
 *        - The end_position parameter returned by the previous
 *          call to FSAL_readdir.
 * \param get_attr_mask (input)
 *        Specify the set of attributes to be retrieved for directory entries.
 * \param buffersize (input)
 *        The size (in bytes) of the buffer where
 *        the direntries are to be stored.
 * \param pdirent (output)
 *        Adresse of the buffer where the direntries are to be stored.
 * \param end_position (output)
 *        Cookie that indicates the current position in the directory.
 * \param nb_entries (output)
 *        Pointer to the number of entries read during the call.
 * \param end_of_dir (output)
 *        Pointer to a boolean that indicates if the end of dir
 *        has been reached during the call.
 * 
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument) 
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */

fsal_status_t PROXYFSAL_readdir(fsal_dir_t * dir_desc,       /* IN */
                                fsal_cookie_t start_pos,      /* IN */
                                fsal_attrib_mask_t get_attr_mask,       /* IN */
                                fsal_mdsize_t buffersize,       /* IN */
                                fsal_dirent_t * pdirent,        /* OUT */
                                fsal_cookie_t * end_position,      /* OUT */
                                fsal_count_t * nb_entries,      /* OUT */
                                fsal_boolean_t * end_of_dir     /* OUT */
    )
{
  nfs_fh4 nfs4fh;
  bitmap4 bitmap;
  uint32_t bitmap_val[2];
  count4 nbreaddir;
  int rc = 0;
  int i = 0;
  int cpt = 0;
  entry4 *piter_entry = NULL;
  entry4 tabentry4[FSAL_READDIR_SIZE];
  //char tabentry4name[FSAL_READDIR_SIZE][MAXNAMLEN];
  char * tabentry4name = NULL ;
  uint32_t tabentry4bitmap[FSAL_READDIR_SIZE][2];
  struct timeval timeout = TIMEOUTRPC;
  //fsal_proxy_internal_fattr_readdir_t tabentry4attr[FSAL_READDIR_SIZE];
  fsal_proxy_internal_fattr_readdir_t * tabentry4attr = NULL ;
#define FSAL_READDIR_NB_OP_ALLOC 2
  nfs_argop4 argoparray[FSAL_READDIR_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_READDIR_NB_OP_ALLOC];
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  proxyfsal_dir_t * dir_descriptor = (proxyfsal_dir_t *)dir_desc;
  proxyfsal_cookie_t start_position;

  /* sanity checks */

  if(!dir_descriptor || !pdirent || !end_position || !nb_entries || !end_of_dir)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  if( ( tabentry4name = Mem_Alloc( FSAL_READDIR_SIZE * MAXNAMLEN ) ) == NULL )
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);

  if( ( tabentry4attr =
	 (fsal_proxy_internal_fattr_readdir_t *)Mem_Alloc( sizeof( fsal_proxy_internal_fattr_readdir_t ) * FSAL_READDIR_SIZE ) ) == NULL )
   {
     Mem_Free( tabentry4name ) ;
     Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);
   }

  memcpy( (char *)&start_position.data, start_pos.data, sizeof( nfs_cookie4 ) ) ;

  LogFullDebug(COMPONENT_FSAL, "---> Readdir Offset=%llu sizeof(entry4)=%lu sizeof(fsal_dirent_t)=%lu \n",
               (unsigned long long)start_position.data, sizeof(entry4), sizeof(fsal_dirent_t));

  /* >> retrieve root handle filehandle here << */
  bitmap.bitmap4_len = 2;
  bitmap.bitmap4_val = bitmap_val;
  fsal_internal_proxy_create_fattr_readdir_bitmap(&bitmap);

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Readdir" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

#define FSAL_READDIR_IDX_OP_PUTFH      0
#define FSAL_READDIR_IDX_OP_READDIR    1

  /* Set up reply structure */
  resnfs4.resarray.resarray_val = resoparray;

  /* How much data should I read ? */
  nbreaddir = buffersize / (sizeof(fsal_dirent_t));
  if(nbreaddir > FSAL_READDIR_SIZE)
    nbreaddir = FSAL_READDIR_SIZE;

  memset((char *)tabentry4, 0, FSAL_READDIR_SIZE * sizeof(entry4));
  memset((char *)tabentry4name, 0, FSAL_READDIR_SIZE * MAXNAMLEN);
  memset((char *)tabentry4attr, 0,
         FSAL_READDIR_SIZE * sizeof(fsal_proxy_internal_fattr_readdir_t));
  for(i = 0; i < nbreaddir; i++)
    {
      fsal_internal_proxy_setup_readdir_fattr(&tabentry4attr[i]);

      tabentry4[i].name.utf8string_val = (char *)(tabentry4name+i*MAXNAMLEN*sizeof(char) ) ;
      tabentry4[i].name.utf8string_len = MAXNAMLEN;

      tabentry4[i].attrs.attr_vals.attrlist4_val = (char *)&(tabentry4attr[i]);
      tabentry4[i].attrs.attr_vals.attrlist4_len =
          sizeof(fsal_proxy_internal_fattr_readdir_t);

      tabentry4[i].attrs.attrmask.bitmap4_val = tabentry4bitmap[i];
      tabentry4[i].attrs.attrmask.bitmap4_len = 2;
    }
  resnfs4.resarray.resarray_val[FSAL_READDIR_IDX_OP_READDIR].nfs_resop4_u.opreaddir.
      READDIR4res_u.resok4.reply.entries = (entry4 *) tabentry4;

  /* >> Call your filesystem lookup function here << */
  if(fsal_internal_proxy_extract_fh(&nfs4fh, (fsal_handle_t *) &dir_descriptor->fhandle) == FALSE)
   {
    Mem_Free( tabentry4attr ) ;
    Mem_Free( tabentry4name ) ;
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);
   }
  /** @todo : use NFS4_OP_VERIFY to implement a cache validator, BUGAZOMEU */
  COMPOUNDV4_ARG_ADD_OP_PUTFH(argnfs4, nfs4fh);
  COMPOUNDV4_ARG_ADD_OP_READDIR(argnfs4, start_position.data, nbreaddir,
                                dir_descriptor->verifier, bitmap);

  TakeTokenFSCall();
  /* Call the NFSv4 function */
  COMPOUNDV4_EXECUTE(dir_descriptor->pcontext, argnfs4, resnfs4, rc);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      Mem_Free( tabentry4attr ) ;
      Mem_Free( tabentry4name ) ;
      Return(ERR_FSAL_IO, rc, INDEX_FSAL_readdir);
    }
  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status, INDEX_FSAL_readdir);

  /* Set the reply structure */
  if(resnfs4.resarray.resarray_val[FSAL_READDIR_IDX_OP_READDIR].nfs_resop4_u.opreaddir.
     READDIR4res_u.resok4.reply.eof == TRUE)
    {
      *end_of_dir = TRUE;
    }

  /* >> convert error code and return on error << */

  /* >> fill the output dirent array << */

  /* until the requested count is reached
   * or the end of dir is reached...
   */

  /* Don't forget setting output vars : end_position, nb_entries, end_of_dir  */
  for(piter_entry =
      resnfs4.resarray.resarray_val[FSAL_READDIR_IDX_OP_READDIR].nfs_resop4_u.opreaddir.
      READDIR4res_u.resok4.reply.entries; piter_entry != NULL;
      piter_entry = piter_entry->nextentry)
    {
      if(proxy_Fattr_To_FSAL_attr(&pdirent[cpt].attributes,
                                  (proxyfsal_handle_t *) &(pdirent[cpt].handle), &(piter_entry->attrs)) != 1)
        {
          FSAL_CLEAR_MASK(pdirent[cpt].attributes.asked_attributes);
          FSAL_SET_MASK(pdirent[cpt].attributes.asked_attributes, FSAL_ATTR_RDATTR_ERR);

          Mem_Free( tabentry4attr ) ;
          Mem_Free( tabentry4name ) ;
          Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);
        }

      if(fsal_internal_proxy_fsal_utf8_2_name(&(pdirent[cpt].name),
                                              &(piter_entry->name)) == FALSE)
        {
          Mem_Free( tabentry4attr ) ;
          Mem_Free( tabentry4name ) ;
          Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readdir);
        }

      /* Link the entries together */
      pdirent[cpt].nextentry = NULL;    /* Will be overwritten if there is an entry behind, stay NULL if not */
      if(cpt != 0)
        pdirent[cpt - 1].nextentry = &pdirent[cpt];

      /* Set end position */
      ((proxyfsal_cookie_t *)end_position)->data = piter_entry->cookie;

      /* Get ready for next pdirent */
      cpt += 1;

      if(cpt >= FSAL_READDIR_SIZE)
        break;
    }

  /* The number of entries to be returned */
  *nb_entries = cpt;

  Mem_Free( tabentry4attr ) ;
  Mem_Free( tabentry4name ) ;
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readdir);

}

/**
 * FSAL_closedir :
 * Free the resources allocated for reading directory entries.
 *     
 * \param dir_descriptor (input):
 *        Pointer to a directory descriptor filled by FSAL_opendir.
 * 
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_IO, ...
 */
fsal_status_t PROXYFSAL_closedir(fsal_dir_t * dir_descriptor       /* IN */
    )
{

  /* sanity checks */
  if(!dir_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_closedir);

  ((proxyfsal_dir_t *)dir_descriptor)->pcontext = NULL;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_closedir);

}
