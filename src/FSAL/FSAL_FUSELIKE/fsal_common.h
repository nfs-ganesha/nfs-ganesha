/**
 * Common FS tools for internal use in the FSAL.
 *
 */

#ifndef FSAL_COMMON_H
#define FSAL_COMMON_H

#include "fsal.h"

/* >> You can define here the functions that are to be used
 * all over your FSAL but you don't want to be called externaly <<
 *
 * Ex: YouFS_GetRoot( fsal_handle_t * out_hdl, char * server_name, ... );
 */

void FSAL_internal_append_path(char *tgt, char *parent, char *child);

#endif
