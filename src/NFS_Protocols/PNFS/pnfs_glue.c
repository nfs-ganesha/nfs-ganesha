/**
 * \file    pnfs_glue.c
 * \brief   pNFS glue functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

void pnfs_create_file( void ) ;
void pnfs_remove_file( void ) ;
void pnfs_encode_getdeviceinfo( char *buff, unsigned int *plen);
void pnfs_encode_layoutget( void * pds_file, char *buff, unsigned int *plen);
void pnfs_init() ;
void pnfs_terminate();

