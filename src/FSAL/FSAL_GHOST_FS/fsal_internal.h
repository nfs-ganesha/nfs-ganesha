/**
 *
 * \file    fsal_internal.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:37:24 $
 * \version $Revision: 1.8 $
 * \brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 * 
 */

#include  "fsal.h"

/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern fsal_staticfsinfo_t global_fs_info;

#endif

/* defined the set of attributes supported with HPSS */
#define GHOSTFS_SUPPORTED_ATTRIBUTES (                                    \
          FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE     | FSAL_ATTR_SIZE      | \
          FSAL_ATTR_FSID     | FSAL_ATTR_FILEID    | \
          FSAL_ATTR_MODE     | FSAL_ATTR_NUMLINKS | FSAL_ATTR_OWNER     | \
          FSAL_ATTR_GROUP    | FSAL_ATTR_ATIME    | FSAL_ATTR_CREATION  | \
          FSAL_ATTR_CTIME    | FSAL_ATTR_MTIME  | FSAL_ATTR_SPACEUSED | \
          FSAL_ATTR_CHGTIME )

/**
 *  This function initializes shared variables of the FSAL.
 */
fsal_status_t fsal_internal_init_global(fsal_init_info_t * fsal_info,
                                        fs_common_initinfo_t * fs_common_info);

/**
 *  Increments the number of calls for a function.
 */
void fsal_increment_nbcall(int function_index, fsal_status_t status);

/**
 * Retrieves current thread statistics.
 */
void fsal_internal_getstats(fsal_statistics_t * output_stats);

/* automaticaly sets the function name, from the function index. */
/*#define SetFuncID(_f_) SetNameFunction(fsal_function_names[_f_])*/
#define SetFuncID(_f_)
