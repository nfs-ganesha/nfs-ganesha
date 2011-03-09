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
#else

void pnfs_encode_getdeviceinfo( char *buff, unsigned int *plen)
{
   return pnfs_lustre_encode_getdeviceinfo( buff, plen) ;
}

void pnfs_encode_layoutget( char *buffin, unsigned int *plenin, char *buffout, unsigned int *plenout)
{
   return pnfs_lustre_encode_layoutget(  buffin, plenin, buffout, plenout ) ;
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

