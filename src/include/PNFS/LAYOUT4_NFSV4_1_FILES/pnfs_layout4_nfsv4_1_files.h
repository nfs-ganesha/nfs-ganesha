/*
 *
 *
 * Copyright CEA/DAM/DIF  (2010)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
 * ---------------------------------------
 */

#ifndef _PNFS_LAYOUT4_NFSV4_1_FILES_H
#define _PNFS_LAYOUT4_NFSV4_1_FILES_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#endif

#include "RW_Lock.h"
#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "fsal_types.h"
#ifdef _USE_MFSL
#include "mfsl.h"
#endif
#include "log_functions.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"

#define NB_MAX_PNFS_DS 2
#define PNFS_NFS4      4
#define PNFS_SENDSIZE 32768
#define PNFS_RECVSIZE 32768

#define PNFS_LAYOUTFILE_FILEHANDLE_MAX_LEN 128
#define PNFS_LAYOUTFILE_PADDING_LEN  NFS4_OPAQUE_LIMIT
#define PNFS_LAYOUTFILE_OWNER_LEN 128

typedef struct pnfs_ds_parameter__
{
  unsigned int ipaddr;
  unsigned short ipport;
  unsigned int prognum;
  char rootpath[MAXPATHLEN];
  char ipaddr_ascii[MAXNAMLEN];
  unsigned int id;
  bool_t is_ganesha;
} pnfs_ds_parameter_t;

typedef struct pnfs_layoutfile_parameter__
{
  unsigned int stripe_size;
  unsigned int stripe_width;
  pnfs_ds_parameter_t ds_param[NB_MAX_PNFS_DS];
} pnfs_layoutfile_parameter_t;

typedef struct pnfs_client__
{
  sessionid4 session;
  sequenceid4 sequence;
  nfs_fh4 ds_rootfh[NB_MAX_PNFS_DS];
  CLIENT *rpc_client;
} pnfs_client_t;

typedef struct pnfs_ds_file__
{
  bool_t allocated;
  unsigned int deviceid;
  bool_t is_ganesha;
  nfs_fh4 handle;
  stateid4 stateid;
} pnfs_ds_file_t;

/* Mandatory functions */
int pnfs_init(pnfs_client_t * pnfsclient,
              pnfs_layoutfile_parameter_t * pnfs_layout_param);

int pnfs_create_ds_file(pnfs_client_t * pnfsclient,
                        fattr4_fileid fileid, pnfs_ds_file_t * pfile);

int pnfs_lookup_ds_file(pnfs_client_t * pnfsclient,
                        fattr4_fileid fileid, pnfs_ds_file_t * pfile);

int pnfs_unlink_ds_file(pnfs_client_t * pnfsclient,
                        fattr4_fileid fileid, pnfs_ds_file_t * pfile);

void pnfs_encode_getdeviceinfo(char *buff, unsigned int *plen);
void pnfs_encode_layoutget(pnfs_ds_file_t * pds_file, char *buff, unsigned int *plen);

/* Internal functions */
int pnfs_connect(pnfs_client_t * pnfsclient,
                 pnfs_layoutfile_parameter_t * pnfs_layout_param);

int pnfs_do_mount(pnfs_client_t * pnfsclient, pnfs_ds_parameter_t * pds_param);

int pnfs_lookupPath(pnfs_client_t * pnfsclient, char *p_path, nfs_fh4 * object_handle);

#endif                          /* _PNFS_LAYOUT4_NFSV4_1_FILES_H */
