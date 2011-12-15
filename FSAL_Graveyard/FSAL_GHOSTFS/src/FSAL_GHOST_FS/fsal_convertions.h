/**
 *
 * \file    fsal_convertions.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:37:24 $
 * \version $Revision: 1.2 $
 * \brief   ghostfs to FSAL type converting function.
 *
 */
#ifndef _FSAL_CONVERTION_H
#define _FSAL_CONVERTION_H

#include "fsal.h"

/** converts GHOST_FS type to FSAL type */
fsal_nodetype_t ghost2fsal_type(GHOSTFS_typeitem_t type);

/** converts GHOST_FS mode to FSAL mode */
fsal_accessmode_t ghost2fsal_mode(GHOSTFS_perm_t mode);

/** converts FSAL to GHOST_FS mode */
GHOSTFS_perm_t fsal2ghost_mode(fsal_accessmode_t mode);

/** converts an FSAL permission test to a GHOSTFS permission test. */
GHOSTFS_testperm_t fsal2ghost_testperm(fsal_accessflags_t testperm);

/* convert a gost fs error code to an FSAL error code */
int ghost2fsal_error(int code);

/* convert ghostfs attributes to FSAL attributes */
int ghost2fsal_attrs(fsal_attrib_list_t * p_fsal_attrs, GHOSTFS_Attrs_t * p_ghost_attrs);

#endif
