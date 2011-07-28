#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <u_signed64.h>
#include <hpssclapiext.h>
#include <api_internal.h>
#include <acct_av_lib.h>

/*
 * Prototype(s) for static routines.
 */

static int
HPSSFSAL_Common_Mkdir(apithrdstate_t * ThreadContext,
                      ns_ObjHandle_t * ObjHandle,
                      char *Path,
                      api_cwd_stack_t * CwdStack,
                      int Mode,
                      TYPE_CRED_HPSS * Ucred,
                      ns_ObjHandle_t * RetObjHandle, hpss_Attrs_t * RetAttrs);

/*============================================================================
 *
 * Function:    hpss_MkdirHandle
 *
 * Synopsis:
 *
 * int
 * HPSSFSAL_MkdirHandle(
 * ns_ObjHandle_t    *ObjHandle,    ** IN - handle of parent directory
 * char              *Path,         ** IN - path of directory
 * mode_t            Mode,          ** IN - perm bits for new directory
 * hsec_UserCred_t   *Ucred,        ** IN - user credentials
 * ns_ObjHandle_t    *HandleOut,    ** OUT - returned object handle
 * hpss_Attrs_t      *AttrsOut)      ** OUT - returned VFS attributes
 *
 * Description:
 *
 *    The 'hpss_MkdirHandle' function creates a new directory with the name
 *    'Path', taken relative to the directory indicated by 'ObjHandle'.
 *    The directory permission bits of the new directory are initialized by
 *    'Mode', and modified by the file creation mask of the thread.  The
 *    newly created directory's object handle and attributes are
 *    returned in the areas pointed to by 'RetObjHandle' and
 *    'RetVAttrs', respectively.
 *
 * Other Inputs:
 *    None.
 *
 * Outputs:
 *    0 -        No error. New directory created.
 *
 * Interfaces:
 *    DCE pthreads, DCE/RPC, HPSSFSAL_Common_Mkdir.
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

int HPSSFSAL_MkdirHandle(ns_ObjHandle_t * ObjHandle,    /* IN - handle of parent directory */
                         char *Path,    /* IN - path of directory */
                         mode_t Mode,   /* IN - perm bits for new directory */
                         TYPE_CRED_HPSS * Ucred,        /* IN - user credentials */
                         ns_ObjHandle_t * HandleOut,    /* OUT - returned object handle */
                         hpss_Attrs_t * AttrsOut)       /* OUT - returned attributes */
{
  long error = 0;
  apithrdstate_t *threadcontext;
  TYPE_CRED_HPSS *ucred_ptr;

  API_ENTER("hpss_MkdirHandle");

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
   *  Check that there is a name for the new object
   */

  if(Path == NULL)
    API_RETURN(-EFAULT);

  if(*Path == '\0')
    API_RETURN(-ENOENT);

  /*
   *  If user credentials were not passed, use the ones in the
   *  current thread context.
   */

  if(Ucred == (TYPE_CRED_HPSS *) NULL)
    ucred_ptr = &threadcontext->UserCred;
  else
    ucred_ptr = Ucred;

  error = HPSSFSAL_Common_Mkdir(threadcontext,
                                ObjHandle,
                                Path,
                                API_NULL_CWD_STACK, Mode, ucred_ptr, HandleOut, AttrsOut);

  API_RETURN(error);
}

/*============================================================================
 *
 * Function:    HPSSFSAL_Common_Mkdir
 *
 * Synopsis:
 *
 * static int
 * HPSSFSAL_Common_Mkdir(
 * apithrdstate_t    *ThreadContext,    ** IN - thread context
 * ns_ObjHandle_t    *ObjHandle,        ** IN - handle of parent directory
 * char              *Path,             ** IN - path of directory
 * mode_t            Mode,              ** IN - perm bits for new directory
 * hsec_UserCred_t   *Ucred,            ** IN - user credentials
 * ns_ObjHandle_t    *RetObjHandle,     ** OUT - returned object handle
 * hpss_Attrs_t      *RetAttrs)        ** OUT - returned attributes
 *
 * Description:
 *
 *    The 'HPSSFSAL_Common_Mkdir' function performs the common processing for
 *    hpss_Mkdir, hpss_MkdirHandle, and hpss_MkdirDMHandle functions.
 *
 * Other Inputs:
 *    None.
 *
 * Outputs:
 *    0 -        No error. New directory created.
 *
 * Interfaces:
 *    DCE pthreads, DCE/RPC, API_core_MkDir, API_ConvertPosixModeToMode,
 *    API_TraversePath, API_DetermineAcct, av_cli_ValidateCreate,
 *    API_AddRegisterValues
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

static int
HPSSFSAL_Common_Mkdir(apithrdstate_t * ThreadContext,
                      ns_ObjHandle_t * ObjHandle,
                      char *Path,
                      api_cwd_stack_t * CwdStack,
                      int Mode,
                      TYPE_CRED_HPSS * Ucred,
                      ns_ObjHandle_t * RetObjHandle, hpss_Attrs_t * RetAttrs)
{
#if HPSS_MAJOR_VERSION < 7
  call_type_t call_type;
#endif
#if HPSS_MAJOR_VERSION == 5
  volatile long error = 0;      /* return error */
#else
  signed32 error = 0;           /* return error */
#endif
  static char function_name[] = "HPSSFSAL_Common_Mkdir";
  ns_ObjHandle_t objhandle_parent;
  ns_ObjHandle_t objhandle_newdir;
  hpss_Attrs_t attr_parent;
  hpss_Attrs_t attr_newdir_in;
  hpss_Attrs_t attr_newdir_out;
  char *path_parent;
  char *path_newdir;
  retry_cb_t retry_cb;
  hpss_reqid_t rqstid;
  hpss_AttrBits_t select_flags;
  hpss_AttrBits_t update_flags;
  acct_rec_t new_acct_code;
  acct_rec_t temp_acct_code;
  TYPE_UUID_HPSS siteId;
  TYPE_TOKEN_HPSS ta;
#if defined ( API_DMAP_SUPPORT ) && !defined ( API_DMAP_GATEWAY )
  byte dm_handle[MAX_DMEPI_HANDLE_SIZE];
  unsigned32 dm_handle_length;
#endif

  API_ENTER(function_name);

  /*
   * Break the path into a path and a name, so that
   * we can get information about the parent.
   */

  path_parent = malloc(HPSS_MAX_PATH_NAME);
  if(path_parent == NULL)
    {
      return (-ENOMEM);
    }

  path_newdir = malloc(HPSS_MAX_PATH_NAME);
  if(path_newdir == NULL)
    {
      free(path_parent);
      return (-ENOMEM);
    }

  error = API_DivideFilePath(Path, path_parent, path_newdir);

  if(error != 0)
    {
      free(path_parent);
      free(path_newdir);
      return (error);
    }

  /*
   *  Get a valid request id.
   */

  rqstid = API_GetUniqueRequestID();

  /*
   * Get an object handle and ns attributes for the parent
   * directory in which the new directory is to be created.
   * Determine from the attributes of the parent directory
   * whether it is dmap managed or not, and from that determine
   * whether to call the dmap gateway or the name server to
   * create the new directory.
   */

  (void)memset(&attr_parent, 0, sizeof(attr_parent));
  (void)memset(&objhandle_parent, 0, sizeof(objhandle_parent));

  select_flags = API_AddRegisterValues(cast64m(0), CORE_ATTR_ACCOUNT,
#if HPSS_MAJOR_VERSION < 7
                                       CORE_ATTR_FILESET_ID,
                                       CORE_ATTR_FILESET_TYPE,
                                       CORE_ATTR_GATEWAY_UUID,
                                       CORE_ATTR_DM_HANDLE, CORE_ATTR_DM_HANDLE_LENGTH,
#endif
                                       -1);

  error = API_TraversePath(ThreadContext,
                           rqstid,
                           Ucred,
                           ObjHandle,
                           path_parent,
                           CwdStack,
                           API_CHASE_ALL,
                           0,
                           0,
                           select_flags,
                           cast64m(0),
                           API_NULL_CWD_STACK, &objhandle_parent, &attr_parent,
#if HPSS_MAJOR_VERSION < 7
                           NULL,
#endif
                           NULL, NULL, NULL, NULL);

  if(error != 0)
    {
      API_DEBUG_FPRINTF(DebugFile, &rqstid,
                        "%s: Could not get attributes.\n", function_name);
    }
  else
    {
      /*
       *  Check to see if we need to return the attributes of
       *  the newly created directory and set up the select flags
       *  appropriately.
       */

      if(RetAttrs != (hpss_Attrs_t *) NULL)
        {
          select_flags = API_AddAllRegisterValues(MAX_CORE_ATTR_INDEX);
        }
      else
        {
          select_flags = cast64m(0);
        }

      /*
       * Determine the appropriate accounting to use.
       */

      error = API_DetermineAcct(Ucred,
                                ThreadContext,
                                objhandle_parent.CoreServerUUID,
                                rqstid, &siteId, &temp_acct_code);
      if(error != 0)
        {
          API_DEBUG_FPRINTF(DebugFile, &rqstid,
                            "%s: Could not determine which"
                            " account to use.\n", function_name);
        }
      else
        {
          /*
           * Validate the account.
           */
#if HPSS_MAJOR_VERSION == 5
          error = av_cli_ValidateCreate(siteId,
                                        rqstid,
                                        Ucred->DCECellId,
                                        Ucred->SecPWent.Uid,
                                        Ucred->SecPWent.Gid,
                                        temp_acct_code,
                                        attr_parent.Account, &new_acct_code);
#elif (HPSS_MAJOR_VERSION == 6) || (HPSS_MAJOR_VERSION == 7)
          error = av_cli_ValidateCreate(siteId,
                                        rqstid,
                                        Ucred->RealmId,
                                        Ucred->Uid,
                                        Ucred->Gid,
                                        temp_acct_code,
                                        attr_parent.Account, &new_acct_code);
#endif

          if(error != 0)
            {
              API_DEBUG_FPRINTF(DebugFile, &rqstid,
                                "%s: Could not validate"
                                " the account.\n", function_name);
            }
        }
    }

  if(error == 0)
    {

#if HPSS_MAJOR_VERSION < 7

      /*
       * Do we call the dmap gateway or the name server?
       */

#if HPSS_MAJOR_VERSION == 5
      call_type = API_DetermineCall(attr_parent.FilesetType, (long *)&error);
#elif HPSS_MAJOR_VERSION == 6
      call_type = API_DetermineCall(attr_parent.FilesetType, &error);
#endif

      switch (call_type)
        {

        case API_CALL_DMG:

#if defined ( API_DMAP_SUPPORT ) && !defined ( API_DMAP_GATEWAY )
          /*
           * Call the dmap gateway to create the file.  This will
           * have the side effect that the dmap will call us to
           * create the directory on the hpss side.  By the time this
           * call returns, the directory will exist on both sides.
           */

          Ucred->CurAccount = new_acct_code;
          memset(dm_handle, 0, (sizeof(byte) * MAX_DMEPI_HANDLE_SIZE));
          memset(&dm_handle_length, 0, sizeof(unsigned32));

          /*
           * The mode should have the directory bit set and
           * the umask bits reset.
           */

          Mode |= S_IFDIR;
          Mode &= ~(ThreadContext->Umask);

          error = API_dmg_Create(ThreadContext, rqstid, Ucred, &attr_parent.GatewayUUID, attr_parent.FilesetId, attr_parent.DMHandle, attr_parent.DMHandleLength, path_newdir, Mode, NS_OBJECT_TYPE_DIRECTORY, NULL,    /* cos hints in */
                                 NULL,  /* cos hints priority */
                                 dm_handle, (unsigned32 *) & dm_handle_length, NULL);   /* cos hints out */

          if(error != 0)
            {
              API_DEBUG_FPRINTF(DebugFile, &rqstid,
                                "%s: API_dmg_Create failed.\n", function_name);
            }
          else
            {
              /*
               * If the caller asked for them, obtain the hpss attributes
               * and an object handle for the new directory.
               *
               * ChaseSymlinks and ChaseJunctions shouldn't matter at
               * this point; the object should be a directory.
               */

              if(RetObjHandle || RetVAttrs)
                {
                  error = API_TraversePath(ThreadContext,
                                           rqstid,
                                           Ucred,
                                           &objhandle_parent,
                                           path_newdir,
                                           CwdStack,
                                           API_CHASE_NONE,
                                           0,
                                           0,
                                           select_flags,
                                           cast64m(0),
                                           API_NULL_CWD_STACK,
                                           &objhandle_newdir,
                                           NULL,
                                           NULL, &attr_newdir_out, NULL, NULL, NULL);

                  if(error != 0)
                    {
                      API_DEBUG_FPRINTF(DebugFile, &rqstid,
                                        "%s: Could not get attributes"
                                        " of new directory.\n", function_name);
                    }
                }
            }
#else
          error = EACCES;
          API_DEBUG_FPRINTF(DebugFile, &rqstid,
                            "%s: No dmap support compiled in.\n", function_name);
#endif
          break;

        case API_CALL_HPSS:

#endif                          /* HPSS < 7 */

          /*
           * Set up the parameters for the new directory.
           */

          (void)memset(&attr_newdir_out, 0, sizeof(attr_newdir_out));
          (void)memset(&attr_newdir_in, 0, sizeof(attr_newdir_in));

          attr_newdir_in.Account = new_acct_code;

          API_ConvertPosixModeToMode(Mode & ~(ThreadContext->Umask), &attr_newdir_in);

          update_flags = API_AddRegisterValues(cast64m(0),
                                               CORE_ATTR_ACCOUNT,
                                               CORE_ATTR_USER_PERMS,
                                               CORE_ATTR_GROUP_PERMS,
                                               CORE_ATTR_OTHER_PERMS, -1);

#if defined(API_DMAP_GATEWAY)

          /*
           * If the gateway is trying to create a directory on a
           * mirrored fileset, it must supply a UID & GID for
           * the directory.
           */

          if(attr_parent.FilesetType == CORE_FS_TYPE_MIRRORED)
            {
              attr_newdir_in.UID = Ucred->SecPWent.Uid;
              attr_newdir_in.GID = Ucred->SecPWent.Gid;
              update_flags = API_AddRegisterValues(update_flags,
                                                   CORE_ATTR_UID, CORE_ATTR_GID, -1);
            }
#endif
          /*
           * Note: The DM handle is not loaded here for non
           * HPSS filesets because the handle can only be
           * determined after the file is created on the
           * DMAP side. Therefore, the gateway must update
           * the directories attributes after the directory
           * is created.
           */

          error = API_core_MkDir(ThreadContext,
                                 rqstid,
                                 Ucred,
                                 &objhandle_parent,
                                 path_newdir,
                                 update_flags,
                                 &attr_newdir_in,
                                 select_flags, &attr_newdir_out, &objhandle_newdir);

          if(error != 0)
            {
              API_DEBUG_FPRINTF(DebugFile, &rqstid,
                                "%s: Could not create directory"
                                ", error=%d\n", function_name, error);

              API_LogMsg(function_name, rqstid, CS_DEBUG,
                         SOFTWARE_ERROR, NONE, API_REQUEST_ERROR, error);
            }
#if HPSS_MAJOR_VERSION < 7
          break;

        default:
          if(error == 0)
            error = EIO;
          API_DEBUG_FPRINTF(DebugFile, &rqstid,
                            "%s: Bad case from DetermineCall().\n", function_name);
          break;
        }                       /* end switch */
#endif

    }

  /* end (if error == 0) */
  /*
   *  Convert the returned attributes, if necessary.
   */
  if(RetAttrs != (hpss_Attrs_t *) NULL)
    {
      *RetAttrs = attr_newdir_out;
    }

  if(RetObjHandle != (ns_ObjHandle_t *) NULL)
    *RetObjHandle = objhandle_newdir;

  free(path_newdir);
  free(path_parent);

  return (error);
}
