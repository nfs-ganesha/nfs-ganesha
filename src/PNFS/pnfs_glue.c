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

nfsstat4 pnfs_service_getdevicelist( char * buffin, unsigned int * plenin, char * buff, unsigned int * plen ) 
{
   return NFS4_OK ;
}

nfsstat4 pnfs_service_getdeviceinfo( char * buffin, unsigned int * plenin, char *buff, unsigned int *plen)
{
   return pnfs_lustre_getdeviceinfo( buff, plen) ;
}

nfsstat4 pnfs_service_layoutget( char *buffin, unsigned int *plenin, char *buffout, unsigned int *plenout)
{
   return pnfs_lustre_layoutget(  buffin, plenin, buffout, plenout ) ;
}

nfsstat4 pnfs_service_layoutcommit( char * buffin, unsigned int * plenin, char * buff, unsigned int * plen ) 
{
   return NFS4_OK ;
}

nfsstat4 pnfs_service_layoutreturn( char * buffin, unsigned int * plenin, char * buff, unsigned int * plen ) 
{
   return NFS4_OK ;
}

nfsstat4 pnfs_nit(pnfs_client_t * pnfsclient,
              pnfs_layoutfile_parameter_t * pnfs_layout_param)
{
   return NFS4_OK;
}

nfsstat4 pnfs_terminate()
{
   return ;
}

/* Forthcoming functions */

nfsstat4 pnfs_getdevicelist( GETDEVICELIST4args * pargs, GETDEVICELIST4res * pres ) 
{
   return NFS4_OK ;
}

nfsstat4 pnfs_getdeviceinfo( GETDEVICEINFO4args * pargs, GETDEVICEINFO4res * pres ) 
{
   return NFS4_OK ;
}

nfsstat4 pnfs_layoutget( nfs_fh4 * pnfsfh4, LAYOUTGET4args * pargs, LAYOUTGET4res * pres ) 
{
   return __pnfs_lustre_layoutget(  pnfsfh4, pargs, pres ) ;
}

nfsstat4 pnfs_layoutcommit( nfs_fh4 * pnfsfh4, LAYOUTCOMMIT4args * pargs, LAYOUTCOMMIT4res * pres ) 
{
   return NFS4_OK ;
}

nfsstat4 pnfs_layoutreturn( LAYOUTRETURN4args * pargs, LAYOUTRETURN4res * pres ) 
{
   return NFS4_OK ;
}

 
#endif

