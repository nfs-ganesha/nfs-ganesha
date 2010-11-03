/**
 * \file    pnfs_glue.c
 * \brief   pNFS glue functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pnfs.h"

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
                      pnfs_file_t    * pfile ) 
{
   return pnfs_ds_unlink_file( pnfsclient, &pfile->ds_file ) ;
}

void pnfs_encode_getdeviceinfo( char *buff, unsigned int *plen)
{
   return  pnfs_ds_encode_getdeviceinfo( buff, plen) ;
}

void pnfs_encode_layoutget( void * pds_file, char *buff, unsigned int *plen)
{
   return pnfs_ds_encode_layoutget( pds_file, buff, plen ) ;
}

int pnfs_init(pnfs_client_t * pnfsclient,
              pnfs_layoutfile_parameter_t * pnfs_layout_param)
{
   return pnfs_ds_init( pnfsclient, pnfs_layout_param ) ;
}

void pnfs_terminate();

