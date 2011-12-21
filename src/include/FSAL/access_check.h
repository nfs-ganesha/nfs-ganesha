#ifndef _ACCESS_CHECK_H
#define _ACCESS_CHECK_H

/* A few headers required to have "struct stat" */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * file/object access checking
 */

fsal_status_t fsal_check_access(fsal_op_context_t * p_context,   /* IN */
				fsal_accessflags_t access_type,  /* IN */
				struct stat *p_buffstat, /* IN */
				fsal_attrib_list_t * p_object_attributes /* IN */ );
#endif 
