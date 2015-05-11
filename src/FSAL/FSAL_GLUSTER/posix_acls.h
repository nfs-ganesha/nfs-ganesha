#include <sys/acl.h>

#include "fsal_types.h"

/* permission set for ACE's */
#define FSAL_ACE_PERM_SET_DEFAULT \
	(FSAL_ACE_PERM_READ_ACL	| FSAL_ACE_PERM_READ_ATTR \
	| FSAL_ACE_PERM_SYNCHRONIZE)
#define FSAL_ACE_PERM_SET_DEFAULT_WRITE \
	(FSAL_ACE_PERM_WRITE_DATA | FSAL_ACE_PERM_APPEND_DATA)
#define FSAL_ACE_PERM_SET_OWNER_WRITE \
	(FSAL_ACE_PERM_WRITE_ACL | FSAL_ACE_PERM_WRITE_ATTR)

fsal_status_t
posix_acl_2_fsal_acl(acl_t p_posixacl, fsal_acl_t **p_falacl);

acl_t
fsal_acl_2_posix_acl(fsal_acl_t *p_fsalacl);

acl_entry_t
find_entry(acl_t acl, acl_tag_t tag, int id);
