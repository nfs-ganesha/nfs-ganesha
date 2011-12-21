#ifndef _COMMON_METHODS_H
#define _COMMON_METHODS_H
/*
 * Common FSAL methods
 */

fsal_status_t COMMON_CleanUpExportContext_noerror(fsal_export_context_t * p_export_context);

fsal_status_t COMMON_InitClientContext(fsal_op_context_t * p_thr_context);

fsal_status_t COMMON_GetClientContext(fsal_op_context_t * p_thr_context,  /* IN/OUT  */
				      fsal_export_context_t * p_export_context, /* IN */
				      fsal_uid_t uid,     /* IN */
				      fsal_gid_t gid,     /* IN */
				      fsal_gid_t * alt_groups,    /* IN */
				      fsal_count_t nb_alt_groups /* IN */ );

fsal_status_t COMMON_setattr_access_notsupp(fsal_op_context_t * p_context,        /* IN */
					    fsal_attrib_list_t * candidate_attributes,/* IN */
					    fsal_attrib_list_t * object_attributes    /* IN */);

fsal_status_t COMMON_rename_access(fsal_op_context_t * pcontext,  /* IN */
				   fsal_attrib_list_t * pattrsrc, /* IN */
				   fsal_attrib_list_t * pattrdest); /* IN */

fsal_status_t COMMON_rename_access_notsupp(fsal_op_context_t * pcontext,  /* IN */
					   fsal_attrib_list_t * pattrsrc, /* IN */
					   fsal_attrib_list_t * pattrdest);/* IN */

fsal_status_t COMMON_create_access(fsal_op_context_t * pcontext,  /* IN */
				   fsal_attrib_list_t * pattr);   /* IN */

fsal_status_t COMMON_unlink_access(fsal_op_context_t * pcontext,  /* IN */
				   fsal_attrib_list_t * pattr);   /* IN */

fsal_status_t COMMON_link_access(fsal_op_context_t * pcontext,    /* IN */
				 fsal_attrib_list_t * pattr);     /* IN */

fsal_status_t COMMON_merge_attrs(fsal_attrib_list_t * pinit_attr,
				 fsal_attrib_list_t * pnew_attr,
				 fsal_attrib_list_t * presult_attr);

#if 0
fsal_status_t COMMON_get_quota(fsal_path_t * pfsal_path,       /* IN */
			       int quota_type, /* IN */
			       fsal_uid_t fsal_uid,    /* IN */
			       fsal_quota_t * pquota);  /* OUT */

fsal_status_t COMMON_set_quota(fsal_path_t * pfsal_path,       /* IN */
			       int quota_type, /* IN */
			       fsal_uid_t fsal_uid,    /* IN */
			       fsal_quota_t * pquota,  /* IN */
			       fsal_quota_t * presquota);       /* OUT */
#endif

fsal_status_t COMMON_get_quota_noquota(fsal_path_t * pfsal_path,  /* IN */
				       int quota_type, fsal_uid_t fsal_uid,
				       fsal_quota_t * pquota); /* OUT */

fsal_status_t COMMON_set_quota_noquota(fsal_path_t * pfsal_path,  /* IN */
				       int quota_type, fsal_uid_t fsal_uid, /* IN */
				       fsal_quota_t * pquot,      /* IN */
				       fsal_quota_t * presquot);   /* OUT */

fsal_status_t COMMON_CleanObjectResources(fsal_handle_t * in_fsal_handle);

fsal_status_t COMMON_open_by_fileid(fsal_handle_t * filehandle,   /* IN */
				    fsal_u64_t fileid,    /* IN */
				    fsal_op_context_t * p_context,        /* IN */
				    fsal_openflags_t openflags,   /* IN */
				    fsal_file_t * file_descriptor,        /* OUT */
				    fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ );

fsal_status_t COMMON_close_by_fileid(fsal_file_t * file_descriptor /* IN */ ,
				     fsal_u64_t fileid);

fsal_status_t COMMON_rcp_by_fileid(fsal_handle_t * filehandle,    /* IN */
				   fsal_u64_t fileid,     /* IN */
				   fsal_op_context_t * p_context, /* IN */
				   fsal_path_t * p_local_path,    /* IN */
				   fsal_rcpflag_t transfer_opt /* IN */ );

fsal_status_t COMMON_getextattrs_notsupp(fsal_handle_t * p_filehandle, /* IN */
				 fsal_op_context_t * p_context,        /* IN */
				 fsal_extattrib_list_t * p_object_attributes /* OUT */ );

fsal_status_t COMMON_terminate_noerror();
#endif
