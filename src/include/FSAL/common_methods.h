/*
 * Common FSAL methods
 */

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

