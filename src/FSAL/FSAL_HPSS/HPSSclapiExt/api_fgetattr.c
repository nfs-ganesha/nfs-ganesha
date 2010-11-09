#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "hpssclapiext.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <u_signed64.h>
#include <api_internal.h>
#include <ss_pvlist.h>

/*
 * The BFS needs to add this somewhere
 */

#define BFS_SET_MAX (32)

/*
 * Macro to retrieve size of PV list
 */

#define SIZEOF_PVLIST(N) \
   (sizeof(pv_list_t)+((N)-1)*sizeof(pv_list_element_t))

/*
 * Local function definition
 */

static int HPSSFSAL_Common_FileGetAttributes(apithrdstate_t * ThreadContext,    /* IN - thread context */
                                             ns_ObjHandle_t * ObjHandle,        /* IN - parent object handle */
                                             char *Path,        /* IN - path to the object */
                                             api_cwd_stack_t * CwdStack,        /* IN - cwd stack */
                                             hpss_reqid_t RequestID,    /* IN - request id */
                                             unsigned32 Flags,  /* IN - flags for storage attrs */
                                             unsigned32 ChaseFlags,     /* IN - whether to chase symlinks/junctions */
                                             unsigned32 StorageLevel,   /* IN - storage level to query  */
                                             TYPE_CRED_HPSS * Ucred,    /* IN - user credentials */
                                             TYPE_TOKEN_HPSS * AuthzTicket,     /* OUT - authorization ticket */
                                             hpss_fileattr_t * AttrOut, /* OUT - attributes after query */
                                             hpss_xfileattr_t * XAttrOut);      /* OUT - xattributes after query */

/*============================================================================
 *
 * Function:    hpss_GetRawAttrHandle
 *
 * Synopsis:
 *
 * int
 * hpss_GetRawAttrHandle(
 * ns_ObjHandle_t       *ObjHandle,     ** IN - parent object handle
 * char                 *Path,          ** IN - path of file to get attributes
 * hsec_UserCred        *Ucred,         ** IN - user credentials
 * int                  traverse_junction, ** IN - boolean for junction traversal
 * ns_ObjHandle_t       *HandleOut,     ** OUT - returned object handle
 * gss_token_t          *AuthzTicket,   ** OUT - returned authorization
 * hpss_vattr_t         *AttrOut)       ** OUT - returned attributes
 *
 * Description:
 *
 *      The 'hpss_GetRawAttrHandle' function obtains information about the 
 *      symlink or the junction named by 'Path', taken relative to the 
 *      directory indicated by 'ObjHandle'.  Attributes are returned in the 
 *      area pointed to by 'AttrOut'.
 *
 * Other Inputs:
 *      None.
 *
 * Outputs:
 *              0               - No error.  Valid information returned.
 *
 * Interfaces:
 *      DCE pthreads, DCE/RPC, HPSSFSAL_Common_FileGetAttributes.
 *
 * Resources Used:
 *
 * Limitations:
 *
 * Assumptions:
 *
 * Notes:
 *
 *-------------------------------------------------------------------------*/

int HPSSFSAL_GetRawAttrHandle(ns_ObjHandle_t * ObjHandle,       /* IN - parent object handle */
                              char *Path,       /* IN - path of junction to get attributes */
                              TYPE_CRED_HPSS * Ucred,   /* IN - user credentials */
                              int traverse_junction,
                                   /** IN - boolean for junction traversal*/
                              ns_ObjHandle_t * HandleOut,       /* OUT - returned object handle */
                              TYPE_TOKEN_HPSS * AuthzTicket,    /* OUT - returned authorization */
                              hpss_Attrs_t * AttrsOut)  /* OUT - returned attributes */
{
  static char function_name[] = "hpss_GetRawAttrHandle";
  long error = 0;               /* return error */
  apithrdstate_t *threadcontext;        /* thread context */
  TYPE_CRED_HPSS *ucred_ptr;    /* user credentials */
  hpss_fileattr_t file_attrs_out;       /* file attributes out */
  hpss_reqid_t rqstid;          /* request id */

  API_ENTER(function_name);

  /*
   *  Initialize the thread if not already initialized.
   *  Get a pointer back to the thread specific context.
   */

  error = API_ClientAPIInit(&threadcontext);
  if(error != 0)
    API_RETURN(error);

  /*
   *  Check that the object handle is not NULL.
   */

  if(ObjHandle == (ns_ObjHandle_t *) NULL)
    API_RETURN(-EINVAL);

  /*
   *  Check that the pathname the string is not the NULL string.
   */

  if(Path != NULL && *Path == '\0')
    API_RETURN(-ENOENT);

  /*
   *  If user credentials were not passed, use the ones in the
   *  current thread context.
   */

  if(Ucred == (TYPE_CRED_HPSS *) NULL)
    ucred_ptr = &threadcontext->UserCred;
  else
    ucred_ptr = Ucred;

  /*
   *  Generate a unique request id.
   */

  rqstid = API_GetUniqueRequestID();

  /*
   * Call the common routine to do most of the get attribute
   * processing
   */

  error = HPSSFSAL_Common_FileGetAttributes(threadcontext,
                                            ObjHandle,
                                            Path,
                                            API_NULL_CWD_STACK,
                                            rqstid,
                                            0,
                                            (traverse_junction ? API_CHASE_JUNCTION :
                                             API_CHASE_NONE), 0, ucred_ptr, AuthzTicket,
                                            &file_attrs_out, (hpss_xfileattr_t *) NULL);

  /*
   * WE DON'T WANT TO Convert the HPSS attributes to HPSS VFS attributes !!!
   */

  if(AttrsOut != (hpss_Attrs_t *) NULL)
    {
      *AttrsOut = file_attrs_out.Attrs;
    }

  if(HandleOut != (ns_ObjHandle_t *) NULL)
    {
      *HandleOut = file_attrs_out.ObjectHandle;
    }

  API_RETURN(error);
}

/*============================================================================
 *
 * Function:    HPSSFSAL_Common_FileGetAttributes
 *
 * Synopsis:
 *
 * int
 * HPSSFSAL_Common_FileGetAttributes(
 * apithrdstate_t       *ThreadContext, ** IN - thread context
 * ns_ObjHandle_t       *ObjHandle,     ** IN - parent object handle
 * char                 *Path,          ** IN - path to the object
 * api_cwd_stack_t      *CwdStack,      ** IN - cwd stack
 * unsigned32           RequestID,      ** IN - request id
 * unsigned32           Flags,          ** IN - flags for storage attrs
 * unsigned32           ChaseFlags,     ** IN - chase symlinks/junctions ?
 * unsigned32           StorageLevel,   ** IN - storage level to query
 * hsec_UserCred_t      *Ucred,         ** IN - user credentials
 * gss_token_t          *AuthzTicket,   ** OUT - authorization ticket
 * hpss_fileattr_t      *AttrOut,       ** OUT - attributes after query
 * hpss_xfileattr_t     *XAttrOut)      ** OUT - xattributes after query
 *
 * Description:
 *
 *      The HPSSFSAL_Common_FileGetAttributes function can be used to query attributes
 *      on an entry in the name/file system that is refered to by 'ObjHandle'
 *      and 'Path'.
 *
 * Other Inputs:
 *      None.
 *
 * Outputs:
 *              0 -             No error, caller has access.
 *
 * Interfaces:
 *      DCE pthreads, DCE/RPC, API_TraversePath.
 *
 * Resources Used:
 *
 * Limitations:
 *
 * Assumptions:
 *
 * Notes:
 *
 *-------------------------------------------------------------------------*/

static int HPSSFSAL_Common_FileGetAttributes(apithrdstate_t * ThreadContext,    /* IN - thread context */
                                             ns_ObjHandle_t * ObjHandle,        /* IN - parent object handle */
                                             char *Path,        /* IN - path to the object */
                                             api_cwd_stack_t * CwdStack,        /* IN - cwd stack */
                                             hpss_reqid_t RequestID,    /* IN - request id */
                                             unsigned32 Flags,  /* IN - flags for storage attrs */
                                             unsigned32 ChaseFlags,     /* IN - chase symlinks/junctions ? */
                                             unsigned32 StorageLevel,   /* IN - storage level to query  */
                                             TYPE_CRED_HPSS * Ucred,    /* IN - user credentials */
                                             TYPE_TOKEN_HPSS * AuthzTicket,     /* OUT - authorization ticket */
                                             hpss_fileattr_t * AttrOut, /* OUT - attributes after query */
                                             hpss_xfileattr_t * XAttrOut)       /* OUT - xattributes after query */
{
  static char function_name[] = "HPSSFSAL_Common_FileGetAttributes";
#if (HPSS_MAJOR_VERSION == 5) || (HPSS_MAJOR_VERSION == 7)
  volatile long error = 0;      /* return error */
#elif (HPSS_MAJOR_VERSION == 6)
  signed32 error = 0;           /* return error */
#else
#error "Unexpected HPSS VERSION MAJOR"
#endif
  hpss_AttrBits_t select_flags; /* attribute select bits */
  hpss_AttrBits_t parent_flags; /* attribute select bits */
  TYPE_TOKEN_HPSS ta;           /* security token */
  unsigned32 xattr_options;     /* xattribute option flags */
  unsigned32 xattr_options_cnt; /* options specified */
  bf_sc_attrib_t *xattr_ptr;    /* extended information */

  API_ENTER(function_name);

  xattr_options = 0;
  xattr_ptr = NULL;

  if(XAttrOut != NULL)
    {
      memset(XAttrOut, 0, sizeof(hpss_xfileattr_t));
      xattr_options_cnt = 0;
      xattr_ptr = XAttrOut->SCAttrib;
      if((Flags & API_GET_STATS_FOR_LEVEL) != 0)
        {
          xattr_options_cnt++;
          xattr_options |= CORE_GETATTRS_STATS_FOR_LEVEL;
        }
      if((Flags & API_GET_STATS_FOR_1STLEVEL) != 0)
        {
          xattr_options_cnt++;
          xattr_options |= CORE_GETATTRS_STATS_1ST_LEVEL;
        }
      if((Flags & API_GET_STATS_OPTIMIZE) != 0)
        {
          xattr_options_cnt++;
          xattr_options |= CORE_GETATTRS_STATS_OPTIMIZE;
        }
      if((Flags & API_GET_STATS_FOR_ALL_LEVELS) != 0)
        {
          xattr_options_cnt++;
          xattr_options |= CORE_GETATTRS_STATS_ALL_LEVELS;
        }
      if(xattr_options_cnt > 1)
        {
          return (-EINVAL);
        }
#if ( HPSS_LEVEL >= 622 )
      if((Flags & API_GET_XATTRS_NO_BLOCK) != 0)
        xattr_options |= CORE_GETATTRS_NO_BLOCK;
#endif
    }

  /*
   *  Now get the requested attributes
   */

  (void)memset(&ta, 0, sizeof(ta));
  (void)memset(AttrOut, 0, sizeof(*AttrOut));
  (void)memset(&select_flags, 0, sizeof(select_flags));
  (void)memset(&parent_flags, 0, sizeof(parent_flags));

  select_flags = API_AddAllRegisterValues(MAX_CORE_ATTR_INDEX);

  error = API_TraversePath(ThreadContext,
                           RequestID,
                           Ucred,
                           ObjHandle,
                           Path,
                           CwdStack,
                           ChaseFlags,
                           xattr_options,
                           StorageLevel,
                           select_flags,
                           parent_flags,
                           API_NULL_CWD_STACK,
                           &AttrOut->ObjectHandle, &AttrOut->Attrs, NULL, NULL,
#if ( HPSS_MAJOR_VERSION < 7 )
                           &ta,
#endif
                           NULL, xattr_ptr);

  if(error != 0)
    {
      API_DEBUG_FPRINTF(DebugFile, &RequestID,
                        "%s: Could not get attributes, error=%d\n", function_name, error);
    }
  else if(XAttrOut != NULL)
    {
      XAttrOut->ObjectHandle = AttrOut->ObjectHandle;
      XAttrOut->Attrs = AttrOut->Attrs;
    }

  /*
   *  If everything completed successfully, go ahead and copy
   *  the authorization ticket that we received
   */

  if(error == 0 && AuthzTicket != (TYPE_TOKEN_HPSS *) NULL)
    {
      *AuthzTicket = ta;
    }

  return (error);
}

/*============================================================================
 *
 * Function:    hpss_FileGetXAttributes
 *
 * Synopsis:
 *
 * int
 * hpss_FileGetXAttributes(
 * char                 *Path,          ** IN - path to the object
 * unsigned32           Flags,          ** IN - flags for storage attrs
 * unsigned32           StorageLevel,   ** IN - storage level to query
 * hpss_xfileattr_t     *AttrOut);      ** OUT - attributes after query
 *
 * Description:
 *
 *      The hpss_FileGetXAttributes function can be used to query attributes
 *      on an entry in the name/file system that is refered to by 'Path'.
 *      Additional Flags and StorageLevel parms allow getting storage attrs.
 *
 * Other Inputs:
 *      None.
 *
 * Outputs:
 *              0 -             No error, caller has access.
 *
 * Interfaces:
 *      Common_FileGetAttributes
 *
 * Resources Used:
 *
 * Limitations:
 *
 * Assumptions:
 *
 * Notes:
 *
 *-------------------------------------------------------------------------*/

/* This function is provided from HPSS 6.2.2 */
#if ( HPSS_LEVEL < 622 )
int HPSSFSAL_FileGetXAttributesHandle(ns_ObjHandle_t * ObjHandle,       /* IN - object handle */
                                      unsigned32 Flags, /* IN - flags for storage attrs */
                                      unsigned32 StorageLevel,  /* IN - level to query     */
                                      hpss_xfileattr_t * AttrOut)       /* OUT - attributes after query */
{
  static char function_name[] = "hpss_FileGetXAttributes";
  volatile long error = 0;      /* return error */
  hpss_reqid_t rqstid;          /* request id */
  int i, j;                     /* array indices */
  char *newpath;                /* new path */
  hpss_fileattr_t file_attrs;   /* NS object attributes */
  apithrdstate_t *threadcontext;        /* thread context */

  API_ENTER(function_name);

  /*
   *  Initialize the thread if not already initialized.
   *  Get a pointer back to the thread specific context.
   */

  error = API_ClientAPIInit(&threadcontext);
  if(error != 0)
    API_RETURN(error);

  /*
   *  Check that the return attribute pointer is not NULL.
   */

  if(AttrOut == (hpss_xfileattr_t *) NULL)
    API_RETURN(-EFAULT);

  /*
   *  Generate a unique request id.
   */

  rqstid = API_GetUniqueRequestID();

  /*
   * Call the common routine to do most of the get attribute
   * processing
   */

  error = HPSSFSAL_Common_FileGetAttributes(threadcontext, ObjHandle, NULL,
#if ( HPSS_MAJOR_VERSION < 7 )
                                            threadcontext->CwdStackPtr,
#else
                                            API_NULL_CWD_STACK,
#endif
                                            rqstid,
                                            Flags,
                                            API_CHASE_ALL,
                                            StorageLevel,
                                            &threadcontext->UserCred,
                                            NULL, &file_attrs, AttrOut);
  error = Common_FileGetAttributes(threadcontext,
                                   ObjHandle,
                                   Path,
                                   API_NULL_CWD_STACK,
                                   rqstid,
                                   Flags,
                                   API_CHASE_ALL,
                                   StorageLevel, ucred_ptr, &file_attrs, AttrOut);

  if(error != 0)
    xdr_free((xdrproc_t) xdr_bf_sc_attrib_t, (void *)AttrOut->SCAttrib);

  API_RETURN(error);

}
#endif
