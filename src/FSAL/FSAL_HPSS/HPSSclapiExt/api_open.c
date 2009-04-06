#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include "hpss_soid_func.h"
#include "u_signed64.h"
#include "pdata.h"
#include "hpssclapiext.h"
#include "api_internal.h"
/*#include "dmg_types.h"*/
#include "acct_av_lib.h"


#ifdef LINUX
#define pthread_mutexattr_default NULL
#define pthread_condattr_default NULL
#endif

/*
 *  For determining if a returned access ticket is NULL.
 */

TYPE_TOKEN_HPSS	null_ticket;

/*
 *  Prototypes for static routines.
 */

static int HPSSFSAL_Common_Open(
    apithrdstate_t		*ThreadContext,	/* IN - thread context */
    hpss_reqid_t		RequestID,      /* IN - request id */
    ns_ObjHandle_t		*ObjHandle,	/* IN - parent object handle */
    char			*Path,		/* IN - file name */
    api_cwd_stack_t         *CwdStack,      /* IN - cwd stack */
    int			Oflag,		/* IN - open flags */
    mode_t			Mode,		/* IN - create mode */
    TYPE_CRED_HPSS		*Ucred,		/* IN - user credentials */
    hpss_cos_hints_t	*HintsIn,	/* IN - creation hints */
    hpss_cos_priorities_t	*HintsPri,	/* IN - hint priorities */
    hpss_cos_hints_t	*HintsOut,	/* OUT - actual hint values used */
    hpss_Attrs_t		*AttrsOut,	/* OUT - returned attributes */
    ns_ObjHandle_t		*HandleOut,	/* OUT - returned handle */
    TYPE_TOKEN_HPSS		*AuthzTicket);	/* OUT - returned authorization */

static int HPSSFSAL_Common_Create(
    apithrdstate_t		*ThreadContext,	/* IN - thread context */
    hpss_reqid_t		RequestID,      /* IN - request id */
    ns_ObjHandle_t		*ObjHandle,	/* IN - parent object handle */
    char			*Path,		/* IN - file name */
    api_cwd_stack_t         *CwdStack,      /* IN - cwd stack */
    mode_t			Mode,		/* IN - create mode */
    TYPE_CRED_HPSS		*Ucred,		/* IN - user credentials */
    hpss_cos_hints_t	*HintsIn,	/* IN - creation hints */
    hpss_cos_priorities_t	*HintsPri,	/* IN - hint priorities */
    TYPE_TOKEN_HPSS		*AuthzIn,	/* IN - authorization for bfs */
    unsigned32	    	CreateFlags,	/* IN - Bfs flags to set on create */
    ns_ObjHandle_t          *ParentHandle,  /* IN - parent handle */
    hpss_Attrs_t            *ParentAttrs,   /* IN - parent attributes */
    api_dmap_attrs_t        *DMAttrsOut,    /* OUT - DMAP attributes */
    unsigned32              *FilesetCOS,    /* OUT - Fileset COS */
    hpss_cos_hints_t	*HintsOut,	/* OUT - actual hint values used */
    hpss_Attrs_t		*AttrsOut,	/* OUT - returned attributes */
    ns_ObjHandle_t		*HandleOut,	/* OUT - returned handle */
    TYPE_TOKEN_HPSS		*AuthzOut);	/* OUT - returned authorization */

static int HPSSFSAL_Common_Open_Bitfile(
    apithrdstate_t	        *ThreadContext,	/* IN - thread context */
    hpss_reqid_t		RequestID,      /* IN - request id */
    hpssoid_t	        *BitFileID,	/* IN - bitfile id */
    ns_ObjHandle_t	        *ObjHandlePtr,	/* IN - NS object handle */
    TYPE_CRED_HPSS	        *Ucred,		/* IN - user credentials */
    int		        Oflag,		/* IN - open flags */
    TYPE_TOKEN_HPSS	        *AuthzTicket,	/* IN - client authorization */
    api_dmap_attrs_t        *DMAttrs,       /* IN - DMAP attributes */
    unsigned32              FilesetCOS,     /* IN - fileset containing file */
    filetable_t	        *FTPtr,		/* IN - file table pointer */
    int                     Fildes
#if defined( _USE_HPSS_62 ) || defined( _USE_HPSS_622 )
    , hpss_cos_hints_t	*HintsOut /* OUT - actual hint values used */
#endif 
#ifdef _USE_HPSS_622
    , u_signed64              *SegmentSize /* OUT - current storage segment size */
#endif
    );        /* IN - file table index */



/*============================================================================
 *
 * Function:	HPSSFSAL_OpenHandle
 *
 * Synopsis:
 *
 * int
 * HPSSFSAL_OpenHandle(
 * ns_ObjHandle_t	    *ObjHandle,	** IN - Parent object handle
 * char			    *Path,	** IN - Path to file to be opened
 * int			    Oflag,	** IN - Type of file access
 * mode_t		    Mode,	** IN - Desired file perms if create
 * TYPE_CRED_HPSS	    *Ucred,	** IN - User credentials
 * hpss_cos_hints_t	    *HintsIn,	** IN - Desired class of service
 * hpss_cos_priorities_t    *HintsPri,	** IN - Priorities of hint struct
 * hpss_cos_hints_t	    *HintsOut,	** OUT - Granted class of service
 * hpss_Attrs_t		*AttrsOut,	** OUT - returned attributes
 * ns_ObjHandle_t		*HandleOut,	** OUT - returned handle
 * TYPE_TOKEN_HPSS		    *AuthzTicket)** OUT - Client authorization
 *
 * Description:
 *
 *	The 'HPSSFSAL_OpenHandle' function establishes a connection between a file,
 *	specified by 'Path', taken relative to the directory indicated by
 *	'ObjHandle', and a file handle.
 *
 * Parameters:
 *
 * Outputs:
 *		non-negative -	opened file handle.
 *
 * Interfaces:
 *	DCE pthreads, DCE/RPC, HPSSFSAL_Common_Open.
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

int
HPSSFSAL_OpenHandle
(ns_ObjHandle_t	    	*ObjHandle,	/* IN - Parent object handle */
 char			*Path,		/* IN - Path to file to be opened */
 int			Oflag,		/* IN - Type of file access */
 mode_t			Mode,		/* IN - Desired file perms if create */
 TYPE_CRED_HPSS	*Ucred,		/* IN - User credentials */
 hpss_cos_hints_t	*HintsIn,	/* IN - Desired class of service */
 hpss_cos_priorities_t	*HintsPri,	/* IN - Priorities of hint struct */
 hpss_cos_hints_t	*HintsOut,	/* OUT - Granted class of service */
 hpss_Attrs_t		*AttrsOut,	/* OUT - returned attributes */
 ns_ObjHandle_t		*HandleOut,	/* OUT - returned handle */
 TYPE_TOKEN_HPSS		*AuthzTicket)	/* OUT - Client authorization */
{
   static char		function_name[] = "HPSSFSAL_OpenHandle";
   long			error = 0;      /* return error */
   hpss_reqid_t         rqstid;	        /* request id */
   TYPE_CRED_HPSS	*ucred_ptr;     /* user credentials */
   apithrdstate_t	*threadcontext; /* thread context pointer */


   API_ENTER(function_name);

   /*
    *  Initialize the thread if not already initialized.
    *  Get a pointer back to the thread specific context.
    */

   error = API_ClientAPIInit(&threadcontext);
   if(error != 0)
      API_RETURN(error);


   /*
    *  Get a valid request id.
    */

   rqstid = API_GetUniqueRequestID();


   /*
    *  Check that the object handle is not NULL.
    */

   if (ObjHandle == (ns_ObjHandle_t *)NULL)
      API_RETURN(-EINVAL);

   /*
    *  Check that the pathname the string is not the NULL string.
    */

   if ((Path != NULL) && (*Path == '\0'))
      API_RETURN(-ENOENT);


   /*
    * Make sure both pointers are NULL or both pointers are non-NULL
    */

   if ( (HintsIn == (hpss_cos_hints_t *)NULL &&
	 HintsPri != (hpss_cos_priorities_t *) NULL)
	||
	( HintsIn != (hpss_cos_hints_t *)NULL &&
	  HintsPri == (hpss_cos_priorities_t *) NULL))
      API_RETURN(-EINVAL);


   /*
    *  If user credentials were not passed, use the ones in the
    *  current thread context.
    */

   if (Ucred == (TYPE_CRED_HPSS *)NULL)
      ucred_ptr = &threadcontext->UserCred;
   else
      ucred_ptr = Ucred;

   /*
    *  Call HPSSFSAL_Common_Open() to perform the majority of the common open
    *  processing.
    */

   error = HPSSFSAL_Common_Open(threadcontext,
		       rqstid,
		       ObjHandle,
		       Path,
		       API_NULL_CWD_STACK,
		       Oflag,
		       Mode,
		       ucred_ptr,
		       HintsIn,
		       HintsPri,
		       HintsOut,
		       AttrsOut,
		       HandleOut,
		       AuthzTicket);
			 
   API_RETURN(error);
}




/*============================================================================
 *
 * Function:	HPSSFSAL_CreateHandle
 *
 * Synopsis:
 *
 * int
 * HPSSFSAL_CreateHandle(
 * ns_ObjHandle_t	    *ObjHandle,	** IN - Parent object handle
 * char			    *Path,	** IN - Path to file to be opened
 * mode_t		    Mode,	** IN - Desired file perms if create
 * TYPE_CRED_HPSS	    *Ucred,	** IN - User credentials
 * hpss_cos_hints_t	    *HintsIn,	** IN - Desired class of service
 * hpss_cos_priorities_t    *HintsPri,	** IN - Priorities of hint struct
 * hpss_cos_hints_t	    *HintsOut,	** OUT - Granted class of service
 * hpss_Attrs_t		*AttrsOut,	** OUT - returned attributes 
 * ns_ObjHandle_t     *HandleOut,	** OUT - returned handle 
 * TYPE_TOKEN_HPSS		    *AuthzTicket)** OUT - Client authorization
 *
 * Description:
 *
 *	The 'hpss_Create' function creates a file specified by
 *	by 'Path', with permissions as specified by 'Mode' and using
 *	the class of service values specified by 'HintsIn' and 'HintsPri',
 *	if non-NULL.
 *
 * Parameters:
 *
 *	ObjHandle	NS object handle of parent directory of Path.
 *
 *	Path		Names the file to be created.
 *
 *	Mode		Gives the file mode used for determining the
 *			file mode for the created file.
 *
 *	HintsPri	Pointer to structure defining client preferences
 *			for class of service.
 *
 *	HintsPri	Pointer to structure defining the priorities of
 *			each COS structure field.
 *
 *	HintsPri	Pointer to structure defining COS with which
 *			file was created.
 *
 * Outputs:
 *	Return value:
 *		Zero		- sucess.
 *		Non-zero	- error occurred.
 *
 * Interfaces:
 *	DCE pthreads, DCE/RPC, Common_Create.
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

int
HPSSFSAL_CreateHandle
(ns_ObjHandle_t	    	*ObjHandle,	/* IN - Parent object handle */
 char			*Path,		/* IN - Path to file to be created */
 mode_t			Mode,		/* IN - Desired file perms */
 TYPE_CRED_HPSS    	*Ucred,		/* IN - User credentials */
 hpss_cos_hints_t	*HintsIn,	/* IN - Desired class of service */
 hpss_cos_priorities_t	*HintsPri,	/* IN - Priorities of hint struct */
 hpss_cos_hints_t	*HintsOut,	/* OUT - Granted class of service */
 hpss_Attrs_t		*AttrsOut,	/* OUT - returned attributes */
 ns_ObjHandle_t     *HandleOut,	/* OUT - returned handle */
 TYPE_TOKEN_HPSS		*AuthzTicket)	/* OUT - Client authorization */
{
   static char		function_name[] = "HPSSFSAL_CreateHandle";
   long			error = 0;      /* return error */
   hpss_reqid_t         rqstid;	        /* request id */
   TYPE_TOKEN_HPSS		ta;             /* authorization ticket */
   hpss_Attrs_t	        attr;           /* file attributes */
   hpss_Attrs_t	        parent_attr;    /* parent attributes */
   hpss_AttrBits_t      select_flags;   /* attribute selection flags */
   hpss_AttrBits_t      parent_flags;   /* attribute selection flags */
   ns_ObjHandle_t	obj_handle;     /* file object handle */
   ns_ObjHandle_t	parent_handle;  /* file object handle */
   apithrdstate_t	*threadcontext; /* thread context pointer */
   TYPE_CRED_HPSS	*ucred_ptr;     /* user credentials pointer */
   retry_cb_t           retry_cb;       /* retry control block */


   API_ENTER(function_name);

   /*
    *  Initialize the thread if not already initialized.
    *  Get a pointer back to the thread specific context.
    */

   error = API_ClientAPIInit(&threadcontext);
   if(error != 0)
      API_RETURN(error);

   /*
    *  Get a valid request id.
    */

   rqstid = API_GetUniqueRequestID();

   /*
    *  Check that the object handle is not NULL.
    */

   if (ObjHandle == (ns_ObjHandle_t *)NULL)
      API_RETURN(-EINVAL);

   /*
    *  Check that there is a name for the new object
    */

   if (Path == NULL)
      API_RETURN(-EFAULT);

   if (*Path == '\0')
      API_RETURN(-ENOENT);

   /*
    * Make sure both pointers are NULL or both pointers are non-NULL
    */

   if ( (HintsIn == (hpss_cos_hints_t *)NULL &&
	 HintsPri != (hpss_cos_priorities_t *) NULL)
	||
	( HintsIn != (hpss_cos_hints_t *)NULL &&
	  HintsPri == (hpss_cos_priorities_t *) NULL))
      API_RETURN(-EINVAL);


   /*
    *  If user credentials were not passed, use the ones in the
    *  current thread context.
    */

   if (Ucred == (TYPE_CRED_HPSS *)NULL)
      ucred_ptr = &threadcontext->UserCred;
   else
      ucred_ptr = Ucred;


   /*
    *  Need to see if the file already exists, and if not,
    *  get an access ticket.
    */

   (void)memset(&select_flags,0,sizeof(select_flags));
   parent_flags = API_AddRegisterValues(cast64m(0),
                                        CORE_ATTR_ACCOUNT,
					CORE_ATTR_FILESET_ID,
					CORE_ATTR_FILESET_TYPE,
					CORE_ATTR_GATEWAY_UUID,
					CORE_ATTR_DM_HANDLE,
					CORE_ATTR_DM_HANDLE_LENGTH,
					CORE_ATTR_COS_ID,
					CORE_ATTR_FAMILY_ID,
					-1);
   (void)memset(&obj_handle,0,sizeof(obj_handle));
   (void)memset(&attr,0,sizeof(attr));     
   (void)memset(&ta,0,sizeof(ta));

   error = API_TraversePath(threadcontext,
			    rqstid,
			    ucred_ptr,
			    ObjHandle,
			    Path,
			    API_NULL_CWD_STACK,
			    API_CHASE_NONE,
			    0,
			    0,
			    select_flags,
			    parent_flags,
			    API_NULL_CWD_STACK,
			    &obj_handle,
			    &attr,
			    &parent_handle,
			    &parent_attr,
			    &ta,
			    NULL,
			    NULL);

   if (error == 0)
      error = -EEXIST;
         
   /*
    *  If we got another error other than ENOENT or
    *  the returned ticket is zeroes (indicating
    *  that a component of the path prefix does
    *  not exist), we are done.
    */
     
   if ((error != -ENOENT)
       ||
       (memcmp(&ta,&null_ticket,sizeof(ta)) == 0))
   {
      API_DEBUG_FPRINTF(DebugFile, &rqstid,
			"%s: Could not get attributes, error=%d\n",
			function_name, error);
   }
   else  
   {
      /*
       *  Call Common_Create() to perform the majority of the common open
       *  processing.
       */

      error = HPSSFSAL_Common_Create(threadcontext,
			    rqstid,
			    ObjHandle,
			    Path,
			    API_NULL_CWD_STACK,
			    Mode,
			    ucred_ptr,
			    HintsIn,
			    HintsPri,
			    &ta,
			    0,
			    &parent_handle,
			    &parent_attr,
			    NULL,
			    NULL,
			    HintsOut,
			    AttrsOut,
			    HandleOut,
			    AuthzTicket);

      if (error != 0)
      {
	 API_DEBUG_FPRINTF(DebugFile, &rqstid,
			   "%s: Common_Create failed, error=%d\n",
			   function_name, error);
      }
   }
      
   /*
    * Gatekeeper retries have timed-out.
    */

   if (error == HPSS_ERETRY)
      error = -EAGAIN;


   API_RETURN(error);
}



/*============================================================================
 *
 * Function:	HPSSFSAL_Common_Open
 *
 * Synopsis:
 *
 * static int
 * HPSSFSAL_Common_Open(
 * apithrdstate_t		*ThreadContext,	** IN - thread context
 * hpss_reqid_t	                RequestID,      ** IN - request id
 * ns_ObjHandle_t		*ObjHandle,	** IN - parent object handle
 * char				*Path,		** IN - file name
 * api_cwd_stack_t              *CwdStack,      ** IN - cwd stack
 * int				Oflag,		** IN - open flags
 * mode_t			Mode,		** IN - create mode
 * TYPE_CRED_HPSS		*Ucred,		** IN - user credentials
 * hpss_cos_hints_t		*HintsIn,	** IN - creation hints
 * hpss_cos_priorities_t	*HintsPri,	** IN - hint priorities
 * hpss_cos_hints_t		*HintsOut,	** OUT - actual hint values
 * hpss_vattr_t			*AttrsOut,	** OUT - returned attributes
 * TYPE_TOKEN_HPSS			*AuthzTicket)	** OUT - returned authz
 *
 * Description:
 *
 *	The 'HPSSFSAL_Common_Open' function performs common processing for
 *	hpss_Open and HPSSFSAL_OpenHandle.
 *
 * Parameters:
 *
 * Outputs:
 *		non-negative -	opened file handle.
 *
 * Interfaces:
 *	DCE pthreads, DCE/RPC, API_core_GetAttrs, ns_Insert, 
 *      HPSSFSAL_Common_Open_Bitfile, API_ConvertPosixModeToMode,
 *      API_ConvertAttrsToVAttrs.
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
HPSSFSAL_Common_Open
(apithrdstate_t		*ThreadContext,	/* IN - thread context */
 hpss_reqid_t		RequestID,      /* IN - request id */
 ns_ObjHandle_t		*ObjHandle,	/* IN - parent object handle */
 char			*Path,		/* IN - file name */
 api_cwd_stack_t        *CwdStack,      /* IN - cwd stack */
 int			Oflag,		/* IN - open flags */
 mode_t			Mode,		/* IN - create mode */
 TYPE_CRED_HPSS	*Ucred,		/* IN - user credentials */
 hpss_cos_hints_t	*HintsIn,	/* IN - creation hints */
 hpss_cos_priorities_t	*HintsPri,	/* IN - hint priorities */
 hpss_cos_hints_t	*HintsOut,	/* OUT - actual hint values used */
 hpss_Attrs_t		*AttrsOut,	/* OUT - returned attributes */
 ns_ObjHandle_t		*HandleOut,	/* OUT - returned handle */
 TYPE_TOKEN_HPSS		*AuthzTicket)	/* OUT - returned authorization */
{
   static char		function_name[] = "HPSSFSAL_Common_Open";
   int			fildes;         /* File descriptor */
   int			checkflag;      /* Oflag check variable */
#ifdef _USE_HPSS_51      
   volatile long	error = 0;        /* return error */
#elif defined( _USE_HPSS_62) || defined( _USE_HPSS_622)
   signed32 	error = 0;        /* return error */
#endif
   retry_cb_t           retry_cb;       /* retry control block */
   hpss_Attrs_t	        attr;           /* file attributes */
   hpss_Attrs_t	        parent_attr;    /* parent attributes */
   hpss_AttrBits_t	select_flags;   /* attribute select bits */
   hpss_AttrBits_t	parent_flags;   /* parent attributes select bits */
   ns_ObjHandle_t	obj_handle;     /* file handle */
   ns_ObjHandle_t	parent_handle;  /* parent handle */
   hpss_Attrs_t	     file_attrs;     /* attribute for created file */
   TYPE_TOKEN_HPSS		ta,create_ta;   /* security tokens */
   filetable_t		*ftptr;         /* file table entry pointer */
   api_dmap_attrs_t     dmap_attrs;     /* DMAP attibutes for the file */
   int                  called_create = FALSE; /* called common create */
   unsigned32           fileset_cos;    /* Fileset COS */



   /*
    *  Verify that the Oflag is valid.
    */

   checkflag = Oflag & O_ACCMODE;
   if((checkflag != O_RDONLY) 
      &&
      (checkflag != O_RDWR)
      &&
      (checkflag != O_WRONLY))
   {
      return(-EINVAL);
   }
	
   /*
    *  Check that we do not have too many descriptors already open.
    */

   ftptr = ThreadContext->FileTable;

   API_LockMutex(&ftptr->Mutex);

   if(ftptr->NumOpenDesc >= _HPSS_OPEN_MAX)
   {
      error = -EMFILE;
   }

   if(error == 0)
   {
      /*
       *  Allocate a slot for the file to be opened.
       */

      for(fildes = 0;fildes < _HPSS_OPEN_MAX;fildes++)
      {
	 if(ftptr->OpenDesc[fildes].Type == NO_OPEN_HANDLE) break;
      }
      if(fildes >= _HPSS_OPEN_MAX)
      {
	 API_DEBUG_FPRINTF(DebugFile,&RequestID,
			   "%s: Inconsistent descriptor table\n",
			   function_name);
	 kill(getpid(), SIGABRT);
      }
      ftptr->OpenDesc[fildes].Type = BFS_OPEN_HANDLE;
      ftptr->OpenDesc[fildes].Flags |= ENTRY_BUSY;
      ftptr->TotalOpens++;
      ftptr->NumOpenDesc++;
      ftptr->OpenDesc[fildes].descunion_u.OpenBF.DataDesc = -1;
   }

   API_UnlockMutex(&ftptr->Mutex);
	
   if(error != 0) return(error);

   /*
    * Store the global request id in the file table entry.
    */

   ftptr->OpenDesc[fildes].GlobalRqstId = RequestID;


   /*
    * If needed, retry the get attributes if the file is
    * created between the time we first get attributes and
    * the time we issue the create.
    */

   for(;;)
   {

      /*
       *  Get all the information about the entry at the given path.
       */
     
      (void)memset(&ta,0,sizeof(ta));
      (void)memset(&attr,0,sizeof(attr));
      (void)memset(&parent_attr,0,sizeof(parent_attr));
      (void)memset(&file_attrs,0,sizeof(file_attrs));
      (void)memset(&select_flags,0,sizeof(select_flags));
      (void)memset(&parent_flags,0,sizeof(parent_flags));
     
     
      /*
       * Get the COS for the parent directory
       */
     
      parent_flags = API_AddRegisterValues(cast64m(0),
					   CORE_ATTR_ACCOUNT,
					   CORE_ATTR_FILESET_ID,
					   CORE_ATTR_FILESET_TYPE,
					   CORE_ATTR_GATEWAY_UUID,
					   CORE_ATTR_DM_HANDLE,
					   CORE_ATTR_DM_HANDLE_LENGTH,
					   CORE_ATTR_COS_ID,
					   CORE_ATTR_FAMILY_ID,
					   -1);
     
      /*
       *  If we are returning attributes, we need to get
       *  them all here.
       */
     
      if (AttrsOut == NULL)
      {
	 select_flags = API_AddRegisterValues(cast64m(0),
					      CORE_ATTR_BIT_FILE_ID,
					      CORE_ATTR_TYPE,
					      CORE_ATTR_FILESET_ID,
					      CORE_ATTR_FILESET_TYPE,
					      CORE_ATTR_GATEWAY_UUID,
					      CORE_ATTR_DM_HANDLE,
					      CORE_ATTR_DM_HANDLE_LENGTH,
					      -1);
      }
      else
	 select_flags = API_AddAllRegisterValues(MAX_CORE_ATTR_INDEX);
     
      error = API_TraversePath(ThreadContext,
			       RequestID,
			       Ucred, 
			       ObjHandle,
			       Path,
			       CwdStack,
			       API_CHASE_NONE,
			       0,
			       0,
			       select_flags,
			       parent_flags,
			       API_NULL_CWD_STACK,
			       &obj_handle,
			       &attr,
			       &parent_handle,
			       &parent_attr,
			       &ta,
			       NULL,
			       NULL);

      if((error != 0)
	 &&
	 (error != -ENOENT ||
	  (memcmp(&ta,&null_ticket,sizeof(ta)) == 0)))
      {
	 API_DEBUG_FPRINTF(DebugFile, &RequestID,
			   "%s: Could not find entry.\n",
			   function_name);
         break; /* break out of retry loop */
      }
      else if(error == -ENOENT && (Oflag & O_CREAT) == 0)
      {
	 /*
	  *  No entry and we are not allowed to create a new one.
	  */
     
	 API_DEBUG_FPRINTF(DebugFile, &RequestID,
			   "%s: Could not find entry (!O_CREATE).\n",
			   function_name);
         break; /* break out of retry loop */
      }
      else if((error == 0)
	      &&
	      ((Oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)))
      {
	 /*
	  *  Can not create an already existing file.
	  */
     
	 error = -EEXIST;
	 API_DEBUG_FPRINTF(DebugFile, &RequestID,
			   "%s: File exists on a create exclusive.\n",
			   function_name);
         break; /* break out of retry loop */
      }

     
      /*
       *  If we get here then the file already exists or we are going to
       *  create a new one.
       */
     
      /*
       *  Check to make sure that we are not opening a directory.
       */
     
      if ((error == 0)
	  &&
	  (attr.Type != NS_OBJECT_TYPE_HARD_LINK)
	  &&
	  (attr.Type != NS_OBJECT_TYPE_FILE))
      {
	 error = -EISDIR;
	 API_DEBUG_FPRINTF(DebugFile, &RequestID,
			   "%s: Attempt to open a directory.\n",
			   function_name);
         break; /* break out of retry loop */
      }

     
      if((error == -ENOENT) && (Oflag & O_CREAT))
      {

#if defined(API_DMAP_GATEWAY)
	 /*
	  * The gateway shouldn't be trying to create a
	  * file through the open call, error out
	  */

	 error = -EINVAL;
	 API_DEBUG_FPRINTF(DebugFile, &RequestID,
			   "%s: Gateway attempting to create"
			   " a file via open.\n",
			   function_name);
         break; /* break out of retry loop */
#else

	 /*
	  *  Call HPSSFSAL_Common_Create() to perform the majority of the
	  *  common open processing.
	  */

	 Oflag &= (~O_TRUNC);  /* just creating, don't need to truncate */
	 called_create = TRUE; /* set a flag to indicate file creation */

	 create_ta = ta;
	 memset(&dmap_attrs,0,sizeof(api_dmap_attrs_t));
	 error = HPSSFSAL_Common_Create(ThreadContext,
			       RequestID,
			       ObjHandle,
			       Path,
			       CwdStack,
			       Mode,
			       Ucred,
			       HintsIn,
			       HintsPri,
			       &create_ta,
			       0,
			       &parent_handle,
			       &parent_attr,
			       &dmap_attrs,
			       &fileset_cos,
			       HintsOut,                   
			       &file_attrs,
                   &obj_handle,
			       &ta);
     
	 if (error != 0)
	 {
	    API_DEBUG_FPRINTF(DebugFile, &RequestID,
			      "%s: Could not create file.\n",
			      function_name);
	 }
#endif
      }

      /*
       * This file already exists
       */
     
      else if ( error == 0 )
      {
     
     file_attrs = attr;
	 fileset_cos = parent_attr.COSId;
     
#if ( defined(_USE_HPSS_51) && defined(API_DMAP_SUPPORT) ) \
    || ( (defined(_USE_HPSS_62)||defined( _USE_HPSS_622)) && defined(API_DMAP_SUPPORT) && defined( API_MIRRORED_FILESETS ) )
     
	 /*
	  * If DMAPI support is enabled, then get the
	  * fileset information and save the DMAP attributes
	  */

	 memset(&dmap_attrs,0,sizeof(api_dmap_attrs_t));    
	 dmap_attrs.FilesetID = attr.FilesetId;
	 dmap_attrs.FilesetType = attr.FilesetType;
	 dmap_attrs.DMGuuid = attr.GatewayUUID;
	 dmap_attrs.HandleLength = attr.DMHandleLength;
	 memcpy(dmap_attrs.Handle,attr.DMHandle,
		dmap_attrs.HandleLength);

#endif

      }

      /*
       * If the create failed because the file already
       * existed, just retry the loop, we should get the
       * file's attributes and not try creating again.
       * Otherwise, we are done with the create.
       */

      if ((error == -EEXIST) && ((Oflag & O_EXCL) == 0))
      {
	 /*
	  * Reset the error status and retry count and
	  * go back to the top of the loop.
	  */

	 error = 0;
      }
      else break;

   } /* end of create retry loop */


   /*
    *  If everything is OK to this point, try and open the file.
    */

   if (error == 0)
   {
      /*
       * Open the bitfile
       */

      error = HPSSFSAL_Common_Open_Bitfile(ThreadContext,
			      RequestID,
			      &file_attrs.BitfileId,
			      &obj_handle,
			      Ucred,
			      Oflag,
			      &ta,
			      &dmap_attrs,
			      fileset_cos,
			      ftptr,
			      fildes
#if defined(_USE_HPSS_62) || defined(_USE_HPSS_622)
                  ,HintsOut
#endif
#ifdef _USE_HPSS_622
		  ,NULL
#endif
                  );
      
   }
   
   if(error != 0)
   {
      /*
       *  We had an open problem. Free up the allocated slot.
       */

      API_LockMutex(&ftptr->Mutex);

      ftptr->OpenDesc[fildes].Type = NO_OPEN_HANDLE;
      ftptr->OpenDesc[fildes].Flags = 0;
      ftptr->TotalOpens--;
      ftptr->NumOpenDesc--;

      API_UnlockMutex(&ftptr->Mutex);

      return(error);
   }


   /*
    *  Make sure 0 length files get invalidated on close, if necessary
    */

   if( (called_create != FALSE)
             ||
      (Oflag & O_TRUNC) )
   {
      ftptr->OpenDesc[fildes].descunion_u.OpenBF.Updates++;
   }


   /*
    * If request return the security token and
    * the file attributes
    */

   if (AuthzTicket != (TYPE_TOKEN_HPSS *)NULL)
      *AuthzTicket = ta;
   
   if (AttrsOut != (hpss_Attrs_t *)NULL)
      *AttrsOut = file_attrs;

   if (HandleOut != (ns_ObjHandle_t *)NULL)
      *HandleOut = obj_handle;

   return(fildes);
}


/*============================================================================
 *
 * Function:	HPSSFSAL_Common_Create
 *
 * Synopsis:
 *
 * static int
 * HPSSFSAL_Common_Create(
 * apithrdstate_t		*ThreadContext,	** IN - thread context
 * unsigned32			RequestID,	** IN - request id
 * ns_ObjHandle_t		*ObjHandle,	** IN - parent object handle
 * char				*Path,		** IN - file name
 * api_cwd_stack_t              *CwdStack,      ** IN - cwd stack
 * mode_t			Mode,		** IN - create mode
 * TYPE_CRED_HPSS		*Ucred,		** IN - user credentials
 * hpss_cos_hints_t		*HintsIn,	** IN - creation hints
 * hpss_cos_priorities_t	*HintsPri,	** IN - hint priorities
 * TYPE_TOKEN_HPSS			*AuthzIn,	** IN - create authorization
 * unsigned32	    	        CreateFlags,	** IN - bfs consistency flags
 * ns_ObjHandle_t               *ParentHandle,  ** IN - parent handle
 * hpss_Attrs_t                 *ParentAttrs,   ** IN - parent attributes
 * char                         *FileName,      ** IN - file name to create
 * api_dmap_attrs_t             *DMAttrsOut,    ** OUT - DMAP attributes
 * unsigned32                   *FilesetCOS,    ** OUT - Fileset COS
 * hpss_cos_hints_t		*HintsOut,	** OUT - actual hint values
 * hpss_vattr_t			*AttrsOut,	** OUT - returned attributes
 * TYPE_TOKEN_HPSS			*AuthzOut)	** OUT - returned authz
 *
 * Description:
 *
 *	The 'Common_Create' function performs common processing for
 *	HPSSFSAL_Common_Open and hpss_Create.
 *
 * Parameters:
 *
 * Outputs:
 *		zero - create succeeded.
 *
 * Interfaces:
 *	DCE pthreads, DCE/RPC, API_TraversePath, API_core_CreateFile,
 *	API_ConvertPosixModeToMode, API_ConvertAttrsToVAttrs.
 *      API_DetermineAcct, av_cli_ValidateCreate, API_AddRegisterValues
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
HPSSFSAL_Common_Create
(apithrdstate_t		*ThreadContext,	/* IN - thread context */
 hpss_reqid_t	        RequestID,	/* IN - request id */
 ns_ObjHandle_t		*ObjHandle,	/* IN - parent object handle */
 char			*Path,		/* IN - file name */
 api_cwd_stack_t        *CwdStack,      /* IN - cwd stack */
 mode_t			Mode,		/* IN - create mode */
 TYPE_CRED_HPSS	*Ucred,		/* IN - user credentials */
 hpss_cos_hints_t	*HintsIn,	/* IN - creation hints */
 hpss_cos_priorities_t	*HintsPri,	/* IN - hint priorities */
 TYPE_TOKEN_HPSS		*AuthzIn,	/* IN - create authorization */
 unsigned32	    	CreateFlags,	/* IN - bfs consistency flags */
 ns_ObjHandle_t         *ParentHandle,  /* IN - parent handle */
 hpss_Attrs_t           *ParentAttrs,   /* IN - parent attributes */
 api_dmap_attrs_t       *DMAttrsOut,    /* OUT - DMAP attributes */
 unsigned32             *FilesetCOS,    /* OUT - Fileset COS */
 hpss_cos_hints_t	*HintsOut,	/* OUT - actual hint values used */
 hpss_Attrs_t		*AttrsOut,	/* OUT - returned attributes */
 ns_ObjHandle_t		*HandleOut,	/* OUT - returned handle */
 TYPE_TOKEN_HPSS		*AuthzOut)	/* OUT - returned authorization */
{
   static char 		  function_name[] = "HPSSFSAL_Common_Create";
   long		          error;            /* return error */
   TYPE_TOKEN_HPSS		  ta;               /* security token */
   ns_ObjHandle_t	  obj_handle;       /* object handle */
   hpss_AttrBits_t	  select_flags;     /* retrieve object attr bits */
   hpss_AttrBits_t        update_flags;     /* updated object attr bits */
   hpss_Attrs_t		  new_attr;         /* object attributes */
   hpss_Attrs_t           attr_out;         /* returned object attributes */
   hpss_cos_md_t	  cos_info;         /* returned COS information */
   hpss_sclass_md_t	  sclass_info;      /* returned level 0 sclass info */
   call_type_t            call_type;        /* call HPSS or gateway */
   api_dmap_attrs_t       dmap_attrs;       /* DMAP attributes */
   hpss_cos_hints_t	  hint, *hintptr;   /* overridden creation hints */
   hpss_cos_priorities_t  prio, *prioptr;   /* overridden hint priorities */
   retry_cb_t             retry_cb;         /* retry control block */
   acct_rec_t             cur_acct_code;    /* current site account code */
   acct_rec_t             new_acct_code;    /* validated account code */
   TYPE_UUID_HPSS                 site_id;          /* site */
   ls_map_array_t         *ls_map_array;    /* Location map array */
   char                   *file;            /* file name to create */
   char                   *newpath;         /* path to file */


   /*
    * We want the last component of the supplied path.
    */

   file = (char *)malloc(HPSS_MAX_FILE_NAME);
   if ( file == NULL )
      return(-ENOMEM);

   newpath = (char *)malloc(HPSS_MAX_PATH_NAME);
   if ( newpath == NULL )
   {
      free(file);
      free(newpath);
      return(-ENOMEM);
   }

   /*
    * Divide the path into a path and object
    */

   error = API_DivideFilePath(Path, newpath, file);
   free(newpath);

   if ( error != 0)
   {
      free(file);
      return(-error);
   }


   /*
    *  Do account validation.
    */

   error = API_DetermineAcct(Ucred,
			     ThreadContext,
			     ParentHandle->CoreServerUUID,
			     RequestID,
			     &site_id,
			     &cur_acct_code);
   if (error != 0)
   {
      API_DEBUG_FPRINTF(DebugFile, &RequestID, "%s: couldn't"
			" get the account code from the given"
			" information: error= %d\n",
			function_name, error);
      free(file);
      return(error);
   }

#ifdef _USE_HPSS_51      
   error = av_cli_ValidateCreate(site_id,
				 RequestID,
				 Ucred->DCECellId,
				 Ucred->SecPWent.Uid,
				 Ucred->SecPWent.Gid,
				 cur_acct_code,
				 ParentAttrs->Account,
				 &new_acct_code);
#elif defined( _USE_HPSS_62) || defined( _USE_HPSS_622)
   error = av_cli_ValidateCreate(site_id,
				 RequestID,
				 Ucred->RealmId,
				 Ucred->Uid,
				 Ucred->Gid,
				 cur_acct_code,
				 ParentAttrs->Account,
				 &new_acct_code);
#endif
   if (error != 0)
   {
      API_DEBUG_FPRINTF(DebugFile, &RequestID, "%s: couldn't validate"
			" the account code: error= %d\n",
			function_name, error);
      free(file);
      return(error);
   }

   /*
    * Get the fileset id, type and gateway UUID from the parent
    */

   Ucred->CurAccount = new_acct_code;
   memset(&dmap_attrs,0,sizeof(dmap_attrs));
   dmap_attrs.FilesetID = ParentAttrs->FilesetId;
   dmap_attrs.FilesetType = ParentAttrs->FilesetType;
   dmap_attrs.DMGuuid = ParentAttrs->GatewayUUID;


   /*
    * Call this function to determine which interface
    * to call (DMAP Gateway or the Core Server) depending
    * on if the object is DMAP mapped, which version of
    * the library this is, what type of DMAP file set
    * this is (sync or backup).
    */

#ifdef _USE_HPSS_51      
   call_type = API_DetermineCall(dmap_attrs.FilesetType, &error);
#elif defined( _USE_HPSS_62) || defined( _USE_HPSS_622)
   call_type = API_DetermineCall(dmap_attrs.FilesetType, (signed32 *)&error);
#endif


   if( call_type == API_CALL_DMG )
   {
      /*
       * Here we are being called by a non-gateway client and
       * trying to create a object in a DMAP file set.
       */

#if ( defined(_USE_HPSS_51) && defined(API_DMAP_SUPPORT) && !defined(API_DMAP_GATEWAY) )\
     || ( (defined(_USE_HPSS_62)||defined( _USE_HPSS_622)) && defined(API_DMAP_SUPPORT) && !defined(API_DMAP_GATEWAY)  && defined( API_MIRRORED_FILESETS ) )

      /*
       * The mode should have the regular file bit set and
       * the umask bits reset.
       */

      Mode |= S_IFREG;
      Mode &= ~(ThreadContext->Umask);


      /*
       * Call the DMAP Gateway to create the object on
       * HPSS and the DMAPI file system
       */

      error = API_dmg_Create(ThreadContext,
			     RequestID,
			     Ucred,
			     &ParentAttrs->GatewayUUID,
			     ParentAttrs->FilesetId,
			     ParentAttrs->DMHandle,
			     ParentAttrs->DMHandleLength,
			     file,
			     Mode,
			     NS_OBJECT_TYPE_FILE,
			     HintsIn,
			     HintsPri,
			     dmap_attrs.Handle,
			     &dmap_attrs.HandleLength,
			     HintsOut);

      if(error != 0)
      {
	 API_DEBUG_FPRINTF(DebugFile, &RequestID,
			   "%s: API_dmg_Create failed:"
			   " error = %d\n",
			   function_name, error);
	 free(file);
	 return(error);
      }

      /*
       * At this point the object should be created
       * so, if requested get the attributes and/or
       * a security token from the Core Server
       */

      if (AttrsOut != (hpss_Attrs_t *)NULL ||
	  AuthzOut != (TYPE_TOKEN_HPSS *)NULL )
      {

	 select_flags = cast64m(0);
	 (void)memset(&obj_handle,0,sizeof(obj_handle));
	 (void)memset(&attr_out,0,sizeof(attr_out));
	 (void)memset(&ta,0,sizeof(ta));

	 if (AttrsOut != (hpss_Attrs_t *)NULL)
	    select_flags = API_AddAllRegisterValues(MAX_CORE_ATTR_INDEX);

	 error = API_TraversePath(ThreadContext,
				  RequestID,
				  Ucred,
				  ParentHandle,
				  file,
				  CwdStack,
				  API_CHASE_NONE,
				  0,
				  0,
				  select_flags,
				  cast64m(0),
				  API_NULL_CWD_STACK,
				  &obj_handle,
				  &attr_out,
				  NULL,
				  NULL,
				  &ta,
				  NULL,
				  NULL);

	 if(error != 0)
	 {
	    free(file);
	    return(error);
	 }


	 /*
	  * Return the requested security token
	  */

	 if (AuthzOut != (TYPE_TOKEN_HPSS *)NULL)
	    (void)memcpy(AuthzOut,&ta,sizeof(*AuthzOut));

      }
#endif
		      
   } /* end  call_type == DMG */

   else if ( call_type == API_CALL_HPSS )
   {

      /*
       * Here we are being call by either a gateway client
       * (creating a file on a DMAP file set) or a non-gateway
       * client trying to create a object in a non-DMAP
       * file set.
       */

      /*
       * If the COS for the fileset (where the file is to
       * be placed) is non-zero, redirect the file to the
       * COS specified by the fileset attributes.
       */

      if ( ParentAttrs->COSId != 0 )
      {
         /* Start with any hints & prios passed in */
	 if(HintsIn != NULL)
	    memcpy(&hint,HintsIn,sizeof(hint));
         else
	    memset(&hint,0,sizeof(hint));
	 if(HintsPri != NULL)
	    memcpy(&prio,HintsPri,sizeof(prio));
         else
	    memset(&prio,0,sizeof(prio));

	 /* Select the COS based on that of the parent */	
	 hint.COSId = ParentAttrs->COSId;
	 prio.COSIdPriority = REQUIRED_PRIORITY;
	 hintptr = &hint;
	 prioptr = &prio;
      }
      else
      {
	 /*
	  * Just use the Hints that were passed in.
	  */

	 hintptr = HintsIn;
	 prioptr = HintsPri;
      }


      /*
       *  Create the file in HPSS.
       */

      (void)memset(&new_attr,0,sizeof(new_attr));
      (void)memset(&attr_out,0,sizeof(attr_out));
      if (AuthzOut != (TYPE_TOKEN_HPSS *)NULL)
	 (void)memset(AuthzOut,0,sizeof(*AuthzOut));
      (void)memset(&obj_handle,0,sizeof(obj_handle));

      memset(&cos_info,0,sizeof(cos_info));
      memset(&sclass_info,0,sizeof(sclass_info));
      memset(&obj_handle,0,sizeof(obj_handle));

      /*
       * Setup the input attributes.
       */

      API_ConvertPosixModeToMode(Mode & ~ThreadContext->Umask,&new_attr);
      new_attr.DataLength = cast64m(0);
      new_attr.Account = new_acct_code;
      new_attr.FamilyId = ParentAttrs->FamilyId;
      update_flags = API_AddRegisterValues(cast64m(0),
					   CORE_ATTR_USER_PERMS,
					   CORE_ATTR_GROUP_PERMS,
					   CORE_ATTR_OTHER_PERMS,
					   CORE_ATTR_SET_UID,
					   CORE_ATTR_SET_GID,
					   CORE_ATTR_SET_STICKY,
					   CORE_ATTR_DATA_LENGTH,
					   CORE_ATTR_ACCOUNT,
					   CORE_ATTR_FAMILY_ID,
					   -1);

#if defined(API_DMAP_GATEWAY)

      /*
       * If the gateway asked us to set some consistency
       * bits, add'em before the call to create.
       */

      if ( CreateFlags != 0 )
      {
	 new_attr.DMDataStateFlags = CreateFlags;
	 update_flags = API_AddRegisterValues(update_flags,
					      CORE_ATTR_DM_DATA_STATE_FLAGS,
					      -1);
      }
      
      /*
       * If the gateway is trying to create a file on a
       * mirrored fileset, it must supply a UID & GID for
       * the file.
       */

      if ( dmap_attrs.FilesetType == CORE_FS_TYPE_MIRRORED )
      {
	 new_attr.UID = Ucred->SecPWent.Uid;
	 new_attr.GID = Ucred->SecPWent.Gid;

	 update_flags = API_AddRegisterValues(update_flags,
					      CORE_ATTR_UID,
					      CORE_ATTR_GID,
					      -1);
      }
#endif

      /*
       *  Only request returned attributes if we have somewhere to
       *  put them.
       */

      if (AttrsOut != (hpss_Attrs_t *)NULL)
	 select_flags = API_AddAllRegisterValues(MAX_CORE_ATTR_INDEX);
      else
	 select_flags = cast64m(0);

      error = API_core_CreateFile(ThreadContext,
				  RequestID,
				  Ucred,
				  ParentHandle,
				  file,
				  hintptr,
				  prioptr,
				  update_flags,
				  &new_attr,
				  AuthzIn,
				  select_flags,
				  &attr_out,
				  &obj_handle,
				  &cos_info,
				  &sclass_info,
				  AuthzOut);

      if ( error == 0 
	   &&
	   (HintsOut != (hpss_cos_hints_t *)NULL) )
      {
	 /*
	  *  The file now exists, go ahead and convert
	  *  the returned hints, if requested.
	  */

	 HintsOut->COSId = cos_info.COSId;
	 strncpy((char *)HintsOut->COSName,
		 (char *)cos_info.COSName,
		 HPSS_MAX_COS_NAME);
	 HintsOut->OptimumAccessSize = cast64m(cos_info.OptimumAccessSize);
	 HintsOut->MinFileSize = cos_info.MinFileSize;
	 HintsOut->MaxFileSize = cos_info.MaxFileSize;
	 HintsOut->AccessFrequency = cos_info.AccessFrequency;
	 HintsOut->TransferRate = cos_info.TransferRate;
	 HintsOut->AvgLatency = cos_info.AvgLatency;
	 HintsOut->WriteOps = cos_info.WriteOps;
	 HintsOut->ReadOps = cos_info.ReadOps;
	 HintsOut->StageCode = cos_info.StageCode;
	 HintsOut->StripeWidth = sclass_info.StripeWidth;
	 HintsOut->StripeLength = sclass_info.StripeLength;
      }

   } /* end (if call_type == HPSS) */

   if ( error == 0 )
   {
      /*
       *  The file now exists, go ahead and convert
       *  the returned name server attributes.
       */

      if ( AttrsOut != (hpss_Attrs_t *)NULL )
      {
        *AttrsOut = attr_out;
      }

      if ( HandleOut != (ns_ObjHandle_t *)NULL )
      {
        *HandleOut = obj_handle;
      }

      /*
       * Return the COS of the parent
       */

      if ( FilesetCOS != (unsigned32 *) NULL )
	 *FilesetCOS = ParentAttrs->COSId;


      /*
       * If the DM attributes were requested, return them here.
       * The fileset id, fileset type and gateway UUID are
       * extracted from the parent's attributes. The DM handle
       * and handle length are returned from the gateway for
       * backup and mirror fileset accesses via a client, but
       * any creates coming from the gateway will not contain
       * a valid handle yet. This is because the handle can
       * only be determined after the file is created on the
       * DMAP side.
       */

      if ( DMAttrsOut != (api_dmap_attrs_t *)NULL )
      {
	 DMAttrsOut->FilesetID = dmap_attrs.FilesetID;
	 DMAttrsOut->FilesetType = dmap_attrs.FilesetType;
	 DMAttrsOut->DMGuuid = dmap_attrs.DMGuuid;
	 DMAttrsOut->HandleLength = dmap_attrs.HandleLength;
	 memcpy(DMAttrsOut->Handle,dmap_attrs.Handle,
		DMAttrsOut->HandleLength);
      }
   }

   free(file);
   return(error);
}


/*============================================================================
 *
 * Function:	HPSSFSAL_Common_Open_Bitfile
 *
 * Synopsis:
 *
 * static int
 * HPSSFSAL_Common_Open_Bitfile(
 * apithrdstate_t	*ThreadContext,	** IN - thread context
 * hpss_reqid_t	        RequestID,      ** IN - request id
 * hpssoid_t		*BitFileID,	** IN - bitfile id
 * ns_ObjHandle_t	*ObjHandlePtr,	** IN - NS object handle
 * int			Oflag,		** IN - open flags
 * TYPE_TOKEN_HPSS		*AuthzTicket,	** IN - client authorization
 * api_dmap_attrs_t     *DMAttrs,       ** IN - DMAP attributes
 * unsigned32           FilesetCOS,     ** IN - fileset containing file
 * filetable_t		*FTPtr,		** IN - file table pointer
 * int                  Fildes)         ** IN - file table index
 *
 * Description:
 *
 *	The 'HPSSFSAL_Common_Open_Bitfile' function performs common processing for
 *	hpss_OpenBitfile and Common_Open.
 *
 * Parameters:
 *
 * Outputs:
 *		0 - success.
 *
 * Interfaces:
 *	DCE pthreads, DCE/RPC, API_bfs_Open.
 *
 * Resources Used:
 *
 * Limitations:
 *
 * Assumptions:
 *
 * Notes:
 *   If this function returns an error, the calling function is
 *   responsible for freeing the allocated slot in the file table.
 *
 *-------------------------------------------------------------------------*/

static int
HPSSFSAL_Common_Open_Bitfile(
apithrdstate_t	        *ThreadContext,	/* IN - thread context */
hpss_reqid_t		RequestID,      /* IN - request id */
hpssoid_t	        *BitFileID,	/* IN - bitfile id */
ns_ObjHandle_t	        *ObjHandlePtr,	/* IN - NS object handle */
TYPE_CRED_HPSS	        *Ucred,		/* IN - user credentials */
int		        Oflag,		/* IN - open flags */
TYPE_TOKEN_HPSS	        *AuthzTicket,	/* IN - client authorization */
api_dmap_attrs_t        *DMAttrs,       /* IN - DMAP attributes */
unsigned32              FilesetCOS,     /* IN - fileset containing file */
filetable_t	        *FTPtr,		/* IN - file table pointer */
int                     Fildes         /* IN - file table index */
#if defined( _USE_HPSS_62 ) || defined( _USE_HPSS_622 )
  , hpss_cos_hints_t	*HintsOut /* OUT - actual hint values used */
#endif
#ifdef _USE_HPSS_622 
  , u_signed64          *SegmentSize /* OUT - current storage segment size */
#endif
)
{
   static char 		function_name[] = "HPSSFSAL_Common_Open_Bitfile";
   long			error = 0;     /* returned error */
   retry_cb_t           retry_cb;      /* Retry control block */
   unsigned32		bfsopenflags;  /* open flags */
   hpss_object_handle_t	bfhandle;      /* handle to open bitfile */
   bf_attrib_t		bfattr;        /* attribute of open bitfile */
   TYPE_UUID_HPSS		uuid;          /* BFS server UUID */
   openfiletable_t      *open_ftptr;   /* open file table entry */
   open_bf_desc_t       *open_bfdesc_ptr; /* open bitfile descriptor */

#if defined( _USE_HPSS_62 ) || defined( _USE_HPSS_622)
   hpss_cos_md_t        cos_info,      /* COS metadata values */
                        *cos_info_ptr = NULL;  
   hpss_sclass_md_t     sclass_info,   /* SClass metadata values */
                        *sclass_info_ptr = NULL;
#endif
   
#if (defined(_USE_HPSS_51) && defined (API_DMAP_SUPPORT)	&& !defined(API_DMAP_GATEWAY))\
   ||((defined(_USE_HPSS_62)||defined(_USE_HPSS_622)) && defined (API_DMAP_SUPPORT)	&& !defined(API_DMAP_GATEWAY) &&  defined ( API_MIRRORED_FILESETS ))
   dmg_object_attrs_t   dmg_attr;      /* DMAP attributes */
   u_signed64           dmg_attr_bits; /* DMAP attributes bits */
   u_signed64           current_size;  /* current size of the file */
#endif


   /*
    * Get a pointer to the open file table
    * and the open bitfile descriptor.
    */

   open_ftptr = &FTPtr->OpenDesc[Fildes];
   open_bfdesc_ptr = &open_ftptr->descunion_u.OpenBF;

   /*
    * Translate the Oflag into BFS open flags.
    */

#ifdef _USE_HPSS_622
   if(Oflag & HPSS_O_STAGE_ASYNC && Oflag & HPSS_O_STAGE_BKGRD)
   {
      API_DEBUG_FPRINTF(DebugFile, &RequestID,
                        "%s: can't specify both STAGE_ASYNC & STAGE_BKGRD\n",
                        function_name);
      return(HPSS_EINVAL);
   }
#endif

   if ((Oflag & O_ACCMODE) == O_RDONLY)
   {
      bfsopenflags = BFS_OPEN_READ;
   }
   else if ((Oflag & O_ACCMODE) == O_WRONLY)
   {
      bfsopenflags = BFS_OPEN_WRITE;
   }
   else
   {
      bfsopenflags = BFS_OPEN_READ | BFS_OPEN_WRITE;
   }
	
   if(Oflag & O_APPEND) bfsopenflags |= BFS_OPEN_APPEND;
   if(Oflag & O_TRUNC)  bfsopenflags |= BFS_OPEN_TRUNCATE;
   if(Oflag & O_NONBLOCK)  bfsopenflags |= BFS_OPEN_NO_STAGE;

#ifdef _USE_HPSS_622
   if(Oflag & HPSS_O_STAGE_ASYNC)
   {
      bfsopenflags |= (BFS_OPEN_STAGE_ASYNC);
      bfsopenflags &= ~(BFS_OPEN_NO_STAGE);
      if(Oflag & O_NONBLOCK)  bfsopenflags |= BFS_OPEN_NDELAY;
   }

   if(Oflag & HPSS_O_STAGE_BKGRD)
   {
      bfsopenflags |= (BFS_OPEN_STAGE_BKGRD);
      bfsopenflags &= ~(BFS_OPEN_NO_STAGE);
      if(Oflag & O_NONBLOCK)  bfsopenflags |= BFS_OPEN_NDELAY;
   }


   /*
    * if the file is being opened just to be truncated, don't bother
    * trying to stage it.
    */
   if(bfsopenflags & BFS_OPEN_TRUNCATE && bfsopenflags & BFS_OPEN_WRITE)
   {
      bfsopenflags &= ~(BFS_OPEN_STAGE_BKGRD|HPSS_O_STAGE_ASYNC);
      bfsopenflags |= BFS_OPEN_NO_STAGE;
   }
#endif


#ifdef _USE_HPSS_62
   /* have we already gotten hints data? */ 
   if(HintsOut != NULL && HintsOut->COSId == 0) 
   {
      cos_info_ptr = &cos_info;
      sclass_info_ptr = &sclass_info;
   }   
#elif defined( _USE_HPSS_622 )
   /* have we already gotten hints data? */
   if((HintsOut != NULL && HintsOut->COSId == 0)
      ||
      SegmentSize != NULL)
   {
      cos_info_ptr = &cos_info;
      sclass_info_ptr = &sclass_info;
   }
#endif   


   
   
   /*
    * Get the Bitfile Server UUID from the bitfile SOID.
    */

   SOID_GetServerID(BitFileID, &uuid);
 
   /*
    *  Now try to issue an open the file in the BFS.
    */
#ifdef _USE_HPSS_51
   error = API_core_OpenFile(ThreadContext,
			     RequestID,
			     BitFileID,
			     *AuthzTicket,
			     bfsopenflags,
			     &bfhandle);
#elif defined( _USE_HPSS_62 ) || defined( _USE_HPSS_622 )
   error = API_core_OpenFile(ThreadContext,
			     RequestID,
			     BitFileID,
			     *AuthzTicket,
			     bfsopenflags,
                 	     cos_info_ptr,
                 	     sclass_info_ptr,
			     &bfhandle);
#endif

#if (defined(_USE_HPSS_51) && defined ( API_DMAP_SUPPORT ) && !defined (API_DMAP_GATEWAY )) \
    ||((defined(_USE_HPSS_62)||defined(_USE_HPSS_622)) && defined ( API_DMAP_SUPPORT ) && !defined (API_DMAP_GATEWAY ) && defined ( API_MIRRORED_FILESETS ))


   if ( error == HPSS_ENOTVALID ) 
   {

      /*
       * We are assuming that the client is trying to get all
       * the most valid data to the top level of the HPSS
       * hierarchy. The only way to make this possible is
       * to first make sure all the data in DMAP is backed
       * into HPSS properly (migrate) and then try to open
       * the file again. There should no need to stage data
       * on the second open because we will have just migrated
       * the valid data from the DMAP cache to the top level
       * in the HPSS hierarchy.
       */
	 
      memset(&bfattr,0,sizeof(bfattr));

      /*
       * Get the bitfile attributes because we need the 
       * the file size for the migrate
       */

      error = API_core_BitfileGetAttrs(ThreadContext,
				       RequestID,
				       BitFileID,
				       *AuthzTicket,
				       &bfattr);


      if ( error != HPSS_E_NOERROR )
      {
	 API_DEBUG_FPRINTF(DebugFile, &RequestID,
			   "%s: Couldn't get"
			   " bitfile attributes: %d\n",
			   function_name, error);
      }
      else if ( bfattr.BfAttribMd.Flags & CACHE_DATA_VALID )
      {

	 /*
	  * Migrate the data from DMAP
	  */

	 error = API_dmg_Migrate(ThreadContext,
				 RequestID,
				 &ThreadContext->UserCred,
				 &DMAttrs->DMGuuid,
				 DMAttrs->FilesetID,
				 DMAttrs->Handle,
				 DMAttrs->HandleLength,
				 cast64m(0),
				 bfattr.BfAttribMd.DataLen,
				 0);

      }

      if ( error == HPSS_E_NOERROR )
      {

	 /*
	  * Try to open the file again, but this time there
	  * should be no stage because we just updated the
	  * data a the top of the hierarchy.
	  */

	 error = API_core_OpenFile(ThreadContext,
				   RequestID,
				   BitFileID,
				   *AuthzTicket,
				   bfsopenflags,
				   &bfhandle);

	 /*
	  * If we were not able to migrate the cache
	  * return a EBUSY to the user.
	  */

	 if ( error == HPSS_ENOTVALID )
	 {
	    API_DEBUG_FPRINTF(DebugFile, &RequestID,
			      "%s: Could not migrate: %d\n",
			      function_name, error);

	    error = -EBUSY;
	 }
      }
   }
#else
   if ( error == HPSS_ENOTVALID )
   {
      API_DEBUG_FPRINTF(DebugFile, &RequestID,
			"%s: No dmap support compiled in\n",
			function_name);
      API_LogMsg(function_name,RequestID,CS_DEBUG,
		 COMMUNICATION_ERROR,WARNING,
		 API_HPSS_DATA_NOT_VALID,errno);
      error = -EINVAL;
   }
#endif

   if (error == 0)
   {

      /*
       *  We found an open table descriptor
       *  Fill in the descriptor with the open file state.
       */

      open_ftptr->Type = BFS_OPEN_HANDLE;

      open_bfdesc_ptr->FilesetCOS = FilesetCOS;
      if (ObjHandlePtr != (ns_ObjHandle_t *)NULL)
	 open_ftptr->ObjectHandle = *ObjHandlePtr;
      open_bfdesc_ptr->BFHandle = bfhandle;
      open_bfdesc_ptr->Offset = cast64m(0);
      open_bfdesc_ptr->OpenFlag = Oflag;
      open_bfdesc_ptr->DataConnPtr = NULL;
      memset(&open_bfdesc_ptr->DMattrs,0,sizeof(api_dmap_attrs_t));
      open_bfdesc_ptr->CoreServerUUID = uuid;
      open_bfdesc_ptr->DMattrs.FilesetType = CORE_FS_TYPE_HPSS_ONLY;
      open_bfdesc_ptr->Updates = 0;
      pthread_mutex_init(&open_bfdesc_ptr->Mutex,
			 pthread_mutexattr_default);
      pthread_cond_init(&open_bfdesc_ptr->Cond,
			pthread_condattr_default);


      if ( DMAttrs != NULL )
      {
         /*
          * Don't forget to save any DMAP information
          * in the open file descriptor entry
          */

         open_bfdesc_ptr->DMattrs = *DMAttrs;
      }


      /*
       *  Get a socket and put out a listen for data transfers.
       */

      error = API_OpenListenDesc(API_TRANSFER_TCP,
				 &open_bfdesc_ptr->ListenDesc,
				 &open_bfdesc_ptr->ListenAddr_u);

      if(error != HPSS_E_NOERROR)
      {
	 API_DEBUG_FPRINTF(DebugFile, &RequestID,
			   "Could not get listen socket."
			   " errno =%d\n",-error);

	 API_LogMsg(function_name,RequestID,CS_DEBUG,
		    COMMUNICATION_ERROR,WARNING,
		    API_OPEN_LISTEN_DESC_ERROR,error);

      }

#if defined(IPI3_SUPPORT)

      /*
       *  If we are doing IPI-3 transfers, then we need to open a
       *  data descriptor here.
       */

      if (API_TransferType == API_TRANSFER_IPI3)
      {
	 error = API_OpenListenDesc(API_TRANSFER_IPI3,
				    &open_bfdesc_ptr->DataDesc,
				    &open_bfdesc_ptr->DataAddr_u);

	 if (error != HPSS_E_NOERROR)
	 {
	    API_DEBUG_FPRINTF(DebugFile, &RequestID,
			      "Could not get data port."
			      " errno =%d\n",-error);

	    API_LogMsg(function_name,RequestID,CS_DEBUG,
		       COMMUNICATION_ERROR,WARNING,
		       API_OPEN_LISTEN_DESC_ERROR,error);

	 }
      }
#endif

      /*
       * Mark the file table entry as not busy.
       */

      API_LockMutex(&FTPtr->Mutex);
      open_ftptr->Flags = 0;
      API_UnlockMutex(&FTPtr->Mutex);

#if defined( _USE_HPSS_62 ) || defined (_USE_HPSS_622 )
      /*
       * If needed, build the return hints
       */

      if(HintsOut != NULL && HintsOut->COSId == 0) 
      {
	 HintsOut->COSId = cos_info.COSId;
	 strncpy((char *)HintsOut->COSName,
		 (char *)cos_info.COSName,
		 HPSS_MAX_COS_NAME);
	 HintsOut->Flags = cos_info.Flags;
	 HintsOut->OptimumAccessSize = cast64m(cos_info.OptimumAccessSize);
	 HintsOut->MinFileSize = cos_info.MinFileSize;
	 HintsOut->MaxFileSize = cos_info.MaxFileSize;
	 HintsOut->AccessFrequency = cos_info.AccessFrequency;
	 HintsOut->TransferRate = cos_info.TransferRate;
	 HintsOut->AvgLatency = cos_info.AvgLatency;
	 HintsOut->WriteOps = cos_info.WriteOps;
	 HintsOut->ReadOps = cos_info.ReadOps;
	 HintsOut->StageCode = cos_info.StageCode;
	 HintsOut->StripeWidth = sclass_info.StripeWidth;
	 HintsOut->StripeLength = sclass_info.StripeLength;
      }
#endif

#ifdef _USE_HPSS_622
      /*
       * If requested, return the storage segment size
       */

      if(SegmentSize != NULL) 
         *SegmentSize = sclass_info.StorageSegmentSize;
#endif

#if (defined( _USE_HPSS_51) && defined (API_DMAP_SUPPORT) && !defined(API_DMAP_GATEWAY) )\
  || ((defined( _USE_HPSS_62)||defined( _USE_HPSS_622)) && defined (API_DMAP_SUPPORT) && !defined(API_DMAP_GATEWAY) &&  defined ( API_MIRRORED_FILESETS ) )
      /*
       * If no errors and this file is not in a non-DMAP mannaged
       * area of the name space.
       */

      if( DMAttrs->FilesetType != CORE_FS_TYPE_HPSS_ONLY )
      {

	 /*
	  * If this file is in a DMAP mannaged area of the
	  * name space and the truncate flag was set for
	  * the open then, invalidate the file on the DMAP
	  * side.
	  */

	 if ( Oflag & O_TRUNC )
	 {

	    memset(&bfattr,0,sizeof(bfattr));

	    /*
	     * Get the bitfile attributes because we need the 
	     * file size for the invalidate
	     */

	    error = API_core_BitfileOpenGetAttrs(ThreadContext,
						 RequestID,
						 open_bfdesc_ptr,
						 &bfattr);

	    if ( error == HPSS_E_NOERROR )
	    {
	       memset(&dmg_attr,0,sizeof(dmg_object_attrs_t));
	       memset(&dmg_attr_bits,0,sizeof(u_signed64));

	       /*
		* File size should be zero
		*/

	       dmg_attr_bits = cast64m(CHANGE_FILESIZE);
	       dmg_attr.Type = NS_OBJECT_TYPE_FILE;
	       dmg_attr.Attrs.Attrs.DataLength = bld64m(0,0);

	       error = API_dmg_InvalidateCache(ThreadContext,
					       RequestID,
					       Ucred,
					       &DMAttrs->DMGuuid,
					       DMAttrs->FilesetID,
					       DMAttrs->Handle,
					       DMAttrs->HandleLength,
					       cast64m(0),
					       bfattr.BfAttribMd.DataLen,
					       0,
					       dmg_attr_bits,
					       &dmg_attr);

	    }
	 }
      }
#endif

   }

   return(error);
}


