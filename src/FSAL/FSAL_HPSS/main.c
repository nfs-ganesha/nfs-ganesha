/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF (2013)
 * contributeur : Dominique MARTINET dominique.martinet@cea.fr
 *                Philippe DENIEL    philippe.deniel@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */

/* main.c
 * Module core functions
 */

#include "config.h"

#include "fsal.h"
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "hpss_methods.h"
#include "FSAL/fsal_init.h"
#include "nfs_exports.h"

#include <hpss_Getenv.h>

/* HPSS FSAL module private storage
 */

struct hpss_fsal_module {	
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	fsal_init_info_t fsal_info;
        hpssfs_specific_initinfo_t specific_info;
};

const char myname[] = "HPSS";
/* filesystem info for HPSS */
static struct fsal_staticfsinfo_t default_hpss_info = {
  0xFFFFFFFFFFFFFFFFLL,         /* max file size (64bits) */
  NS_MAX_HARD_LINK_VALUE,       /* max links */
  MAXNAMLEN,                    /* max filename */
  MAXPATHLEN,                   /* min filename */
  true,                         /* no_trunc */
  true,                         /* chown restricted */
  false,                        /* case insensitivity */
  true,                         /* case preserving */
  true,                         /* hard link support */
  true,                         /* symlink support */
  false,                        /* lock management */
  false,                        /* lock owners */
  false,                        /* async blocking locks */
  true,                         /* named attributes are supported */
  true,                         /* handles are unique and persistent */
  {10, 0},                      /* Duration of lease at FS in seconds */
  FSAL_ACLSUPPORT_ALLOW,        /* ACL support */
  true,                         /* can change times */
  true,                         /* homogenous */
  HPSS_SUPPORTED_ATTRIBUTES,    /* supported attributes */
  (1024 * 1024),                /* maxread size */
  (1024 * 1024),                /* maxwrite size */
  0,                            /* default umask */
  0,                            /* don't allow cross fileset export path */
  0400,                         /* default access rights for xattrs: root=RW, owner=R */
  0,                            /* default access check support in FSAL */
  0,                            /* default share reservation support in FSAL */
  0                             /* default share reservation support with open owners in FSAL */
};

/* private helper for export object
 */

#define SET_INIT_INT(init_val, key, val, rc, end) do {                                       \
  errno = 0;                                                                                 \
  rc = strtol(val, &end, 10);                                                                \
  if (end == val || *end != '\0' || errno != 0) {                                            \
      LogCrit(COMPONENT_FSAL,"FSAL LOAD PARAMETER: ERROR: Unexpected value \"%s\" for %s.",  \
              val, key);                                                                     \
      return 1;                                                                              \
  }                                                                                          \
  init_val = rc;                                                                             \
} while(0)

static int
hpss_key_to_param(const char *key, const char *val,
                  fsal_init_info_t *info, const char *name)
{
  int rc;
  char *end;

  struct hpss_fsal_module *fsal_module = container_of(info, struct hpss_fsal_module, fsal_info);
  hpssfs_specific_initinfo_t *init_info = &fsal_module->specific_info;

  if(!strcasecmp(key, "Principal")) {
    strncpy(init_info->Principal, val, MAXNAMLEN);
  } else if(!strcasecmp(key, "KeytabPath")) {
    strncpy(init_info->KeytabPath, val, MAXPATHLEN);
  } else if(!strcasecmp(key, "CredentialLifetime")) {
    SET_INIT_INT(init_info->CredentialLifetime, key, val, rc, end);
  } else if(!strcasecmp(key, "ReturnInconsistentDirent")) {
    if(!strcasecmp(val, "TRUE"))
      init_info->ReturnInconsistentDirent = 1;
  } else if(!strcasecmp(key, "AuthnMech")) {
    rc = hpss_AuthnMechTypeFromString(val, &init_info->hpss_config.AuthnMech);
    if( rc != HPSS_E_NOERROR )
      {
        LogCrit(COMPONENT_FSAL,"FSAL LOAD PARAMETER: ERROR: Unexpected value \"%s\" for %s.",
                val, key);
        return 1;
      }
    init_info->behaviors.AuthnMech = 1;
  } else if(!strcasecmp(key, "NumRetries")) {
    SET_INIT_INT(init_info->hpss_config.NumRetries, key, val, rc, end);
    init_info->behaviors.NumRetries = 1;
  } else if(!strcasecmp(key, "BusyDelay")) {
    SET_INIT_INT(init_info->hpss_config.BusyDelay, key, val, rc, end);
    init_info->behaviors.BusyDelay = 1;
  } else if(!strcasecmp(key, "BusyRetries")) {
    SET_INIT_INT(init_info->hpss_config.BusyRetries, key, val, rc, end);
    init_info->behaviors.BusyRetries = 1;
  } else if(!strcasecmp(key, "MaxConnections")) {
    SET_INIT_INT(init_info->hpss_config.MaxConnections, key, val, rc, end);
    init_info->behaviors.MaxConnections = 1;
  } else if(!strcasecmp(key, "DebugPath")) {
    strncpy(init_info->hpss_config.DebugPath, val, HPSS_MAX_PATH_NAME);
    init_info->behaviors.DebugPath = 1;
  } else if(!strcasecmp(key, "default_cos")) {
    SET_INIT_INT(init_info->default_cos, key, val, rc, end);
  } else if(!strcasecmp(key, "filesetname")) {
    strncpy(init_info->filesetname, val, MAXPATHLEN);
  } else {
    LogCrit(COMPONENT_CONFIG,
            "Unknown key: %s in %s (value: %s)", key, name, val);
    return 1;
  }

  return 0;
}


struct fsal_staticfsinfo_t *hpss_staticinfo(struct fsal_module *hdl)
{
  struct hpss_fsal_module *myself;

  myself = container_of(hdl, struct hpss_fsal_module, fsal);
  return &myself->fs_info;
}

hpssfs_specific_initinfo_t *hpss_specific_initinfo(struct fsal_module *hdl)
{
  struct hpss_fsal_module *myself;

  myself = container_of(hdl, struct hpss_fsal_module, fsal);
  return &myself->specific_info;
}


/** Initializes security context */
static int HPSSFSAL_SecInit(struct fsal_module *fsal_hdl)
{
  int rc;
  hpss_rpc_auth_type_t auth_type;
  hpssfs_specific_initinfo_t *specific_info;

  if( !fsal_hdl )
    return ERR_FSAL_FAULT;

  specific_info = hpss_specific_initinfo(fsal_hdl);


  rc = hpss_GetAuthType(specific_info->hpss_config.AuthnMech, &auth_type);
  if(rc != 0)
    return rc;

  if(auth_type != hpss_rpc_auth_type_keytab && auth_type != hpss_rpc_auth_type_keyfile)
    {
      return ERR_FSAL_INVAL;
    }

  rc = hpss_SetLoginCred(specific_info->Principal,
                         specific_info->hpss_config.AuthnMech,
                         hpss_rpc_cred_client, auth_type, (void *)specific_info->KeytabPath);

  LogDebug(COMPONENT_FSAL, "FSAL SEC INIT: Auth Mech is set to '%s'",
                    hpss_AuthnMechTypeString(specific_info->hpss_config.AuthnMech));
  LogDebug(COMPONENT_FSAL, "FSAL SEC INIT: Auth Type is set to '%s'",
                    hpss_AuthenticatorTypeString(auth_type));
  LogDebug(COMPONENT_FSAL, "FSAL SEC INIT: Principal is set to '%s'",
                    specific_info->Principal);
  LogDebug(COMPONENT_FSAL, "FSAL SEC INIT: Keytab is set to '%s'",
                    specific_info->KeytabPath);

  return rc;

}

static int HPSSFSAL_Init_Internals(struct fsal_module *fsal_hdl)
{
  int rc;
  api_config_t hpss_config;
  hpssfs_specific_initinfo_t *specific_info;

  if(!fsal_hdl)
    return ERR_FSAL_FAULT;

  specific_info = hpss_specific_initinfo(fsal_hdl);

  /* First, get current values for config. */

  if((rc = hpss_GetConfiguration(&hpss_config)))
    return rc;

#define API_DEBUG_ERROR    (1)
#define API_DEBUG_REQUEST  (2)
#define API_DEBUG_TRACE    (4)

  hpss_config.Flags |= API_USE_CONFIG;

  /* Change values if they were set in configuration */
  if(specific_info->behaviors.AuthnMech != 0)
    hpss_config.AuthnMech = specific_info->hpss_config.AuthnMech;
  else
    specific_info->hpss_config.AuthnMech = hpss_config.AuthnMech;

  if(specific_info->behaviors.NumRetries != 0)
    hpss_config.NumRetries = specific_info->hpss_config.NumRetries;

  if(specific_info->behaviors.BusyRetries != 0)
    hpss_config.BusyRetries = specific_info->hpss_config.BusyRetries;

  if(specific_info->behaviors.BusyDelay != 0)
    hpss_config.BusyDelay = specific_info->hpss_config.BusyDelay;

  if(specific_info->behaviors.BusyRetries != 0)
    hpss_config.BusyRetries = specific_info->hpss_config.BusyRetries;

  if(specific_info->behaviors.MaxConnections != 0)
    hpss_config.MaxConnections = specific_info->hpss_config.MaxConnections;

  if(specific_info->behaviors.DebugPath != 0)
    {
      strcpy(hpss_config.DebugPath, specific_info->hpss_config.DebugPath);
      hpss_config.DebugValue |= API_DEBUG_ERROR | API_DEBUG_REQUEST | API_DEBUG_TRACE;
      hpss_config.Flags |= API_ENABLE_LOGGING;
    }

  strcpy(hpss_config.DescName, "hpss.ganesha.nfsd");

  /* Display Clapi configuration */
  LogDebug(COMPONENT_FSAL, "HPSS Client API configuration:");
  LogDebug(COMPONENT_FSAL, "  Flags: %08X", hpss_config.Flags);
  LogDebug(COMPONENT_FSAL, "  TransferType: %d", hpss_config.TransferType);
  LogDebug(COMPONENT_FSAL, "  NumRetries: %d", hpss_config.NumRetries);
  LogDebug(COMPONENT_FSAL, "  BusyDelay: %d", hpss_config.BusyDelay);
  LogDebug(COMPONENT_FSAL, "  BusyRetries: %d", hpss_config.BusyRetries);
  LogDebug(COMPONENT_FSAL, "  TotalDelay: %d", hpss_config.TotalDelay);
  LogDebug(COMPONENT_FSAL, "  LimitedRetries: %d",
                    hpss_config.LimitedRetries);
  LogDebug(COMPONENT_FSAL, "  MaxConnections: %d",
                    hpss_config.MaxConnections);
  LogDebug(COMPONENT_FSAL, "  ReuseDataConnections: %d",
                    hpss_config.ReuseDataConnections);
  LogDebug(COMPONENT_FSAL, "  UsePortRange: %d", hpss_config.UsePortRange);
  LogDebug(COMPONENT_FSAL, "  RetryStageInp: %d",
                    hpss_config.RetryStageInp);
  LogDebug(COMPONENT_FSAL, "  DebugValue: %#X", hpss_config.DebugValue);
  LogDebug(COMPONENT_FSAL, "  DebugPath: %s", hpss_config.DebugPath);


  /* set the final configuration */
  rc = hpss_SetConfiguration(&hpss_config);

  return rc;
}


/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *fsal_hdl,
         config_file_t config_struct)
{
  int retval;
  struct hpss_fsal_module * hpss_me
    = container_of(fsal_hdl, struct hpss_fsal_module, fsal);
  fsal_status_t fsal_status;

  hpss_me->fs_info = default_hpss_info; /* get a copy of the defaults */
  memset(&hpss_me->specific_info, 0, sizeof(hpssfs_specific_initinfo_t));

  fsal_status = fsal_load_config(fsal_hdl->ops->get_name(fsal_hdl),
                                 config_struct,
                                 &hpss_me->fsal_info,
                                 &hpss_me->fs_info,
                                 hpss_key_to_param);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /* default values */
  if( hpss_me->specific_info.CredentialLifetime == 0 )
    hpss_me->specific_info.CredentialLifetime = HPSS_DEFAULT_CREDENTIAL_LIFETIME;

 if((retval = HPSSFSAL_Init_Internals(fsal_hdl)))
    return fsalstat(hpss2fsal_error(retval), 0);

  if((retval = HPSSFSAL_SecInit(fsal_hdl)))
    return fsalstat(hpss2fsal_error(retval), 0);

  display_fsinfo(&hpss_me->fs_info);
  LogFullDebug(COMPONENT_FSAL,
               "Supported attributes constant = 0x%"PRIx64,
               (uint64_t) HPSS_SUPPORTED_ATTRIBUTES);
  LogFullDebug(COMPONENT_FSAL,
               "Supported attributes default = 0x%"PRIx64,
                default_hpss_info.supported_attrs);
  LogDebug(COMPONENT_FSAL,
           "FSAL INIT: Supported attributes mask = 0x%"PRIx64,
           hpss_me->fs_info.supported_attrs);
  return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal HPSS method linkage to export object
 */

fsal_status_t hpss_create_export(struct fsal_module *fsal_hdl,
                                 const char *export_path,
                                 const char *fs_options,
                                 exportlist_t *exp_entry,
                                 struct fsal_module *next_fsal,
                                 const struct fsal_up_vector *up_ops,
                                 struct fsal_export **export);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct hpss_fsal_module HPSS;

/* linkage to the exports and handle ops initializers
 */

void hpss_export_ops_init(struct export_ops *ops);
void hpss_handle_ops_init(struct fsal_obj_ops *ops);

MODULE_INIT void hpss_load(void) {
  int retval;
  struct fsal_module *myself = &HPSS.fsal;

  retval = register_fsal(myself, myname,
                         FSAL_MAJOR_VERSION,
                         FSAL_MINOR_VERSION);
  if(retval != 0) {
    fprintf(stderr, "HPSS module failed to register");
    return;
  }

  myself->ops->create_export = hpss_create_export;
  myself->ops->init_config = init_config;
  init_fsal_parameters(&HPSS.fsal_info);
}

MODULE_FINI void hpss_unload(void) {
  int retval;

  retval = unregister_fsal(&HPSS.fsal);
  if(retval != 0) {
    fprintf(stderr, "HPSS module failed to unregister");
    return;
  }
}
