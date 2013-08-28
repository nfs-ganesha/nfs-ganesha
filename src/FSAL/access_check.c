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

  LogFullDebug(COMPONENT_NFS_V4_ACL,
               "result: %d, cause: %s, flag: 0x%X, who: %d",
               result, cause, pace->flag, GET_FSAL_ACE_WHO(*pace));

  return result;
}

fsal_boolean_t fsal_check_ace_applicable(fsal_ace_t *pace,
                                         fsal_op_context_t *p_context,
                                         fsal_boolean_t is_dir,
                                         fsal_boolean_t is_owner,
                                         fsal_boolean_t is_group,
                                         fsal_boolean_t is_root)
{
  fsal_boolean_t is_applicable = FALSE;
  fsal_boolean_t is_file = !is_dir;

  /* To be applicable, the entry should not be INHERIT_ONLY. */
  if (IS_FSAL_ACE_INHERIT_ONLY(*pace))
    {
      LogFullDebug(COMPONENT_NFS_V4_ACL,
                   "Not applicable, inherit only");
      return FALSE;
    }

  /* Use GPFS internal flag to further check the entry is applicable to this
   * object type. */
  if(is_file)
    {
      if(!IS_FSAL_FILE_APPLICABLE(*pace))
        {
          LogFullDebug(COMPONENT_NFS_V4_ACL,
                       "Not applicable to file");
          return FALSE;
        }
    }
  else  /* directory */
    {
      if(!IS_FSAL_DIR_APPLICABLE(*pace))
        {
          LogFullDebug(COMPONENT_NFS_V4_ACL,
                       "Not applicable to dir");
          return FALSE;
        }
    }

  /* The user should match who value. */
  is_applicable = is_root || fsal_check_ace_matches(pace,
                                                    p_context,
                                                    is_owner,
                                                    is_group);
  if(is_applicable)
    LogFullDebug(COMPONENT_NFS_V4_ACL,
                 "Applicable, flag=0X%x",
                 pace->flag);
  else
    LogFullDebug(COMPONENT_NFS_V4_ACL,
                 "Not applicable to given user");

  return is_applicable;
}

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
                     fsal_boolean_t          is_dir)
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
                        fsal_boolean_t          is_dir)
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

  if(b_left > 0 && IS_FSAL_ACE_BIT(v4mask, FSAL_ACE4_PERM_CONTINUE))
    b_left = display_cat(dspbuf, " CONTINUE");
  
  return b_left;
}

static void fsal_print_access_by_acl(int naces, int ace_number,
	                             fsal_ace_t *pace, fsal_aceperm_t perm,
                                     unsigned int access_result,
                                     fsal_boolean_t is_dir,
                                     fsal_op_context_t *p_context)
{
  char                  str[LOG_BUFF_LEN];
  struct display_buffer dspbuf = {sizeof(str), str, str};
  int                   b_left;

  if(!isDebug(COMPONENT_NFS_V4_ACL))
    return;

  if((access_result == ERR_FSAL_NO_ERROR))
    b_left = display_cat(&dspbuf, "access granted");
  else
    b_left = display_cat(&dspbuf, "access denied");

  if(b_left > 0)
    b_left = display_printf(&dspbuf, " uid %d gid %d Access req:",
                            p_context->credential.user,
                            p_context->credential.group);

  if(b_left > 0)
    b_left = display_fsal_v4mask(&dspbuf, perm, is_dir);

  if(b_left > 0 && (naces != ace_number))
    b_left = display_fsal_ace(&dspbuf, ace_number, pace, is_dir);

  LogDebug(COMPONENT_NFS_V4_ACL, "%s", str);
}

fsal_status_t fsal_check_access_acl(fsal_op_context_t  * p_context,   /* IN */
                                    fsal_accessflags_t   v4mask,  /* IN */
                                    fsal_accessflags_t * allowed,  /* OUT */
                                    fsal_accessflags_t * denied,  /* OUT */
                                    fsal_attrib_list_t * p_object_attributes   /* IN */ )
{
  fsal_aceperm_t missing_access;
  fsal_uid_t uid;
  fsal_gid_t gid;
  fsal_acl_t *pacl = NULL;
  fsal_ace_t *pace = NULL;
  fsal_aceperm_t tperm;
  int ace_number = 0;
  fsal_boolean_t is_dir = FALSE;
  fsal_boolean_t is_owner = FALSE;
  fsal_boolean_t is_group = FALSE;
  fsal_boolean_t is_root = FALSE;

  if(allowed != NULL)
    *allowed = 0;

  if(denied != NULL)
    *denied = 0;

  /* unsatisfied flags */
  missing_access = v4mask & ~FSAL_ACE4_PERM_CONTINUE;
  if(!missing_access)
    {
      LogDebug(COMPONENT_NFS_V4_ACL,
               "Nothing was requested");
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
          if(allowed != NULL)
            *allowed = v4mask;

          /* On a directory, allow root anything. */
          LogDebug(COMPONENT_NFS_V4_ACL,
                   "Met root privileges on directory");
          ReturnCode(ERR_FSAL_NO_ERROR, 0);
        }

      /* Otherwise, allow root anything but execute. */
      missing_access &= FSAL_ACE_PERM_EXECUTE;

      if(allowed != NULL)
        *allowed = v4mask & ~FSAL_ACE_PERM_EXECUTE;

      if(!missing_access)
        {
          LogDebug(COMPONENT_NFS_V4_ACL,
                   "Met root privileges");
          ReturnCode(ERR_FSAL_NO_ERROR, 0);
        }
    }

  LogFullDebug(COMPONENT_NFS_V4_ACL,
               "file acl=%p, file uid=%d, file gid= %d",
               pacl,uid, gid);

  if(isDebug(COMPONENT_NFS_V4_ACL))
    {
      char                  str[LOG_BUFF_LEN];
      struct display_buffer dspbuf = {sizeof(str), str, str};

      (void) display_fsal_v4mask(&dspbuf,
                                 v4mask,
                                 p_object_attributes->type == FSAL_TYPE_DIR);

      LogDebug(COMPONENT_NFS_V4_ACL,
               "user uid=%d, user gid= %d, v4mask=%s",
               p_context->credential.user,
               p_context->credential.group,
               str);
    }

  is_owner = fsal_check_ace_owner(uid, p_context);
  is_group = fsal_check_ace_group(gid, p_context);

  /* Always grant READ_ACL, WRITE_ACL and READ_ATTR, WRITE_ATTR to the file
   * owner. */
  if(is_owner)
    {
      if(allowed != NULL) 
        *allowed |= (v4mask & (FSAL_ACE_PERM_WRITE_ACL |
                             FSAL_ACE_PERM_READ_ACL |
                             FSAL_ACE_PERM_WRITE_ATTR |
                             FSAL_ACE_PERM_READ_ATTR));

      missing_access &= ~(FSAL_ACE_PERM_WRITE_ACL | FSAL_ACE_PERM_READ_ACL);
      missing_access &= ~(FSAL_ACE_PERM_WRITE_ATTR | FSAL_ACE_PERM_READ_ATTR);
      if(!missing_access)
        {
          LogDebug(COMPONENT_NFS_V4_ACL,
                   "Met owner privileges");
          ReturnCode(ERR_FSAL_NO_ERROR, 0);
        }
    }

  // TODO: Even if user is admin, audit/alarm checks should be done.

  for(pace = pacl->aces; pace < pacl->aces + pacl->naces; pace++)
    {
      ace_number += 1;

      LogFullDebug(COMPONENT_NFS_V4_ACL,
                   "ace type 0x%X perm 0x%X flag 0x%X who %d",
                   pace->type, pace->perm, pace->flag, GET_FSAL_ACE_WHO(*pace));

      /* Process Allow and Deny entries. */
      if(IS_FSAL_ACE_ALLOW(*pace) || IS_FSAL_ACE_DENY(*pace))
        {
          /* Check if this ACE is applicable. */
          if(fsal_check_ace_applicable(pace, p_context, is_dir, is_owner, is_group, is_root))
            {
              if(IS_FSAL_ACE_ALLOW(*pace))
                {
                  tperm = pace->perm;

                  /* Do not set bits which are already denied */
                  if(denied)
                    tperm &= ~*denied;

                  LogFullDebug(COMPONENT_NFS_V4_ACL,
                               "allow perm 0x%X remainingPerms 0x%X",
                               tperm,
                               missing_access);

                  if(allowed != NULL)
                    *allowed |= v4mask & tperm;

                  missing_access &= ~(tperm & missing_access);

                  if(!missing_access)
                    {
                      fsal_print_access_by_acl(pacl->naces,
                                               ace_number,
                                               pace,
                                               v4mask,
                                               ERR_FSAL_NO_ERROR,
                                               is_dir,
                                               p_context);
                      break;
                    }
                }
             else if((pace->perm & missing_access) && !is_root)
               {
                 fsal_print_access_by_acl(pacl->naces,
                                          ace_number,
                                          pace,
                                          v4mask,
                                          (pace->perm & missing_access & 
                                           (FSAL_ACE_PERM_WRITE_ATTR |
                                            FSAL_ACE_PERM_WRITE_ACL |
                                            FSAL_ACE_PERM_WRITE_OWNER)) != 0 ?
                                          ERR_FSAL_PERM : ERR_FSAL_ACCESS,
                                          is_dir,
                                          p_context);

                 if(denied != NULL)
                   *denied |= v4mask & pace->perm;
                 if(denied == NULL ||
                    (v4mask & FSAL_ACE4_PERM_CONTINUE) == 0)
                   {
                     if((pace->perm & missing_access &
                         (FSAL_ACE_PERM_WRITE_ATTR |
                          FSAL_ACE_PERM_WRITE_ACL |
                          FSAL_ACE_PERM_WRITE_OWNER)) != 0)
                       {
                         LogDebug(COMPONENT_NFS_V4_ACL,
                                  "access denied (EPERM)");
                         ReturnCode(ERR_FSAL_PERM, 0);
                       }
                     else
                       {
                         LogDebug(COMPONENT_NFS_V4_ACL,
                                  "access denied (EACCESS)");
                         ReturnCode(ERR_FSAL_ACCESS, 0);
                       }
                   }

                 missing_access &= ~(pace->perm & missing_access);

                 /* If this DENY ACE blocked the last remaining requested access
                  * bits, break out of the loop because we're done and don't
                  * want to evaluate any more ACEs.
                  */
                 if(!missing_access)
                   break;
               }
            }
        }
    }

  if(missing_access || (denied != NULL && *denied != 0))
    {
      if((missing_access & (FSAL_ACE_PERM_WRITE_ATTR |
                            FSAL_ACE_PERM_WRITE_ACL |
                            FSAL_ACE_PERM_WRITE_OWNER)) != 0)
        {
          LogDebug(COMPONENT_NFS_V4_ACL,
                   "access denied (EPERM)");
          ReturnCode(ERR_FSAL_PERM, 0);
        }
      else
        {
          LogDebug(COMPONENT_NFS_V4_ACL,
                   "access denied (EACCESS)");
          ReturnCode(ERR_FSAL_ACCESS, 0);
        }
    }
  else
    {
      LogDebug(COMPONENT_NFS_V4_ACL,
               "access granted");
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }
}
#endif                          /* _USE_NFS4_ACL */

fsal_status_t fsal_check_access_no_acl(fsal_op_context_t * p_context,   /* IN */
                                       fsal_accessflags_t access_type,  /* IN */
                                       fsal_accessflags_t * allowed,  /* OUT */
                                       fsal_accessflags_t * denied,  /* OUT */
                                       struct stat *p_buffstat, /* IN */
                                       fsal_attrib_list_t * p_object_attributes /* IN */ )
{
  fsal_accessflags_t missing_access;
  unsigned int is_grp, i;
  fsal_uid_t uid;
  fsal_gid_t gid;
  fsal_accessmode_t mode;

  if(allowed != NULL)
    *allowed = 0;

  if(denied != NULL)
    *denied = 0;

  /* unsatisfied flags */
  missing_access = access_type;
  if(!missing_access)
    {
      LogDebug(COMPONENT_NFS_V4_ACL,
               "Nothing was requested");
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

  LogDebug(COMPONENT_NFS_V4_ACL,
           "file Mode=%#o, file uid=%d, file gid= %d",
           mode,uid, gid);
#ifdef _USE_HPSS
  LogDebug(COMPONENT_FSAL, COMPONENT_NFS_V4_ACL,
           "user uid=%d, user gid= %d, access_type=0X%x",
#if HPSS_MAJOR_VERSION == 5
           p_context->credential.hpss_usercred.SecPWent.Uid,
           p_context->credential.hpss_usercred.SecPWent.Gid,
#else
           p_context->credential.hpss_usercred.Uid,
           p_context->credential.hpss_usercred.Gid,
#endif
           access_type);
#else
  LogDebug(COMPONENT_NFS_V4_ACL,
           "user uid=%d, user gid= %d, access_type=0X%x",
           p_context->credential.user,
           p_context->credential.group,
           access_type);
#endif

#ifdef _USE_HPSS
#if HPSS_MAJOR_VERSION == 5
  if(p_context->credential.hpss_usercred.SecPWent.Uid == 0)
#else
  if(p_context->credential.hpss_usercred.Uid == 0)
#endif
#else
  if(p_context->credential.user == 0)
#endif
    {
      /* Always grant read/write access to root */
      missing_access &= ~(FSAL_R_OK | FSAL_W_OK);
      if(allowed != NULL)
        *allowed |= (FSAL_R_OK | FSAL_W_OK) & access_type;

      /* Grant execute permission for root if ANYONE has execute permission */
      if(mode & (FSAL_MODE_XUSR | FSAL_MODE_XGRP | FSAL_MODE_XOTH)) {
        missing_access &= ~FSAL_X_OK;
        if(allowed != NULL)
          *allowed |= FSAL_X_OK & access_type;
      }
    }

  /* If the uid of the file matches the uid of the user,
   * then the uid mode bits take precedence. */
#ifdef _USE_HPSS
#if HPSS_MAJOR_VERSION == 5
  if(p_context->credential.hpss_usercred.SecPWent.Uid == uid)
#else
  if(p_context->credential.hpss_usercred.Uid == uid)
#endif
#else
  if(p_context->credential.user == uid)
#endif
    {

      LogDebug(COMPONENT_NFS_V4_ACL,
               "File belongs to user %d", uid);

      if(mode & FSAL_MODE_RUSR) {
        missing_access &= ~FSAL_R_OK;
        if(allowed != NULL)
          *allowed |= FSAL_R_OK & access_type;
      }

      if(mode & FSAL_MODE_WUSR) {
        missing_access &= ~FSAL_W_OK;
        if(allowed != NULL)
          *allowed |= FSAL_W_OK & access_type;
      }

      if(mode & FSAL_MODE_XUSR) {
        missing_access &= ~FSAL_X_OK;
        if(allowed != NULL)
          *allowed |= FSAL_X_OK & access_type;
      }

      if(missing_access == 0)
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
      else
        {
          if(denied != NULL)
            *denied = missing_access;

          LogDebug(COMPONENT_NFS_V4_ACL,
                   "Mode=%#o, Access=0X%x, Rights missing: 0X%x",
                   mode, access_type, missing_access);
          ReturnCode(ERR_FSAL_ACCESS, 0);
        }

    }

  /* Test if the file belongs to user's group. */
#ifdef _USE_HPSS
#if HPSS_MAJOR_VERSION == 5
  is_grp = (p_context->credential.hpss_usercred.SecPWent.Gid == gid);
#else
  is_grp = (p_context->credential.hpss_usercred.Gid == gid);
#endif  
  if(is_grp)
    LogDebug(COMPONENT_NFS_V4_ACL,
             "File belongs to user's group %d",
#if HPSS_MAJOR_VERSION == 5
             p_context->credential.hpss_usercred.SecPWent.Gid);
#else
             p_context->credential.hpss_usercred.Gid);
#endif                 

  /* Test if file belongs to alt user's groups */
  if(!is_grp)
    for(i = 0; i < p_context->credential.hpss_usercred.NumGroups; i++)
      {
        is_grp = (p_context->credential.hpss_usercred.AltGroups[i] == gid);
        if(is_grp)
          LogDebug(COMPONENT_NFS_V4_ACL,
                   "File belongs to user's alt group %d",
                   p_context->credential.hpss_usercred.AltGroups[i]);
        if(is_grp)
          break;
      }
#else
  is_grp = (p_context->credential.group == gid);
  if(is_grp)
    LogDebug(COMPONENT_NFS_V4_ACL,
             "File belongs to user's group %d",
             p_context->credential.group);

  /* Test if file belongs to alt user's groups */
  if(!is_grp)
    for(i = 0; i < p_context->credential.nbgroups; i++)
      {
        is_grp = (p_context->credential.alt_groups[i] == gid);
        if(is_grp)
          LogDebug(COMPONENT_NFS_V4_ACL,
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
      if(mode & FSAL_MODE_RGRP) {
        missing_access &= ~FSAL_R_OK;
        if(allowed != NULL)
          *allowed |= FSAL_R_OK & access_type;
      }

      if(mode & FSAL_MODE_WGRP) {
        missing_access &= ~FSAL_W_OK;
        if(allowed != NULL)
          *allowed |= FSAL_W_OK & access_type;
      }

      if(mode & FSAL_MODE_XGRP) {
        missing_access &= ~FSAL_X_OK;
        if(allowed != NULL)
          *allowed |= FSAL_X_OK & access_type;
      }

      if(missing_access == 0)
        ReturnCode(ERR_FSAL_NO_ERROR, 0);
      else {
        if(denied != NULL)
          *denied = missing_access;

        ReturnCode(ERR_FSAL_ACCESS, 0);
      }

    }

  /* If the user uid is not 0, the uid does not match the file's, and
   * the user's gids do not match the file's gid, we apply the "other"
   * mode bits to the user. */
  if(mode & FSAL_MODE_ROTH) {
      missing_access &= ~FSAL_R_OK;
      if(allowed != NULL)
        *allowed |= FSAL_R_OK & access_type;
    }

  if(mode & FSAL_MODE_WOTH) {
      missing_access &= ~FSAL_W_OK;
      if(allowed != NULL)
        *allowed |= FSAL_W_OK & access_type;
    }

  if(mode & FSAL_MODE_XOTH) {
      missing_access &= ~FSAL_X_OK;
      if(allowed != NULL)
        *allowed |= FSAL_X_OK & access_type;
    }

  if(missing_access == 0)
    ReturnCode(ERR_FSAL_NO_ERROR, 0);
  else {
    if(denied != NULL)
      *denied = missing_access;

    LogDebug(COMPONENT_NFS_V4_ACL,
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
                                fsal_accessflags_t * allowed,  /* OUT */
                                fsal_accessflags_t * denied,  /* OUT */
                                struct stat *p_buffstat, /* IN */
                                fsal_attrib_list_t * p_object_attributes /* IN */ )
{
  /* sanity checks. */
  if((!p_object_attributes && !p_buffstat) || !p_context)
    ReturnCode(ERR_FSAL_FAULT, 0);

#ifdef _USE_NFS4_ACL
  /* If ACL exists and given access type is ace4 mask, use ACL to check access. */
  LogDebug(COMPONENT_NFS_V4_ACL,
           "pattr=%p, pacl=%p, is_ace4_mask=%d, access_type=%x",
           p_object_attributes, p_object_attributes ? p_object_attributes->acl : 0,
           IS_FSAL_ACE4_MASK_VALID(access_type),
           access_type);

  if(p_object_attributes && p_object_attributes->acl &&
     IS_FSAL_ACE4_MASK_VALID(access_type))
    {
      return fsal_check_access_acl(p_context, FSAL_ACE4_MASK(access_type),
                                   allowed, denied,
                                   p_object_attributes);
    }
#endif

  /* Use mode to check access. */
  return fsal_check_access_no_acl(p_context, FSAL_MODE_MASK(access_type),
                                  allowed, denied,
                                  p_buffstat, p_object_attributes);

  LogDebug(COMPONENT_NFS_V4_ACL,
           "fsal_check_access: invalid access_type = 0X%x",
           access_type);

  ReturnCode(ERR_FSAL_ACCESS, 0);
}

uid_t   ganesha_uid;
gid_t   ganesha_gid;
int     ganesha_ngroups;
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
  int                   i;
  int                   b_left;
  char                  buffer[1024];
  struct display_buffer dspbuf = {sizeof(buffer), buffer, buffer};

  ganesha_uid = setfsuid(0);
  setfsuid(ganesha_uid);
  ganesha_gid = setfsgid(0);
  setfsgid(ganesha_gid);
  ganesha_ngroups = getgroups(0, NULL);
  if(ganesha_ngroups != 0)
    {
      ganesha_groups = gsh_malloc(ganesha_ngroups * sizeof(gid_t));
      if(ganesha_groups == NULL)
        {
          LogFatal(COMPONENT_FSAL,
                   "Could not allocate memory for Ganesha group list");
        }
      if(getgroups(ganesha_ngroups, ganesha_groups) != ganesha_ngroups)
        {
          LogFatal(COMPONENT_FSAL,
                   "Could not get list of ganesha groups");
        }
    }

  b_left = display_printf(&dspbuf,
                          "Ganesha uid=%d gid=%d ngroups=%d",
                          (int) ganesha_uid,
                          (int) ganesha_gid,
                          ganesha_ngroups);

  if(ganesha_ngroups != 0 && b_left > 0)
    b_left = display_cat(&dspbuf, " (");

  for(i = 0; i < ganesha_ngroups && b_left > 0; i++)
    {
      if(i == 0)
        b_left = display_printf(&dspbuf, "%d", (int) ganesha_groups[i]);
      else
        b_left = display_printf(&dspbuf, " %d", (int) ganesha_groups[i]);
    }

  if(ganesha_ngroups != 0 && b_left > 0)
    b_left = display_cat(&dspbuf, ")");

  LogInfo(COMPONENT_FSAL,
          "%s", buffer);
}

void fsal_restore_ganesha_credentials()
{
  setfsuid(ganesha_uid);
  setfsgid(ganesha_gid);
  if(syscall(__NR_setgroups, ganesha_ngroups, ganesha_groups) != 0)
    LogFatal(COMPONENT_FSAL, "Could not set Ganesha credentials");
}
