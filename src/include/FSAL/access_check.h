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
			       fsal_accessflags_t access_type,
			       fsal_accessflags_t *allowed,
			       fsal_accessflags_t *denied);

int display_fsal_v4mask(struct display_buffer *dspbuf, fsal_aceperm_t v4mask,
			bool is_dir);

void fsal_set_credentials(const struct user_cred *creds);
void fsal_save_ganesha_credentials();
void fsal_restore_ganesha_credentials();

#endif
