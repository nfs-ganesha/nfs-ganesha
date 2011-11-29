#ifndef _ACCESS_CHECK_H
#define _ACCESS_CHECK_H

/* A few headers required to have "struct stat" */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * file/object access checking
 * Deprecated
 */

fsal_status_t fsal_check_access(fsal_op_context_t * p_context,   /* IN */
				fsal_accessflags_t access_type,  /* IN */
				struct stat *p_buffstat, /* IN */
				fsal_attrib_list_t * p_object_attributes /* IN */ );

/* fsal_test_access
 * common (default) access check method for fsal_obj_handle objects.
 */

fsal_status_t fsal_test_access(struct fsal_obj_handle *obj_hdl,
			       struct user_cred *creds,
			       fsal_accessflags_t access_type);


/* sticky bit access check for delete and rename actions
 * obj_hdl == NULL, just do directory check
 */

static inline fsal_boolean_t sticky_dir_allows(struct fsal_obj_handle *dir_hdl,
					       struct fsal_obj_handle *obj_hdl,
					       struct user_cred *creds)
{
	struct fsal_export *exp_hdl = dir_hdl->export;
	fsal_attrib_list_t *dir_attr = &dir_hdl->attributes;
	fsal_attrib_list_t *obj_attr = NULL;
	fsal_boolean_t retval = TRUE;

	if(obj_hdl)
		obj_attr = &obj_hdl->attributes;
	if(exp_hdl->ops->fs_supports(exp_hdl, dirs_have_sticky_bit) &&
	   dir_attr->mode & FSAL_MODE_SVTX &&
	   dir_attr->owner != creds->caller_uid &&
	   (obj_attr && (obj_attr->owner != creds->caller_uid)) &&
	   creds->caller_uid != 0)
		retval = FALSE;
	return retval;
}
#endif 
