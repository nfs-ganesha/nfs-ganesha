/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_convert.c
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.32 $
 * \brief   FS-FSAL type translation functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "fsal_convert.h"
#include "fsal_internal.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

/* some ideas of conversion functions...

int fs2fsal_error(int fs_errorcode);

int fsal2fs_openflags( fsal_openflags_t fsal_flags, int * p_fs_flags );

int fsal2fs_testperm(fsal_accessflags_t testperm);

fsal_status_t fs2fsal_attributes(  <your fs attribute structure (input)>,
                                   fsal_attrib_list_t * p_fsalattr_out );

fsal_accessmode_t fs2fsal_mode( <your fs object permission (input)> );

void fsal2fs_mode( fsal_accessmode_t fsal_mode, <your fs mode type (output)> );

fsal_nodetype_t  hpss2fsal_type( <your fs object type (input)> );

fsal_u64_t fs2fsal_64( <your fs 64bits type> );

<your fs 64bits type> fsal2hpss_64( fsal_u64_t fsal_size_in );

fsal_fsid_t fs2fsal_fsid( <you fs fsid type> );

fsal_status_t fsal2fs_attribset( proxyfsal_handle_t  * p_fsal_handle,
                                 fsal_attrib_list_t  * p_attrib_set ,
                                <depends on your fs way of setting attributes> );

fsal_time_t fs2fsal_time( <your fs time structure> );

<your fs time structure> fsal2fs_time(fsal_time_t in_time);
     
*/


