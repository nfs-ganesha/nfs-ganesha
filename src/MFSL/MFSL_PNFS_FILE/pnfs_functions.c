/**
 * \file    pnfs_functions.c
 * \brief   pNFS glue functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pnfs.h"

int pnfs_get_location(  pnfs_client_t      * pnfsclient,
                        fsal_handle_t      * phandle, 
                        pnfs_hints_t       * phints,
	                pnfs_fileloc_t     * pnfs_fileloc ) 
{
  return pnfs_ds_get_location( pnfsclient, phandle, &phints->ds_hints,  &pnfs_fileloc->ds_loc ) ;
}

int pnfs_create_file( pnfs_client_t  * pnfsclient,
	              pnfs_fileloc_t * pnfs_fileloc,
		      pnfs_file_t    * pnfs_file ) 
{
  return pnfs_ds_create_file( pnfsclient, &pnfs_fileloc->ds_loc, &pnfs_file->ds_file ) ;
}

int pnfs_lookup_file( pnfs_client_t  * pnfsclient,
	              pnfs_fileloc_t * pnfs_fileloc,
		      pnfs_file_t    * pnfs_file ) 
{
  return pnfs_ds_lookup_file( pnfsclient, &pnfs_fileloc->ds_loc, &pnfs_file->ds_file ) ;
}

int pnfs_remove_file( pnfs_client_t  * pnfsclient,
                      pnfs_file_t    * pnfs_file ) 
{
   return pnfs_ds_unlink_file( pnfsclient, &pnfs_file->ds_file ) ;
}

int pnfs_truncate_file( pnfs_client_t * pnfsclient,
			size_t newsize,
			pnfs_file_t * pnfs_file ) 
{
   return pnfs_ds_truncate_file( pnfsclient, newsize, &pnfs_file->ds_file ) ;
}

int  pnfs_service_getdeviceinfo( char *buffin, unsigned int *plenin, char *buff, unsigned int *plen)
{
   return pnfs_ds_encode_getdeviceinfo( buff, plen) ;
}

int pnfs_service_layoutget( char *buffin, unsigned int *plenin, char *buffout, unsigned int *plenout)
{
   pnfs_ds_file_t * pnfs_ds_file = ( pnfs_ds_file_t *)buffin ;
   return pnfs_ds_encode_layoutget( pnfs_ds_file, buffout, plenout ) ;
}

int pnfs_init(pnfs_client_t * pnfsclient,
              pnfs_layoutfile_parameter_t * pnfs_layout_param)
{
   return pnfs_ds_init( pnfsclient, pnfs_layout_param ) ;
}

void pnfs_terminate()
{
   return ;
}

