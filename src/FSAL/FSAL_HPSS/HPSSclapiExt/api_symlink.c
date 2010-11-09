#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <u_signed64.h>
#include <hpssclapiext.h>
#include <api_internal.h>

/*
 *  For determining if a returned access ticket is NULL.
 */

TYPE_TOKEN_HPSS null_ticket;

#if HPSS_MAJOR_VERSION < 7

static int
HPSSFSAL_Common_Symlink(apithrdstate_t * ThreadContext,
                        hpss_reqid_t RequestID,
                        ns_ObjHandle_t * ObjHandle,
                        TYPE_CRED_HPSS * Ucred,
                        char *Path,
                        api_cwd_stack_t * CwdStack,
                        char *Contents,
                        ns_ObjHandle_t * HandleOut, hpss_Attrs_t * AttrsOut)
{
  static char function_name[] = "HPSSFSAL_Common_Symlink";

#if HPSS_MAJOR_VERSION == 5
  volatile long error = 0;      /* return error */
#elif HPSS_MAJOR_VERSION >= 6
  signed32 error = 0;           /* return error */
#endif

  call_type_t call_type;        /* interface to call */
  TYPE_TOKEN_HPSS ta;           /* security token */
  ns_ObjHandle_t parent_handle; /* parent object handle */
  u_signed64 parent_attr_bits;  /* object attribute bits */
  hpss_Attrs_t parent_attrs;    /* new object attributes */
  char *file;                   /* name of the new object */
  char *parent;                 /* name of existing parent */
  ns_ObjHandle_t new_handle;    /* new object handle */
  ns_ObjHandle_t ret_handle;    /* returned new object handle */
  u_signed64 new_attr_bits;     /* object attribute bits */
  hpss_Attrs_t new_attrs;       /* new object attributes */

  /*
   * Allocate memory for object name and parent
   */

  file = (char *)malloc(HPSS_MAX_FILE_NAME);
  if(file == NULL)
    return (-ENOMEM);

  parent = (char *)malloc(HPSS_MAX_PATH_NAME);
  if(parent == NULL)
    {
      free(file);
      return (-ENOMEM);
    }

  /*
   * Divide the object component from the rest of the path
   */

  error = API_DivideFilePath(Path, parent, file);

  free(parent);
  if(error != 0)
    {
      free(file);
      return (error);
    }

  /*
   * Traverse the path to the parent and get
   * it's attributes and handle.
   */

  (void)memset(&ta, 0, sizeof(TYPE_TOKEN_HPSS));
  (void)memset(&parent_handle, 0, sizeof(parent_handle));
  (void)memset(&parent_attrs, 0, sizeof(parent_attrs));

  parent_attr_bits = API_AddRegisterValues(cast64m(0),
                                           CORE_ATTR_TYPE,
                                           CORE_ATTR_FILESET_TYPE,
                                           CORE_ATTR_FILESET_ID,
                                           CORE_ATTR_GATEWAY_UUID,
                                           CORE_ATTR_DM_HANDLE,
                                           CORE_ATTR_DM_HANDLE_LENGTH, -1);

  error = API_TraversePath(ThreadContext,
                           RequestID,
                           Ucred,
                           ObjHandle,
                           Path,
                           CwdStack,
                           API_CHASE_ALL,
                           0,
                           0,
                           cast64m(0),
                           parent_attr_bits,
                           API_NULL_CWD_STACK,
                           NULL, NULL, &parent_handle, &parent_attrs, &ta, NULL, NULL);

  if(error == 0)
    error = -EEXIST;
  else if(error == -ENOENT && memcmp(&ta, &null_ticket, sizeof(ta)) != 0)
    error = 0;
  else
    {
      API_DEBUG_FPRINTF(DebugFile, &RequestID,
                        "%s: Could not find object.\n", function_name);
    }

  if(error == 0)
    {

      /*
       * Call this function to determine which interface
       * to call (DMAP Gateway or the Core Server) depending
       * on if the object is DMAP mapped, which version of
       * the library this is, what type of DMAP file set
       * this is (sync or backup).
       */
#if HPSS_MAJOR_VERSION == 5
      call_type = API_DetermineCall(parent_attrs.FilesetType, (long *)&error);
#elif HPSS_MAJOR_VERSION == 6
      call_type = API_DetermineCall(parent_attrs.FilesetType, &error);
#endif

      if(call_type == API_CALL_DMG)
        {
          /*
           * Here we are being called by a non-gateway client and
           * trying to link a object in a DMAP file set.
           */

#if defined (API_DMAP_SUPPORT) && !defined (API_DMAP_GATEWAY)

          error = API_dmg_Symlink(ThreadContext, RequestID, Ucred, &parent_attrs.GatewayUUID, parent_attrs.FilesetId, parent_attrs.DMHandle, parent_attrs.DMHandleLength, file, Contents, NULL, /* not used */
                                  NULL);        /* not used */
#else
          error = EACCES;
          API_DEBUG_FPRINTF(DebugFile, &RequestID,
                            "%s: No dmap support compiled in.\n", function_name);
#endif

        }
      else if(call_type == API_CALL_HPSS)
        {
          /*
           * Here we are being call by either a gateway client
           * or a non-gateway client trying to link a object
           * in a non-DMAP file set.
           */

          error = API_core_MkSymLink(ThreadContext,
                                     RequestID,
                                     Ucred,
                                     &parent_handle,
                                     file,
                                     &parent_attrs.FilesetId, Contents, &new_handle);

          if(error != 0)
            {
              API_DEBUG_FPRINTF(DebugFile, &RequestID,
                                "%s: can't make symlink, error=%d\n,",
                                function_name, error);
            }

        }

      /*
       * Call type is not DMG or HPSS.
       */

      else
        {
          if(error == 0)
            error = EIO;

          API_DEBUG_FPRINTF(DebugFile, &RequestID,
                            "%s: Bad case from DetermineCall().\n", function_name);
        }

      /*
       * If requested, return the vattr for the newly created
       * link
       */

      if(error == 0 && AttrsOut != (hpss_Attrs_t *) NULL)
        {
          new_attr_bits = cast64m(0);
          new_attr_bits = API_AddAllRegisterValues(MAX_CORE_ATTR_INDEX);
          (void)memset(&ret_handle, 0, sizeof(ret_handle));
          (void)memset(&new_attrs, 0, sizeof(new_attrs));
          (void)memset(&ta, 0, sizeof(ta));

          error = API_TraversePath(ThreadContext,
                                   RequestID,
                                   Ucred,
                                   &new_handle,
                                   NULL,
                                   API_NULL_CWD_STACK,
                                   API_CHASE_NONE,
                                   0,
                                   0,
                                   new_attr_bits,
                                   cast64m(0),
                                   API_NULL_CWD_STACK,
                                   &ret_handle, &new_attrs, NULL, NULL, &ta, NULL, NULL);

          if(error != 0)
            {
              API_DEBUG_FPRINTF(DebugFile, &RequestID,
                                "%s: Could not get attributes.\n", function_name);
            }
          else
            {
              *AttrsOut = new_attrs;
              *HandleOut = new_handle;
            }
        }
    }

  free(file);
  return (error);
}
#else                           /* from v7 */

static int
HPSSFSAL_Common_Symlink(apithrdstate_t * ThreadContext,
                        hpss_reqid_t RequestID,
                        ns_ObjHandle_t * ObjHandle,
                        sec_cred_t * Ucred,
                        char *Path,
                        api_cwd_stack_t * CwdStack,
                        char *Contents,
                        ns_ObjHandle_t * HandleOut, hpss_Attrs_t * AttrsOut)
{
  static char function_name[] = "Common_Symlink";
  signed32 error = 0;           /* return error */
  ns_ObjHandle_t parent_handle; /* parent object handle */
  char *file;                   /* name of the new object */
  char *parentpath;             /* name of existing parent */
  hpss_AttrBits_t attr_bits;    /* parent attributes bits */
  hpss_Attrs_t attrs;           /* parent attributes */
  ns_ObjHandle_t *hndl_ptr = ObjHandle; /* parent handle pointer */
  ns_ObjHandle_t new_handle;    /* new object handle */
  ns_ObjHandle_t ret_handle;    /* returned new object handle */
  u_signed64 new_attr_bits;     /* object attribute bits */
  hpss_Attrs_t new_attrs;       /* new object handle */

  /*
   * Allocate memory for object name and parent name
   */

  file = (char *)malloc(HPSS_MAX_FILE_NAME);
  if(file == NULL)
    return (-ENOMEM);

  parentpath = (char *)malloc(HPSS_MAX_PATH_NAME);
  if(parentpath == NULL)
    {
      free(file);
      return (-ENOMEM);
    }

  /*
   * Divide the object component from the rest of the path
   */

  if((error = API_DivideFilePath(Path, parentpath, file)) != 0)
    {
      free(file);
      free(parentpath);
      return (error);
    }

  if(API_PATH_NEEDS_TRAVERSAL(parentpath))
    {
      /*
       * If there is a path provided then, then lookup
       * the parent handle
       */

      attr_bits = API_AddRegisterValues(cast64m(0), CORE_ATTR_TYPE, -1);

      memset(&parent_handle, 0, sizeof(parent_handle));
      memset(&attrs, '\0', sizeof(attrs));

      /*
       * Traverse the path to the parent and get it's handle.
       */

      error = API_TraversePath(ThreadContext,
                               RequestID,
                               Ucred,
                               ObjHandle,
                               parentpath,
                               CwdStack,
                               API_CHASE_ALL,
                               0,
                               0,
                               attr_bits,
                               cast64m(0),
                               API_NULL_CWD_STACK,
                               &parent_handle,
                               &attrs,
                               (ns_ObjHandle *) NULL, (hpss_Attrs_t *) NULL, NULL, NULL);

      if(error == 0)
        {
          if(attrs.Type != NS_OBJECT_TYPE_DIRECTORY)
            error = -ENOTDIR;
          else
            {
              hndl_ptr = &parent_handle;
            }
        }
    }
  else if(API_PATH_IS_ROOT(parentpath))
    {
      /* If needed, use the root handle */
      error = API_InitRootHandle(ThreadContext, RequestID, &hndl_ptr);
    }

  if(error == 0)
    {
      error = API_core_MkSymLink(ThreadContext,
                                 RequestID, Ucred, hndl_ptr, file, Contents, &new_handle);

      if(error != 0)
        {
          API_DEBUG_FPRINTF(DebugFile, &RequestID,
                            "%s: can't make symlink, error=%d\n,", function_name, error);
        }
    }

  /*
   * If requested, return the vattr for the newly created link
   */

  if(error == 0 && AttrsOut != (hpss_Attrs_t *) NULL)
    {
      new_attr_bits = API_VAttrAttrBits;
      (void)memset(&ret_handle, 0, sizeof(ret_handle));
      (void)memset(&new_attrs, 0, sizeof(new_attrs));

      error = API_TraversePath(ThreadContext,
                               RequestID,
                               Ucred,
                               &new_handle,
                               NULL,
                               API_NULL_CWD_STACK,
                               API_CHASE_NONE,
                               0,
                               0,
                               new_attr_bits,
                               cast64m(0),
                               API_NULL_CWD_STACK,
                               &ret_handle, &new_attrs, NULL, NULL, NULL, NULL);

      if(error != 0)
        {
          API_DEBUG_FPRINTF(DebugFile, &RequestID,
                            "%s: Could not get attributes.\n", function_name);
        }
      else
        {
          *AttrsOut = new_attrs;
          *HandleOut = ret_handle;
        }
    }

  free(file);
  free(parentpath);

  return (error);
}

#endif

int HPSSFSAL_SymlinkHandle(ns_ObjHandle_t * ObjHandle,  /* IN - Handle of existing file */
                           char *Contents,      /* IN - Desired contents of the link */
                           char *Path,  /* IN - New name of the symbolic link */
                           TYPE_CRED_HPSS * Ucred,      /* IN - pointer to user credentials */
                           ns_ObjHandle_t * HandleOut,  /* OUT - Handle of crfeated link */
                           hpss_Attrs_t * AttrsOut)     /* OUT - symbolic link attributes */
{
  long error = 0;
  apithrdstate_t *threadcontext;
  hpss_reqid_t rqstid;
  TYPE_CRED_HPSS *ucred_ptr;

  API_ENTER("HPSSFSAL_SymlinkHandle");

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
   *  We need an path to create the new symlink
   */

  if(Path == NULL)
    API_RETURN(-EFAULT);

  if(*Path == '\0')
    API_RETURN(-ENOENT);

  /*
   * Need to have contents for the symlink
   */

  if(Contents == NULL)
    API_RETURN(-EFAULT);

  /*
   *  If user credentials were not passed, use the ones in the
   *  current thread context.
   */

  if(Ucred == (TYPE_CRED_HPSS *) NULL)
    ucred_ptr = &threadcontext->UserCred;
  else
    ucred_ptr = Ucred;

  /*
   *  Get a valid request id.
   */

  rqstid = API_GetUniqueRequestID();

  /*
   * Call HPSSFSAL_Common_Symlink() to perform the majority of the common symbolic 
   * link processing.
   */

  error = HPSSFSAL_Common_Symlink(threadcontext,
                                  rqstid,
                                  ObjHandle,
                                  ucred_ptr,
                                  Path,
                                  API_NULL_CWD_STACK, Contents, HandleOut, AttrsOut);

  API_RETURN(error);
}
