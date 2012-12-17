#ifndef _ACCESS_CHECK_H
#define _ACCESS_CHECK_H

/* A few headers required to have "struct stat" */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * file/object access checking
 */

fsal_status_t fsal_check_access_acl(fsal_op_context_t * p_context,   /* IN */
                                    fsal_aceperm_t v4mask,  /* IN */
                                    fsal_attrib_list_t * p_object_attributes   /* IN */ );

fsal_status_t fsal_check_access_no_acl(fsal_op_context_t * p_context,   /* IN */
                                       fsal_accessflags_t access_type,  /* IN */
                                       struct stat *p_buffstat, /* IN */
                                       fsal_attrib_list_t * p_object_attributes /* IN */ );

fsal_status_t fsal_check_access(fsal_op_context_t * p_context,   /* IN */
				fsal_accessflags_t access_type,  /* IN */
				struct stat *p_buffstat, /* IN */
				fsal_attrib_list_t * p_object_attributes /* IN */ );

#ifdef _USE_NFS4_ACL
static inline fsal_boolean_t fsal_check_ace_owner(fsal_uid_t uid, fsal_op_context_t *p_context)
{
  return (p_context->credential.user == uid);
}

fsal_boolean_t fsal_check_ace_group(fsal_gid_t gid, fsal_op_context_t *p_context);

fsal_boolean_t fsal_check_ace_matches(fsal_ace_t *pace,
                                      fsal_op_context_t *p_context,
                                      fsal_boolean_t is_owner,
                                      fsal_boolean_t is_group);

fsal_boolean_t fsal_check_ace_applicable(fsal_ace_t *pace,
                                         fsal_op_context_t *p_context,
                                         fsal_boolean_t is_dir,
                                         fsal_boolean_t is_owner,
                                         fsal_boolean_t is_group);

int display_fsal_v4mask(struct display_buffer * dspbuf,
                        fsal_aceperm_t          v4mask,
                        fsal_boolean_t          is_dir);
#endif

void fsal_set_credentials(fsal_op_context_t * context);
void fsal_save_ganesha_credentials();
void fsal_restore_ganesha_credentials();

#endif
