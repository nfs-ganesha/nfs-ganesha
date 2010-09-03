/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    pnfs_init.c
 * \brief   Initialization functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <string.h>
#include <signal.h>

#include "stuff_alloc.h"

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#else
#include <rpc/rpc.h>
#endif

#include "log_macros.h"

#include "PNFS/LAYOUT4_NFSV4_1_FILES/pnfs_layout4_nfsv4_1_files.h"

/**
 *
 * pnfs_init: Inits a pnfs client structure.
 *
 * Inits a pnfs client structure to a DS.
 *
 * @param pnfsclient        [INOUT] pointer to the pnfsclient structure (client to the ds).
 * @param pnfs_layout_param [IN]    pointer to pnfs layoutfile configuration
 *
 * @return 0 if successful
 * @return -1 if one of its argument is NULL, exists if failed
 *
 */
int pnfs_init(pnfs_client_t * pnfsclient, pnfs_layoutfile_parameter_t * pnfs_layout_param)
{
  unsigned int i;

  if(!pnfsclient || !pnfs_layout_param)
    return -1;

  for(i = 0; i < pnfs_layout_param->stripe_width; i++)
    {
      if(pnfs_connect(&pnfsclient->ds_client[i], &pnfs_layout_param->ds_param[i]))
        {
          /* Failed init */
          LogMajor(COMPONENT_PNFS,"PNFS INIT: pNFS engine could not be initialized, exiting...");
          exit(1);
        }
      LogDebug(COMPONENT_PNFS, "PNFS INIT: pNFS engine successfully initialized");

      if(pnfs_do_mount(&pnfsclient->ds_client[i], &pnfs_layout_param->ds_param[i]))
        {
          /* Failed init */
          LogMajor(COMPONENT_PNFS,"PNFS INIT: pNFS engine could not initialized session, exiting...");
          exit(1);
        }
      LogDebug(COMPONENT_PNFS, "PNFS INIT: pNFS session successfully initialized");

      /* Lookup to find the DS's root FH */
      pnfsclient->ds_client[i].ds_rootfh.nfs_fh4_val =
          (char *)Mem_Alloc(PNFS_LAYOUTFILE_FILEHANDLE_MAX_LEN);

      if(pnfs_lookupPath
         (&pnfsclient->ds_client[i], pnfs_layout_param->ds_param[i].rootpath,
          &pnfsclient->ds_client[i].ds_rootfh))
        {
          /* Failed init */
          LogMajor(COMPONENT_PNFS,"PNFS INIT: pNFS engine could not look up %s on DS=%s",
                     pnfs_layout_param->ds_param[i].rootpath,
                     pnfs_layout_param->ds_param[i].ipaddr_ascii);
          exit(1);
        }
      LogDebug(COMPONENT_PNFS,
                      "PNFS INIT: pNFS engine successfully got DS's rootFH for %s",
                      pnfs_layout_param->ds_param[i].ipaddr_ascii);

    }                           /* for */

  /* Set nb_ds field */
  pnfsclient->nb_ds = pnfs_layout_param->stripe_width;

  return 0;
}                               /* pnfs_init */
