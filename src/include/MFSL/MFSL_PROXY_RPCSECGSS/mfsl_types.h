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

#ifndef _MFSL_PROXY_RPCSECGSS_TYPES_H
#define _MFSL_PROXY_RPCSECGSS_TYPES_H

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * labels in the config file
 */

#define CONF_LABEL_MFSL_NULL          "MFSL_NULL"

/* other includes */
#include <sys/types.h>
#include <sys/param.h>
#include <dirent.h>             /* for MAXNAMLEN */
#include "config_parsing.h"
#include "err_fsal.h"
#include "err_mfsl.h"

typedef struct mfsl_parameter__
{

  int i;
} mfsl_parameter_t;

typedef struct mfsl_context__
{

  int i;
} mfsl_context_t;

typedef struct mfsl_object__
{
  int nothing;
} mfsl_object_specific_data_t;

#endif                          /* _MFSL_PROXY_RPCSECGSS_TYPES_H */
