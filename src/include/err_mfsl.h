/*
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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

#include "log_macros.h"

static family_error_t __attribute__ ((__unused__)) tab_errstatus_MFSL[] =
{

#define ERR_MFSL_NO_ERROR 0
  {
  ERR_MFSL_NO_ERROR, "ERR_MFSL_NO_ERROR", "No error"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif /*_ERR_MFSL_H*/
