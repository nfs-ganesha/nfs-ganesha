/*
 * Common FSAL functions
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
#include "FSAL/common_functions.h"

/* Internal and misc functions used by all/most FSALs
 */

void display_fsinfo(fsal_staticfsinfo_t *info) {
	LogDebug(COMPONENT_FSAL, "FileSystem info: {");
	LogDebug(COMPONENT_FSAL, "  maxfilesize  = %llX    ",
		 info->maxfilesize);
	LogDebug(COMPONENT_FSAL, "  maxlink  = %lu   ",
		 info->maxlink);
	LogDebug(COMPONENT_FSAL, "  maxnamelen  = %lu  ",
		 info->maxnamelen);
	LogDebug(COMPONENT_FSAL, "  maxpathlen  = %lu  ",
		 info->maxpathlen);
	LogDebug(COMPONENT_FSAL, "  no_trunc  = %d ",
		 info->no_trunc);
	LogDebug(COMPONENT_FSAL, "  chown_restricted  = %d ",
		 info->chown_restricted);
	LogDebug(COMPONENT_FSAL, "  case_insensitive  = %d ",
		 info->case_insensitive);
	LogDebug(COMPONENT_FSAL, "  case_preserving  = %d ",
		 info->case_preserving);
	LogDebug(COMPONENT_FSAL, "  fh_expire_type  = %hu ",
		 info->fh_expire_type);
	LogDebug(COMPONENT_FSAL, "  link_support  = %d  ",
		 info->link_support);
	LogDebug(COMPONENT_FSAL, "  symlink_support  = %d  ",
		 info->symlink_support);
	LogDebug(COMPONENT_FSAL, "  lock_support  = %d  ",
		 info->lock_support);
	LogDebug(COMPONENT_FSAL, "  lock_support_owner  = %d  ",
		 info->lock_support_owner);
	LogDebug(COMPONENT_FSAL, "  lock_support_async_block  = %d  ",
		 info->lock_support_async_block);
	LogDebug(COMPONENT_FSAL, "  named_attr  = %d  ",
		 info->named_attr);
	LogDebug(COMPONENT_FSAL, "  unique_handles  = %d  ",
		 info->unique_handles);
	LogDebug(COMPONENT_FSAL, "  acl_support  = %hu  ",
		 info->acl_support);
	LogDebug(COMPONENT_FSAL, "  cansettime  = %d  ",
		 info->cansettime);
	LogDebug(COMPONENT_FSAL, "  homogenous  = %d  ",
		 info->homogenous);
	LogDebug(COMPONENT_FSAL, "  supported_attrs  = %llX  ",
		 info->supported_attrs);
	LogDebug(COMPONENT_FSAL, "  maxread  = %llX     ",
		 info->maxread);
	LogDebug(COMPONENT_FSAL, "  maxwrite  = %llX     ",
		 info->maxwrite);
	LogDebug(COMPONENT_FSAL, "  umask  = %X ", info->umask);
	LogDebug(COMPONENT_FSAL, "  auth_exportpath_xdev  = %d  ",
		 info->auth_exportpath_xdev);
	LogDebug(COMPONENT_FSAL, "  xattr_access_rights = %#o ",
		 info->xattr_access_rights);
	LogDebug(COMPONENT_FSAL, "  accesscheck_support  = %d  ",
		 info->accesscheck_support);
	LogDebug(COMPONENT_FSAL, "  share_support  = %d  ",
		 info->share_support);
	LogDebug(COMPONENT_FSAL, "  share_support_owner  = %d  ",
		 info->share_support_owner);
	LogDebug(COMPONENT_FSAL, "}");
}

