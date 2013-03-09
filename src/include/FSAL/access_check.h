#ifndef _ACCESS_CHECK_H
#define _ACCESS_CHECK_H

/* A few headers required to have "struct stat" */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/* fsal_test_access
 * common (default) access check method for fsal_obj_handle objects.
 */

fsal_status_t fsal_test_access(struct fsal_obj_handle *obj_hdl,
			       struct req_op_context *req_ctx,
			       fsal_accessflags_t access_type);


/* sticky bit access check for delete and rename actions
 * obj_hdl == NULL, just do directory check
 */

static inline bool sticky_dir_allows(struct fsal_obj_handle *dir_hdl,
                                     struct fsal_obj_handle *obj_hdl,
                                     const struct user_cred *creds)
{
	struct fsal_export *exp_hdl = dir_hdl->export;
	struct attrlist *dir_attr = &dir_hdl->attributes;
	struct attrlist *obj_attr = NULL;
	bool retval = true;

	if(obj_hdl)
		obj_attr = &obj_hdl->attributes;
	if(exp_hdl->ops->fs_supports(exp_hdl, fso_dirs_have_sticky_bit) &&
	   dir_attr->mode & S_ISVTX &&
	   dir_attr->owner != creds->caller_uid &&
	   (obj_attr && (obj_attr->owner != creds->caller_uid)) &&
	   creds->caller_uid != 0)
		retval = false;
	return retval;
}

void fsal_set_credentials(const struct user_cred *creds);
void fsal_save_ganesha_credentials();
void fsal_restore_ganesha_credentials();

#endif
