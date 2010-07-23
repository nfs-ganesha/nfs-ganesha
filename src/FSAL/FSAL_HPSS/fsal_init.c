/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_init.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.20 $
 * \brief   Initialization functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_common.h"
#include "HPSSclapiExt/hpssclapiext.h"

#if HPSS_MAJOR_VERSION >= 6
#include <hpss_Getenv.h>

static hpss_authn_mech_t FSAL_auth_mech;

#endif

static char FSAL_PrincipalName[HPSS_MAX_PRINCIPAL_NAME];
static char FSAL_KeytabPath[HPSS_MAX_PATH_NAME];

/** Initializes security context */
static int HPSSFSAL_SecInit(hpssfs_specific_initinfo_t * hpss_init_info)
{

  int rc;

#if HPSS_MAJOR_VERSION == 5
  /** @todo : hpss_SetLoginContext : is it a good way of proceeding ? */

  rc = hpss_SetLoginContext(FSAL_PrincipalName, FSAL_KeytabPath);

  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "FSAL SEC INIT: DCE principal is set to '%s'",
                    FSAL_PrincipalName);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "FSAL SEC INIT: Keytab is set to '%s'",
                    FSAL_KeytabPath);

#elif HPSS_MAJOR_VERSION >= 6
  hpss_rpc_auth_type_t auth_type;

  rc = hpss_GetAuthType(FSAL_auth_mech, &auth_type);
  if(rc != 0)
    return rc;

  if(auth_type != hpss_rpc_auth_type_keytab && auth_type != hpss_rpc_auth_type_keyfile)
    {
      return ERR_FSAL_INVAL;
    }

  rc = hpss_SetLoginCred(FSAL_PrincipalName,
                         FSAL_auth_mech,
                         auth_type, hpss_rpc_cred_client, (void *)FSAL_KeytabPath);

  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "FSAL SEC INIT: Auth Mech is set to '%s'",
                    hpss_AuthnMechTypeString(FSAL_auth_mech));
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "FSAL SEC INIT: Auth Type is set to '%s'",
                    hpss_AuthenticatorTypeString(auth_type));
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "FSAL SEC INIT: Principal is set to '%s'",
                    FSAL_PrincipalName);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "FSAL SEC INIT: Keytab is set to '%s'",
                    FSAL_KeytabPath);

#endif

  return rc;

}

/* Macros for analysing parameters. */
#define SET_BITMAP_PARAM( api_cfg, p_init_info, _field )      \
    switch( (p_init_info)->behaviors._field ){                \
      case FSAL_INIT_FORCE_VALUE :                            \
        /* force the value in any case */                     \
        api_cfg._field = (p_init_info)->hpss_config._field;   \
        break;                                                \
      case FSAL_INIT_MAX_LIMIT :                              \
        /* remove the flags not specified by user (AND) */    \
        api_cfg._field &= (p_init_info)->hpss_config._field;  \
        break;                                                \
      case FSAL_INIT_MIN_LIMIT :                              \
        /* add the flags specified by user (OR) */            \
        api_cfg._field |= (p_init_info)->hpss_config._field;  \
        break;                                                \
    /* In the other cases, we keep the default value. */      \
    }                                                         \


#define SET_INTEGER_PARAM( api_cfg, p_init_info, _field )         \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
        /* force the value in any case */                         \
        api_cfg._field = (p_init_info)->hpss_config._field;       \
        break;                                                \
    case FSAL_INIT_MAX_LIMIT :                                    \
      /* check the higher limit */                                \
      if ( api_cfg._field > (p_init_info)->hpss_config._field )   \
        api_cfg._field = (p_init_info)->hpss_config._field ;      \
        break;                                                \
    case FSAL_INIT_MIN_LIMIT :                                    \
      /* check the lower limit */                                 \
      if ( api_cfg._field < (p_init_info)->hpss_config._field )   \
        api_cfg._field = (p_init_info)->hpss_config._field ;      \
        break;                                                \
    /* In the other cases, we keep the default value. */          \
    }                                                             \


#define SET_STRING_PARAM( api_cfg, p_init_info, _field )          \
    switch( (p_init_info)->behaviors._field ){                    \
    case FSAL_INIT_FORCE_VALUE :                                  \
      /* force the value in any case */                           \
      strcpy(api_cfg._field,(p_init_info)->hpss_config._field);   \
      break;                                                \
    /* In the other cases, we keep the default value. */          \
    }                                                             \

/** Initialize HPSS client API */
static int HPSSFSAL_Init_internal(hpssfs_specific_initinfo_t * hpss_init_info)
{

  int rc = 0;
  api_config_t hpss_config;

  if(!hpss_init_info)
    return ERR_FSAL_FAULT;

  /* First, get current values for config. */

  if(rc = hpss_GetConfiguration(&hpss_config))
    return rc;

#if HPSS_MAJOR_VERSION == 5

  /* Then analyze user's init info. */

  SET_STRING_PARAM(hpss_config, hpss_init_info, PrincipalName)
      SET_STRING_PARAM(hpss_config, hpss_init_info, KeytabPath)
      /* retrieve authentication values. */
      strcpy(FSAL_PrincipalName, hpss_config.PrincipalName);
  strcpy(FSAL_KeytabPath, hpss_config.KeytabPath);

#elif HPSS_MAJOR_VERSION >= 6

#define API_DEBUG_ERROR    (1)
#define API_DEBUG_REQUEST  (2)
#define API_DEBUG_TRACE    (4)

  hpss_config.Flags |= API_USE_CONFIG;

  /* retrieve authentication mechanism */

  if(hpss_init_info->behaviors.AuthnMech == FSAL_INIT_FORCE_VALUE)
    {
      FSAL_auth_mech = hpss_init_info->hpss_config.AuthnMech;
      hpss_config.AuthnMech = hpss_init_info->hpss_config.AuthnMech;
    }
  else
    {
      FSAL_auth_mech = hpss_config.AuthnMech;
    }

  if(hpss_init_info->behaviors.NumRetries == FSAL_INIT_FORCE_VALUE)
    hpss_config.NumRetries = hpss_init_info->hpss_config.NumRetries;

  if(hpss_init_info->behaviors.BusyRetries == FSAL_INIT_FORCE_VALUE)
    hpss_config.BusyRetries = hpss_init_info->hpss_config.BusyRetries;

  if(hpss_init_info->behaviors.BusyDelay == FSAL_INIT_FORCE_VALUE)
    hpss_config.BusyDelay = hpss_init_info->hpss_config.BusyDelay;

  if(hpss_init_info->behaviors.BusyRetries == FSAL_INIT_FORCE_VALUE)
    hpss_config.BusyRetries = hpss_init_info->hpss_config.BusyRetries;

  if(hpss_init_info->behaviors.MaxConnections == FSAL_INIT_FORCE_VALUE)
    hpss_config.MaxConnections = hpss_init_info->hpss_config.MaxConnections;

  if(hpss_init_info->behaviors.Principal == FSAL_INIT_FORCE_VALUE)
    strcpy(FSAL_PrincipalName, hpss_init_info->Principal);

  if(hpss_init_info->behaviors.KeytabPath == FSAL_INIT_FORCE_VALUE)
    strcpy(FSAL_KeytabPath, hpss_init_info->KeytabPath);

  if(hpss_init_info->behaviors.DebugPath == FSAL_INIT_FORCE_VALUE)
    {
      strcpy(hpss_config.DebugPath, hpss_init_info->hpss_config.DebugPath);
      hpss_config.DebugValue |= API_DEBUG_ERROR | API_DEBUG_REQUEST | API_DEBUG_TRACE;
      hpss_config.Flags |= API_ENABLE_LOGGING;
    }

  /* Display Clapi configuration */

  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "HPSS Client API configuration:");
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  Flags: %08X", hpss_config.Flags);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  TransferType: %d", hpss_config.TransferType);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  NumRetries: %d", hpss_config.NumRetries);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  BusyDelay: %d", hpss_config.BusyDelay);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  BusyRetries: %d", hpss_config.BusyRetries);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  TotalDelay: %d", hpss_config.TotalDelay);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  LimitedRetries: %d",
                    hpss_config.LimitedRetries);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  MaxConnections: %d",
                    hpss_config.MaxConnections);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  ReuseDataConnections: %d",
                    hpss_config.ReuseDataConnections);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  UsePortRange: %d", hpss_config.UsePortRange);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  RetryStageInp: %d",
                    hpss_config.RetryStageInp);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  DebugValue: %#X", hpss_config.DebugValue);
  DisplayLogJdLevel(fsal_log, NIV_DEBUG, "  DebugPath: %s", hpss_config.DebugPath);

#endif

  strcpy(hpss_config.DescName, "hpss.ganesha.nfsd");

  /* set the final configuration */
  rc = hpss_SetConfiguration(&hpss_config);

  return rc;

}

/**
 * FSAL_Init : Initializes the FileSystem Abstraction Layer.
 *
 * \param init_info (input, fsal_parameter_t *) :
 *        Pointer to a structure that contains
 *        all initialization parameters for the FSAL.
 *        Specifically, it contains settings about
 *        the filesystem on which the FSAL is based,
 *        security settings, logging policy and outputs,
 *        and other general FSAL options.
 *
 * \return Major error codes :
 *         ERR_FSAL_NO_ERROR     (initialisation OK)
 *         ERR_FSAL_FAULT        (init_info pointer is null)
 *         ERR_FSAL_SERVERFAULT  (misc FSAL error)
 *         ERR_FSAL_ALREADY_INIT (The FS is already initialized)
 *         ERR_FSAL_BAD_INIT     (FS specific init error,
 *                                minor error code gives the reason
 *                                for this error.)
 *         ERR_FSAL_SEC_INIT     (Security context init error).
 */
fsal_status_t HPSSFSAL_Init(fsal_parameter_t * init_info        /* IN */
    )
{

  fsal_status_t status;
  hpss_fileattr_t rootattr;
  int rc;

  /* sanity check.  */

  if(!init_info)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);

  /* Check for very important args */

  if(init_info->fsal_info.log_outputs.liste_voies == NULL)
    {
      /* issue a warning on stderr */
      DisplayLog
          ("FSAL INIT: *** WARNING: No logging file specified for FileSystem Abstraction Layer.");
    }
#if HPSS_MAJOR_VERSION == 5

  if(init_info->fs_specific_info.behaviors.KeytabPath == FSAL_INIT_FS_DEFAULT)
    {
      /* issue a warning on stderr if no credential is specified */
      DisplayLog
          ("FSAL INIT: *** WARNING: No keytab file specified for HPSS, default client keytab will be used...");
      DisplayLog
          ("FSAL INIT: ***          Set %s::KeytabPath into config file to use another keytab",
           CONF_LABEL_FS_SPECIFIC);
    }

  if(init_info->fs_specific_info.behaviors.PrincipalName == FSAL_INIT_FS_DEFAULT)
    {
      /* issue a warning on stderr if no credential is specified */
      DisplayLog
          ("FSAL INIT: *** WARNING: No principal name specified for HPSS, default will be used...");
      DisplayLog
          ("FSAL INIT: ***          Set %s::PrincipalName into config file to use another principal",
           CONF_LABEL_FS_SPECIFIC);
    }
#elif HPSS_MAJOR_VERSION >= 6

  if(init_info->fs_specific_info.behaviors.AuthnMech == FSAL_INIT_FS_DEFAULT)
    {
      /* issue a warning on stderr if no credential is specified */
      DisplayLog
          ("FSAL INIT: *** WARNING: No authentication mechanism specified for HPSS, default authentication mechanism will be used...");
      DisplayLog
          ("FSAL INIT: ***          Set %s::AuthMech into config file to use another Authentication Mechanism",
           CONF_LABEL_FS_SPECIFIC);
    }

  if(init_info->fs_specific_info.behaviors.KeytabPath == FSAL_INIT_FS_DEFAULT)
    {
      /* issue a warning on stderr if no credential is specified */
      DisplayLog
          ("FSAL INIT: *** WARNING: No keytab file specified for HPSS, default client keytab will be used...");
      DisplayLog
          ("FSAL INIT: ***          Set %s::KeytabPath into config file to use another keytab",
           CONF_LABEL_FS_SPECIFIC);
    }

  if(init_info->fs_specific_info.behaviors.Principal == FSAL_INIT_FS_DEFAULT)
    {
      /* issue a warning on stderr if no credential is specified */
      DisplayLog
          ("FSAL INIT: *** WARNING: No principal name specified for HPSS, default principal name will be used...");
      DisplayLog
          ("FSAL INIT: ***          Set %s::PrincipalName into config file to use another principal",
           CONF_LABEL_FS_SPECIFIC);
    }
#endif

  /* proceeds FSAL internal initialization */

  status = fsal_internal_init_global(&(init_info->fsal_info),
                                     &(init_info->fs_common_info));

  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_Init);

  /* configure HPSS cl api  
     (and retrieve current PrincipalName and KeytabName) */

  if(rc = HPSSFSAL_Init_internal(&init_info->fs_specific_info))
    Return(ERR_FSAL_BAD_INIT, -rc, INDEX_FSAL_Init);

  /* Init security context */

  if(rc = HPSSFSAL_SecInit(&init_info->fs_specific_info))
    Return(ERR_FSAL_SEC, -rc, INDEX_FSAL_Init);

  /* sets the credential renewal time */

  switch (init_info->fs_specific_info.behaviors.CredentialLifetime)
    {
    case FSAL_INIT_FORCE_VALUE:
      /* force the value in any case */
      fsal_internal_SetCredentialLifetime(init_info->fs_specific_info.CredentialLifetime);
      break;
      /* In the other cases, we keep the default value. */
    }

  /* sets behavior for inconsistent directory entries */

  switch (init_info->fs_specific_info.behaviors.ReturnInconsistentDirent)
    {
    case FSAL_INIT_FORCE_VALUE:
      /* force the value in any case */
      fsal_internal_SetReturnInconsistentDirent(init_info->fs_specific_info.
                                                ReturnInconsistentDirent);
      break;
      /* In the other cases, we keep the default value. */
    }

  /* Everything went OK. */

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_Init);

}

/* To be called before exiting */
fsal_status_t HPSSFSAL_terminate()
{
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
