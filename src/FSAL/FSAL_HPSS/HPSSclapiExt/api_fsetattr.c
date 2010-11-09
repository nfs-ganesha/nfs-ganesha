#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <u_signed64.h>
#include <hpssclapiext.h>
#include <api_internal.h>
#include <acct_av_lib.h>

#define BFS_SET_MAX (32)

/* these functions are working in HPSS 7.x */
#if (HPSS_MAJOR_VERSION < 7)

/*
 * Local function definition
 */

static int HPSSFSAL_Common_FileSetAttributes(apithrdstate_t * ThreadContext,    /* IN - thread context */
                                             ns_ObjHandle_t * ObjHandle,        /* IN - parent object handle */
                                             char *Path,        /* IN - path to the object */
                                             api_cwd_stack_t * CwdStack,        /* IN - cwd stack */
                                             hpss_reqid_t RequestID,    /* IN - request id */
                                             TYPE_CRED_HPSS * Ucred,    /* IN - user credentials */
                                             unsigned32 ChaseFlags,     /* IN - chase symlinks/junctions */
                                             hpss_fileattrbits_t SelFlagsIn,    /* IN - attributes fields to set */
                                             hpss_fileattr_t * AttrIn,  /* IN - input attributes */
                                             hpss_fileattrbits_t * SelFlagsOut, /* OUT - attributes fields set */
                                             hpss_fileattr_t * AttrOut);        /* OUT - attributes after change */

/*============================================================================
 *
 * Function:    HPSSFSAL_FileSetAttrHandle
 *
 * Synopsis:
 *
 * int
 * HPSSFSAL_FileSetAttrHandle(
 * ns_ObjHandle_t       *ObjHandle,     ** IN  - parent object handle 
 * char                 *Path,          ** IN  - path to the object
 * TYPE_CRED_HPSS      *Ucred,         ** IN  - user credentials
 * hpss_fileattrbits_t  SelFlags,       ** IN - attributes fields to set
 * hpss_fileattr_t      *AttrIn,        ** IN  - input attributes
 * hpss_fileattr_t      *AttrOut)       ** OUT - attributes after change
 *
 * Description:
 *
 *      The like the hpss_FileSetAttributesHandle function
 *      except that it doesn't chase junctions nor symlinks.
 *
 * Other Inputs:
 *      None.
 *
 * Outputs:
 *              0 -             No error, caller has access.
 *
 * Interfaces:
 *      DCE pthreads, DCE/RPC, Common_FileSetAttributes
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

int HPSSFSAL_FileSetAttrHandle(ns_ObjHandle_t * ObjHandle,      /* IN  - parent object handle */
                               char *Path,      /* IN  - path to the object */
                               TYPE_CRED_HPSS * Ucred,  /* IN  - user credentials */
                               hpss_fileattrbits_t SelFlags,    /* IN - attributes fields to set */
                               hpss_fileattr_t * AttrIn,        /* IN  - input attributes */
                               hpss_fileattr_t * AttrOut)       /* OUT - attributes after change */
{
  static char function_name[] = "hpss_FileSetAttributesHandle";
  volatile long error = 0;      /* return error */
  hpss_reqid_t rqstid;          /* request id */
  TYPE_CRED_HPSS *ucred_ptr;    /* user credentials */
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
   *  Call the common routine to do most of the set attribute
   *  processing
   */

  error = HPSSFSAL_Common_FileSetAttributes(threadcontext,
                                            ObjHandle,
                                            Path,
                                            API_NULL_CWD_STACK,
                                            rqstid,
                                            ucred_ptr,
                                            API_CHASE_NONE,
                                            SelFlags, AttrIn, NULL, AttrOut);

  API_RETURN(error);
}

/*============================================================================
 *
 * Function:    Common_FileSetAttributes
 *
 * Synopsis:
 *
 * static int
 * HPSSFSAL_Common_FileSetAttributes(
 * apithrdstate_t      *ThreadContext, ** IN - thread context
 * ns_ObjHandle_t      *ObjHandle,     ** IN - parent object handle
 * char                *Path,          ** IN - path to the object
 * api_cwd_stack_t     *CwdStack,      ** IN - cwd stack
 * hpss_reqid_t        RequestID,      ** IN - request id
 * TYPE_CRED_HPSS     *Ucred,         ** IN - user credentials
 * unsigned32          ChaseFlags,     ** IN - chase symlinks/junctions
 * hpss_fileattrbits_t SelFlagsIn,     ** IN - attributes fields to set
 * hpss_fileattr_t     *AttrIn,        ** IN - input attributes
 * hpss_fileattrbits_t *SelFlagsOut,   ** OUT - attributes fields set
 * hpss_fileattr_t     *AttrOut)       ** OUT - attributes after change
 *
 *
 * Description:
 *
 *      The Common_FileSetAttributes function can be used to change attributes
 *      on an entry in the name/file system that is refered to by 'Path' and
 *      'ObjHandle'.
 *
 * Other Inputs:
 *      None.
 *
 * Outputs:
 *              0 -             No error, caller has access.
 *
 * Interfaces:
 *      DCE pthreads, DCE/RPC, API_TraversePath, API_core_SetAttrs,
 *      hpss_LocateServerByUUID, av_cli_AcctNameToIdx,
 *      av_cli_ValidateChown,  API_DetermineAcct.
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

static int HPSSFSAL_Common_FileSetAttributes(apithrdstate_t * ThreadContext,    /* IN - thread context */
                                             ns_ObjHandle_t * ObjHandle,        /* IN - parent object handle */
                                             char *Path,        /* IN - path to the object */
                                             api_cwd_stack_t * CwdStack,        /* IN - cwd stack */
                                             hpss_reqid_t RequestID,    /* IN - request id */
                                             TYPE_CRED_HPSS * Ucred,    /* IN - user credentials */
                                             unsigned32 ChaseFlags,     /* IN - chase symlinks/junctions */
                                             hpss_fileattrbits_t SelFlagsIn,    /* IN - attributes fields to set */
                                             hpss_fileattr_t * AttrIn,  /* IN - input attributes */
                                             hpss_fileattrbits_t * SelFlagsOut, /* OUT - attributes fields set */
                                             hpss_fileattr_t * AttrOut) /* OUT - attributes after change */
{
  unsigned32 call_type;         /* whether to call dmg or ns */
#if  (HPSS_MAJOR_VERSION == 5)
  volatile long error = 0;      /* return error */
#else
  signed32 error = 0;           /* return error */
#endif
  static char function_name[] = "Common_FileSetAttributes";
  ns_ObjHandle_t obj_handle;    /* object handle of object */
  ns_ObjHandle_t ret_obj_handle;        /* returned object handle */
  hpss_Attrs_t attr;            /* attributes of file */
  hpss_Attrs_t attr_parent;     /* attributes of parent dir */
  hpss_AttrBits_t return_flags; /* attribute return flags */
  hpss_AttrBits_t select_flags; /* attribute flags */
  hpss_AttrBits_t parent_flags; /* attribute flags */
  char *path_object;            /* path to file */
  TYPE_TOKEN_HPSS ta;           /* security token */
  ls_map_t ls_map;              /* location information */
  acct_rec_t acct_code;         /* new account code */
  acct_rec_t cur_acct_code;     /* current account code */
  unsigned32 acl_options;       /* ACL Options */
#if defined ( API_DMAP_SUPPORT ) && !defined (API_DMAP_GATEWAY)
  u_signed64 dmg_attr_bits;     /* DM attributes to set */
  dmg_object_attrs_t dmg_attr_in;       /* DM attributes in */
  dmg_object_attrs_t dmg_attr_out;      /* DM attributes returned */
#endif

  API_ENTER(function_name);

  /*
   *  Make sure that AttrIn and AttrOut are not NULL.
   */

  if((AttrIn == (hpss_fileattr_t *) NULL) || (AttrOut == (hpss_fileattr_t *) NULL))
    {
      return (-EFAULT);
    }

  /*
   *  We do not allow the bitfile id to be changed via this interface.
   */

  if(chkbit64m(SelFlagsIn, CORE_ATTR_BIT_FILE_ID))
    {
      return (-EINVAL);
    }

  /*
   *  If the site does uid-style accounting, the user may not set the
   *  account code.  The user may change the account code if the site
   *  uses site-style accounting.
   *
   *  However, the account code must change on the hpss side whenever the
   *  uid is changed, and the uid must change on both the dmap and hpss
   *  sides.  The gateway doesn't understand account ids.  So, when the
   *  user changes the uid, we let the gateway change it, and when he calls
   *  us back to change the uid on the hpss side we also set the account id.
   *  Since the only information we have at that point is the uid, we set
   *  the account id to a) the uid, if the site does uid-style accounting,
   *  or b) the default account for that uid, if the site does site-style
   *  accounting.
   *
   *  Therefore, even if the site does site-style accounting, we can't let
   *  the caller specify both a new uid and a new account code different
   *  from the default on the same call, because we can't pass that
   *  nondefault account code through the gateway and because setting the
   *  uid and account code must be done in one operation on the hpss side.
   *  So we disallow setting both the uid and account code on the same call.
   */

  /*
   * The statement above only applies to non-gateway clients. If
   * this is the gateway library, just ignore the account check.
   */

#if !defined(API_DMAP_GATEWAY)
  if(chkbit64m(SelFlagsIn, CORE_ATTR_ACCOUNT) && !chkbit64m(SelFlagsIn, CORE_ATTR_UID))
    {
      return (-EPERM);
    }
#endif

  /*
   *  Allocate memory for object path.
   */

  path_object = (char *)malloc(HPSS_MAX_PATH_NAME);
  if(path_object == NULL)
    {
      return (-ENOMEM);
    }

  /*
   * Clear the return structures.
   */

  (void)memset(AttrOut, 0, sizeof(hpss_fileattr_t));

  /*
   *  Find the object to which this ObjHandle and Path refer and
   *  its immediate parent directory.  The Type and BitfileId will
   *  be returned in the attributes.
   */

  (void)memset(&select_flags, 0, sizeof(select_flags));
  (void)memset(&parent_flags, 0, sizeof(parent_flags));

#if HPSS_MAJOR_VERSION < 7
  select_flags = API_AddRegisterValues(cast64m(0),
                                       CORE_ATTR_TYPE,
                                       CORE_ATTR_FILESET_ID,
                                       CORE_ATTR_FILESET_TYPE,
                                       CORE_ATTR_GATEWAY_UUID,
                                       CORE_ATTR_DM_HANDLE,
                                       CORE_ATTR_DM_HANDLE_LENGTH,
                                       CORE_ATTR_COS_ID,
                                       CORE_ATTR_USER_PERMS,
                                       CORE_ATTR_GROUP_PERMS,
                                       CORE_ATTR_OTHER_PERMS,
                                       CORE_ATTR_SET_UID,
                                       CORE_ATTR_SET_GID, CORE_ATTR_SET_STICKY, -1);

  parent_flags = API_AddRegisterValues(cast64m(0),
                                       CORE_ATTR_FILESET_ID,
                                       CORE_ATTR_FILESET_TYPE,
                                       CORE_ATTR_GATEWAY_UUID,
                                       CORE_ATTR_DM_HANDLE,
                                       CORE_ATTR_DM_HANDLE_LENGTH, CORE_ATTR_COS_ID, -1);
#else
  select_flags = API_AddRegisterValues(cast64m(0),
                                       CORE_ATTR_TYPE,
                                       CORE_ATTR_UID,
                                       CORE_ATTR_GID,
                                       CORE_ATTR_ACCOUNT,
                                       CORE_ATTR_REALM_ID,
                                       CORE_ATTR_COS_ID,
                                       CORE_ATTR_USER_PERMS,
                                       CORE_ATTR_GROUP_PERMS,
                                       CORE_ATTR_OTHER_PERMS, CORE_ATTR_MODE_PERMS, -1);

  parent_flags = API_AddRegisterValues(cast64m(0), CORE_ATTR_COS_ID, -1);
#endif

  /*
   * Zero the output structures
   */

  memset(&attr, 0, sizeof(attr));
  memset(&attr_parent, 0, sizeof(attr_parent));
  memset(&obj_handle, 0, sizeof(obj_handle));

  error = API_TraversePath(ThreadContext,
                           RequestID,
                           Ucred,
                           ObjHandle,
                           Path,
                           CwdStack,
                           ChaseFlags,
                           0,
                           0,
                           select_flags,
                           parent_flags,
                           API_NULL_CWD_STACK, &obj_handle, &attr, NULL, &attr_parent,
#if HPSS_MAJOR_VERSION < 7
                           &ta, path_object,
#else
                           NULL,
#endif
                           NULL);

  if(error != 0)
    {
      API_DEBUG_FPRINTF(DebugFile, &RequestID,
                        "%s: Could get attributes, error=%d\n", function_name, error);
    }
  else
    {
      /*
       * Check the flags in the returned handle to determine
       * if the object is the root of a fileset. In this case,
       * the object is its own parent.
       */

      if(obj_handle.Flags & NS_OH_FLAG_FILESET_ROOT)
        {
          memcpy(&attr_parent, &attr, sizeof(attr_parent));
          strcpy(path_object, ".");
        }

      /*
       *  Store the returned object handle and attributes in the
       *  hpss_fileattrs_t structure to be returned to the caller.
       */

      AttrOut->ObjectHandle = obj_handle;

      /*
       *  If being asked to changed the COS information and there
       *  is a COS specified for the fileset where the file resides,
       *  and the new COS will not match the fileset COS (I can't
       *  image how that can happen - but...), then set an error.
       */

      if(chkbit64m(SelFlagsIn, CORE_ATTR_COS_ID)
         && (attr_parent.COSId != 0) && (AttrIn->Attrs.COSId != attr_parent.COSId))
        {
          error = -EPERM;
          API_DEBUG_FPRINTF(DebugFile, &RequestID,
                            "%s: File is in a fileset with an"
                            " assigned COS.\n", function_name);
          if(path_object != NULL)
            free(path_object);
          return (error);
        }
#if ( HPSS_MAJOR_VERSION < 7 )
      /*
       *  Determine whether to call the dmg or ns based on whether the
       *  parent directory is dmap managed.
       */

#if ( HPSS_MAJOR_VERSION == 5 )
      call_type = API_DetermineCall(attr_parent.FilesetType, (long *)&error);
#elif ( HPSS_MAJOR_VERSION == 6 )
      call_type = API_DetermineCall(attr_parent.FilesetType, &error);
#endif

      if(call_type == API_CALL_DMG)
        {

#if defined ( API_DMAP_SUPPORT ) && !defined ( API_DMAP_GATEWAY )

          /*
           *  The parent is dmap-managed.  Call the dmap gateway.
           *
           *  Only the fields checked below may be changed by a call to
           *  dmg_hp_setattrs.  But the user may want to set other fields
           *  in the object on the same call.  Therefore, the client api
           *  set attributes calls work a little differently than other
           *  calls.  They first call the gateway to set the attributes it
           *  recognizes; it will set them on the dmap side and then make
           *  a call back to hpss to set them here.  If the caller specified
           *  additional attributes which the dmap doesn't recognize, the
           *  hpss set attributes routines will then call the local ns and
           *  bfs to set just those.
           *
           *  After we've set these fields via the gateway, we remove the
           *  bits from the ns and bfs selection flags so we won't ask the
           *  ns and bfs to set them again.
           */

          memset(&dmg_attr_in, 0, sizeof(dmg_attr_in));
          memset(&dmg_attr_out, 0, sizeof(dmg_attr_out));

          /* Set the dmg attributes from the existing NS attrs - 1599 */
          dmg_attr_in.Attrs.Attrs = attr;
          dmg_attr_in.Attrs.ObjectHandle = obj_handle;
          dmg_attr_bits = cast64m(0);
          acl_options = 0;

          /*
           * 1599 -
           * Now, depending on what attribute flags were set,
           * overwrite the corresponding attribute object fields
           * with the information supplied by the user.
           */

          /*
           * Owner
           */

          if(chkbit64m(SelFlagsIn, CORE_ATTR_UID))
            {
              /*
               *  The dmg doesn't know anything about the account id, yet
               *  we must change it in both the ns and bfs if we are changing
               *  the owner.  That's okay.  Call the dmg to change the owner.
               *  It will call us back and we'll follow the logic to change
               *  the owner from the hpss side.  At that point, we'll catch
               *  the uid change and make the corresponding account change
               *  on the hpss side.
               */

              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_OWNER));
              dmg_attr_in.Attrs.Attrs.UID = AttrIn->Attrs.UID;
            }

          /*
           * Group
           */

          if(chkbit64m(SelFlagsIn, CORE_ATTR_GID))
            {
              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_GROUP));

              dmg_attr_in.Attrs.Attrs.GID = AttrIn->Attrs.GID;
            }

          /*
           * Permissions
           */

          if(chkbit64m(SelFlagsIn, CORE_ATTR_USER_PERMS))
            {
              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_MODE));
              dmg_attr_in.Attrs.Attrs.UserPerms = AttrIn->Attrs.UserPerms;
            }
          if(chkbit64m(SelFlagsIn, CORE_ATTR_GROUP_PERMS))
            {
              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_MODE));
              dmg_attr_in.Attrs.Attrs.GroupPerms = AttrIn->Attrs.GroupPerms;
            }
          if(chkbit64m(SelFlagsIn, CORE_ATTR_OTHER_PERMS))
            {
              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_MODE));
              dmg_attr_in.Attrs.Attrs.OtherPerms = AttrIn->Attrs.OtherPerms;
            }
          if(chkbit64m(SelFlagsIn, CORE_ATTR_SET_UID))
            {
              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_MODE));
              dmg_attr_in.Attrs.Attrs.SetUIDBit = AttrIn->Attrs.SetUIDBit;
            }
          if(chkbit64m(SelFlagsIn, CORE_ATTR_SET_GID))
            {
              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_MODE));
              dmg_attr_in.Attrs.Attrs.SetGIDBit = AttrIn->Attrs.SetGIDBit;
            }
          if(chkbit64m(SelFlagsIn, CORE_ATTR_SET_STICKY))
            {
              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_MODE));
              dmg_attr_in.Attrs.Attrs.SetStickyBit = AttrIn->Attrs.SetStickyBit;
            }

          /*
           * Modification/Access times
           */

          if(chkbit64m(SelFlagsIn, CORE_ATTR_TIME_LAST_READ))
            {
              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_UTIME));
              dmg_attr_in.Attrs.Attrs.TimeLastRead = AttrIn->Attrs.TimeLastRead;
            }

          if(chkbit64m(SelFlagsIn, CORE_ATTR_TIME_LAST_WRITTEN))
            {
              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_UTIME));
              dmg_attr_in.Attrs.Attrs.TimeLastWritten = AttrIn->Attrs.TimeLastWritten;
            }

          /*
           * Size
           */

          if(chkbit64m(SelFlagsIn, CORE_ATTR_DATA_LENGTH))
            {
              dmg_attr_bits = bld64m(high32m(dmg_attr_bits),
                                     (low32m(dmg_attr_bits) | CHANGE_FILESIZE));
              dmg_attr_in.Attrs.Attrs.DataLength = AttrIn->Attrs.DataLength;
            }

          error = API_dmg_SetAttrs(ThreadContext,
                                   RequestID,
                                   Ucred,
                                   &attr_parent.GatewayUUID,
                                   attr_parent.FilesetId,
                                   attr_parent.DMHandle,
                                   attr_parent.DMHandleLength,
                                   path_object,
                                   dmg_attr_bits,
                                   &dmg_attr_in, acl_options, &dmg_attr_out);

          if(error != 0)
            {
              API_DEBUG_FPRINTF(DebugFile, &RequestID,
                                "%s: API_dmg_SetAttrs() failed,"
                                " error=%d.\n", function_name, error);
            }
          else
            {
              hpss_AttrBits_t attr_flags;

              /*
               *  Save the hpss portion of the returned dmg attributes in
               *  the structure to be returned to the caller.
               */

              AttrOut->Attrs = dmg_attr_out.Attrs.Attrs;

              /*
               *  Now turn off any of these bits in the input selection flags,
               *  so if we have to call the hpss side below to set other 
               *  fields, we won't reset these.
               */

              attr_flags = API_AddRegisterValues(cast64m(0),
                                                 CORE_ATTR_UID,
                                                 CORE_ATTR_GID,
                                                 CORE_ATTR_USER_PERMS,
                                                 CORE_ATTR_GROUP_PERMS,
                                                 CORE_ATTR_OTHER_PERMS,
                                                 CORE_ATTR_SET_UID,
                                                 CORE_ATTR_SET_GID,
                                                 CORE_ATTR_SET_STICKY,
                                                 CORE_ATTR_TIME_LAST_READ,
                                                 CORE_ATTR_TIME_LAST_WRITTEN,
                                                 CORE_ATTR_DATA_LENGTH, -1);

              SelFlagsIn = and64(SelFlagsIn, not64(attr_flags));
            }
#else
          error = EACCES;
          API_DEBUG_FPRINTF(DebugFile, &RequestID,
                            "%s: No dmap support compiled in.\n", function_name);
#endif
        }                       /* end "if call_type == DMG" */
#endif                          /* end of version < 7 */

      if(error == 0)
        {
          /*
           *  In most calls that might have to call the gateway, there would
           *  be an "else" clause here; we would call the gateway or the hpss
           *  side but not both.  For set attributes routines, however,
           *  we proceed with this code unconditionally.  If we did already
           *  call the gateway, we might still have attributes to set 
           *  which the gateway doesn't recognize.  If we didn't call the
           *  gateway, because call_type was API_CALL_HPSS, then now's the
           *  time to do it.
           *
           *  First set the returned bits structures.
           */

          return_flags = SelFlagsIn;

          /*
           *  Next set things up to handle any account code changes:
           *
           *  If the UID is changed, then the account id must also change.
           *
           *  Only root is allowed to change the UID, but we don't check for
           *  that here; we just pass it through to the name server and let
           *  him check the security.  
           *
           *  If the account code changes, either because the use
           *  specifically requested it or because the UID changed, it 
           *  must change in both the name and the bitfile attributes
           *  and must be changed to the same value in both.
           *
           *  Since we don't allow users to specify both an account code
           *  change and a uid change in the same call, by the time we
           *  get here only one selection bit or the other might be set,
           *  but not both.
           */

          if(chkbit64m(SelFlagsIn, CORE_ATTR_UID))
            {
              /*
               * Do account validation -
               * First, get the Core Server's site id.
               */

              error = hpss_LocateServerByUUID(RequestID,
                                              obj_handle.CoreServerUUID, &ls_map);
              if(error != 0)
                {
                  API_DEBUG_FPRINTF(DebugFile, &RequestID,
                                    "%s: Could not get location, error=%d\n",
                                    function_name, error);
                }
              else
                {
                  /*
                   * Get the user's current session account code.
                   */

                  error = API_DetermineAcct(Ucred,
                                            ThreadContext,
                                            obj_handle.CoreServerUUID,
                                            RequestID, &ls_map.SiteId, &cur_acct_code);
                  if(error != 0)
                    {
                      API_DEBUG_FPRINTF(DebugFile, &RequestID,
                                        "%s: couldn't determine"
                                        " account code, error= %d\n",
                                        function_name, error);
                    }
                  else
                    {
                      /*
                       * Ask Account Validation for the account code
                       * to use by passing in the file's old and new
                       * attributes and the user's current session account.
                       */

#if HPSS_MAJOR_VERSION == 5
                      error = av_cli_ValidateChown(ls_map.SiteId,
                                                   RequestID,
                                                   attr.CellId,
                                                   attr.UID,
                                                   attr.GID,
                                                   attr.Account,
                                                   attr.CellId,
                                                   AttrIn->Attrs.UID,
                                                   attr.GID, cur_acct_code, &acct_code);
#elif (HPSS_MAJOR_VERSION == 6)||(HPSS_MAJOR_VERSION == 7)
                      error = av_cli_ValidateChown(ls_map.SiteId,
                                                   RequestID,
                                                   attr.RealmId,
                                                   attr.UID,
                                                   attr.GID,
                                                   attr.Account,
                                                   attr.RealmId,
                                                   AttrIn->Attrs.UID,
                                                   attr.GID, cur_acct_code, &acct_code);
#endif

                      if(error != 0)
                        {
                          API_DEBUG_FPRINTF(DebugFile, &RequestID,
                                            "%s: av_cli_ValidateChown"
                                            " failed using the default"
                                            " account code, error=%d\n",
                                            function_name, error);
                        }
                    }
                }

              if(error == 0)
                {
                  AttrIn->Attrs.Account = acct_code;
                  SelFlagsIn = orbit64m(SelFlagsIn, CORE_ATTR_ACCOUNT);
                }

            }
          else if(chkbit64m(SelFlagsIn, CORE_ATTR_ACCOUNT))
            {
              /*
               * User is setting account id, but not uid. 
               */

              /*
               * Do account validation -
               * First, get the Core Server's site id.
               */

              error = hpss_LocateServerByUUID(RequestID,
                                              obj_handle.CoreServerUUID, &ls_map);
              if(error != 0)
                {
                  API_DEBUG_FPRINTF(DebugFile, &RequestID,
                                    "%s: Could not get location, error=%d\n",
                                    function_name, error);
                }
              else
                {
                  /* 
                   * Validate that the account code can be changed.
                   */

#if HPSS_MAJOR_VERSION == 5
                  error = av_cli_ValidateChacct(ls_map.SiteId,
                                                RequestID,
                                                Ucred->DCECellId,
                                                Ucred->SecPWent.Uid,
                                                attr.CellId,
                                                attr.UID,
                                                attr.GID,
                                                attr.Account,
                                                AttrIn->Attrs.Account, &acct_code);
#elif (HPSS_MAJOR_VERSION == 6)||(HPSS_MAJOR_VERSION == 7)
                  error = av_cli_ValidateChacct(ls_map.SiteId,
                                                RequestID,
                                                Ucred->RealmId,
                                                Ucred->Uid,
                                                attr.RealmId,
                                                attr.UID,
                                                attr.GID,
                                                attr.Account,
                                                AttrIn->Attrs.Account, &acct_code);
#endif
                  if(error != 0)
                    {
                      API_DEBUG_FPRINTF(DebugFile, &RequestID,
                                        "%s: av_cli_ValidateChacct failed."
                                        " using the account code %d,"
                                        " error=%d\n", function_name,
                                        AttrIn->Attrs.Account, error);
                    }
                }
            }

          if(error == 0)
            {
              (void)memset(AttrOut, 0, sizeof(*AttrOut));

              error = API_core_SetAttrs(ThreadContext,
                                        RequestID,
                                        Ucred,
                                        &obj_handle,
                                        NULL,
                                        SelFlagsIn,
                                        &AttrIn->Attrs, return_flags, &AttrOut->Attrs);

              if(error != 0)
                {
                  API_DEBUG_FPRINTF(DebugFile, &RequestID,
                                    "%s: Could not set attributes,"
                                    " error=%d\n", function_name, error);
                }
            }
        }
    }

  /*
   * Free the locally used path name
   */

  if(path_object != NULL)
    free(path_object);

  /*
   *  If the user wanted them, return the out bits.
   *  Note that these are always 0 if we called the
   *  dmg; it doesn't return any out bits.
   */

  if(SelFlagsOut)
    *SelFlagsOut = return_flags;

  return (error);

}

#endif                          /* hpss 7+ */
