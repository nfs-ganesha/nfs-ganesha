/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * DISCLAIMER
 * ----------
 * This file is part of FSAL_HPSS.
 * FSAL HPSS provides the glue in-between FSAL API and HPSS CLAPI
 * You need to have HPSS installed to properly compile this file.
 *
 * Linkage/compilation/binding/loading/etc of HPSS licensed Software
 * must occur at the HPSS partner's or licensee's location.
 * It is not allowed to distribute this software as compiled or linked
 * binaries or libraries, as they include HPSS licensed material.
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
#include "fsal_internal.h"
#include "hpss_methods.h"
#include "FSAL/fsal_init.h"
#include "nfs_exports.h"

#include <hpss_Getenv.h>
#include <hpss_types.h>
#include <hpss_api.h>

/* HPSS FSAL module private storage
 */

struct hpss_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	struct hpss_specific_initinfo specific_info;
	struct api_config hpss_config;
	char *principal;
	char *keytab_path;
	char *debug_path;
	uint32_t credential_lifetime;
	bool return_inconsistent_dirent;
	uint16_t default_cos;
	char *fileset_name;
	char *hostname;
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
	0,                            /* don't allow cross fileset exportpath */
	0400,                         /* default access rights for xattrs */
	0,                            /* default access check support in FSAL */
	0,                            /* default share reservation support */
	0,                            /* delegation */
	0                             /* pnfs_file */
};

/* private helper for export object
 */

struct fsal_staticfsinfo_t *hpss_staticinfo(struct fsal_module *hdl)
{
	struct hpss_fsal_module *myself;

	myself = container_of(hdl, struct hpss_fsal_module, fsal);
	return &myself->fs_info;
}

struct hpss_specific_initinfo *hpss_specific_initinfo(struct fsal_module *hdl)
{
	struct hpss_fsal_module *myself;

	myself = container_of(hdl, struct hpss_fsal_module, fsal);
	return &myself->specific_info;
}

static struct config_item_list hpss_authn_mechs[] = {
	CONFIG_LIST_TOK("krb5", hpss_authn_mech_krb5),
	CONFIG_LIST_TOK("unix", hpss_authn_mech_unix),
	CONFIG_LIST_EOL
};

static struct config_item hpss_params[] = {
	CONF_ITEM_STR("Principal", 0, MAXNAMLEN, NULL,
		      hpss_fsal_module, principal),
	CONF_ITEM_STR("KeytabPath", 0, MAXPATHLEN, NULL,
		      hpss_fsal_module, keytab_path),
	CONF_ITEM_UI32("CredentialLifetime", 1, UINT32_MAX,
		       HPSS_DEFAULT_CREDENTIAL_LIFETIME,
		       hpss_fsal_module, credential_lifetime),
	CONF_ITEM_BOOL("ReturnInconsistentDirent", false,
		       hpss_fsal_module, return_inconsistent_dirent),
	CONF_ITEM_TOKEN("AuthnMech", hpss_authn_mech_invalid,
			hpss_authn_mechs,
			hpss_fsal_module, hpss_config.AuthnMech),
	CONF_ITEM_I32("NumRetries", -1, INT16_MAX, -1,
		       hpss_fsal_module, hpss_config.NumRetries),
	CONF_ITEM_I32("BusyDelay", -1, INT32_MAX, -1,
		       hpss_fsal_module, hpss_config.BusyDelay),
	CONF_ITEM_I32("BusyRetries", -1, INT16_MAX, -1,
		       hpss_fsal_module, hpss_config.BusyRetries),
	CONF_ITEM_I32("MaxConnections", -1, INT32_MAX, -1,
		       hpss_fsal_module, hpss_config.MaxConnections),
	CONF_ITEM_STR("DebugPath", 0, HPSS_MAX_PATH_NAME, NULL,
		       hpss_fsal_module, debug_path),
	CONF_ITEM_UI16("default_cos", 0, UINT16_MAX, 0,
		       hpss_fsal_module, default_cos),
	CONF_ITEM_STR("filesetName", 0, MAXNAMLEN, NULL,
		       hpss_fsal_module, fileset_name),
	CONF_ITEM_STR("HostName", 0, HPSS_MAX_HOST_NAME, NULL,
		      hpss_fsal_module, hostname),
	CONFIG_EOL
};

struct config_block hpss_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.hpss",
	.blk_desc.name = "HPSS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = hpss_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/** Initializes security context */
static int HPSSFSAL_SecInit(struct hpss_fsal_module *hpss_mod)
{
	int rc;
	hpss_rpc_auth_type_t auth_type;

	if (!hpss_mod)
		return ERR_FSAL_FAULT;

	rc = hpss_GetAuthType(hpss_mod->hpss_config.AuthnMech, &auth_type);
	if (rc != 0)
		return rc;

	if (auth_type != hpss_rpc_auth_type_keytab &&
	    auth_type != hpss_rpc_auth_type_keyfile)
		return ERR_FSAL_INVAL;

	rc = hpss_SetLoginCred(hpss_mod->principal,
	hpss_mod->hpss_config.AuthnMech,
	hpss_rpc_cred_client, auth_type, (void *)hpss_mod->keytab_path);

	LogDebug(COMPONENT_FSAL, "FSAL SEC INIT: Auth Mech is set to '%s'",
		 hpss_AuthnMechTypeString(hpss_mod->hpss_config.AuthnMech));
	LogDebug(COMPONENT_FSAL, "FSAL SEC INIT: Auth Type is set to '%s'",
		 hpss_AuthenticatorTypeString(auth_type));
	LogDebug(COMPONENT_FSAL, "FSAL SEC INIT: Principal is set to '%s'",
		 hpss_mod->principal);
	LogDebug(COMPONENT_FSAL, "FSAL SEC INIT: Keytab is set to '%s'",
		 hpss_mod->keytab_path);

	return rc;
}

static int HPSSFSAL_Init_Internals(struct hpss_fsal_module *hpss_mod)
{
	int rc;
	api_config_t hpss_config;

	if (!hpss_mod)
		return ERR_FSAL_FAULT;

	/* First, get current values for config. */

	rc = hpss_GetConfiguration(&hpss_config);
	if (rc)
		return rc;

#define API_DEBUG_ERROR    (1)
#define API_DEBUG_REQUEST  (2)
#define API_DEBUG_TRACE    (4)

	hpss_config.Flags |= API_USE_CONFIG;

	/* Change values if they were set in configuration */
	if (hpss_mod->hpss_config.AuthnMech != hpss_authn_mech_invalid)
		hpss_config.AuthnMech = hpss_mod->hpss_config.AuthnMech;
	else
		hpss_mod->hpss_config.AuthnMech = hpss_config.AuthnMech;

	if (hpss_mod->hpss_config.NumRetries != -1)
		hpss_config.NumRetries = hpss_mod->hpss_config.NumRetries;

	if (hpss_mod->hpss_config.BusyRetries != -1)
		hpss_config.BusyRetries = hpss_mod->hpss_config.BusyRetries;

	if (hpss_mod->hpss_config.BusyDelay != -1)
		hpss_config.BusyDelay = hpss_mod->hpss_config.BusyDelay;

	if (hpss_mod->hpss_config.BusyRetries != -1)
		hpss_config.BusyRetries = hpss_mod->hpss_config.BusyRetries;

	if (hpss_mod->hpss_config.MaxConnections != -1)
		hpss_config.MaxConnections =
			 hpss_mod->hpss_config.MaxConnections;

	if (hpss_mod->debug_path != NULL) {
		strncpy(hpss_config.DebugPath, hpss_mod->debug_path,
			HPSS_MAX_PATH_NAME);
		hpss_config.DebugValue |= API_DEBUG_ERROR |
				 API_DEBUG_REQUEST | API_DEBUG_TRACE;
		hpss_config.Flags |= API_ENABLE_LOGGING;
	}

	if (hpss_mod->hostname != NULL)
		strncpy(hpss_config.HostName, hpss_mod->hostname,
			HPSS_MAX_HOST_NAME);

	strcpy(hpss_config.DescName, "hpss.ganesha.nfsd");

	/* Display Clapi configuration */
	LogDebug(COMPONENT_FSAL, "HPSS Client API configuration:");
	LogDebug(COMPONENT_FSAL, "  Flags: %08X", hpss_config.Flags);
	LogDebug(COMPONENT_FSAL, "  TransferType: %d",
		 hpss_config.TransferType);
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
	LogDebug(COMPONENT_FSAL, "  UsePortRange: %d",
		 hpss_config.UsePortRange);
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

static fsal_status_t hpss_init_config(struct fsal_module *fsal_hdl,
				      config_file_t config_struct,
				      struct config_error_type *err_type)
{
	int retval;
	struct hpss_fsal_module *hpss_me
		  = container_of(fsal_hdl, struct hpss_fsal_module, fsal);

	hpss_me->fs_info = default_hpss_info; /* get a copy of the defaults */

	(void) load_config_from_parse(config_struct,
				      &hpss_param,
				      hpss_me,
				      true,
				      err_type);

	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	/* default values */
	retval = HPSSFSAL_Init_Internals(hpss_me);
	if (retval)
		return fsalstat(hpss2fsal_error(retval), 0);

	retval = HPSSFSAL_SecInit(hpss_me);
	if (retval)
		return fsalstat(hpss2fsal_error(retval), 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Internal HPSS method linkage to export object
 */

fsal_status_t hpss_create_export(struct fsal_module *fsal_hdl,
				 void *parse_node,
				 struct config_error_type *err_type,
				 const struct fsal_up_vector *up_ops);

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* my module private storage
 */

static struct hpss_fsal_module HPSS;

/* linkage to the exports and handle ops initializers
 */

MODULE_INIT void hpss_load(void)
{
	int retval;
	struct fsal_module *myself = &HPSS.fsal;

	retval = register_fsal(myself, myname,
			       FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION,
			       FSAL_ID_NO_PNFS);
	if (retval != 0) {
		fprintf(stderr, "HPSS module failed to register");
		return;
	}

	myself->m_ops.create_export = hpss_create_export;
	myself->m_ops.init_config = hpss_init_config;
}

MODULE_FINI void hpss_unload(void)
{
	int retval;

	retval = unregister_fsal(&HPSS.fsal);
	if (retval != 0)
		fprintf(stderr, "HPSS module failed to unregister");
}
