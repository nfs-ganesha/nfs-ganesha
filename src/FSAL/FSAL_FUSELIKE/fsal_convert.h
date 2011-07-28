/**
 *
 * \file    fsal_convert.h
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/27 14:15:57 $
 * \version $Revision: 1.13 $
 * \brief   Your FS to FSAL type converting function.
 *
 */
#ifndef _FSAL_CONVERTION_H
#define _FSAL_CONVERTION_H

#include "fsal.h"

int fsal2posix_testperm(fsal_accessflags_t testperm);
int fuse2fsal_error(int errorcode, int noent_is_stale);
fsal_status_t posix2fsal_attributes(struct stat *p_buffstat,
                                    fsal_attrib_list_t * p_fsalattr_out);

fsal_nodetype_t posix2fsal_type(mode_t posix_type_in);
fsal_time_t posix2fsal_time(time_t tsec);
fsal_fsid_t posix2fsal_fsid(dev_t posix_devid);
fsal_dev_t posix2fsal_devt(dev_t posix_devid);

#endif
