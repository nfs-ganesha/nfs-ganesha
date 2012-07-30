/**
 *
 * \file    hpssclapiext.h
 * \date    $Date: 2006/01/17 14:20:07 $
 * \version $Revision: 1.7 $
 * \brief   This module is an extension of the HPSS client API.
 *
 */

#include <hpss_types.h>
#include <hpss_api.h>
#include <hpss_version.h>

#ifndef _HPSSCLAPIEXT_H
#define _HPSSCLAPIEXT_H

#if HPSS_MAJOR_VERSION == 5

#define TYPE_CRED_HPSS  hsec_UserCred_t
#define TYPE_TOKEN_HPSS  gss_token_t
#define TYPE_UUID_HPSS  uuid_t

#elif HPSS_MAJOR_VERSION == 6

#define TYPE_CRED_HPSS  sec_cred_t
#define TYPE_TOKEN_HPSS  hpss_authz_token_t
#define TYPE_UUID_HPSS  hpss_uuid_t

#elif HPSS_MAJOR_VERSION == 7

#define TYPE_CRED_HPSS  sec_cred_t
typedef void *TYPE_TOKEN_HPSS;
#define TYPE_UUID_HPSS  hpss_uuid_t

#else
#error "Unexpected HPSS VERSION"
#endif

int HPSSFSAL_SymlinkHandle(ns_ObjHandle_t * ObjHandle,  /* IN - Handle of existing file */
                           char *Contents,      /* IN - Desired contents of the link */
                           char *Path,  /* IN - New name of the symbolic link */
                           TYPE_CRED_HPSS * Ucred,      /* IN - pointer to user credentials */
                           ns_ObjHandle_t * HandleOut,  /* OUT - Handle of crfeated link */
                           hpss_Attrs_t * AttrsOut);    /* OUT - symbolic link attributes */

int HPSSFSAL_MkdirHandle(ns_ObjHandle_t * ObjHandle,    /* IN - handle of parent directory */
                         char *Path,    /* IN - path of directory */
                         mode_t Mode,   /* IN - perm bits for new directory */
                         TYPE_CRED_HPSS * Ucred,        /* IN - user credentials */
                         ns_ObjHandle_t * HandleOut,    /* OUT - returned object handle */
                         hpss_Attrs_t * AttrsOut);      /* OUT - returned attributes */

int HPSSFSAL_GetRawAttrHandle(ns_ObjHandle_t * ObjHandle,       /* IN - parent object handle */
                              char *Path,       /* IN - path of junction to get attributes */
                              TYPE_CRED_HPSS * Ucred,   /* IN - user credentials */
                              int traverse_junction,
                                   /** IN - boolean for junction traversal*/
                              ns_ObjHandle_t * HandleOut,       /* OUT - returned object handle */
                              TYPE_TOKEN_HPSS * AuthzTicket,    /* OUT - returned authorization */
                              hpss_Attrs_t * AttrsOut); /* OUT - returned attributes */

#if (HPSS_MAJOR_VERSION < 7)
int HPSSFSAL_FileSetAttrHandle(ns_ObjHandle_t * ObjHandle,      /* IN  - parent object handle */
                               char *Path,      /* IN  - path to the object */
                               TYPE_CRED_HPSS * Ucred,  /* IN  - user credentials */
                               hpss_fileattrbits_t SelFlags,    /* IN - attributes fields to set */
                               hpss_fileattr_t * AttrIn,        /* IN  - input attributes */
                               hpss_fileattr_t * AttrOut);      /* OUT - attributes after change */
#else
#define HPSSFSAL_FileSetAttrHandle hpss_FileSetAttributesHandle
#endif

int HPSSFSAL_OpenHandle(ns_ObjHandle_t * ObjHandle,     /* IN - Parent object handle */
                        char *Path,     /* IN - Path to file to be opened */
                        int Oflag,      /* IN - Type of file access */
                        mode_t Mode,    /* IN - Desired file perms if create */
                        TYPE_CRED_HPSS * Ucred, /* IN - User credentials */
                        hpss_cos_hints_t * HintsIn,     /* IN - Desired class of service */
                        hpss_cos_priorities_t * HintsPri,       /* IN - Priorities of hint struct */
                        hpss_cos_hints_t * HintsOut,    /* OUT - Granted class of service */
                        hpss_Attrs_t * AttrsOut,        /* OUT - returned attributes */
                        ns_ObjHandle_t * HandleOut,     /* OUT - returned handle */
                        TYPE_TOKEN_HPSS * AuthzTicket); /* OUT - Client authorization */

int HPSSFSAL_CreateHandle(ns_ObjHandle_t * ObjHandle,   /* IN - Parent object handle */
                          char *Path,   /* IN - Path to file to be created */
                          mode_t Mode,  /* IN - Desired file perms */
                          TYPE_CRED_HPSS * Ucred,       /* IN - User credentials */
                          hpss_cos_hints_t * HintsIn,   /* IN - Desired class of service */
                          hpss_cos_priorities_t * HintsPri,     /* IN - Priorities of hint struct */
                          hpss_cos_hints_t * HintsOut,  /* OUT - Granted class of service */
                          hpss_Attrs_t * AttrsOut,      /* OUT - returned attributes */
                          ns_ObjHandle_t * HandleOut,   /* OUT - returned handle */
                          TYPE_TOKEN_HPSS * AuthzTicket);       /* OUT - Client authorization */

int HPSSFSAL_ReadRawAttrsHandle(ns_ObjHandle_t * ObjHandle,     /* IN - directory object handle */
                                u_signed64 OffsetIn,    /* IN - directory position */
                                sec_cred_t * Ucred,     /* IN - user credentials */
                                unsigned32 BufferSize,  /* IN - size of output buffer */
                                unsigned32 GetAttributes,       /* IN - get object attributes? */
                                unsigned32 IgnInconstitMd,      /* IN - ignore in case of inconstitent MD */
                                unsigned32 * End,       /* OUT - hit end of directory */
                                u_signed64 * OffsetOut, /* OUT - resulting directory position */
                                ns_DirEntry_t * DirentPtr);     /* OUT - directory entry information */

#if (HPSS_LEVEL < 622)
int HPSSFSAL_FileGetXAttributesHandle(ns_ObjHandle_t * ObjHandle,       /* IN - object handle */
                                      unsigned32 Flags, /* IN - flags for storage attrs */
                                      unsigned32 StorageLevel,  /* IN - level to query     */
                                      hpss_xfileattr_t * AttrOut);      /* OUT - attributes after query */
#endif

#endif
