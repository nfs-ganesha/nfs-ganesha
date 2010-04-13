/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_convert.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:46:59 $
 * \version $Revision: 1.32 $
 * \brief   FS-FSAL type translation functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
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

fsal_status_t fsal2fs_attribset( fsal_handle_t  * p_fsal_handle,
                                 fsal_attrib_list_t  * p_attrib_set ,
                                <depends on your fs way of setting attributes> );

fsal_time_t fs2fsal_time( <your fs time structure> );

<your fs time structure> fsal2fs_time(fsal_time_t in_time);
     
*/

/* THOSE FUNCTIONS CAN BE USED FROM OUTSIDE THE MODULE : */

/**
 * fsal2unix_mode:
 * Convert FSAL mode to posix mode.
 *
 * \param fsal_mode (input):
 *        The FSAL mode to be translated.
 *
 * \return The posix mode associated to fsal_mode.
 */
mode_t fsal2unix_mode(fsal_accessmode_t fsal_mode)
{

  mode_t out_mode = 0;

  if((fsal_mode & FSAL_MODE_SUID))
    out_mode |= S_ISUID;
  if((fsal_mode & FSAL_MODE_SGID))
    out_mode |= S_ISGID;

  if((fsal_mode & FSAL_MODE_RUSR))
    out_mode |= S_IRUSR;
  if((fsal_mode & FSAL_MODE_WUSR))
    out_mode |= S_IWUSR;
  if((fsal_mode & FSAL_MODE_XUSR))
    out_mode |= S_IXUSR;
  if((fsal_mode & FSAL_MODE_RGRP))
    out_mode |= S_IRGRP;
  if((fsal_mode & FSAL_MODE_WGRP))
    out_mode |= S_IWGRP;
  if((fsal_mode & FSAL_MODE_XGRP))
    out_mode |= S_IXGRP;
  if((fsal_mode & FSAL_MODE_ROTH))
    out_mode |= S_IROTH;
  if((fsal_mode & FSAL_MODE_WOTH))
    out_mode |= S_IWOTH;
  if((fsal_mode & FSAL_MODE_XOTH))
    out_mode |= S_IXOTH;

  return out_mode;

}

/**
 * unix2fsal_mode:
 * Convert posix mode to FSAL mode.
 *
 * \param unix_mode (input):
 *        The posix mode to be translated.
 *
 * \return The FSAL mode associated to unix_mode.
 */
fsal_accessmode_t unix2fsal_mode(mode_t unix_mode)
{

  fsal_accessmode_t fsalmode = 0;

  if(unix_mode & S_ISUID)
    fsalmode |= FSAL_MODE_SUID;
  if(unix_mode & S_ISGID)
    fsalmode |= FSAL_MODE_SGID;

  if(unix_mode & S_IRUSR)
    fsalmode |= FSAL_MODE_RUSR;
  if(unix_mode & S_IWUSR)
    fsalmode |= FSAL_MODE_WUSR;
  if(unix_mode & S_IXUSR)
    fsalmode |= FSAL_MODE_XUSR;

  if(unix_mode & S_IRGRP)
    fsalmode |= FSAL_MODE_RGRP;
  if(unix_mode & S_IWGRP)
    fsalmode |= FSAL_MODE_WGRP;
  if(unix_mode & S_IXGRP)
    fsalmode |= FSAL_MODE_XGRP;

  if(unix_mode & S_IROTH)
    fsalmode |= FSAL_MODE_ROTH;
  if(unix_mode & S_IWOTH)
    fsalmode |= FSAL_MODE_WOTH;
  if(unix_mode & S_IXOTH)
    fsalmode |= FSAL_MODE_XOTH;

  return fsalmode;

}
