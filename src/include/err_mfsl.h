/*
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
 * \file    err_fsal.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 14:22:57 $
 * \version $Revision: 1.30 $
 * \brief   MFSL error codes.
 *
 *
 */

#ifndef _ERR_MFSL_H
#define _ERR_MFSL_H

#include <log_functions.h>

static family_error_t __attribute__ ((__unused__)) tab_errstatus_MFSL[] =
{

#define ERR_MFSL_NO_ERROR 0
  {
  ERR_MFSL_NO_ERROR, "ERR_MFSL_NO_ERROR", "No error"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif /*_ERR_MFSL_H*/
