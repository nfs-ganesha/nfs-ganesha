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

fsal_status_t fsal2fs_attribset( fsal_handle_t  * p_fsal_handle,
                                 fsal_attrib_list_t  * p_attrib_set ,
                                <depends on your fs way of setting attributes> );

fsal_time_t fs2fsal_time( <your fs time structure> );

<your fs time structure> fsal2fs_time(fsal_time_t in_time);
     
*/

#endif
