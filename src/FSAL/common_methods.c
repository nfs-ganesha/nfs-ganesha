/*
 * Common FSAL methods
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define fsal_increment_nbcall( _f_,_struct_status_ )

#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/quota.h>
#include "log.h"
#include "fsal.h"
#include "FSAL/common_methods.h"


/* Methods shared by most/all fsals.
 * These are either used in place of or can be called from the fsal specific
 * method to handle common (base class) operations
 */

/* Export context
 */

/**
 * FSAL_CleanUpExportContext :
 * this will clean up and state in an export that was created during
 * the BuildExportContext phase.  For many FSALs this may be a noop.
 *
 * \param p_export_context
 */

fsal_status_t COMMON_CleanUpExportContext_noerror(fsal_export_context_t * p_export_context)
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanUpExportContext);
}

/* Client context */

fsal_status_t COMMON_InitClientContext(fsal_op_context_t * p_thr_context)
{
  /* sanity check */
  if(!p_thr_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* initialy set the export entry to none */
  p_thr_context->export_context = NULL;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);
}

#ifndef _USE_HPSS
fsal_status_t COMMON_GetClientContext(fsal_op_context_t * p_thr_context,  /* IN/OUT  */
                                    fsal_export_context_t * p_export_context,   /* IN */
                                    fsal_uid_t uid,     /* IN */
                                    fsal_gid_t gid,     /* IN */
                                    fsal_gid_t * alt_groups,    /* IN */
                                    fsal_count_t nb_alt_groups /* IN */ )
{
  fsal_count_t ng = nb_alt_groups;
  unsigned int i;

  /* sanity check */
  if(!p_thr_context || !p_export_context ||
     ((ng > 0) && (alt_groups == NULL)))
	  Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetClientContext);

  /* set the export specific context */
  p_thr_context->export_context = p_export_context;
  p_thr_context->credential.user = uid;
  p_thr_context->credential.group = gid;

  if(ng > FSAL_NGROUPS_MAX) /* this artificially truncates the group list ! */
	  ng = FSAL_NGROUPS_MAX;
  p_thr_context->credential.nbgroups = ng;

  for(i = 0; i < ng; i++)
	  p_thr_context->credential.alt_groups[i] = alt_groups[i];

  if(isFullDebug(COMPONENT_FSAL)) {
	  /* traces: prints p_credential structure */

	  LogFullDebug(COMPONENT_FSAL, "credential modified:\tuid = %d, gid = %d",
		       p_thr_context->credential.user,
		       p_thr_context->credential.group);
	  for(i = 0; i < p_thr_context->credential.nbgroups; i++)
		  LogFullDebug(COMPONENT_FSAL, "\tAlt grp: %d",
			       p_thr_context->credential.alt_groups[i]);
  }
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetClientContext);
}
#endif

/* Access controls
 */

/**
 * FSAL_test_setattr_access :
 * test if a client identified by cred can access setattr on the object
 * knowing its attributes and parent's attributes.
 * The following fields of the object_attributes structures MUST be filled :
 * acls (if supported), mode, owner, group.
 * This doesn't make any call to the filesystem,
 * as a result, this doesn't ensure that the file exists, nor that
 * the permissions given as parameters are the actual file permissions :
 * this must be ensured by the cache_inode layer, using VFSFSAL_getattrs,
 * for example.
 *
 * \param cred (in fsal_cred_t *) user's identifier.
 * \param candidate_attrbutes the attributes we want to set on the object
 * \param object_attributes (in fsal_attrib_list_t *) the cached attributes
 *        for the object.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t COMMON_setattr_access_notsupp(fsal_op_context_t * p_context,        /* IN */
                                  fsal_attrib_list_t * candidate_attributes,    /* IN */
                                  fsal_attrib_list_t * object_attributes        /* IN */
    )
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_setattr_access);
}                               /* FSAL_test_setattr_access */

/**
 * FSAL_rename_access :
 * test if a client identified by cred can be renamed on the object
 * knowing the parents attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattrsrc      source directory attributes
 * \param pattrdest     destination directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */

fsal_status_t COMMON_rename_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattrsrc, /* IN */
                                 fsal_attrib_list_t * pattrdest)        /* IN */
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_test_access(pcontext, FSAL_W_OK, pattrsrc);
  if(FSAL_IS_ERROR(fsal_status))
    Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_rename_access);

  fsal_status = FSAL_test_access(pcontext, FSAL_W_OK, pattrdest);
  if(FSAL_IS_ERROR(fsal_status))
    Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_rename_access);

  /* If this point is reached, then access is granted */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_rename_access);
}                               /* FSAL_rename_access */

/* Not supported version
 */

fsal_status_t COMMON_rename_access_notsupp(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattrsrc, /* IN */
                                 fsal_attrib_list_t * pattrdest)        /* IN */
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_rename_access);
}                               /* FSAL_rename_access */

/**
 * FSAL_create_access :
 * test if a client identified by cred can create an object within a directory knowing its attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattr      source directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t COMMON_create_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattr)    /* IN */
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_test_access(pcontext, FSAL_W_OK, pattr);
  if(FSAL_IS_ERROR(fsal_status))
    Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_create_access);

  /* If this point is reached, then access is granted */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_create_access);
}                               /* FSAL_create_access */

/**
 * FSAL_unlink_access :
 * test if a client identified by cred can unlink on a directory knowing its attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattr      source directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */
fsal_status_t COMMON_unlink_access(fsal_op_context_t * pcontext,  /* IN */
                                 fsal_attrib_list_t * pattr)    /* IN */
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_test_access(pcontext, FSAL_W_OK, pattr);
  if(FSAL_IS_ERROR(fsal_status))
    Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_unlink_access);

  /* If this point is reached, then access is granted */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlink_access);

}                               /* FSAL_unlink_access */

/**
 * FSAL_link_access :
 * test if a client identified by cred can link to a directory knowing its attributes
 *
 * \param pcontext (in fsal_cred_t *) user's context.
 * \param pattr      destination directory attributes
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_ACCESS       (Permission denied)
 *        - ERR_FSAL_FAULT        (null pointer parameter)
 *        - ERR_FSAL_INVAL        (missing attributes : mode, group, user,...)
 *        - ERR_FSAL_SERVERFAULT  (unexpected error)
 */

fsal_status_t COMMON_link_access(fsal_op_context_t * pcontext,    /* IN */
                               fsal_attrib_list_t * pattr)      /* IN */
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_test_access(pcontext, FSAL_W_OK, pattr);
  if(FSAL_IS_ERROR(fsal_status))
    Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_unlink_access);

  /* If this point is reached, then access is granted */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_link_access);
}                               /* FSAL_link_access */

/**
 * FSAL_merge_attrs: merge to attributes structure.
 *
 * This functions merge the second attributes list into the first argument. 
 * Results in returned in the last argument.
 *
 * @param pinit_attr   [IN] attributes to be changed
 * @param pnew_attr    [IN] attributes to be added
 * @param presult_attr [IN] resulting attributes
 * 
 * @return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_INVAL        Invalid argument(s)
 */

fsal_status_t COMMON_merge_attrs(fsal_attrib_list_t * pinit_attr,
                               fsal_attrib_list_t * pnew_attr,
                               fsal_attrib_list_t * presult_attr)
{
  if(pinit_attr == NULL || pnew_attr == NULL || presult_attr == NULL)
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_merge_attrs);

  /* The basis for the result attr is the fist argument */
  *presult_attr = *pinit_attr;

  /* Now deal with the attributes to be merged in this set of attributes */
  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_MODE))
    presult_attr->mode = pnew_attr->mode;

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_OWNER))
    presult_attr->owner = pnew_attr->owner;

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_GROUP))
    presult_attr->group = pnew_attr->group;

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_SIZE))
    presult_attr->filesize = pnew_attr->filesize;

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_SPACEUSED))
    presult_attr->spaceused = pnew_attr->spaceused;

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_ATIME))
    {
      presult_attr->atime.seconds = pnew_attr->atime.seconds;
      presult_attr->atime.nseconds = pnew_attr->atime.nseconds;
    }

  if(FSAL_TEST_MASK(pnew_attr->asked_attributes, FSAL_ATTR_MTIME))
    {
      presult_attr->mtime.seconds = pnew_attr->mtime.seconds;
      presult_attr->mtime.nseconds = pnew_attr->mtime.nseconds;
    }

  /* Do not forget the ctime */
  FSAL_SET_MASK(presult_attr->asked_attributes, FSAL_ATTR_CTIME);
  presult_attr->ctime.seconds = pnew_attr->ctime.seconds;
  presult_attr->ctime.nseconds = pnew_attr->ctime.nseconds;

  /* Regular exit */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_merge_attrs);
}                               /* FSAL_merge_attrs */


/* Quota management
 */

#if 0

/**
 * FSAL_get_quota :
 * Gets the quota for a given path.
 *
 * \param  pfsal_path
 *        path to the filesystem whose quota are requested
 * \param quota_type
 *         the kind of requested quota (user or group)
 * \param  fsal_uid
 * 	  uid for the user whose quota are requested
 * \param pquota (input):
 *        Parameter to the structure containing the requested quotas
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t COMMON_get_quota(fsal_path_t * pfsal_path,       /* IN */
                                int quota_type, /* IN */
                                fsal_uid_t fsal_uid,    /* IN */
                                fsal_quota_t * pquota)  /* OUT */
{
  struct dqblk fs_quota;
  char fs_spec[MAXPATHLEN];

  if(!pfsal_path || !pquota)
    ReturnCode(ERR_FSAL_FAULT, 0);

  if(fsal_internal_path2fsname(pfsal_path->path, fs_spec) == -1)
    ReturnCode(ERR_FSAL_INVAL, 0);

  memset((char *)&fs_quota, 0, sizeof(struct dqblk));

  if(quotactl(FSAL_QCMD(Q_GETQUOTA, quota_type), fs_spec, fsal_uid, (caddr_t) & fs_quota)
     < 0)
    ReturnCode(posix2fsal_error(errno), errno);

  /* Convert XFS structure to FSAL one */
  pquota->bhardlimit = fs_quota.dqb_bhardlimit;
  pquota->bsoftlimit = fs_quota.dqb_bsoftlimit;
  pquota->curblocks = fs_quota.dqb_curspace;
  pquota->fhardlimit = fs_quota.dqb_ihardlimit;
  pquota->curfiles = fs_quota.dqb_curinodes;
  pquota->btimeleft = fs_quota.dqb_btime;
  pquota->ftimeleft = fs_quota.dqb_itime;
  pquota->bsize = DEV_BSIZE;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /*  FSAL_get_quota */

/**
 * FSAL_get_quota :
 * Gets the quota for a given path.
 *
 * \param  pfsal_path
 *        path to the filesystem whose quota are requested
 * \param quota_type
 *         the kind of requested quota (user or group)
 * \param  fsal_uid
 * 	  uid for the user whose quota are requested
 * \param pquota (input):
 *        pointer to the structure containing the wanted quotas
 * \param presquot (output)
 *        pointer to the structure containing the resulting quotas
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */

fsal_status_t COMMON_set_quota(fsal_path_t * pfsal_path,       /* IN */
                                int quota_type, /* IN */
                                fsal_uid_t fsal_uid,    /* IN */
                                fsal_quota_t * pquota,  /* IN */
                                fsal_quota_t * presquota)       /* OUT */
{
  struct dqblk fs_quota;
  fsal_status_t fsal_status;
  char fs_spec[MAXPATHLEN];

  if(!pfsal_path || !pquota)
    ReturnCode(ERR_FSAL_FAULT, 0);

  if(fsal_internal_path2fsname(pfsal_path->path, fs_spec) == -1)
    ReturnCode(ERR_FSAL_INVAL, 0);

  memset((char *)&fs_quota, 0, sizeof(struct dqblk));

  /* Convert FSAL structure to XFS one */
  if(pquota->bhardlimit != 0)
    {
      fs_quota.dqb_bhardlimit = pquota->bhardlimit;
      fs_quota.dqb_valid |= QIF_BLIMITS;
    }

  if(pquota->bsoftlimit != 0)
    {
      fs_quota.dqb_bsoftlimit = pquota->bsoftlimit;
      fs_quota.dqb_valid |= QIF_BLIMITS;
    }

  if(pquota->fhardlimit != 0)
    {
      fs_quota.dqb_ihardlimit = pquota->fhardlimit;
      fs_quota.dqb_valid |= QIF_ILIMITS;
    }

  if(pquota->btimeleft != 0)
    {
      fs_quota.dqb_btime = pquota->btimeleft;
      fs_quota.dqb_valid |= QIF_BTIME;
    }

  if(pquota->ftimeleft != 0)
    {
      fs_quota.dqb_itime = pquota->ftimeleft;
      fs_quota.dqb_valid |= QIF_ITIME;
    }

  if(quotactl(FSAL_QCMD(Q_SETQUOTA, quota_type), fs_spec, fsal_uid, (caddr_t) & fs_quota)
     < 0)
    ReturnCode(posix2fsal_error(errno), errno);

  if(presquota != NULL)
    {
      fsal_status = FSAL_get_quota(pfsal_path, quota_type, fsal_uid, presquota);

      if(FSAL_IS_ERROR(fsal_status))
        return fsal_status;
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /*  FSAL_set_quota */

#endif

/* No quota support case
 */

/**
 * FSAL_get_quota :
 * Gets the quota for a given path.
 *
 * \param  pfsal_path
 *        path to the filesystem whose quota are requested
 * \param  fsal_uid
 * 	  uid for the user whose quota are requested
 * \param pquota (input):
 *        Parameter to the structure containing the requested quotas
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t COMMON_get_quota_noquota(fsal_path_t * pfsal_path,  /* IN */
                             int quota_type, fsal_uid_t fsal_uid, fsal_quota_t * pquota)        /* OUT */
{
  ReturnCode(ERR_FSAL_NO_QUOTA, 0);
}                               /*  FSAL_get_quota */

/**
 * FSAL_get_quota :
 * Gets the quota for a given path.
 *
 * \param  pfsal_path
 *        path to the filesystem whose quota are requested
 * \param  fsal_uid
 * 	  uid for the user whose quota are requested
 * \param pquota (input):
 *        pointer to the structure containing the wanted quotas
 * \param presquot (output)
 *        pointer to the structure containing the resulting quotas
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */

fsal_status_t COMMON_set_quota_noquota(fsal_path_t * pfsal_path,  /* IN */
                             int quota_type, fsal_uid_t fsal_uid,       /* IN */
                             fsal_quota_t * pquot,      /* IN */
                             fsal_quota_t * presquot)   /* OUT */
{
  ReturnCode(ERR_FSAL_NO_QUOTA, 0);
}                               /*  FSAL_set_quota */

/**
 * FSAL_check_quota :
 * checks if quotas allow a user to do an operation
 *
 * \param  pfsal_path
 *        path to the filesystem whose quota are requested
 * \param  quota_type
 *        type of quota to be checked (inodes or blocks       
 * \param  fsal_uid
 *        uid for the user whose quota are requested
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */


fsal_status_t COMMON_check_quota( char              * pfsal_path,  /* IN */
                                  fsal_quota_type_t   quota_type,
                                  fsal_uid_t          fsal_uid)      /* IN */
{
   ReturnCode(ERR_FSAL_NO_ERROR, 0) ;
} /* COMMON_check_quota */

/* Object Resources
 */

/**
 * FSAL_CleanObjectResources:
 * This function cleans remanent internal resources
 * that are kept for a given FSAL handle.
 *
 * \param in_fsal_handle (input):
 *        The handle whose the resources are to be cleaned.
 */

fsal_status_t COMMON_CleanObjectResources(fsal_handle_t * in_fsal_handle)
{

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_CleanObjectResources);

}

/* operation by file id.  PROXY specific.
 */

fsal_status_t COMMON_open_by_fileid(fsal_handle_t * filehandle,   /* IN */
                                  fsal_u64_t fileid,    /* IN */
                                  fsal_op_context_t * p_context,        /* IN */
                                  fsal_openflags_t openflags,   /* IN */
                                  fsal_file_t * file_descriptor,        /* OUT */
                                  fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_open_by_fileid);
}

fsal_status_t COMMON_close_by_fileid(fsal_file_t * file_descriptor /* IN */ ,
                                   fsal_u64_t fileid)
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_open_by_fileid);
}

/**
 * FSAL_getetxattrs:
 * Get attributes for the object specified by its filehandle.
 *
 * \param filehandle (input):
 *        The handle of the object to get parameters.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param object_attributes (mandatory input/output):
 *        The retrieved attributes for the object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */
fsal_status_t COMMON_getextattrs_notsupp(fsal_handle_t * p_filehandle, /* IN */
                                   fsal_op_context_t * p_context,        /* IN */
                                   fsal_extattrib_list_t * p_object_attributes /* OUT */
    )
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_getextattrs);
}

/*
 * init/terminate
 */


/* To be called before exiting */
fsal_status_t COMMON_terminate_noerror()
{
  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/* Parameter management and initialization
 */

/**
 * Those routines set the default parameters
 * for FSAL init structure.
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 */

#define STRCMP   strcasecmp

fsal_status_t COMMON_SetDefault_FSAL_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* init max FS calls = unlimited */
  out_parameter->fsal_info.max_fs_calls = 0;

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t COMMON_SetDefault_FS_common_parameter(fsal_parameter_t * out_parameter)
{
  /* defensive programming... */
  if(out_parameter == NULL)
    ReturnCode(ERR_FSAL_FAULT, 0);

  /* set default values for all parameters of fs_common_info */

  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxfilesize);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxlink);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxnamelen);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxpathlen);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, no_trunc);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, chown_restricted);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, case_insensitive);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, case_preserving);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, fh_expire_type);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, link_support);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, symlink_support);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, lock_support);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, lock_support_owner);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, lock_support_async_block);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, named_attr);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, unique_handles);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, lease_time);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, acl_support);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, cansettime);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, homogenous);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, supported_attrs);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxread);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, maxwrite);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, umask);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, auth_exportpath_xdev);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, xattr_access_rights);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, accesscheck_support);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, share_support);
  FSAL_SET_INIT_DEFAULT(out_parameter->fs_common_info, share_support_owner);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}

/**
 * FSAL_load_FSAL_parameter_from_conf,
 * FSAL_load_FS_common_parameter_from_conf,
 * FSAL_load_FS_specific_parameter_from_conf:
 *
 * Those functions initialize the FSAL init parameter
 * structure from a configuration structure.
 *
 * \param in_config (input):
 *        Structure that represents the parsed configuration file.
 * \param out_parameter (ouput)
 *        FSAL initialization structure filled according
 *        to the configuration file given as parameter.
 *
 * \return ERR_FSAL_NO_ERROR (no error) ,
 *         ERR_FSAL_NOENT (missing a mandatory stanza in config file),
 *         ERR_FSAL_INVAL (invalid parameter),
 *         ERR_FSAL_SERVERFAULT (unexpected error)
 *         ERR_FSAL_FAULT (null pointer given as parameter),
 */

fsal_status_t COMMON_load_FSAL_parameter_from_conf(config_file_t in_config,
                                                 fsal_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;

  int DebugLevel = -1;
  char *LogFile = NULL;

  block = config_FindItemByName(in_config, CONF_LABEL_FSAL);

  /* cannot read item */

  if(block == NULL)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
              CONF_LABEL_FSAL);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
              CONF_LABEL_FSAL);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  /* read variable for fsal init */

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      err = config_GetKeyValue(item, &key_name, &key_value);
      if(err)
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_FSAL);
          ReturnCode(ERR_FSAL_SERVERFAULT, err);
        }

      if(!STRCMP(key_name, "DebugLevel"))
        {
          DebugLevel = ReturnLevelAscii(key_value);

          if(DebugLevel == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Invalid debug level name: \"%s\".",
                      key_value);
              ReturnCode(ERR_FSAL_INVAL, -1);
            }

        }
      else if(!STRCMP(key_name, "LogFile"))
        {

          LogFile = key_value;

        }
      else if(!STRCMP(key_name, "Max_FS_calls"))
        {

          int maxcalls = s_read_int(key_value);

          if(maxcalls < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: null or positive integer expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          out_parameter->fsal_info.max_fs_calls = (unsigned int)maxcalls;

        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
                  key_name, CONF_LABEL_FSAL);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

    }

  /* init logging */

  if(LogFile)
    SetComponentLogFile(COMPONENT_FSAL, LogFile);

  if(DebugLevel != -1)
    SetComponentLogLevel(COMPONENT_FSAL, DebugLevel);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FSAL_parameter_from_conf */

/* load general filesystem configuration options */

fsal_status_t COMMON_load_FS_common_parameter_from_conf(config_file_t in_config,
                                                      fsal_parameter_t * out_parameter)
{
  int err;
  int var_max, var_index;
  char *key_name;
  char *key_value;
  config_item_t block;

  block = config_FindItemByName(in_config, CONF_LABEL_FS_COMMON);

  /* cannot read item */
  if(block == NULL)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Cannot read item \"%s\" from configuration file",
              CONF_LABEL_FS_COMMON);
      ReturnCode(ERR_FSAL_NOENT, 0);
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      LogCrit(COMPONENT_CONFIG,
              "FSAL LOAD PARAMETER: Item \"%s\" is expected to be a block",
              CONF_LABEL_FS_COMMON);
      ReturnCode(ERR_FSAL_INVAL, 0);
    }

  /*
     configurable common info for filesystem are:
     link_support      # hardlink support
     symlink_support   # symlinks support
     cansettime        # Is it possible to change file times
     maxread           # Max read size from FS
     maxwrite          # Max write size to FS
     umask
     auth_exportpath_xdev
     xattr_access_rights

   */

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      err = config_GetKeyValue(item, &key_name, &key_value);
      if(err)
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR reading key[%d] from section \"%s\" of configuration file.",
                  var_index, CONF_LABEL_FS_COMMON);
          ReturnCode(ERR_FSAL_SERVERFAULT, err);
        }

      /* does the variable exists ? */
      if(!STRCMP(key_name, "link_support"))
        {

          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, link_support,
                             FSAL_INIT_MAX_LIMIT, bool);

        }
      else if(!STRCMP(key_name, "symlink_support"))
        {
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, symlink_support,
                             FSAL_INIT_MAX_LIMIT, bool);
        }
      else if(!STRCMP(key_name, "cansettime"))
        {
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: 0 or 1 expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          /* if set to false, force value to false.
           * else keep fs default.
           */
          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, cansettime,
                             FSAL_INIT_MAX_LIMIT, bool);

        }
      else if(!STRCMP(key_name, "maxread"))
        {
          fsal_u64_t size;

          if(s_read_int64(key_value, &size))
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, maxread,
                             FSAL_INIT_FORCE_VALUE, size);

        }
      else if(!STRCMP(key_name, "maxwrite"))
        {
          fsal_u64_t size;

          if(s_read_int64(key_value, &size))
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: positive integer expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, maxwrite,
                             FSAL_INIT_FORCE_VALUE, size);

        }
      else if(!STRCMP(key_name, "umask"))
        {
          int mode = s_read_octal(key_value);

          if(mode < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: octal expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, umask,
                             FSAL_INIT_FORCE_VALUE, unix2fsal_mode(mode));

        }
      else if(!STRCMP(key_name, "auth_xdev_export"))
        {
          int bool = StrToBoolean(key_value);

          if(bool == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: boolean expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, auth_exportpath_xdev,
                             FSAL_INIT_FORCE_VALUE, bool);
        }
      else if(!STRCMP(key_name, "xattr_access_rights"))
        {
          int mode = s_read_octal(key_value);

          if(mode < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "FSAL LOAD PARAMETER: ERROR: Unexpected value for %s: octal expected.",
                      key_name);
              ReturnCode(ERR_FSAL_INVAL, 0);
            }

          FSAL_SET_INIT_INFO(out_parameter->fs_common_info, xattr_access_rights,
                             FSAL_INIT_FORCE_VALUE, unix2fsal_mode(mode));

        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "FSAL LOAD PARAMETER: ERROR: Unknown or unsettable key: %s (item %s)",
                   key_name, CONF_LABEL_FS_COMMON);
          ReturnCode(ERR_FSAL_INVAL, 0);
        }

    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);

}                               /* FSAL_load_FS_common_parameter_from_conf */

fsal_status_t COMMON_share_op_notsupp( fsal_file_t       * p_file_descriptor,   /* IN */
                                       fsal_handle_t     * p_filehandle,        /* IN */
                                       fsal_op_context_t * p_context,           /* IN */
                                       void              * p_owner,             /* IN (opaque to FSAL) */
                                       fsal_share_param_t  request_share )      /* IN */
{
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_share_op);
}
