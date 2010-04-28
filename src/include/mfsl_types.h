/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
 * ---------------------------------------
 */

/**
 * \file    mfsl_types.h
 */

#ifndef _MFSL_TYPES_H
#define _MFSL_TYPES_H

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * labels in the config file
 */

#define CONF_LABEL_MFSL          "MFSL"

/* other includes */
#include <sys/types.h>
#include <sys/param.h>
#include <dirent.h>             /* for MAXNAMLEN */
#include "config_parsing.h"
#include "fsal.h"
#include "fsal_types.h"
#include "err_fsal.h"
#include "err_mfsl.h"

#ifdef _USE_MFSL_NULL
#include "MFSL/MFSL_NULL/mfsl_types.h"
#endif

#ifdef _USE_MFSL_ASYNC
#include "MFSL/MFSL_ASYNC/mfsl_types.h"
#endif

#ifdef _USE_MFSL_PROXY_RPCSECGSS
#include "MFSL/MFSL_PROXY_RPCSECGSS/mfsl_types.h"
#endif

#endif                          /* _MFSL_TYPES_H */
