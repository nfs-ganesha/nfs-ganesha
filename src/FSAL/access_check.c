/*
 * file/object access checking
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include  "fsal.h"
#include <sys/stat.h>
#include "FSAL/access_check.h"
#include <unistd.h>
#include <sys/fsuid.h>
#include <sys/syscall.h>
#include <grp.h>
#include <sys/types.h>

#define ACL_DEBUG_BUF_SIZE 256

#ifdef _USE_NFS4_ACL
fsal_boolean_t fsal_check_ace_group(fsal_gid_t gid, fsal_op_context_t *p_context)
{
  int i;

  if(p_context->credential.group == gid)
    return TRUE;

  for(i = 0; i < p_context->credential.nbgroups; i++)
    {
      if(p_context->credential.alt_groups[i] == gid)
        return TRUE;
    }

  return FALSE;
}

fsal_boolean_t fsal_check_ace_matches(fsal_ace_t *pace,
                                      fsal_op_context_t *p_context,
                                      fsal_boolean_t is_owner,
                                      fsal_boolean_t is_group)
{
  fsal_boolean_t result = FALSE;
  char *cause = "";

  if (IS_FSAL_ACE_SPECIAL_ID(*pace))
    switch(pace->who.uid)
      {
        case FSAL_ACE_SPECIAL_OWNER:
          if(is_owner)
            {
              result = TRUE;
              cause = "special owner";
            }
        break;

        case FSAL_ACE_SPECIAL_GROUP:
          if(is_group)
            {
              result = TRUE;
              cause = "special group";
            }
        break;

        case FSAL_ACE_SPECIAL_EVERYONE:
          result = TRUE;
          cause = "special everyone";
        break;

        default:
        break;
      }
  else if (IS_FSAL_ACE_GROUP_ID(*pace))
    {
      if(fsal_check_ace_group(pace->who.gid, p_context))
        {
          result = TRUE;
          cause = "group";
        }
    }
  else
    {
      if(fsal_check_ace_owner(pace->who.uid, p_context))
        {
          result = TRUE;
          cause = "owner";
        }
    }

  LogDebug(COMPONENT_FSAL,
           "result: %d, cause: %s, flag: 0x%X, who: %d",
           result, cause, pace->flag, GET_FSAL_ACE_WHO(*pace));

  return result;
}

fsal_boolean_t fsal_check_ace_applicable(fsal_ace_t *pace,
                                         fsal_op_context_t *p_context,
                                         fsal_boolean_t is_dir,
                                         fsal_boolean_t is_owner,
                                         fsal_boolean_t is_group)
{
  fsal_boolean_t is_applicable = FALSE;
  fsal_boolean_t is_file = !is_dir;

  /* To be applicable, the entry should not be INHERIT_ONLY. */
  if (IS_FSAL_ACE_INHERIT_ONLY(*pace))
    {
      LogDebug(COMPONENT_FSAL, "Not applicable, "
               "inherit only");
      return FALSE;
    }

  /* Use GPFS internal flag to further check the entry is applicable to this
   * object type. */
  if(is_file)
    {
      if(!IS_FSAL_FILE_APPLICABLE(*pace))
        {
          LogDebug(COMPONENT_FSAL, "Not applicable to file");
          return FALSE;
        }
    }
  else  /* directory */
    {
      if(!IS_FSAL_DIR_APPLICABLE(*pace))
        {
          LogDebug(COMPONENT_FSAL, "Not applicable to dir");
          return FALSE;
        }
    }

  /* The user should match who value. */
  is_applicable = fsal_check_ace_matches(pace, p_context, is_owner, is_group);
  if(is_applicable)
    LogDebug(COMPONENT_FSAL, "Applicable, flag=0X%x",
             pace->flag);
  else
    LogDebug(COMPONENT_FSAL, "Not applicable to given user");

  return is_applicable;
}

void fsal_print_inherit_flags(fsal_ace_t *pace, char *p_buf)
{
  if(!pace || !p_buf)
    return;

  memset(p_buf, 0, ACL_DEBUG_BUF_SIZE);

  sprintf(p_buf, "Inherit:%s,%s,%s,%s",
          IS_FSAL_ACE_FILE_INHERIT(*pace)? "file":"",
          IS_FSAL_ACE_DIR_INHERIT(*pace) ? "dir":"",
          IS_FSAL_ACE_INHERIT_ONLY(*pace)? "inherit_only":"",
          IS_FSAL_ACE_NO_PROPAGATE(*pace)? "no_propagate":"");
}

void fsal_print_ace(int ace_number, fsal_ace_t *pace, char *p_acebuf)
{
  char inherit_flags[ACL_DEBUG_BUF_SIZE];
  char ace_buf[ACL_DEBUG_BUF_SIZE];

  if(!pace)
    return;

  memset(inherit_flags, 0, ACL_DEBUG_BUF_SIZE);
  memset(ace_buf, 0, ACL_DEBUG_BUF_SIZE);

  /* Get inherit flags if any. */
  fsal_print_inherit_flags(pace, inherit_flags);

  /* Print the entire ACE. */
  sprintf(ace_buf, "ACE %d %s %s %s %d %s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s %s",
          ace_number,
          /* ACE type. */
          IS_FSAL_ACE_ALLOW(*pace)? "allow":
          IS_FSAL_ACE_DENY(*pace) ? "deny":
          IS_FSAL_ACE_AUDIT(*pace)? "audit": "?",
          /* ACE who and its type. */
          (IS_FSAL_ACE_SPECIAL_ID(*pace) && IS_FSAL_ACE_SPECIAL_OWNER(*pace))    ? "owner@":
          (IS_FSAL_ACE_SPECIAL_ID(*pace) && IS_FSAL_ACE_SPECIAL_GROUP(*pace))    ? "group@":
          (IS_FSAL_ACE_SPECIAL_ID(*pace) && IS_FSAL_ACE_SPECIAL_EVERYONE(*pace)) ? "everyone@":"",
          IS_FSAL_ACE_SPECIAL_ID(*pace)						 ? "specialid":
          IS_FSAL_ACE_GROUP_ID(*pace) 						 ? "gid": "uid",
          GET_FSAL_ACE_WHO(*pace),
          /* ACE mask. */
          IS_FSAL_ACE_READ_DATA(*pace)           ? "read":"",
          IS_FSAL_ACE_WRITE_DATA(*pace)          ? "write":"",
          IS_FSAL_ACE_EXECUTE(*pace)             ? "execute":"",
          IS_FSAL_ACE_ADD_SUBDIRECTORY(*pace)    ? "append":"",
          IS_FSAL_ACE_READ_NAMED_ATTR(*pace)     ? "read_named_attr":"",
          IS_FSAL_ACE_WRITE_NAMED_ATTR(*pace)    ? "write_named_attr":"",
          IS_FSAL_ACE_DELETE_CHILD(*pace)        ? "delete_child":"",
          IS_FSAL_ACE_READ_ATTR(*pace)           ? "read_attr":"",
          IS_FSAL_ACE_WRITE_ATTR(*pace)          ? "write_attr":"",
          IS_FSAL_ACE_DELETE(*pace)              ? "delete":"",
          IS_FSAL_ACE_READ_ACL(*pace)            ? "read_acl":"",
          IS_FSAL_ACE_WRITE_ACL(*pace)           ? "write_acl":"",
          IS_FSAL_ACE_WRITE_OWNER(*pace)         ? "write_owner":"",
          IS_FSAL_ACE_SYNCHRONIZE(*pace)         ? "synchronize":"",
          /* ACE Inherit flags. */
          IS_FSAL_ACE_INHERIT(*pace)? inherit_flags: "");
  LogDebug(COMPONENT_FSAL, "%s", ace_buf);
}

void fsal_print_v4mask(fsal_aceperm_t v4mask)
{
  fsal_ace_t ace;
  fsal_ace_t *pace = &ace;
  char v4mask_buf[ACL_DEBUG_BUF_SIZE];

  pace->perm = v4mask;
  memset(v4mask_buf, 0, ACL_DEBUG_BUF_SIZE);

  sprintf(v4mask_buf, "v4mask %s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s",
	  IS_FSAL_ACE_READ_DATA(*pace)           ? "read":"",
	  IS_FSAL_ACE_WRITE_DATA(*pace)          ? "write":"",
	  IS_FSAL_ACE_EXECUTE(*pace)             ? "execute":"",
	  IS_FSAL_ACE_ADD_SUBDIRECTORY(*pace)    ? "append":"",
	  IS_FSAL_ACE_READ_NAMED_ATTR(*pace)     ? "read_named_attr":"",
	  IS_FSAL_ACE_WRITE_NAMED_ATTR(*pace)    ? "write_named_attr":"",
	  IS_FSAL_ACE_DELETE_CHILD(*pace)        ? "delete_child":"",
	  IS_FSAL_ACE_READ_ATTR(*pace)           ? "read_attr":"",
	  IS_FSAL_ACE_WRITE_ATTR(*pace)          ? "write_attr":"",
	  IS_FSAL_ACE_DELETE(*pace)              ? "delete":"",
	  IS_FSAL_ACE_READ_ACL(*pace)            ? "read_acl":"",
	  IS_FSAL_ACE_WRITE_ACL(*pace)           ? "write_acl":"",
	  IS_FSAL_ACE_WRITE_OWNER(*pace)         ? "write_owner":"",
	  IS_FSAL_ACE_SYNCHRONIZE(*pace)         ? "synchronize":"");
  LogDebug(COMPONENT_FSAL, "%s", v4mask_buf);
}

static void fsal_print_access_by_acl(int naces, int ace_number,
	                             fsal_ace_t *pace, fsal_aceperm_t perm,
                                     unsigned int access_result,
                                     fsal_boolean_t is_dir,
                                     fsal_op_context_t *p_context)
{
  char ace_data[ACL_DEBUG_BUF_SIZE];
  char access_data[2 * ACL_DEBUG_BUF_SIZE];
  fsal_uid_t user = p_context->credential.user;
  fsal_boolean_t is_last_ace = (naces == ace_number);

  if(!is_last_ace)
    fsal_print_ace(ace_number, pace, ace_data);

  /* Print the access result and the request. */
  sprintf(access_data, "%s: %s uid %d %s",
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

  LogDebug(COMPONENT_FSAL, "%s", access_data);
}

fsal_status_t fsal_check_access_acl(fsal_op_context_t * p_context,   /* IN */
                                    fsal_aceperm_t v4mask,  /* IN */
                                    fsal_attrib_list_t * p_object_attributes   /* IN */ )
{
  fsal_aceperm_t missing_access;
  fsal_uid_t uid;
  fsal_gid_t gid;
  fsal_acl_t *pacl = NULL;
  fsal_ace_t *pace = NULL;
  int ace_number = 0;
  fsal_boolean_t is_dir = FALSE;
  fsal_boolean_t is_owner = FALSE;
  fsal_boolean_t is_group = FALSE;
  fsal_boolean_t is_root = FALSE;

  /* unsatisfied flags */
  missing_access = v4mask;
  if(!missing_access)
    {
      LogDebug(COMPONENT_FSAL, "Nothing was requested");
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  /* Get file ownership information. */
  uid = p_object_attributes->owner;
  gid = p_object_attributes->group;
  pacl = p_object_attributes->acl;
  is_dir = (p_object_attributes->type == FSAL_TYPE_DIR);
  is_root = (p_context->credential.user == 0);

  if(is_root)
    {
      if(is_dir)
        {
          /* On a directory, allow root anything. */
          LogDebug(COMPONENT_FSAL, "Met root privileges on directory");
          ReturnCode(ERR_FSAL_NO_ERROR, 0);
        }

      /* Otherwise, allow root anything but execute. */
      missing_access &= FSAL_ACE_PERM_EXECUTE;

      if(!missing_access)
        {
          LogDebug(COMPONENT_FSAL, "Met root privileges");
          ReturnCode(ERR_FSAL_NO_ERROR, 0);
        }
    }

  LogDebug(COMPONENT_FSAL,
           "file acl=%p, file uid=%d, file gid= %d",
           pacl,uid, gid);
  LogDebug(COMPONENT_FSAL,
           "user uid=%d, user gid= %d, v4mask=0x%X",
           p_context->credential.user,
           p_context->credential.group,
           v4mask);

  if(isFullDebug(COMPONENT_FSAL))
    fsal_print_v4mask(v4mask);

  is_owner = fsal_check_ace_owner(uid, p_context);
  is_group = fsal_check_ace_group(gid, p_context);

  /* Always grant READ_ACL, WRITE_ACL and READ_ATTR, WRITE_ATTR to the file
   * owner. */
  if(is_owner)
    {
      missing_access &= ~(FSAL_ACE_PERM_WRITE_ACL | FSAL_ACE_PERM_READ_ACL);
      missing_access &= ~(FSAL_ACE_PERM_WRITE_ATTR | FSAL_ACE_PERM_READ_ATTR);
      if(!missing_access)
        {
          LogDebug(COMPONENT_FSAL, "Met owner privileges");
          ReturnCode(ERR_FSAL_NO_ERROR, 0);
        }
    }

  // TODO: Even if user is admin, audit/alarm checks should be done.

  for(pace = pacl->aces; pace < pacl->aces + pacl->naces; pace++)
    {
      ace_number += 1;

      LogDebug(COMPONENT_FSAL,
               "ace type 0x%X perm 0x%X flag 0x%X who %d",
               pace->type, pace->perm, pace->flag, GET_FSAL_ACE_WHO(*pace));

      /* Process Allow and Deny entries. */
      if(IS_FSAL_ACE_ALLOW(*pace) || IS_FSAL_ACE_DENY(*pace))
        {
          LogDebug(COMPONENT_FSAL, "allow or deny");

          /* Check if this ACE is applicable. */
          if(fsal_check_ace_applicable(pace, p_context, is_dir, is_owner, is_group))
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
                                                     v4mask, ERR_FSAL_NO_ERROR, is_dir, p_context);
                      ReturnCode(ERR_FSAL_NO_ERROR, 0);
                    }
                }
             else if(pace->perm & missing_access)
               {
                 LogDebug(COMPONENT_FSAL, "access denied");
                 fsal_print_access_by_acl(pacl->naces, ace_number, pace, v4mask,
                                                ERR_FSAL_ACCESS, is_dir, p_context);
                 ReturnCode(ERR_FSAL_ACCESS, 0);
               }
            }
        }
    }

  if(missing_access)
    {
      LogDebug(COMPONENT_FSAL, "access denied");
      ReturnCode(ERR_FSAL_ACCESS, 0);
    }
  else
    {
      LogDebug(COMPONENT_FSAL, "access granted");
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
#endif                          /* _USE_NFS4_ACL */

fsal_status_t fsal_check_access_no_acl(fsal_op_context_t * p_context,   /* IN */
                                       fsal_accessflags_t access_type,  /* IN */
                                       struct stat *p_buffstat, /* IN */
                                       fsal_attrib_list_t * p_object_attributes /* IN */ )
{
  fsal_accessflags_t missing_access;
  unsigned int is_grp, i;
  fsal_uid_t uid;
  fsal_gid_t gid;
  fsal_accessmode_t mode;

  /* If the FSAL_F_OK flag is set, returns ERR INVAL */

  if(access_type & FSAL_F_OK)
    ReturnCode(ERR_FSAL_INVAL, 0);

  /* unsatisfied flags */
  missing_access = access_type;
  if(!missing_access)
    {
      LogDebug(COMPONENT_FSAL, "Nothing was requested");
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  if(p_object_attributes)
    {
      uid = p_object_attributes->owner;
      gid = p_object_attributes->group;
      mode = p_object_attributes->mode;

    }
  else
    {
      uid = p_buffstat->st_uid;
      gid = p_buffstat->st_gid;
      mode = unix2fsal_mode(p_buffstat->st_mode);
    }

  LogDebug(COMPONENT_FSAL,
               "file Mode=%#o, file uid=%d, file gid= %d",
               mode,uid, gid);
#ifdef _USE_HPSS
  LogDebug(COMPONENT_FSAL,
               "user uid=%d, user gid= %d, access_type=0X%x",
               p_context->credential.hpss_usercred.Uid,
               p_context->credential.hpss_usercred.Gid,
               access_type);
#else
  LogDebug(COMPONENT_FSAL,
               "user uid=%d, user gid= %d, access_type=0X%x",
               p_context->credential.user,
               p_context->credential.group,
               access_type);
#endif

  if(p_context->credential.user == 0)
    {
      /* Always grant read/write access to root */
      missing_access &= ~(FSAL_R_OK | FSAL_W_OK);
    }

  /* If the uid of the file matches the uid of the user,
   * then the uid mode bits take precedence. */
#ifdef _USE_HPSS
  if(p_context->credential.hpss_usercred.Uid == uid)
#else
  if(p_context->credential.user == uid)
#endif
    {

      LogDebug(COMPONENT_FSAL,
                   "File belongs to user %d", uid);

      if(mode & FSAL_MODE_RUSR)
        missing_access &= ~FSAL_R_OK;

      if(mode & FSAL_MODE_WUSR)
        missing_access &= ~FSAL_W_OK;

      if(mode & FSAL_MODE_XUSR)
        missing_access &= ~FSAL_X_OK;

      /* handle the creation of a new 500 file correctly */
      if((missing_access & FSAL_OWNER_OK) != 0)
        missing_access = 0;

      if(missing_access == 0)
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
      else
        {
          LogDebug(COMPONENT_FSAL,
                       "Mode=%#o, Access=0X%x, Rights missing: 0X%x",
                       mode, access_type, missing_access);
          ReturnCode(ERR_FSAL_ACCESS, 0);
        }

    }

  /* missing_access will be nonzero triggering a failure
   * even though FSAL_OWNER_OK is not even a real posix file
   * permission */
  missing_access &= ~FSAL_OWNER_OK;

  /* Test if the file belongs to user's group. */
#ifdef _USE_HPSS
  is_grp = (p_context->credential.hpss_usercred.Gid == gid);
  if(is_grp)
    LogDebug(COMPONENT_FSAL,
                 "File belongs to user's group %d",
                 p_context->credential.hpss_usercred.Gid);

  /* Test if file belongs to alt user's groups */
  if(!is_grp)
    for(i = 0; i < p_context->credential.hpss_usercred.NumGroups; i++)
      {
        is_grp = (p_context->credential.hpss_usercred.AltGroups[i] == gid);
        if(is_grp)
          LogDebug(COMPONENT_FSAL,
                       "File belongs to user's alt group %d",
                       p_context->credential.hpss_usercred.AltGroups[i]);
        if(is_grp)
          break;
      }
#else
  is_grp = (p_context->credential.group == gid);
  if(is_grp)
    LogDebug(COMPONENT_FSAL,
                 "File belongs to user's group %d",
                 p_context->credential.group);

  /* Test if file belongs to alt user's groups */
  if(!is_grp)
    for(i = 0; i < p_context->credential.nbgroups; i++)
      {
        is_grp = (p_context->credential.alt_groups[i] == gid);
        if(is_grp)
          LogDebug(COMPONENT_FSAL,
                       "File belongs to user's alt group %d",
                       p_context->credential.alt_groups[i]);
        if(is_grp)
          break;
      }
#endif

  /* If the gid of the file matches the gid of the user or
   * one of the alternatve gids of the user, then the uid mode
   * bits take precedence. */
  if(is_grp)
    {
      if(mode & FSAL_MODE_RGRP)
        missing_access &= ~FSAL_R_OK;

      if(mode & FSAL_MODE_WGRP)
        missing_access &= ~FSAL_W_OK;

      if(mode & FSAL_MODE_XGRP)
        missing_access &= ~FSAL_X_OK;

      if(missing_access == 0)
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
      else
        ReturnCode(ERR_FSAL_ACCESS, 0);

    }

  /* If the user uid is not 0, the uid does not match the file's, and
   * the user's gids do not match the file's gid, we apply the "other"
   * mode bits to the user. */
  if(mode & FSAL_MODE_ROTH)
    missing_access &= ~FSAL_R_OK;

  if(mode & FSAL_MODE_WOTH)
    missing_access &= ~FSAL_W_OK;

  if(mode & FSAL_MODE_XOTH)
    missing_access &= ~FSAL_X_OK;

  if(missing_access == 0)
    ReturnCode(ERR_FSAL_NO_ERROR, 0);
  else {
    LogDebug(COMPONENT_FSAL,
                 "Mode=%#o, Access=0X%x, Rights missing: 0X%x",
                 mode, access_type, missing_access);
    ReturnCode(ERR_FSAL_ACCESS, 0);
  }

}

/* fsal_check_access
 * Check the access by using NFS4 ACL if it exists. Otherwise, use mode.
 */

fsal_status_t fsal_check_access(fsal_op_context_t * p_context,   /* IN */
                                fsal_accessflags_t access_type,  /* IN */
                                struct stat *p_buffstat, /* IN */
                                fsal_attrib_list_t * p_object_attributes /* IN */ )
{
  /* sanity checks. */
  if((!p_object_attributes && !p_buffstat) || !p_context)
    ReturnCode(ERR_FSAL_FAULT, 0);

#ifdef _USE_NFS4_ACL
  /* If ACL exists and given access type is ace4 mask, use ACL to check access. */
  LogDebug(COMPONENT_FSAL, "pattr=%p, pacl=%p, is_ace4_mask=%d, access_type=%x",
           p_object_attributes, p_object_attributes ? p_object_attributes->acl : 0,
           IS_FSAL_ACE4_MASK_VALID(access_type),
           access_type);

  if(p_object_attributes && p_object_attributes->acl &&
     IS_FSAL_ACE4_MASK_VALID(access_type))
    {
      return fsal_check_access_acl(p_context, FSAL_ACE4_MASK(access_type),
                                   p_object_attributes);
    }
#endif

  /* Use mode to check access. */
  return fsal_check_access_no_acl(p_context, FSAL_MODE_MASK(access_type),
                                           p_buffstat, p_object_attributes);

  LogDebug(COMPONENT_FSAL, "fsal_check_access: invalid access_type = 0X%x",
           access_type);

  ReturnCode(ERR_FSAL_ACCESS, 0);
}

uid_t   ganesha_uid;
gid_t   ganesha_gid;
int     ganehsa_ngroups;
gid_t * ganesha_groups = NULL;

void fsal_set_credentials(fsal_op_context_t * context)
{
  setfsuid(context->credential.user);
  setfsgid(context->credential.group);
  if(syscall(__NR_setgroups,
             context->credential.nbgroups,
             context->credential.alt_groups) != 0)
    LogFatal(COMPONENT_FSAL, "Could not set Context credentials");
}

void fsal_save_ganesha_credentials()
{
  int  i;
  char buffer[1024], *p = buffer;
  ganesha_uid = setfsuid(0);
  setfsuid(ganesha_uid);
  ganesha_gid = setfsgid(0);
  setfsgid(ganesha_gid);
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
  setfsuid(ganesha_uid);
  setfsgid(ganesha_gid);
  if(syscall(__NR_setgroups, ganehsa_ngroups, ganesha_groups) != 0)
    LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
}