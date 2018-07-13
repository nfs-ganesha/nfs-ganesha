/**
 * @addtogroup FSAL
 * @{
 */

/**
 * @file access_check.c
 * @brief File/object access checking
 */

#include "config.h"

#include "fsal.h"
#include "nfs_core.h"
#include <sys/stat.h>
#include "FSAL/access_check.h"
#include <stdbool.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <grp.h>
#include <sys/types.h>
#include <os/subr.h>

static bool fsal_check_ace_owner(uid_t uid, struct user_cred *creds)
{
	return (creds->caller_uid == uid);
}

static bool fsal_check_ace_group(gid_t gid, struct user_cred *creds)
{
	int i;

	if (creds->caller_gid == gid)
		return true;

	for (i = 0; i < creds->caller_glen; i++) {
		if (creds->caller_garray[i] == gid)
			return true;
	}

	return false;
}

static bool fsal_check_ace_matches(fsal_ace_t *pace, struct user_cred *creds,
				   bool is_owner, bool is_group)
{
	bool result = false;
	char *cause = "";

	if (IS_FSAL_ACE_SPECIAL_ID(*pace))
		switch (pace->who.uid) {
		case FSAL_ACE_SPECIAL_OWNER:
			if (is_owner) {
				result = true;
				cause = "special owner";
			}
			break;

		case FSAL_ACE_SPECIAL_GROUP:
			if (is_group) {
				result = true;
				cause = "special group";
			}
			break;

		case FSAL_ACE_SPECIAL_EVERYONE:
			result = true;
			cause = "special everyone";
			break;

		default:
			break;
	} else if (IS_FSAL_ACE_GROUP_ID(*pace)) {
		if (fsal_check_ace_group(pace->who.gid, creds)) {
			result = true;
			cause = "group";
		}
	} else {
		if (fsal_check_ace_owner(pace->who.uid, creds)) {
			result = true;
			cause = "owner";
		}
	}

	LogFullDebug(COMPONENT_NFS_V4_ACL,
		     "result: %d, cause: %s, flag: 0x%X, who: %d", result,
		     cause, pace->flag, GET_FSAL_ACE_WHO(*pace));

	return result;
}

static bool fsal_check_ace_applicable(fsal_ace_t *pace,
				      struct user_cred *creds,
				      bool is_dir,
				      bool is_owner,
				      bool is_group,
				      bool is_root)
{
	bool is_applicable = false;
	bool is_file = !is_dir;

	/* To be applicable, the entry should not be INHERIT_ONLY. */
	if (IS_FSAL_ACE_INHERIT_ONLY(*pace)) {
		LogFullDebug(COMPONENT_NFS_V4_ACL,
			     "Not applicable, inherit only");
		return false;
	}

	/* Use internal flag to further check the entry is applicable
	 * to this object type. */
	if (is_file) {
		if (!IS_FSAL_FILE_APPLICABLE(*pace)) {
			LogFullDebug(COMPONENT_NFS_V4_ACL,
				     "Not applicable to file");
			return false;
		}
	} else {		/* directory */

		if (!IS_FSAL_DIR_APPLICABLE(*pace)) {
			LogFullDebug(COMPONENT_NFS_V4_ACL,
				     "Not applicable to dir");
			return false;
		}
	}

	/* The user should match who value. */
	is_applicable = is_root
	    || fsal_check_ace_matches(pace, creds, is_owner, is_group);
	if (is_applicable)
		LogFullDebug(COMPONENT_NFS_V4_ACL, "Applicable, flag=0X%x",
			     pace->flag);
	else
		LogFullDebug(COMPONENT_NFS_V4_ACL,
			     "Not applicable to given user");

	return is_applicable;
}

static const char *fsal_ace_type(fsal_acetype_t type)
{
	switch (type) {
	case FSAL_ACE_TYPE_ALLOW:
		return "A";
	case FSAL_ACE_TYPE_DENY:
		return "D ";
	case FSAL_ACE_TYPE_AUDIT:
		return "U";
	case FSAL_ACE_TYPE_ALARM:
		return "L";
	default:
		return "unknown";
	}
}

static const char *fsal_ace_perm(fsal_aceperm_t perm)
{
	static char buf[64];
	char *c = buf;

	if (perm & FSAL_ACE_PERM_READ_DATA)
		*c++ = 'r';
	if (perm & FSAL_ACE_PERM_WRITE_DATA)
		*c++ = 'w';
	if (perm & FSAL_ACE_PERM_APPEND_DATA)
		*c++ = 'a';
	if (perm & FSAL_ACE_PERM_EXECUTE)
		*c++ = 'x';
	if (perm & FSAL_ACE_PERM_DELETE)
		*c++ = 'd';
	if (perm & FSAL_ACE_PERM_DELETE_CHILD)
		*c++ = 'D';
	if (perm & FSAL_ACE_PERM_READ_ATTR)
		*c++ = 't';
	if (perm & FSAL_ACE_PERM_WRITE_ATTR)
		*c++ = 'T';
	if (perm & FSAL_ACE_PERM_READ_NAMED_ATTR)
		*c++ = 'n';
	if (perm & FSAL_ACE_PERM_WRITE_NAMED_ATTR)
		*c++ = 'N';
	if (perm & FSAL_ACE_PERM_READ_ACL)
		*c++ = 'c';
	if (perm & FSAL_ACE_PERM_WRITE_ACL)
		*c++ = 'C';
	if (perm & FSAL_ACE_PERM_WRITE_OWNER)
		*c++ = 'o';
	if (perm & FSAL_ACE_PERM_SYNCHRONIZE)
		*c++ = 'y';
	*c = '\0';

	return buf;
}

static const char *fsal_ace_flag(char *buf, fsal_aceflag_t flag)
{
	char *c = buf;

	if (flag & FSAL_ACE_FLAG_GROUP_ID)
		*c++ = 'g';
	if (flag & FSAL_ACE_FLAG_FILE_INHERIT)
		*c++ = 'f';
	if (flag & FSAL_ACE_FLAG_DIR_INHERIT)
		*c++ = 'd';
	if (flag & FSAL_ACE_FLAG_NO_PROPAGATE)
		*c++ = 'n';
	if (flag & FSAL_ACE_FLAG_INHERIT_ONLY)
		*c++ = 'i';
	if (flag & FSAL_ACE_FLAG_SUCCESSFUL)
		*c++ = 'S';
	if (flag & FSAL_ACE_FLAG_FAILED)
		*c++ = 'F';
	if (flag & FSAL_ACE_FLAG_INHERITED)
		*c++ = 'I';
	if (flag & FSAL_ACE_IFLAG_EXCLUDE_FILES)
		*c++ = 'x';
	if (flag & FSAL_ACE_IFLAG_EXCLUDE_DIRS)
		*c++ = 'X';
	if (flag & FSAL_ACE_IFLAG_SPECIAL_ID)
		*c++ = 'S';
	if (flag & FSAL_ACE_IFLAG_MODE_GEN)
		*c++ = 'G';
	*c = '\0';

	return buf;
}

void fsal_print_ace_int(log_components_t component, log_levels_t debug,
			fsal_ace_t *ace, char *file, int line,
			char *function)
{
	char fbuf[16];
	char ibuf[16];

	if (!isLevel(component, debug))
		return;

	DisplayLogComponentLevel(component, file, line, function, debug,
				 "ACE %s:%s(%s):%u:%s",
				 fsal_ace_type(ace->type),
				 fsal_ace_flag(fbuf, ace->flag),
				 fsal_ace_flag(ibuf, ace->iflag),
				 ace->who.uid,
				 fsal_ace_perm(ace->perm));
}

void fsal_print_acl_int(log_components_t component, log_levels_t debug,
			fsal_acl_t *acl, char *file, int line,
			char *function)
{
	fsal_ace_t *ace = NULL;

	if (!isLevel(component, debug))
		return;

	DisplayLogComponentLevel(component, file, line, function, debug,
				 "ACL naces: %u aces:", acl->naces);
	for (ace = acl->aces; ace < acl->aces + acl->naces; ace++)
		fsal_print_ace_int(component, debug, ace, file, line, function);
}

int display_fsal_inherit_flags(struct display_buffer *dspbuf, fsal_ace_t *pace)
{
	if (!pace)
		return display_cat(dspbuf, "NULL");

	return display_printf(dspbuf, "Inherit:%s%s%s%s",
			      IS_FSAL_ACE_FILE_INHERIT(*pace) ? " file" : "",
			      IS_FSAL_ACE_DIR_INHERIT(*pace) ? " dir" : "",
			      IS_FSAL_ACE_INHERIT_ONLY(*pace) ? " inherit_only"
			      : "",
			      IS_FSAL_ACE_NO_PROPAGATE(*pace) ? " no_propagate"
			      : "");
}

int display_fsal_ace(struct display_buffer *dspbuf, int ace_number,
		     fsal_ace_t *pace, bool is_dir)
{
	int b_left;

	if (!pace)
		return display_cat(dspbuf, "ACE: <NULL>");

	/* Print the entire ACE. */
	b_left = display_printf(dspbuf, "ACE %d:", ace_number);

	/* ACE type. */
	if (b_left > 0)
		b_left =
		    display_cat(dspbuf,
				IS_FSAL_ACE_ALLOW(*pace) ? " allow" :
				IS_FSAL_ACE_DENY(*pace) ? " deny" :
				IS_FSAL_ACE_AUDIT(*pace) ? " audit" : " ?");

	/* ACE who and its type. */
	if (b_left > 0 && IS_FSAL_ACE_SPECIAL_ID(*pace))
		b_left =
		    display_cat(dspbuf,
				IS_FSAL_ACE_SPECIAL_OWNER(*pace) ? " owner@" :
				IS_FSAL_ACE_SPECIAL_GROUP(*pace) ? " group@" :
				IS_FSAL_ACE_SPECIAL_EVERYONE(*pace) ?
				" everyone@" : "");

	if (b_left > 0 && !IS_FSAL_ACE_SPECIAL_ID(*pace)) {
		if (IS_FSAL_ACE_SPECIAL_ID(*pace))
			b_left =
			    display_printf(dspbuf, " gid %d", pace->who.gid);
		else
			b_left =
			    display_printf(dspbuf, " uid %d", pace->who.uid);
	}

	/* ACE mask. */
	if (b_left > 0)
		b_left = display_fsal_v4mask(dspbuf, pace->perm, is_dir);

	/* ACE Inherit flags. */
	if (b_left > 0 && IS_FSAL_ACE_INHERIT(*pace))
		b_left = display_fsal_inherit_flags(dspbuf, pace);

	return b_left;
}

int display_fsal_v4mask(struct display_buffer *dspbuf, fsal_aceperm_t v4mask,
			bool is_dir)
{
	int b_left = display_printf(dspbuf, "0x%06x", v4mask);

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_READ_DATA))
		b_left = display_cat(dspbuf, " READ");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_DATA)
	    && is_dir)
		b_left = display_cat(dspbuf, " ADD_FILE");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_DATA)
	    && !is_dir)
		b_left = display_cat(dspbuf, " WRITE");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_APPEND_DATA)
	    && is_dir)
		b_left = display_cat(dspbuf, " ADD_SUBDIR");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_APPEND_DATA)
	    && !is_dir)
		b_left = display_cat(dspbuf, " APPEND");

	if (b_left > 0
	    && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_READ_NAMED_ATTR))
		b_left = display_cat(dspbuf, " READ_NAMED");

	if (b_left > 0
	    && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_NAMED_ATTR))
		b_left = display_cat(dspbuf, " WRITE_NAMED");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_EXECUTE))
		b_left = display_cat(dspbuf, " EXECUTE");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_DELETE_CHILD))
		b_left = display_cat(dspbuf, " DELETE_CHILD");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_READ_ATTR))
		b_left = display_cat(dspbuf, " READ_ATTR");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_ATTR))
		b_left = display_cat(dspbuf, " WRITE_ATTR");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_DELETE))
		b_left = display_cat(dspbuf, " DELETE");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_READ_ACL))
		b_left = display_cat(dspbuf, " READ_ACL");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_ACL))
		b_left = display_cat(dspbuf, " WRITE_ACL");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_OWNER))
		b_left = display_cat(dspbuf, " WRITE_OWNER");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_SYNCHRONIZE))
		b_left = display_cat(dspbuf, " SYNCHRONIZE");

	if (b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE4_PERM_CONTINUE))
		b_left = display_cat(dspbuf, " CONTINUE");

	return b_left;
}

static void fsal_print_access_by_acl(int naces, int ace_number,
				     fsal_ace_t *pace,
				     fsal_aceperm_t perm,
				     enum fsal_errors_t access_result,
				     bool is_dir,
				     struct user_cred *creds)
{
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = { sizeof(str), str, str };
	int b_left;

	if (!isFullDebug(COMPONENT_NFS_V4_ACL))
		return;

	if (access_result == ERR_FSAL_NO_ERROR)
		b_left = display_cat(&dspbuf, "access granted");
	else if (access_result == ERR_FSAL_PERM)
		b_left = display_cat(&dspbuf, "access denied (EPERM)");
	else
		b_left = display_cat(&dspbuf, "access denied (EACCESS)");

	if (b_left > 0)
		b_left =
		    display_printf(&dspbuf, " uid %u gid %u Access req:",
				   creds->caller_uid, creds->caller_gid);

	if (b_left > 0)
		b_left = display_fsal_v4mask(&dspbuf, perm, is_dir);

	if (b_left > 0 && (naces != ace_number))
		b_left = display_fsal_ace(&dspbuf, ace_number, pace, is_dir);

	LogFullDebug(COMPONENT_NFS_V4_ACL, "%s", str);
}

/**
 * @brief Check access using v4 ACL list
 *
 * @param[in] creds
 * @param[in] v4mask
 * @param[in] allowed
 * @param[in] denied
 * @param[in] p_object_attributes
 *
 * @return ERR_FSAL_NO_ERROR, ERR_FSAL_ACCESS, or ERR_FSAL_NO_ACE
 */

static fsal_status_t fsal_check_access_acl(struct user_cred *creds,
					   fsal_aceperm_t v4mask,
					   fsal_accessflags_t *allowed,
					   fsal_accessflags_t *denied,
					   struct attrlist *p_object_attributes)
{
	fsal_aceperm_t missing_access;
	fsal_aceperm_t tperm;
	uid_t uid;
	gid_t gid;
	fsal_acl_t *pacl = NULL;
	fsal_ace_t *pace = NULL;
	int ace_number = 0;
	bool is_dir = false;
	bool is_owner = false;
	bool is_group = false;
	bool is_root = false;

	if (allowed != NULL)
		*allowed = 0;

	if (denied != NULL)
		*denied = 0;

	if (!p_object_attributes->acl) {
		/* Means that FSAL_ACE4_REQ_FLAG was set, but no ACLs */
		LogFullDebug(COMPONENT_NFS_V4_ACL,
			     "Allow ACE required, but no ACLs");
		return fsalstat(ERR_FSAL_NO_ACE, 0);
	}

	/* unsatisfied flags */
	missing_access = v4mask & ~FSAL_ACE4_PERM_CONTINUE;
	if (!missing_access) {
		LogFullDebug(COMPONENT_NFS_V4_ACL, "Nothing was requested");
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	/* Get file ownership information. */
	uid = p_object_attributes->owner;
	gid = p_object_attributes->group;
	pacl = p_object_attributes->acl;
	is_dir = (p_object_attributes->type == DIRECTORY);
	is_root = op_ctx->fsal_export->exp_ops.is_superuser(
						op_ctx->fsal_export, creds);

	if (is_root) {
		if (is_dir) {
			if (allowed != NULL)
				*allowed = v4mask;

			/* On a directory, allow root anything. */
			LogFullDebug(COMPONENT_NFS_V4_ACL,
				     "Met root privileges on directory");
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		}

		/* Otherwise, allow root anything but execute. */
		missing_access &= FSAL_ACE_PERM_EXECUTE;

		if (allowed != NULL)
			*allowed = v4mask & ~FSAL_ACE_PERM_EXECUTE;

		if (!missing_access) {
			LogFullDebug(COMPONENT_NFS_V4_ACL,
				     "Met root privileges");
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		}
	}

	LogFullDebug(COMPONENT_NFS_V4_ACL,
		     "file acl=%p, file uid=%u, file gid=%u, ", pacl, uid, gid);

	if (isFullDebug(COMPONENT_NFS_V4_ACL)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = { sizeof(str), str, str };

		(void)display_fsal_v4mask(&dspbuf, v4mask,
					  p_object_attributes->type ==
					  DIRECTORY);

		LogFullDebug(COMPONENT_NFS_V4_ACL,
			     "user uid=%u, user gid= %u, v4mask=%s",
			     creds->caller_uid, creds->caller_gid, str);
	}

	is_owner = fsal_check_ace_owner(uid, creds);
	is_group = fsal_check_ace_group(gid, creds);

	/* Always grant READ_ACL, WRITE_ACL and READ_ATTR, WRITE_ATTR
	 * to the file owner. */
	if (is_owner) {
		if (allowed != NULL)
			*allowed |=
			    v4mask & (FSAL_ACE_PERM_WRITE_ACL |
				      FSAL_ACE_PERM_READ_ACL |
				      FSAL_ACE_PERM_WRITE_ATTR |
				      FSAL_ACE_PERM_READ_ATTR);

		missing_access &=
		    ~(FSAL_ACE_PERM_WRITE_ACL | FSAL_ACE_PERM_READ_ACL);
		missing_access &=
		    ~(FSAL_ACE_PERM_WRITE_ATTR | FSAL_ACE_PERM_READ_ATTR);
		if (!missing_access) {
			LogFullDebug(COMPONENT_NFS_V4_ACL,
				     "Met owner privileges");
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		}
	}
	/** @todo Even if user is admin, audit/alarm checks should be done. */

	for (pace = pacl->aces; pace < pacl->aces + pacl->naces; pace++) {
		ace_number += 1;

		LogFullDebug(COMPONENT_NFS_V4_ACL,
			     "ace numnber: %d ace type 0x%X perm 0x%X flag 0x%X who %u",
			     ace_number, pace->type, pace->perm, pace->flag,
			     GET_FSAL_ACE_WHO(*pace));

		/* Process Allow and Deny entries. */
		if (!IS_FSAL_ACE_ALLOW(*pace) && !IS_FSAL_ACE_DENY(*pace)) {
			LogFullDebug(COMPONENT_NFS_V4_ACL, "not allow or deny");
			continue;
		}

		LogFullDebug(COMPONENT_NFS_V4_ACL, "allow or deny");

		/* Check if this ACE is applicable. */
		if (fsal_check_ace_applicable(pace, creds, is_dir, is_owner,
					      is_group, is_root)) {
			if (IS_FSAL_ACE_ALLOW(*pace)) {
				/* Do not set bits which are already denied */
				if (denied)
					tperm = pace->perm & ~*denied;
				else
					tperm = pace->perm;

				LogFullDebug(COMPONENT_NFS_V4_ACL,
					     "allow perm 0x%X remainingPerms 0x%X",
					     tperm, missing_access);

				if (allowed != NULL)
					*allowed |= v4mask & tperm;

				missing_access &=
				    ~(tperm & missing_access);

				if (!missing_access) {
					fsal_print_access_by_acl(
							pacl->naces,
							ace_number,
							pace,
							v4mask,
							ERR_FSAL_NO_ERROR,
							is_dir,
							creds);
					break;
				}
			} else if ((pace->perm & missing_access) && !is_root) {
				fsal_print_access_by_acl(
					pacl->naces,
					ace_number,
					pace,
					v4mask,
#ifndef ENABLE_RFC_ACL
					(pace->perm & missing_access &
					 (FSAL_ACE_PERM_WRITE_ATTR |
					  FSAL_ACE_PERM_WRITE_ACL |
					  FSAL_ACE_PERM_WRITE_OWNER))
					    != 0 ?
					    ERR_FSAL_PERM :
#endif /* ENABLE_RFC_ACL */
					    ERR_FSAL_ACCESS,
					is_dir,
					creds);

				if (denied != NULL)
					*denied |= v4mask & pace->perm;
				if (denied == NULL ||
				    (v4mask &
				     FSAL_ACE4_PERM_CONTINUE) == 0) {
#ifndef ENABLE_RFC_ACL
					if ((pace->perm & missing_access &
					    (FSAL_ACE_PERM_WRITE_ATTR |
					     FSAL_ACE_PERM_WRITE_ACL |
					     FSAL_ACE_PERM_WRITE_OWNER))
					    != 0) {
						LogDebug(COMPONENT_NFS_V4_ACL,
							 "access denied (EPERM)");
						return fsalstat(ERR_FSAL_PERM,
							0);
					} else {
						LogDebug(COMPONENT_NFS_V4_ACL,
							 "access denied (EACCESS)");
						return fsalstat(ERR_FSAL_ACCESS,
							0);
					}
#else /* ENABLE_RFC_ACL */
					LogDebug(COMPONENT_NFS_V4_ACL,
						 "access denied (EACCESS)");
					return fsalstat(ERR_FSAL_ACCESS, 0);
#endif /* ENABLE_RFC_ACL */
				}

				missing_access &=
				    ~(pace->perm & missing_access);

				/* If this DENY ACE blocked the last
				 * remaining requested access
				 * bits, break out of the loop because
				 * we're done and don't
				 * want to evaluate any more ACEs.
				 */
				if (!missing_access)
					break;
			}
		}
	}

	if (IS_FSAL_ACE4_REQ(v4mask) && missing_access) {
		LogDebug(COMPONENT_NFS_V4_ACL, "final access unknown (NO_ACE)");
		return fsalstat(ERR_FSAL_NO_ACE, 0);
	} else if (missing_access || (denied != NULL && *denied != 0)) {
#ifndef ENABLE_RFC_ACL
		if ((missing_access &
		     (FSAL_ACE_PERM_WRITE_ATTR | FSAL_ACE_PERM_WRITE_ACL |
		      FSAL_ACE_PERM_WRITE_OWNER)) != 0) {
			LogDebug(COMPONENT_NFS_V4_ACL,
				 "final access denied (EPERM)");
			return fsalstat(ERR_FSAL_PERM, 0);
		} else {
			LogDebug(COMPONENT_NFS_V4_ACL,
				 "final access denied (EACCESS)");
			return fsalstat(ERR_FSAL_ACCESS, 0);
		}
#else /* ENABLE_RFC_ACL */
		LogDebug(COMPONENT_NFS_V4_ACL, "final access denied (EACCESS)");
		return fsalstat(ERR_FSAL_ACCESS, 0);
#endif /* ENABLE_RFC_ACL */
	} else {
		LogFullDebug(COMPONENT_NFS_V4_ACL, "access granted");
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}
}

/**
 * @brief Check access using mode bits only
 *
 * @param[in] creds
 * @param[in] access_type
 * @param[in] allowed
 * @param[in] denied
 * @param[in] p_object_attributes
 *
 * @return ERR_FSAL_NO_ERROR or ERR_FSAL_ACCESS
 */

static fsal_status_t
fsal_check_access_no_acl(struct user_cred *creds,
			 fsal_accessflags_t access_type,
			 fsal_accessflags_t *allowed,
			 fsal_accessflags_t *denied,
			 struct attrlist *p_object_attributes)
{
	uid_t uid;
	gid_t gid;
	mode_t mode;
	fsal_accessflags_t mask;
	bool rc;

	if (allowed != NULL)
		*allowed = 0;

	if (denied != NULL)
		*denied = 0;

	if (!access_type) {
		LogFullDebug(COMPONENT_NFS_V4_ACL, "Nothing was requested");
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	uid = p_object_attributes->owner;
	gid = p_object_attributes->group;
	mode = p_object_attributes->mode;

	LogFullDebug(COMPONENT_NFS_V4_ACL,
		     "file Mode=%#o, file uid=%u, file gid= %u, user uid=%u, user gid= %u, access_type=0X%x",
		     mode, uid, gid,
		     creds->caller_uid, creds->caller_gid,
		     access_type);

	if (op_ctx->fsal_export->exp_ops.is_superuser(op_ctx->fsal_export,
						      creds)) {
		if (p_object_attributes->type == DIRECTORY) {
			if (allowed != NULL)
				*allowed = access_type;
			LogFullDebug(COMPONENT_NFS_V4_ACL,
				     "Root has full access on directories.");
			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		}

		rc = ((access_type & FSAL_X_OK) == 0)
		    || ((mode & (S_IXOTH | S_IXUSR | S_IXGRP)) != 0);
		if (!rc) {
			if (allowed != NULL)
				*allowed = access_type & ~FSAL_X_OK;
			if (denied != NULL)
				*denied = access_type & FSAL_X_OK;
			LogFullDebug(COMPONENT_NFS_V4_ACL,
				     "Root is not allowed execute access unless at least one user is allowed execute access.");
		} else {
			if (allowed != NULL)
				*allowed = access_type;
			LogFullDebug(COMPONENT_NFS_V4_ACL,
				     "Root is granted access.");
		}
		return rc ? fsalstat(ERR_FSAL_NO_ERROR,
				     0) : fsalstat(ERR_FSAL_ACCESS, 0);
	}

	/* If the uid of the file matches the uid of the user,
	 * then the uid mode bits take precedence. */
	if (creds->caller_uid == uid) {
		LogFullDebug(COMPONENT_NFS_V4_ACL,
			     "Using owner mode %#o",
			     mode & S_IRWXU);
		mode >>= 6;
	} else {		/* followed by group(s) */
		if (creds->caller_gid == gid) {
			LogFullDebug(COMPONENT_NFS_V4_ACL,
				     "Using group mode %#o",
				     mode & S_IRWXG);
			mode >>= 3;
		} else {
			/* Test if file belongs to alt user's groups */
			int i;

			for (i = 0; i < creds->caller_glen; i++) {
				if (creds->caller_garray[i] == gid) {
					LogFullDebug(COMPONENT_NFS_V4_ACL,
						     "Using group mode %#o for alt group #%d",
						     mode & S_IRWXG, i);
					mode >>= 3;
					break;
				}
			}
		}
	}

	/* others fall out the bottom... */

	/* Convert the shifted mode into an access_type mask */
	mask =
	    ((mode & S_IROTH) ? FSAL_R_OK : 0) | ((mode & S_IWOTH) ? FSAL_W_OK :
						  0) | ((mode & S_IXOTH) ?
							FSAL_X_OK : 0);
	LogFullDebug(COMPONENT_NFS_V4_ACL,
		     "Mask=0X%x, Access Type=0X%x Allowed=0X%x Denied=0X%x %s",
		     mask, access_type,
		     mask & access_type,
		     ~mask & access_type,
		     (mask & access_type) == access_type ?
			"ALLOWED" : "DENIED");

	if (allowed != NULL)
		*allowed = mask & access_type;

	if (denied != NULL)
		*denied = ~mask & access_type;

	/* Success if mask covers all the requested bits */
	return (mask & access_type) == access_type ? fsalstat(ERR_FSAL_NO_ERROR,
							      0) :
	    fsalstat(ERR_FSAL_ACCESS, 0);
}

/* test_access
 * common (default) access check method for fsal_obj_handle objects.
 * NOTE: A fsal can replace this method with their own custom access
 *       checker.  If so and they wish to have an option to switch
 *       between their custom and this one, it their test_access
 *       method's responsibility to do that test and select this one.
 */

fsal_status_t fsal_test_access(struct fsal_obj_handle *obj_hdl,
			       fsal_accessflags_t access_type,
			       fsal_accessflags_t *allowed,
			       fsal_accessflags_t *denied,
			       bool owner_skip)
{
	struct attrlist attrs;
	fsal_status_t status;

	fsal_prepare_attrs(&attrs,
			   op_ctx->fsal_export->exp_ops.fs_supported_attrs(
							op_ctx->fsal_export)
			   & (ATTRS_CREDS | ATTR_MODE | ATTR_ACL));

	status = obj_hdl->obj_ops->getattrs(obj_hdl, &attrs);

	if (FSAL_IS_ERROR(status))
		goto out;

	if (owner_skip && attrs.owner == op_ctx->creds->caller_uid) {
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		goto out;
	}

	if (IS_FSAL_ACE4_REQ(access_type) ||
	    (attrs.acl != NULL && IS_FSAL_ACE4_MASK_VALID(access_type))) {
		status = fsal_check_access_acl(op_ctx->creds,
					       FSAL_ACE4_MASK(access_type),
					       allowed, denied, &attrs);
	} else {		/* fall back to use mode to check access. */
		status = fsal_check_access_no_acl(op_ctx->creds,
						  FSAL_MODE_MASK(access_type),
						  allowed, denied, &attrs);
	}

 out:

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	return status;
}

uid_t ganesha_uid;
gid_t ganesha_gid;
int ganesha_ngroups;
gid_t *ganesha_groups;

void fsal_set_credentials(const struct user_cred *creds)
{
	if (set_threadgroups(creds->caller_glen, creds->caller_garray) != 0)
		LogFatal(COMPONENT_FSAL, "Could not set Context credentials");
	setgroup(creds->caller_gid);
	setuser(creds->caller_uid);
}

bool fsal_set_credentials_only_one_user(const struct user_cred *creds)
{
	if (creds->caller_uid == ganesha_uid
		    && creds->caller_gid == ganesha_gid)
		return true;
	else
		return false;
}

void fsal_save_ganesha_credentials(void)
{
	int i;
	char buffer[1024], *p = buffer;

	ganesha_uid = getuser();
	ganesha_gid = getgroup();

	ganesha_ngroups = getgroups(0, NULL);
	if (ganesha_ngroups > 0) {
		ganesha_groups = gsh_malloc(ganesha_ngroups * sizeof(gid_t));

		if (getgroups(ganesha_ngroups, ganesha_groups) !=
		    ganesha_ngroups) {
			LogFatal(COMPONENT_FSAL,
				 "Could not get list of ganesha groups");
		}
	}

	p += sprintf(p, "Ganesha uid=%d gid=%d ngroups=%d", (int)ganesha_uid,
		     (int)ganesha_gid, ganesha_ngroups);
	if (ganesha_ngroups != 0)
		p += sprintf(p, " (");
	for (i = 0; i < ganesha_ngroups; i++) {
		if ((p - buffer) < (sizeof(buffer) - 10)) {
			if (i == 0)
				p += sprintf(p, "%d", (int)ganesha_groups[i]);
			else
				p += sprintf(p, " %d", (int)ganesha_groups[i]);
		}
	}
	if (ganesha_ngroups != 0)
		p += sprintf(p, ")");
	LogInfo(COMPONENT_FSAL, "%s", buffer);
}

void fsal_restore_ganesha_credentials(void)
{
	setuser(ganesha_uid);
	setgroup(ganesha_gid);
	if (set_threadgroups(ganesha_ngroups, ganesha_groups) != 0)
		LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
}

/** @} */
