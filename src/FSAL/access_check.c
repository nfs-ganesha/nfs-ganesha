/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file access_check.c
 * @brief File/object access checking
 */

#include "config.h"

#include  "fsal.h"
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

  if(creds->caller_gid == gid)
    return true;

  for(i = 0; i < creds->caller_glen; i++)
    {
      if(creds->caller_garray[i] == gid)
        return true;
    }

  return false;
}

static bool fsal_check_ace_matches(fsal_ace_t *pace,
                                     struct user_cred *creds,
                                     bool is_owner,
                                     bool is_group)
{
  int matches = 0;

  if (IS_FSAL_ACE_SPECIAL_ID(*pace))
    switch(pace->who.uid)
      {
        case FSAL_ACE_SPECIAL_OWNER:
          if(is_owner)
            matches = 1;
        break;

        case FSAL_ACE_SPECIAL_GROUP:
          if(is_group)
            matches = 2;
        break;

        case FSAL_ACE_SPECIAL_EVERYONE:
          matches = 3;
        break;

        default:
        break;
      }
  else if (IS_FSAL_ACE_GROUP_ID(*pace))
    {
      if(fsal_check_ace_group(pace->who.gid, creds))
        matches = 4;
    }
  else
    {
      if(fsal_check_ace_owner(pace->who.uid, creds))
        matches = 5;
    }

  LogDebug(COMPONENT_FSAL,
           "fsal_check_ace_matches: matches %d flag 0x%X who %u",
           matches, pace->flag, GET_FSAL_ACE_WHO(*pace));

  return (matches != 0);
}

static bool fsal_check_ace_applicable(fsal_ace_t *pace,
                                        struct user_cred *creds,
                                        bool is_dir,
                                        bool is_owner,
                                        bool is_group)
{
  bool is_applicable = false;
  bool is_file = !is_dir;

  /* To be applicable, the entry should not be INHERIT_ONLY. */
  if (IS_FSAL_ACE_INHERIT_ONLY(*pace))
    {
      LogDebug(COMPONENT_FSAL, "fsal_check_ace_applicable: Not applicable, "
               "inherit only");
      return false;
    }

  /* Use GPFS internal flag to further check the entry is applicable to this
   * object type. */
  if(is_file)
    {
      if(!IS_FSAL_FILE_APPLICABLE(*pace))
        {
          LogDebug(COMPONENT_FSAL, "fsal_check_ace_applicable: Not applicable to file");
          return false;
        }
    }
  else  /* directory */
    {
      if(!IS_FSAL_DIR_APPLICABLE(*pace))
        {
          LogDebug(COMPONENT_FSAL, "fsal_check_ace_applicable: Not applicable to dir");
          return false;
        }
    }

  /* The user should match who value. */
  is_applicable = fsal_check_ace_matches(pace, creds, is_owner, is_group);
  if(is_applicable)
    LogDebug(COMPONENT_FSAL, "fsal_check_ace_applicable: Applicable, flag=0X%x",
             pace->flag);
  else
    LogDebug(COMPONENT_FSAL, "fsal_check_ace_applicable: Not applicable to given user");

  return is_applicable;
}

/* originally def'd in /FSAL/FSAL_GPFS/fsal_internal.c:56: fix later */

#define ACL_DEBUG_BUF_SIZE 256

static void fsal_print_ace(int ace_number, fsal_ace_t *pace, char *p_acebuf)
{
  char inherit_flags[ACL_DEBUG_BUF_SIZE];

  if(!pace || !p_acebuf)
    return;

  memset(p_acebuf, 0, ACL_DEBUG_BUF_SIZE);
  memset(inherit_flags, 0, ACL_DEBUG_BUF_SIZE);

  /* Get inherit flags if any. */
  sprintf(inherit_flags, "I(%c%c%c%c)",
          IS_FSAL_ACE_FILE_INHERIT(*pace)? 'f': '-',
          IS_FSAL_ACE_DIR_INHERIT(*pace) ? 'd': '-',
          IS_FSAL_ACE_INHERIT_ONLY(*pace)? 'o': '-',
          IS_FSAL_ACE_NO_PROPAGATE(*pace)? 'n': '-');

  /* Print the entire ACE. */
  sprintf(p_acebuf, "ACE %d %s %s %u %c%c%c%c%c%c%c%c%c%c%c%c%c%c %s",
          ace_number,
          /* ACE type. */
          IS_FSAL_ACE_ALLOW(*pace)? "allow":
          IS_FSAL_ACE_DENY(*pace) ? "deny":
          IS_FSAL_ACE_AUDIT(*pace)? "audit": "?",
          /* ACE who and its type. */
          (IS_FSAL_ACE_SPECIAL_ID(*pace) && IS_FSAL_ACE_SPECIAL_OWNER(*pace))    ? "owner@":
          (IS_FSAL_ACE_SPECIAL_ID(*pace) && IS_FSAL_ACE_SPECIAL_GROUP(*pace))    ? "group@":
          (IS_FSAL_ACE_SPECIAL_ID(*pace) && IS_FSAL_ACE_SPECIAL_EVERYONE(*pace)) ? "everyone@":
          IS_FSAL_ACE_SPECIAL_ID(*pace)						 ? "specialid":
          IS_FSAL_ACE_GROUP_ID(*pace) 						 ? "gid": "uid",
          GET_FSAL_ACE_WHO(*pace),
          /* ACE mask. */
          IS_FSAL_ACE_READ_DATA(*pace)		 ? 'r':'-',
          IS_FSAL_ACE_WRITE_DATA(*pace)		 ? 'w':'-',
          IS_FSAL_ACE_EXECUTE(*pace)		 ? 'x':'-',
          IS_FSAL_ACE_ADD_SUBDIRECTORY(*pace)    ? 'm':'-',
          IS_FSAL_ACE_READ_NAMED_ATTR(*pace)	 ? 'n':'-',
          IS_FSAL_ACE_WRITE_NAMED_ATTR(*pace) 	 ? 'N':'-',
          IS_FSAL_ACE_DELETE_CHILD(*pace) 	 ? 'p':'-',
          IS_FSAL_ACE_READ_ATTR(*pace)		 ? 't':'-',
          IS_FSAL_ACE_WRITE_ATTR(*pace)		 ? 'T':'-',
          IS_FSAL_ACE_DELETE(*pace)		 ? 'd':'-',
          IS_FSAL_ACE_READ_ACL(*pace) 		 ? 'c':'-',
          IS_FSAL_ACE_WRITE_ACL(*pace)		 ? 'C':'-',
          IS_FSAL_ACE_WRITE_OWNER(*pace)	 ? 'o':'-',
          IS_FSAL_ACE_SYNCHRONIZE(*pace)	 ? 'z':'-',
          /* ACE Inherit flags. */
          IS_FSAL_ACE_INHERIT(*pace)? inherit_flags: "");
}

static void fsal_print_access_by_acl(int naces, int ace_number,
                                     fsal_ace_t *pace, fsal_aceperm_t perm,
                                     unsigned int access_result,
                                     bool is_dir,
                                     struct user_cred *creds)
{
  char ace_data[ACL_DEBUG_BUF_SIZE];
  uid_t user = creds->caller_uid;
  bool is_last_ace = (naces == ace_number);

  if(!is_last_ace)
    fsal_print_ace(ace_number, pace, ace_data);

  /* Print the access result and the request. */
  LogDebug(COMPONENT_FSAL,
	   "%s: %s uid %u %s",
          (access_result == ERR_FSAL_NO_ERROR)                      ? "permit": "reject",
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_READ_DATA) 	    ?"READ":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_WRITE_DATA) && is_dir ?"ADD_FILE":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_WRITE_DATA)	    ?"WRITE":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_APPEND_DATA) && is_dir?"ADD_SUBDIR":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_APPEND_DATA)	    ?"APPEND":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_READ_NAMED_ATTR)      ?"READ_NAMED":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_WRITE_NAMED_ATTR)     ?"WRITE_NAMED":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_EXECUTE)	            ?"EXECUTE":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_DELETE_CHILD)         ?"DELETE_CHILD":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_READ_ATTR)	    ?"READ_ATTR":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_WRITE_ATTR)	    ?"WRITE_ATTR":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_DELETE)		    ?"DELETE":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_READ_ACL) 	    ?"READ_ACL":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_WRITE_ACL)	    ?"WRITE_ACL":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_WRITE_OWNER)	    ?"WRITE_OWNER":
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_SYNCHRONIZE)	    ?"SYNCHRONIZE":
	   "UNKNOWN",
          user, (!is_last_ace) ? ace_data: "");

}

static int fsal_check_access_acl(struct user_cred *creds,   /* IN */
				 fsal_aceperm_t v4mask,  /* IN */
				 struct attrlist * p_object_attributes   /* IN */ )
{
  fsal_aceperm_t missing_access;
  uid_t uid;
  gid_t gid;
  fsal_acl_t *pacl = NULL;
  fsal_ace_t *pace = NULL;
  int ace_number = 0;
  bool is_dir = false;
  bool is_owner = false;
  bool is_group = false;

  /* unsatisfied flags */
  missing_access = v4mask;
  if(!missing_access)
    {
      LogDebug(COMPONENT_FSAL, "Nothing was requested");
      return true;
    }

  /* Get file ownership information. */
  uid = p_object_attributes->owner;
  gid = p_object_attributes->group;
  pacl = p_object_attributes->acl;
  is_dir = (p_object_attributes->type == DIRECTORY);

  LogDebug(COMPONENT_FSAL,
           "file acl=%p, file uid=%u, file gid= %u, "
           "user uid=%u, user gid= %u, v4mask=0x%X",
           pacl, uid, gid,
           creds->caller_uid,
           creds->caller_gid,
           v4mask);

  is_owner = fsal_check_ace_owner(uid, creds);
  is_group = fsal_check_ace_group(gid, creds);

  /* Always grant READ_ACL, WRITE_ACL and READ_ATTR, WRITE_ATTR to the file
   * owner. */
  if(is_owner)
    {
      missing_access &= ~(FSAL_ACE_PERM_WRITE_ACL | FSAL_ACE_PERM_READ_ACL);
      missing_access &= ~(FSAL_ACE_PERM_WRITE_ATTR | FSAL_ACE_PERM_READ_ATTR);
      if(!missing_access)
        {
          LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: Met owner privileges");
          return true;
        }
    }

  // TODO: Even if user is admin, audit/alarm checks should be done.

  ace_number = 1;
  for(pace = pacl->aces; pace < pacl->aces + pacl->naces; pace++)
    {
      LogDebug(COMPONENT_FSAL,
               "ace type 0x%X perm 0x%X flag 0x%X who %u",
               pace->type, pace->perm, pace->flag, GET_FSAL_ACE_WHO(*pace));

      /* Process Allow and Deny entries. */
      if(IS_FSAL_ACE_ALLOW(*pace) || IS_FSAL_ACE_DENY(*pace))
        {
          LogDebug(COMPONENT_FSAL, "allow or deny");

          /* Check if this ACE is applicable. */
          if(fsal_check_ace_applicable(pace, creds, is_dir, is_owner, is_group))
            {
              if(IS_FSAL_ACE_ALLOW(*pace))
                {
                  LogDebug(COMPONENT_FSAL,
                           "allow perm 0x%X remainingPerms 0x%X",
                           pace->perm, missing_access);

                  missing_access &= ~(pace->perm & missing_access);
                  if(!missing_access)
                    {
                      LogDebug(COMPONENT_FSAL, "access granted");
                      fsal_print_access_by_acl(pacl->naces, ace_number, pace,
					       v4mask, ERR_FSAL_NO_ERROR, is_dir, creds);
                      return true;
                    }
                }
             else if(pace->perm & missing_access)
               {
                 LogDebug(COMPONENT_FSAL, "access denied");
                 fsal_print_access_by_acl(pacl->naces, ace_number, pace, v4mask,
                                                ERR_FSAL_ACCESS, is_dir, creds);
                 return false;
               }
            }
        }

        ace_number += 1;
    }

  if(missing_access)
    {
      LogDebug(COMPONENT_FSAL, "access denied");
      return false;
    }
  else
    {
      LogDebug(COMPONENT_FSAL, "access granted");
      return true;
    }
}

static int fsal_check_access_no_acl(struct user_cred *creds,   /* IN */
				    struct req_op_context *req_ctx,
				    fsal_accessflags_t access_type,  /* IN */
				    struct attrlist * p_object_attributes /* IN */ )
{
	uid_t uid;
	gid_t gid;
	mode_t mode, mask;

	if( !access_type) {
		LogDebug(COMPONENT_FSAL, "Nothing was requested");
		return true;
	}

	uid = p_object_attributes->owner;
	gid = p_object_attributes->group;
	mode = p_object_attributes->mode;
	mask = ((access_type & FSAL_R_OK) ? S_IROTH : 0)
		| ((access_type & FSAL_W_OK) ? S_IWOTH : 0)
		| ((access_type & FSAL_X_OK) ? S_IXOTH : 0);

	LogDebug(COMPONENT_FSAL,
		 "file Mode=%#o, file uid=%u, file gid= %u, "
		 "user uid=%u, user gid= %u, access_type=0X%x",
		 mode,uid, gid,
		 creds->caller_uid,
		 creds->caller_gid,
		 access_type);

	/* If the uid of the file matches the uid of the user,
	 * then the uid mode bits take precedence. */
	if(creds->caller_uid == uid) {
		mode >>= 6;
	} else { /* followed by group(s) */
		if(creds->caller_gid == gid) {
			mode >>= 3;
		} else {
			/* Test if file belongs to alt user's groups */
			int i;

			for(i = 0; i < creds->caller_glen; i++) {
				if(creds->caller_garray[i] == gid) {
					mode >>= 3;
					break;
				}
			}
		}
	}
	/* others fall out the bottom... */
	if((mask & ~mode & S_IRWXO) == 0) {
		return true;
	} else {
                /* NFSv3 exception : if user wants to write to a file
                 * that is readonly but belongs to him, then allow it
                 * to do it, push the permission check to the client
                 * side */
		if((req_ctx->req_type == NFS_CALL ||
		    req_ctx->req_type == NFS_REQUEST) &&
		   req_ctx->nfs_vers == NFS_V3 &&
		   creds->caller_uid == uid &&
		   access_type == FSAL_W_OK &&
		   (mode & (S_IROTH|S_IXOTH))) {
			LogDebug(COMPONENT_FSAL,
				 "fsal_check_access_no_acl: File owner ok user %u",
				 uid);
			return true;
		} else {
			return false;
		}
	}
}

/* test_access
 * common (default) access check method for fsal_obj_handle objects.
 * NOTE: A fsal can replace this method with their own custom access
 *       checker.  If so and they wish to have an option to switch
 *       between their custom and this one, it their test_access
 *       method's responsibility to do that test and select this one. 
 */

fsal_status_t fsal_test_access(struct fsal_obj_handle *obj_hdl,
			       struct req_op_context *req_ctx,
			       fsal_accessflags_t access_type)
{
	struct attrlist *attribs = &obj_hdl->attributes;
	int retval;

	/* The root user always wins */
	if(req_ctx->creds->caller_uid == 0)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	if(attribs->acl && IS_FSAL_ACE4_MASK_VALID(access_type)) {
		retval = fsal_check_access_acl(req_ctx->creds,
					       FSAL_ACE4_MASK(access_type),
					       attribs);
	} else { /* fall back to use mode to check access. */
		retval = fsal_check_access_no_acl(req_ctx->creds,
						  req_ctx,
						  FSAL_MODE_MASK(access_type),
						  attribs);
	}
	if(retval)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	else
		return fsalstat(ERR_FSAL_ACCESS, 0);
}

uid_t   ganesha_uid;
gid_t   ganesha_gid;
int     ganehsa_ngroups;
gid_t * ganesha_groups = NULL;

void fsal_set_credentials(const struct user_cred *creds)
{
  if(set_threadgroups(creds->caller_glen,
             creds->caller_garray) != 0)
    LogFatal(COMPONENT_FSAL, "Could not set Context credentials");
  setgroup(creds->caller_gid);
  setuser(creds->caller_uid);
}

void fsal_save_ganesha_credentials()
{
  int  i;
  char buffer[1024], *p = buffer;
  ganesha_uid = setuser(0);
  setuser(ganesha_uid);
  ganesha_gid = setgroup(0);
  setgroup(ganesha_gid);
  ganehsa_ngroups = getgroups(0, NULL);
  if(ganehsa_ngroups != 0)
    {
      ganesha_groups = gsh_malloc(ganehsa_ngroups * sizeof(gid_t));
      if(ganesha_groups == NULL)
        {
          LogFatal(COMPONENT_FSAL,
                   "Could not allocate memory for Ganesha group list");
        }
      if(getgroups(ganehsa_ngroups, ganesha_groups) != ganehsa_ngroups)
        {
          LogFatal(COMPONENT_FSAL,
                   "Could not get list of ganesha groups");
        }
    }

  p += sprintf(p, "Ganesha uid=%d gid=%d ngroups=%d",
               (int) ganesha_uid, (int) ganesha_gid, ganehsa_ngroups);
  if(ganehsa_ngroups != 0)
    p += sprintf(p, " (");
  for(i = 0; i < ganehsa_ngroups; i++)
    {
      if((p - buffer) < (sizeof(buffer) - 10))
        {
          if(i == 0)
            p += sprintf(p, "%d", (int) ganesha_groups[i]);
          else
            p += sprintf(p, " %d", (int) ganesha_groups[i]);
        }
    }
  if(ganehsa_ngroups != 0)
    p += sprintf(p, ")");
  LogInfo(COMPONENT_FSAL,
          "%s", buffer);
}

void fsal_restore_ganesha_credentials()
{
  setuser(ganesha_uid);
  setgroup(ganesha_gid);
  if(set_threadgroups(ganehsa_ngroups, ganesha_groups) != 0)
    LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
}

/** @} */
