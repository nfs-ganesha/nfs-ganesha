/**
 * \file    pnfs_glue.c
 * \brief   pNFS glue functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pnfs.h"

#ifdef _USE_PNFS_SPNFS_LIKE
#endif

#ifdef _USE_PNFS_PARALLEL_FS

int pnfs_service_getdevicelist( char * buffin, unsigned int * plenin, char * buff, unsigned int * plen ) 
{
   return NFS4_OK ;
}

int pnfs_service_getdeviceinfo( char * buffin, unsigned int * plenin, char *buff, unsigned int *plen)
{
   return pnfs_lustre_encode_getdeviceinfo( buff, plen) ;
}

int pnfs_service_layoutget( char *buffin, unsigned int *plenin, char *buffout, unsigned int *plenout)
{
   return pnfs_lustre_encode_layoutget(  buffin, plenin, buffout, plenout ) ;
}

int pnfs_service_layoutcommit( char * buffin, unsigned int * plenin, char * buff, unsigned int * plen ) 
{
   return NFS4_OK ;
}

int pnfs_service_layoutreturn( char * buffin, unsigned int * plenin, char * buff, unsigned int * plen ) 
{
   return NFS4_OK ;
}

int pnfs_nit(pnfs_client_t * pnfsclient,
              pnfs_layoutfile_parameter_t * pnfs_layout_param)
{
   return NFS4_OK;
}

void pnfs_terminate()
{
   return ;
}
 
#endif

