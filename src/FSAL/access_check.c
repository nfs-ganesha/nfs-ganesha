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
  bool result = false;
  char *cause = "";

  if (IS_FSAL_ACE_SPECIAL_ID(*pace))
    switch(pace->who.uid)
      {
        case FSAL_ACE_SPECIAL_OWNER:
          if(is_owner)
            {
              result = true;
              cause = "special owner";
            }
        break;

        case FSAL_ACE_SPECIAL_GROUP:
          if(is_group)
            {
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
      }
  else if (IS_FSAL_ACE_GROUP_ID(*pace))
    {
      if(fsal_check_ace_group(pace->who.gid, creds))
        {
          result = true;
          cause = "group";
        }
    }
  else
    {
      if(fsal_check_ace_owner(pace->who.uid, creds))
        {
          result = true;
          cause = "owner";
        }
    }

  LogDebug(COMPONENT_FSAL,
           "result: %d, cause: %s, flag: 0x%X, who: %d",
           result, cause, pace->flag, GET_FSAL_ACE_WHO(*pace));

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
  if (IS_FSAL_ACE_INHERIT_ONLY(*pace))
    {
      LogDebug(COMPONENT_FSAL, "Not applicable, "
               "inherit only");
      return false;
    }

  /* Use GPFS internal flag to further check the entry is applicable to this
   * object type. */
  if(is_file)
    {
      if(!IS_FSAL_FILE_APPLICABLE(*pace))
        {
          LogDebug(COMPONENT_FSAL, "Not applicable to file");
          return false;
        }
    }
  else  /* directory */
    {
      if(!IS_FSAL_DIR_APPLICABLE(*pace))
        {
          LogDebug(COMPONENT_FSAL, "Not applicable to dir");
          return false;
        }
    }

  /* The user should match who value. */
  is_applicable = is_root ||
                  fsal_check_ace_matches(pace, creds, is_owner, is_group);
  if(is_applicable)
    LogDebug(COMPONENT_FSAL, "Applicable, flag=0X%x",
             pace->flag);
  else
    LogDebug(COMPONENT_FSAL, "Not applicable to given user");

  return is_applicable;
}

/* originally def'd in /FSAL/FSAL_GPFS/fsal_internal.c:56: fix later */

#define ACL_DEBUG_BUF_SIZE 256

int display_fsal_inherit_flags(struct display_buffer * dspbuf, fsal_ace_t *pace)
{
  if(!pace)
    return display_cat(dspbuf, "NULL");

  return display_printf(dspbuf, "Inherit:%s%s%s%s",
                        IS_FSAL_ACE_FILE_INHERIT(*pace)? " file":"",
                        IS_FSAL_ACE_DIR_INHERIT(*pace) ? " dir":"",
                        IS_FSAL_ACE_INHERIT_ONLY(*pace)? " inherit_only":"",
                        IS_FSAL_ACE_NO_PROPAGATE(*pace)? " no_propagate":"");
}

int display_fsal_ace(struct display_buffer * dspbuf,
                     int                     ace_number,
                     fsal_ace_t            * pace,
                     bool                    is_dir)
{
  int b_left;

  if(!pace)
    return display_cat(dspbuf, "ACE: <NULL>");

  /* Print the entire ACE. */
  b_left = display_printf(dspbuf, "ACE %d:", ace_number);

  /* ACE type. */
  if(b_left > 0)
    b_left = display_cat(dspbuf,
                         IS_FSAL_ACE_ALLOW(*pace)? " allow":
                         IS_FSAL_ACE_DENY(*pace) ? " deny":
                         IS_FSAL_ACE_AUDIT(*pace)? " audit": " ?");

  /* ACE who and its type. */
  if(b_left > 0 && IS_FSAL_ACE_SPECIAL_ID(*pace))
    b_left = display_cat(dspbuf,
                         IS_FSAL_ACE_SPECIAL_OWNER(*pace)    ? " owner@":
                         IS_FSAL_ACE_SPECIAL_GROUP(*pace)    ? " group@":
                         IS_FSAL_ACE_SPECIAL_EVERYONE(*pace) ? " everyone@":"");

  if(b_left > 0 && !IS_FSAL_ACE_SPECIAL_ID(*pace))
    {
      if(IS_FSAL_ACE_SPECIAL_ID(*pace))
        b_left = display_printf(dspbuf, " gid %d", pace->who.gid);
      else
        b_left = display_printf(dspbuf, " uid %d", pace->who.uid);
    }

  /* ACE mask. */
  if(b_left > 0)
    b_left = display_fsal_v4mask(dspbuf, pace->perm, is_dir);

  /* ACE Inherit flags. */
  if(b_left > 0 && IS_FSAL_ACE_INHERIT(*pace))
    b_left = display_fsal_inherit_flags(dspbuf, pace);

  return b_left;
}

int display_fsal_v4mask(struct display_buffer * dspbuf,
                        fsal_aceperm_t          v4mask,
                        bool                    is_dir)
{
  int b_left = display_printf(dspbuf, "0x%06x", v4mask);

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_READ_DATA))
    b_left = display_cat(dspbuf, " READ");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_DATA) && is_dir)
    b_left = display_cat(dspbuf, " ADD_FILE");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_DATA) && !is_dir)
    b_left = display_cat(dspbuf, " WRITE");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_APPEND_DATA) && is_dir)
    b_left = display_cat(dspbuf, " ADD_SUBDIR");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_APPEND_DATA) && !is_dir)
    b_left = display_cat(dspbuf, " APPEND");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_READ_NAMED_ATTR))
    b_left = display_cat(dspbuf, " READ_NAMED");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_NAMED_ATTR))
    b_left = display_cat(dspbuf, " WRITE_NAMED");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_EXECUTE))
    b_left = display_cat(dspbuf, " EXECUTE");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_DELETE_CHILD))
    b_left = display_cat(dspbuf, " DELETE_CHILD");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_READ_ATTR))
    b_left = display_cat(dspbuf, " READ_ATTR");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_ATTR))
    b_left = display_cat(dspbuf, " WRITE_ATTR");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_DELETE))
    b_left = display_cat(dspbuf, " DELETE");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_READ_ACL))
    b_left = display_cat(dspbuf, " READ_ACL");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_ACL))
    b_left = display_cat(dspbuf, " WRITE_ACL");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_WRITE_OWNER))
    b_left = display_cat(dspbuf, " WRITE_OWNER");

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE_PERM_SYNCHRONIZE))
    b_left = display_cat(dspbuf, " SYNCHRONIZE");

  return b_left;
}

static void fsal_print_access_by_acl(int naces, int ace_number,
	                             fsal_ace_t *pace, fsal_aceperm_t perm,
                                     unsigned int access_result,
                                     bool is_dir,
                                     struct user_cred *creds)
{
  char                  str[LOG_BUFF_LEN];
  struct display_buffer dspbuf = {sizeof(str), str, str};
  int                   b_left;

  if(!isFullDebug(COMPONENT_FSAL))
    return;

  if((access_result == ERR_FSAL_NO_ERROR))
    b_left = display_cat(&dspbuf, "access granted");
  else
    b_left = display_cat(&dspbuf, "access denied");

  if(b_left > 0)
    b_left = display_printf(&dspbuf, " uid %u gid %u Access req:",
                            creds->caller_uid,
                            creds->caller_gid);

  if(b_left > 0)
    b_left = display_fsal_v4mask(&dspbuf, perm, is_dir);

  if(b_left > 0 && (naces != ace_number))
    b_left = display_fsal_ace(&dspbuf, ace_number, pace, is_dir);

  LogFullDebug(COMPONENT_FSAL, "%s", str);
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
  bool is_root = false;

  /* unsatisfied flags */
  missing_access = v4mask;
  if(!missing_access)
    {
      LogFullDebug(COMPONENT_FSAL, "Nothing was requested");
      return true;
    }

  /* Get file ownership information. */
  uid = p_object_attributes->owner;
  gid = p_object_attributes->group;
  pacl = p_object_attributes->acl;
  is_dir = (p_object_attributes->type == DIRECTORY);
  is_root = creds->caller_uid == 0;

  if(is_root)
    {
      if(is_dir)
        {
          /* On a directory, allow root anything. */
          LogDebug(COMPONENT_FSAL, "Met root privileges on directory");
          return true;
        }

      /* Otherwise, allow root anything but execute. */
      missing_access &= FSAL_ACE_PERM_EXECUTE;

      if(!missing_access)
        {
          LogDebug(COMPONENT_FSAL, "Met root privileges");
          return true;
        }
    }

  LogFullDebug(COMPONENT_FSAL,
               "file acl=%p, file uid=%u, file gid=%u, ",
               pacl, uid, gid);

  if(isFullDebug(COMPONENT_FSAL))
    {
      char                  str[LOG_BUFF_LEN];
      struct display_buffer dspbuf = {sizeof(str), str, str};

      (void) display_fsal_v4mask(&dspbuf,
                                 v4mask,
                                 p_object_attributes->type == DIRECTORY);

      LogFullDebug(COMPONENT_FSAL,
                   "user uid=%u, user gid= %u, v4mask=%s",
                   creds->caller_uid,
                   creds->caller_gid,
                   str);
    }

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
          LogFullDebug(COMPONENT_FSAL, "Met owner privileges");
          return true;
        }
    }

  // TODO: Even if user is admin, audit/alarm checks should be done.

  for(pace = pacl->aces; pace < pacl->aces + pacl->naces; pace++)
    {
      ace_number += 1;

      LogFullDebug(COMPONENT_FSAL,
                   "ace numnber: %d ace type 0x%X perm 0x%X flag 0x%X who %u",
                   ace_number, pace->type, pace->perm, pace->flag, GET_FSAL_ACE_WHO(*pace));

      /* Process Allow and Deny entries. */
      if(IS_FSAL_ACE_ALLOW(*pace) || IS_FSAL_ACE_DENY(*pace))
        {
          LogFullDebug(COMPONENT_FSAL, "allow or deny");

          /* Check if this ACE is applicable. */
          if(fsal_check_ace_applicable(pace, creds, is_dir, is_owner, is_group, is_root))
            {
              if(IS_FSAL_ACE_ALLOW(*pace))
                {
                  LogFullDebug(COMPONENT_FSAL,
                               "allow perm 0x%X remainingPerms 0x%X",
                               pace->perm,
                               missing_access);

                  missing_access &= ~(pace->perm & missing_access);

                  if(!missing_access)
                    {
                      fsal_print_access_by_acl(pacl->naces,
                                               ace_number,
                                               pace,
                                               v4mask,
                                               ERR_FSAL_NO_ERROR,
                                               is_dir,
                                               creds);
                      return true;
                    }
                }
             else if((pace->perm & missing_access) && !is_root)
               {
                 fsal_print_access_by_acl(pacl->naces,
                                          ace_number,
                                          pace,
                                          v4mask,
                                          ERR_FSAL_ACCESS,
                                          is_dir,
                                          creds);
                 return false;
               }
            }
        }
    }

  if(missing_access)
    {
      LogFullDebug(COMPONENT_FSAL, "access denied");
      return false;
    }
  else
    {
      LogFullDebug(COMPONENT_FSAL, "access granted");
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

	if(creds->caller_uid == 0) {
		bool rc;
		rc = ((access_type & FSAL_X_OK) == 0) ||
		     ((mode & (S_IXOTH | S_IXUSR | S_IXGRP)) != 0);
		if(!rc) {
			LogDebug(COMPONENT_FSAL,
			         "Root is not allowed execute access unless at least one user is allowed execute access.");
		} else {
			LogDebug(COMPONENT_FSAL,
			         "Root is granted access.");
		}
		return rc;
	}

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
	return ((mask & ~mode & S_IRWXO) == 0);
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
  setuser(creds->caller_uid);
  setgroup(creds->caller_gid);
  if(set_threadgroups(creds->caller_glen,
             creds->caller_garray) != 0)
    LogFatal(COMPONENT_FSAL, "Could not set Context credentials");
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
