/*
 * file/object access checking
 */

fsal_status_t fsal_check_access(fsal_op_context_t * p_context,   /* IN */
				fsal_accessflags_t access_type,  /* IN */
				struct stat *p_buffstat, /* IN */
				fsal_attrib_list_t * p_object_attributes /* IN */ );
