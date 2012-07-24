/*
 * file/object access checking
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include  "fsal.h"
#include <sys/stat.h>
#include "FSAL/access_check.h"



static int fsal_check_access_acl(struct user_cred *creds,   /* IN */
                                 fsal_aceperm_t v4mask,  /* IN */
                                 struct attrlist * p_object_attributes   /* IN */ );

static int fsal_check_access_no_acl(struct user_cred *creds,   /* IN */
				    fsal_accessflags_t access_type,  /* IN */
				    struct attrlist * p_object_attributes /* IN */ );


static bool_t fsal_check_ace_owner(uint64_t uid, struct user_cred *creds)
{
  return (creds->caller_uid == uid);
}

static bool_t fsal_check_ace_group(uint64_t gid, struct user_cred *creds)
{
  int i;

  if(creds->caller_gid == gid)
    return TRUE;

  for(i = 0; i < creds->caller_glen; i++)
    {
      if(creds->caller_garray[i] == gid)
        return TRUE;
    }

  return FALSE;
}

static bool_t fsal_check_ace_matches(fsal_ace_t *pace,
                                     struct user_cred *creds,
                                     bool_t is_owner,
                                     bool_t is_group)
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
           "fsal_check_ace_matches: matches %d flag 0x%X who %"PRIu64,
           matches, pace->flag, GET_FSAL_ACE_WHO(*pace));

  return (matches != 0);
}

static bool_t fsal_check_ace_applicable(fsal_ace_t *pace,
                                        struct user_cred *creds,
                                        bool_t is_dir,
                                        bool_t is_owner,
                                        bool_t is_group)
{
  bool_t is_applicable = FALSE;
  bool_t is_file = !is_dir;

  /* To be applicable, the entry should not be INHERIT_ONLY. */
  if (IS_FSAL_ACE_INHERIT_ONLY(*pace))
    {
      LogDebug(COMPONENT_FSAL, "fsal_check_ace_applicable: Not applicable, "
               "inherit only");
      return FALSE;
    }

  /* Use GPFS internal flag to further check the entry is applicable to this
   * object type. */
  if(is_file)
    {
      if(!IS_FSAL_FILE_APPLICABLE(*pace))
        {
          LogDebug(COMPONENT_FSAL, "fsal_check_ace_applicable: Not applicable to file");
          return FALSE;
        }
    }
  else  /* directory */
    {
      if(!IS_FSAL_DIR_APPLICABLE(*pace))
        {
          LogDebug(COMPONENT_FSAL, "fsal_check_ace_applicable: Not applicable to dir");
          return FALSE;
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
  sprintf(p_acebuf, "ACE %d %s %s %"PRIu64" %c%c%c%c%c%c%c%c%c%c%c%c%c%c %s",
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
                                     bool_t is_dir,
                                     struct user_cred *creds)
{
  char ace_data[ACL_DEBUG_BUF_SIZE];
  char access_data[2 * ACL_DEBUG_BUF_SIZE];
  uint64_t user = creds->caller_uid;
  bool_t is_last_ace = (naces == ace_number);

  if(!is_last_ace)
    fsal_print_ace(ace_number, pace, ace_data);

  /* Print the access result and the request. */
  sprintf(access_data, "%s: %s uid %"PRIu64" %s",
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
          IS_FSAL_ACE_BIT(perm, FSAL_ACE_PERM_SYNCHRONIZE)	    ?"SYNCHRONIZE": "UNKNOWN",
          user, (!is_last_ace) ? ace_data: "");

  LogDebug(COMPONENT_FSAL, "fsal_check_access_by_acl_debug: %s", access_data);
}

static int fsal_check_access_acl(struct user_cred *creds,   /* IN */
				 fsal_aceperm_t v4mask,  /* IN */
				 struct attrlist * p_object_attributes   /* IN */ )
{
  fsal_aceperm_t missing_access;
  uint64_t uid;
  uint64_t gid;
  fsal_acl_t *pacl = NULL;
  fsal_ace_t *pace = NULL;
  int ace_number = 0;
  bool_t is_dir = FALSE;
  bool_t is_owner = FALSE;
  bool_t is_group = FALSE;

  /* unsatisfied flags */
  missing_access = v4mask;
  if(!missing_access)
    {
      LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: Nothing was requested");
      return TRUE;
    }

  /* Get file ownership information. */
  uid = p_object_attributes->owner;
  gid = p_object_attributes->group;
  pacl = p_object_attributes->acl;
  is_dir = (p_object_attributes->type == DIRECTORY);

  LogDebug(COMPONENT_FSAL,
           "fsal_check_access_acl: file acl=%p, file uid=%"PRIu64
           ", file gid= %"PRIu64,
           pacl,uid, gid);
  LogDebug(COMPONENT_FSAL,
           "fsal_check_access_acl: user uid=%"PRIu64
           ", user gid= %"PRIu64", v4mask=0x%X",
           (uint64_t) creds->caller_uid,
           (uint64_t) creds->caller_gid,
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
          return TRUE;
        }
    }

  // TODO: Even if user is admin, audit/alarm checks should be done.

  ace_number = 1;
  for(pace = pacl->aces; pace < pacl->aces + pacl->naces; pace++)
    {
      LogDebug(COMPONENT_FSAL,
               "fsal_check_access_acl: ace type 0x%X perm 0x%X "
               "flag 0x%X who %"PRIu64,
               pace->type, pace->perm, pace->flag, GET_FSAL_ACE_WHO(*pace));

      /* Process Allow and Deny entries. */
      if(IS_FSAL_ACE_ALLOW(*pace) || IS_FSAL_ACE_DENY(*pace))
        {
          LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: allow or deny");

          /* Check if this ACE is applicable. */
          if(fsal_check_ace_applicable(pace, creds, is_dir, is_owner, is_group))
            {
              if(IS_FSAL_ACE_ALLOW(*pace))
                {
                  LogDebug(COMPONENT_FSAL,
                           "fsal_check_access_acl: allow perm 0x%X remainingPerms 0x%X",
                           pace->perm, missing_access);

                  missing_access &= ~(pace->perm & missing_access);
                  if(!missing_access)
                    {
                      LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: access granted");
                      fsal_print_access_by_acl(pacl->naces, ace_number, pace,
                                                     v4mask, ERR_FSAL_NO_ERROR, is_dir, creds);
                      return TRUE;
                    }
                }
             else if(pace->perm & missing_access)
               {
                 LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: access denied");
                 fsal_print_access_by_acl(pacl->naces, ace_number, pace, v4mask,
                                                ERR_FSAL_ACCESS, is_dir, creds);
                 return FALSE;
               }
            }
        }

        ace_number += 1;
    }

  if(missing_access)
    {
      LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: access denied");
      return FALSE;
    }
  else
    {
      LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: access granted");
      return TRUE;
    }
}

static int fsal_check_access_no_acl(struct user_cred *creds,   /* IN */
				    fsal_accessflags_t access_type,  /* IN */
				    struct attrlist * p_object_attributes /* IN */ )
{
	uint64_t uid;
	uint64_t gid;
	mode_t mode, mask;

	if( !access_type) {
		LogDebug(COMPONENT_FSAL, "fsal_check_access_no_acl: Nothing was requested");
		return TRUE;
	}

	uid = p_object_attributes->owner;
	gid = p_object_attributes->group;
	mode = p_object_attributes->mode;
	mask = ((access_type & FSAL_R_OK) ? S_IROTH : 0)
		| ((access_type & FSAL_W_OK) ? S_IWOTH : 0)
		| ((access_type & FSAL_X_OK) ? S_IXOTH : 0);

	LogDebug(COMPONENT_FSAL,
		 "fsal_check_access_no_acl: file Mode=%#o, file uid=%"PRIu64
		 ", file gid= %"PRIu64
		 ", user uid=%d, user gid= %d, access_type=0X%x",
		 mode,uid, gid,
		 creds->caller_uid,
		 creds->caller_gid,
		 access_type);

	/* If the uid of the file matches the uid of the user,
	 * then the uid mode bits take precedence. */
	if(creds->caller_uid == uid) {
		if(access_type & FSAL_OWNER_OK) {
			LogDebug(COMPONENT_FSAL,
				 "fsal_check_access_no_acl: File owner ok user %"PRIu64,
				 uid);
			return TRUE;
		}
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
	if((mask & ~mode & S_IRWXO) == 0)
		return TRUE;
	else
		return FALSE;
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
						  FSAL_MODE_MASK(access_type),
						  attribs);
	}
	if(retval)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	else
		return fsalstat(ERR_FSAL_ACCESS, 0);
}
