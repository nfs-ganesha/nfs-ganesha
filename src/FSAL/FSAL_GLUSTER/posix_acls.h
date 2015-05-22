#include <sys/acl.h>

#include "fsal_types.h"

/* inheritance flags checks */
#define IS_FSAL_ACE_HAS_INHERITANCE_FLAGS(ACE) \
	(IS_FSAL_ACE_FILE_INHERIT(ACE) | IS_FSAL_ACE_DIR_INHERIT(ACE) | \
	IS_FSAL_ACE_NO_PROPAGATE(ACE) | IS_FSAL_ACE_INHERIT_ONLY(ACE))

#define IS_FSAL_ACE_APPLICABLE_FOR_BOTH_ACL(ACE) \
	(IS_FSAL_ACE_FILE_INHERIT(ACE) & IS_FSAL_ACE_DIR_INHERIT(ACE) & \
	!IS_FSAL_ACE_INHERIT_ONLY(ACE))

#define IS_FSAL_ACE_APPLICABLE_ONLY_FOR_INHERITED_ACL(ACE) \
	(IS_FSAL_ACE_FILE_INHERIT(ACE) & IS_FSAL_ACE_DIR_INHERIT(ACE) & \
	IS_FSAL_ACE_INHERIT_ONLY(ACE))

/* permission set for ACE's */
#define FSAL_ACE_PERM_SET_DEFAULT \
	(FSAL_ACE_PERM_READ_ACL	| FSAL_ACE_PERM_READ_ATTR \
	| FSAL_ACE_PERM_SYNCHRONIZE)
#define FSAL_ACE_PERM_SET_DEFAULT_WRITE \
	(FSAL_ACE_PERM_WRITE_DATA | FSAL_ACE_PERM_APPEND_DATA)
#define FSAL_ACE_PERM_SET_OWNER_WRITE \
	(FSAL_ACE_PERM_WRITE_ACL | FSAL_ACE_PERM_WRITE_ATTR)
#define FSAL_ACE_PERM_SET_DEFAULT_WRITE_DIR \
	(FSAL_ACE_PERM_WRITE_DATA | FSAL_ACE_PERM_APPEND_DATA \
	| FSAL_ACE_PERM_DELETE_CHILD)

fsal_status_t
posix_acl_2_fsal_acl(acl_t p_posixacl, fsal_acl_t **p_falacl);

acl_t
fsal_acl_2_posix_acl(fsal_acl_t *p_fsalacl, acl_type_t type);

acl_entry_t
find_entry(acl_t acl, acl_tag_t tag, int id);

fsal_status_t
posix_acl_2_fsal_acl_for_dir(acl_t e_acl, acl_t i_acl, fsal_acl_t **p_falacl);
