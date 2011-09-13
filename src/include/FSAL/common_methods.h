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
