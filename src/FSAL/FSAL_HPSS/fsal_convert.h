/**
 *
 * \file    fsal_convert.h
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/27 14:15:57 $
 * \version $Revision: 1.13 $
 * \brief   HPSS to FSAL type converting function.
 *
 */
#ifndef _FSAL_CONVERTION_H
#define _FSAL_CONVERTION_H

#include "fsal.h"

/* convert error codes */
int hpss2fsal_error(int hpss_errorcode);

/** converts an fsal open flag to an hpss open flag. */
int fsal2hpss_openflags(fsal_openflags_t fsal_flags, int *p_hpss_flags);

/* Fills an FSAL attributes struct
 * using hpss handle and hpss attributes.
 */
fsal_status_t hpss2fsal_attributes(ns_ObjHandle_t * p_hpss_handle_in,
                                   hpss_Attrs_t * p_hpss_attr_in,
                                   fsal_attrib_list_t * p_fsalattr_out);

/*
 * 
 */
fsal_status_t hpssHandle2fsalAttributes(ns_ObjHandle_t * p_hpsshandle_in,
                                        fsal_attrib_list_t * p_fsalattr_out);

/** converts HPSS access mode to FSAL mode */
fsal_accessmode_t hpss2fsal_mode(unsigned32 uid_bit,
                                 unsigned32 gid_bit,
                                 unsigned32 sticky_bit,
                                 unsigned32 user_perms,
                                 unsigned32 group_perms, unsigned32 other_perms);

/** converts HPSS access mode to FSAL mode */
void fsal2hpss_mode(fsal_accessmode_t fsal_mode,
#if HPSS_MAJOR_VERSION < 7
                    unsigned32 * uid_bit, unsigned32 * gid_bit, unsigned32 * sticky_bit,
#else
                    unsigned32 * mode_perms,
#endif
                    unsigned32 * user_perms,
                    unsigned32 * group_perms, unsigned32 * other_perms);

/** converts FSAL access mode to unix mode. */
mode_t fsal2unix_mode(fsal_accessmode_t fsal_mode);

/** converts unix access mode to fsal mode. */
fsal_accessmode_t unix2fsal_mode(mode_t unix_mode);

/** converts hpss object type to fsal object type. */
fsal_nodetype_t hpss2fsal_type(unsigned32 hpss_type_in);

/** converts hpss 64 bits integer to fsal 64 bits. */
fsal_u64_t hpss2fsal_64(u_signed64 hpss_size_in);

/** converts fsal 64 bits to hpss 64 bits integer. */
u_signed64 fsal2hpss_64(fsal_u64_t fsal_size_in);

/** converts hpss fsid to fsal FSid. */
fsal_fsid_t hpss2fsal_fsid(u_signed64 hpss_fsid_in);

/** converts fsal attrib list to hpss attrib list and attributes values */
fsal_status_t fsal2hpss_attribset(fsal_handle_t * p_fsal_handle,
                                  fsal_attrib_list_t * p_attrib_set,
                                  hpss_fileattrbits_t * p_hpss_attrmask,
                                  hpss_Attrs_t * p_hpss_attrs);

/**
 * hpss2fsal_time:
 * Convert HPSS time structure (timestamp_sec_t)
 * to FSAL time type (fsal_time_t).
 */
fsal_time_t hpss2fsal_time(timestamp_sec_t tsec);

/**
 * fsal2hpss_time:
 * Converts FSAL time structure (fsal_time_t)
 * to HPSS time type (timestamp_sec_t).
 */
#define fsal2hpss_time(_time_) ((timestamp_sec_t)(_time_).seconds)

#endif
