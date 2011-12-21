/*
 * file/object access checking
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include  "fsal.h"
#include "FSAL/access_check.h"



#ifdef _USE_NFS4_ACL
static fsal_status_t fsal_check_access_acl(fsal_op_context_t * p_context,   /* IN */
                                                  fsal_aceperm_t v4mask,  /* IN */
                                                  fsal_attrib_list_t * p_object_attributes   /* IN */ );
#endif                          /* _USE_NFS4_ACL */

static fsal_status_t fsal_check_access_no_acl(fsal_op_context_t * p_context,   /* IN */
                                                     fsal_accessflags_t access_type,  /* IN */
                                                     struct stat *p_buffstat, /* IN */
                                                     fsal_attrib_list_t * p_object_attributes /* IN */ );


#ifdef _USE_NFS4_ACL
static fsal_boolean_t fsal_check_ace_owner(fsal_uid_t uid, fsal_op_context_t *p_context)
{
  return (p_context->credential.user == uid);
}

static fsal_boolean_t fsal_check_ace_group(fsal_gid_t gid, fsal_op_context_t *p_context)
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

static fsal_boolean_t fsal_check_ace_matches(fsal_ace_t *pace,
                                             fsal_op_context_t *p_context,
                                             fsal_boolean_t is_owner,
                                             fsal_boolean_t is_group)
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
      if(fsal_check_ace_group(pace->who.gid, p_context))
        matches = 4;
    }
  else
    {
      if(fsal_check_ace_owner(pace->who.uid, p_context))
        matches = 5;
    }

  LogDebug(COMPONENT_FSAL,
           "fsal_check_ace_matches: matches %d flag 0x%X who %d",
           matches, pace->flag, GET_FSAL_ACE_WHO(*pace));

  return (matches != 0);
}

static fsal_boolean_t fsal_check_ace_applicable(fsal_ace_t *pace,
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
  is_applicable = fsal_check_ace_matches(pace, p_context, is_owner, is_group);
  if(is_applicable)
    LogDebug(COMPONENT_FSAL, "fsal_check_ace_applicable: Applicable, flag=0X%x",
             pace->flag);
  else
    LogDebug(COMPONENT_FSAL, "fsal_check_ace_applicable: Not applicable to given user");

  return is_applicable;
}

static void fsal_print_inherit_flags(fsal_ace_t *pace, char *p_buf)
{
  if(!pace || !p_buf)
    return;

  memset(p_buf, 0, ACL_DEBUG_BUF_SIZE);

  sprintf(p_buf, "I(%c%c%c%c)",
          IS_FSAL_ACE_FILE_INHERIT(*pace)? 'f': '-',
          IS_FSAL_ACE_DIR_INHERIT(*pace) ? 'd': '-',
          IS_FSAL_ACE_INHERIT_ONLY(*pace)? 'o': '-',
          IS_FSAL_ACE_NO_PROPAGATE(*pace)? 'n': '-');
}

static void fsal_print_ace(int ace_number, fsal_ace_t *pace, char *p_acebuf)
{
  char inherit_flags[ACL_DEBUG_BUF_SIZE];

  if(!pace || !p_acebuf)
    return;

  memset(p_acebuf, 0, ACL_DEBUG_BUF_SIZE);
  memset(inherit_flags, 0, ACL_DEBUG_BUF_SIZE);

  /* Get inherit flags if any. */
  fsal_print_inherit_flags(pace, inherit_flags);

  /* Print the entire ACE. */
  sprintf(p_acebuf, "ACE %d %s %s %d %c%c%c%c%c%c%c%c%c%c%c%c%c%c %s",
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

  LogDebug(COMPONENT_FSAL, "fsal_check_access_by_acl_debug: %s", access_data);
}

static fsal_status_t fsal_check_access_acl(fsal_op_context_t * p_context,   /* IN */
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

  /* unsatisfied flags */
  missing_access = v4mask;
  if(!missing_access)
    {
      LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: Nothing was requested");
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  /* Get file ownership information. */
  uid = p_object_attributes->owner;
  gid = p_object_attributes->group;
  pacl = p_object_attributes->acl;
  is_dir = (p_object_attributes->type == FSAL_TYPE_DIR);

  LogDebug(COMPONENT_FSAL,
           "fsal_check_access_acl: file acl=%p, file uid=%d, file gid= %d",
           pacl,uid, gid);
  LogDebug(COMPONENT_FSAL,
           "fsal_check_access_acl: user uid=%d, user gid= %d, v4mask=0x%X",
           p_context->credential.user,
           p_context->credential.group,
           v4mask);

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
          LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: Met owner privileges");
          ReturnCode(ERR_FSAL_NO_ERROR, 0);
        }
    }

  // TODO: Even if user is admin, audit/alarm checks should be done.

  ace_number = 1;
  for(pace = pacl->aces; pace < pacl->aces + pacl->naces; pace++)
    {
      LogDebug(COMPONENT_FSAL,
               "fsal_check_access_acl: ace type 0x%X perm 0x%X flag 0x%X who %d",
               pace->type, pace->perm, pace->flag, GET_FSAL_ACE_WHO(*pace));

      /* Process Allow and Deny entries. */
      if(IS_FSAL_ACE_ALLOW(*pace) || IS_FSAL_ACE_DENY(*pace))
        {
          LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: allow or deny");

          /* Check if this ACE is applicable. */
          if(fsal_check_ace_applicable(pace, p_context, is_dir, is_owner, is_group))
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
                                                     v4mask, ERR_FSAL_NO_ERROR, is_dir, p_context);
                      ReturnCode(ERR_FSAL_NO_ERROR, 0);
                    }
                }
             else if(pace->perm & missing_access)
               {
                 LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: access denied");
                 fsal_print_access_by_acl(pacl->naces, ace_number, pace, v4mask,
                                                ERR_FSAL_ACCESS, is_dir, p_context);
                 ReturnCode(ERR_FSAL_ACCESS, 0);
               }
            }
        }

        ace_number += 1;
    }

  if(missing_access)
    {
      LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: access denied");
      ReturnCode(ERR_FSAL_ACCESS, 0);
    }
  else
    {
      LogDebug(COMPONENT_FSAL, "fsal_check_access_acl: access granted");
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
#endif                          /* _USE_NFS4_ACL */

static fsal_status_t fsal_check_access_no_acl(fsal_op_context_t * p_context,   /* IN */
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
      LogDebug(COMPONENT_FSAL, "fsal_check_access_no_acl: Nothing was requested");
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
               "fsal_check_access_no_acl: file Mode=%#o, file uid=%d, file gid= %d",
               mode,uid, gid);
  LogDebug(COMPONENT_FSAL,
               "fsal_check_access_no_acl: user uid=%d, user gid= %d, access_type=0X%x",
               p_context->credential.user,
               p_context->credential.group,
               access_type);

  /* If the uid of the file matches the uid of the user,
   * then the uid mode bits take precedence. */
  if(p_context->credential.user == uid)
    {

      LogDebug(COMPONENT_FSAL,
                   "fsal_check_access_no_acl: File belongs to user %d", uid);

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
                       "fsal_check_access_no_acl: Mode=%#o, Access=0X%x, Rights missing: 0X%x",
                       mode, access_type, missing_access);
          ReturnCode(ERR_FSAL_ACCESS, 0);
        }

    }

  /* missing_access will be nonzero triggering a failure
   * even though FSAL_OWNER_OK is not even a real posix file
   * permission */
  missing_access &= ~FSAL_OWNER_OK;

  /* Test if the file belongs to user's group. */
  is_grp = (p_context->credential.group == gid);
  if(is_grp)
    LogDebug(COMPONENT_FSAL,
                 "fsal_check_access_no_acl: File belongs to user's group %d",
                 p_context->credential.group);

  /* Test if file belongs to alt user's groups */
  if(!is_grp)
    for(i = 0; i < p_context->credential.nbgroups; i++)
      {
        is_grp = (p_context->credential.alt_groups[i] == gid);
        if(is_grp)
          LogDebug(COMPONENT_FSAL,
                       "fsal_check_access_no_acl: File belongs to user's alt group %d",
                       p_context->credential.alt_groups[i]);
        if(is_grp)
          break;
      }

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
                 "fsal_check_access_no_acl: Mode=%#o, Access=0X%x, Rights missing: 0X%x",
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

  /* The root user ignores the mode/uid/gid of the file */
  if(p_context->credential.user == 0)
    ReturnCode(ERR_FSAL_NO_ERROR, 0);

#ifdef _USE_NFS4_ACL
  /* If ACL exists and given access type is ace4 mask, use ACL to check access. */
  LogDebug(COMPONENT_FSAL, "fsal_check_access: pattr=%p, pacl=%p, is_ace4_mask=%d",
           p_object_attributes, p_object_attributes ? p_object_attributes->acl : 0,
           IS_FSAL_ACE4_MASK_VALID(access_type));

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
