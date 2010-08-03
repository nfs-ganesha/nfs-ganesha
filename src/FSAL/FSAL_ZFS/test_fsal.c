/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    test_fsal.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/09 12:40:52 $
 * \version $Revision: 1.25 $
 * \brief   Program for testing fsal.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "log_functions.h"
#include <unistd.h>             /* for using gethostname */
#include <stdlib.h>             /* for using exit */
#include <string.h>
#include <sys/types.h>
#include "BuddyMalloc.h"

#define READDIR_SIZE 5

void printmask(fsal_attrib_mask_t mask)
{

  if(FSAL_TEST_MASK(mask, FSAL_ATTR_SUPPATTR))
    printf("FSAL_ATTR_SUPPATTR\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_TYPE))
    printf("FSAL_ATTR_TYPE\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_SIZE))
    printf("FSAL_ATTR_SIZE\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_FSID))
    printf("FSAL_ATTR_FSID\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_ACL))
    printf("FSAL_ATTR_ACL \n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_FILEID))
    printf("FSAL_ATTR_FILEID\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_MODE))
    printf("FSAL_ATTR_MODE\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_NUMLINKS))
    printf("FSAL_ATTR_NUMLINKS\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_OWNER))
    printf("FSAL_ATTR_OWNER\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_GROUP))
    printf("FSAL_ATTR_GROUP\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_RAWDEV))
    printf("FSAL_ATTR_RAWDEV\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_ATIME))
    printf("FSAL_ATTR_ATIME\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_CREATION))
    printf("FSAL_ATTR_CREATION\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_CTIME))
    printf("FSAL_ATTR_CTIME\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_CHGTIME))
    printf("FSAL_ATTR_CHGTIME\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_MTIME))
    printf("FSAL_ATTR_MTIME\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_SPACEUSED))
    printf("FSAL_ATTR_SPACEUSED\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_MOUNTFILEID))
    printf("FSAL_ATTR_MOUNTFILEID\n");

}

char *strtype(fsal_nodetype_t type)
{
  switch (type)
    {
    case FSAL_TYPE_FIFO:
      return "FSAL_TYPE_FIFO ";
    case FSAL_TYPE_CHR:
      return "FSAL_TYPE_CHR  ";
    case FSAL_TYPE_DIR:
      return "FSAL_TYPE_DIR  ";
    case FSAL_TYPE_BLK:
      return "FSAL_TYPE_BLK  ";
    case FSAL_TYPE_FILE:
      return "FSAL_TYPE_FILE ";
    case FSAL_TYPE_LNK:
      return "FSAL_TYPE_LNK  ";
    case FSAL_TYPE_JUNCTION:
      return "FSAL_TYPE_JUNCTION";
    case 0:
      return "(null)         ";

    default:
      return "Unknown type";
    }
}

void printattributes(fsal_attrib_list_t attrs)
{

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_RDATTR_ERR))
    printf("FSAL_ATTR_RDATTR_ERR\n");

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_TYPE))
    printf("Type : %s\n", strtype(attrs.type));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_SIZE))
    printf("Size : %llu\n", attrs.filesize);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_FSID))
    printf("fsId : %llu.%llu\n", attrs.fsid.major, attrs.fsid.minor);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ACL))
    printf("ACL List ...\n");
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_FILEID))
    printf("FileId : %llu\n", attrs.fileid);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    printf("Mode : %#o\n", attrs.mode);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_NUMLINKS))
    printf("Numlinks : %u\n", (unsigned int)attrs.numlinks);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER))
    printf("uid : %d\n", attrs.owner);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_GROUP))
    printf("gid : %d\n", attrs.group);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_RAWDEV))
    printf("Rawdev ...\n");
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME))
    printf("atime : %s", ctime((time_t *) & attrs.atime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_CREATION))
    printf("creation time : %s", ctime((time_t *) & attrs.creation.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_CTIME))
    printf("ctime : %s", ctime((time_t *) & attrs.ctime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME))
    printf("mtime : %s", ctime((time_t *) & attrs.mtime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_CHGTIME))
    printf("chgtime : %s", ctime((time_t *) & attrs.chgtime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_SPACEUSED))
    printf("spaceused : %llu\n", attrs.spaceused);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MOUNTFILEID))
    printf("mounted_on_fileid : %llu\n", attrs.mounted_on_fileid);

}

void usage()
{
  fprintf(stderr, "Usage :\n\ttest_fsal <no_test>\n");
  fprintf(stderr, "\ttests :\n");
  fprintf(stderr, "\t\t1 - getattrs\n");
  fprintf(stderr, "\t\t2 - lookup\n");
  fprintf(stderr, "\t\t3 - lookupPath\n");
  fprintf(stderr, "\t\t4 - readdir (acces par tableau)\n");
  fprintf(stderr, "\t\t5 - readdir (acces liste chainee)\n");
  fprintf(stderr, "\t\t6 - access/test_access\n");
  fprintf(stderr, "\t\t7 - snprintmem/sscanmem\n");
  fprintf(stderr, "\t\t8 - mkdir/rmdir\n");
  fprintf(stderr, "\t\t9 - setattr\n");
  fprintf(stderr, "\t\tA - digest/expend handle\n");
  fprintf(stderr, "\t\tB - dynamic fs info\n");
  return;
}

int main(int argc, char **argv)
{

  char localmachine[256];
  char *test;
  fsal_parameter_t init_param;
  fsal_status_t st;
  log_t log_desc = LOG_INITIALIZER;
  desc_log_stream_t voie;
  uid_t uid;
  fsal_export_context_t export_ctx;
  fsal_op_context_t op_ctx;
  fsal_handle_t root_handle, handle;
  fsal_name_t name;
  fsal_path_t path;
  fsal_attrib_list_t attribs;
  fsal_attrib_mask_t mask;

  char tracebuff[256];

  if(argc < 2)
    {
      usage();
      exit(-1);
    }
  test = argv[1];
  /* retrieving params */

#ifndef _NO_BUDDY_SYSTEM
  BuddyInit(NULL);
#endif

  /* init debug */

  SetNamePgm("test_fsal");
  SetNameFileLog("/dev/tty");
  SetNameFunction("main");

  /* Obtention du nom de la machine */
  if(gethostname(localmachine, sizeof(localmachine)) != 0)
    {
      DisplayErrorLog(ERR_SYS, ERR_GETHOSTNAME, errno);
      exit(1);
    }
  else
    SetNameHost(localmachine);

  InitDebug(NIV_FULL_DEBUG);

  AddFamilyError(ERR_FSAL, "FSAL related Errors", tab_errstatus_FSAL);

  /* creating log */
  voie.fd = fileno(stderr);
  AddLogStreamJd(&log_desc, V_FD, voie, NIV_FULL_DEBUG, SUP);

  /* prepare fsal_init */

  /* 1 - fs specific info */

#ifdef _USE_HPSS_51

  init_param.fs_specific_info.behaviors.PrincipalName = FSAL_INIT_FORCE_VALUE;
  strcpy(init_param.fs_specific_info.hpss_config.PrincipalName, "hpss_nfs");

  init_param.fs_specific_info.behaviors.KeytabPath = FSAL_INIT_FORCE_VALUE;
  strcpy(init_param.fs_specific_info.hpss_config.KeytabPath, "/krb5/hpssserver.keytab");

#elif defined _USE_HPSS_62
  init_param.fs_specific_info.behaviors.AuthnMech = FSAL_INIT_FORCE_VALUE;
  init_param.fs_specific_info.hpss_config.AuthnMech = hpss_authn_mech_krb5;

  init_param.fs_specific_info.behaviors.Principal = FSAL_INIT_FORCE_VALUE;
  strcpy(init_param.fs_specific_info.Principal, "hpssfs");

  init_param.fs_specific_info.behaviors.KeytabPath = FSAL_INIT_FORCE_VALUE;
  strcpy(init_param.fs_specific_info.KeytabPath, "/var/hpss/etc/hpss.keytab");

#endif

  /* 2-common info (default) */
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, maxfilesize);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, maxlink);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, maxnamelen);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, maxpathlen);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, no_trunc);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, chown_restricted);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, case_insensitive);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, case_preserving);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, fh_expire_type);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, link_support);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, symlink_support);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, named_attr);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, unique_handles);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, lease_time);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, acl_support);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, cansettime);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, homogenous);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, supported_attrs);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, maxread);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, maxwrite);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, umask);
  FSAL_SET_INIT_DEFAULT(init_param.fs_common_info, auth_exportpath_xdev);

  /* 3- fsal info */
  init_param.fsal_info.log_outputs = log_desc;
  init_param.fsal_info.max_fs_calls = 0;

  /* Init */
  if(FSAL_IS_ERROR(st = FSAL_Init(&init_param)))
    {
      DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
    }

  /* getting creds */
  uid = getuid();
  printf("uid = %d\n", uid);

  st = FSAL_BuildExportContext(&export_ctx, NULL, NULL);
  if(FSAL_IS_ERROR(st))
    DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);

  st = FSAL_InitClientContext(&op_ctx);

  if(FSAL_IS_ERROR(st))
    DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);

  st = FSAL_GetClientContext(&op_ctx, &export_ctx, uid, -1, NULL, 0);

  if(FSAL_IS_ERROR(st))
    DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);

  /* getting root handle */

  if(FSAL_IS_ERROR(st = FSAL_lookup(NULL, NULL, &op_ctx, &root_handle, NULL)))
    {
      DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
    }

  snprintHandle(tracebuff, 256, &root_handle);
  printf("Root handle = %s\n", tracebuff);

  /* getting what are the supported attributes */

  attribs.asked_attributes = 0;
  FSAL_SET_MASK(attribs.asked_attributes, FSAL_ATTR_SUPPATTR);
  printf("asked attributes :\n");
  printmask(attribs.asked_attributes);

  if(FSAL_IS_ERROR(st = ZFSFSAL_getattrs(&root_handle, &op_ctx, &attribs)))
    {
      DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
    }

  printf("supported attributes :\n");
  printmask(attribs.supported_attributes);

  mask = attribs.supported_attributes;

/* TEST 1 */

  if(test[0] == '1')
    {

      attribs.asked_attributes = 0;
      FSAL_SET_MASK(attribs.asked_attributes, FSAL_ATTR_SUPPATTR);
      printf("asked attributes :\n");
      printmask(attribs.asked_attributes);

      if(FSAL_IS_ERROR(st = ZFSFSAL_getattrs(&root_handle, &op_ctx, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      printf("supported attributes :\n");

      /* getting all spported attributes of root */
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = ZFSFSAL_getattrs(&root_handle, &op_ctx, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      printattributes(attribs);

    }
  else
/* TEST 2 */
  if(test[0] == '2')
    {

      /* getting handle and attributes for subdirectory "OSF1_V5" */
      if(FSAL_IS_ERROR(st = FSAL_str2name("cea", 4, &name)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookup(&root_handle, &name, &op_ctx, &handle, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 256, &handle);
      printf("/cea handle = %s\n", tracebuff);

      /* displaying attributes */
      printattributes(attribs);

      /* getting handle and attributes for subdirectory "bin" */
      if(FSAL_IS_ERROR(st = FSAL_str2name("prot", 5, &name)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      root_handle = handle;
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookup(&root_handle, &name, &op_ctx, &handle, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 256, &handle);
      printf("/cea/prot handle = %s\n", tracebuff);

      /* displaying attributes */
      printattributes(attribs);

      /* getting handle and attributes for symlink "AglaePwrSW" */
      if(FSAL_IS_ERROR(st = FSAL_str2name("lama", 5, &name)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      root_handle = handle;
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookup(&root_handle, &name, &op_ctx, &handle, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 256, &handle);
      printf("/cea/prot/lama handle = %s\n", tracebuff);

      /* displaying attributes */
      printattributes(attribs);

    }
  else
/* TEST 3 */
  if(test[0] == '3')
    {

      /* lookup root */
      if(FSAL_IS_ERROR(st = FSAL_str2path("/", 30, &path)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookupPath(&path, &op_ctx, &handle, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 256, &handle);
      printf("/ handle = %s\n", tracebuff);

      /* displaying attributes */
      printattributes(attribs);

      /* lookup path */
      if(FSAL_IS_ERROR(st = FSAL_str2path("/cea/prot/lama", 15, &path)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookupPath(&path, &op_ctx, &handle, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 256, &handle);
      printf("/cea/prot/lama handle = %s\n", tracebuff);

      /* displaying attributes */
      printattributes(attribs);

    }
  else
/* TEST 4 */
  if(test[0] == '4')
    {

      /* readdir on root */
      fsal_dir_t dir;
      fsal_cookie_t from, to;
      fsal_dirent_t entries[READDIR_SIZE];
      fsal_count_t number;
      fsal_boolean_t eod = FALSE;
      int error = FALSE;

      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_opendir(&root_handle, &op_ctx, &dir, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      printf("'/' attributes :\n");

      /* displaying attributes */
      printattributes(attribs);

      from = FSAL_READDIR_FROM_BEGINNING;

      while(!error && !eod)
        {
          unsigned int i;
          char cookiebuff[256];

          snprintCookie(cookiebuff, 256, &from);
          printf("\nReaddir cookie = %s\n", cookiebuff);
          if(FSAL_IS_ERROR(st = FSAL_readdir(&dir, from,
                                             mask, READDIR_SIZE * sizeof(fsal_dirent_t),
                                             entries, &to, &number, &eod)))
            {
              DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
              error = TRUE;
            }

          for(i = 0; (!error) && (i < number); i++)
            {

              snprintHandle(tracebuff, 256, &entries[i].handle);

              snprintCookie(cookiebuff, 256, &entries[i].cookie);

              printf("\t%s : %s (cookie %s)\n", tracebuff,
                     entries[i].name.name, cookiebuff);
            }
          /* preparing next call */
          from = to;

        }
      printf("Fin de boucle : error=%d ; eod=%d\n", error, eod);

    }
  else
/* TEST 5 */
  if(test[0] == '5')
    {

      /* readdir on root */
      fsal_dir_t dir;
      fsal_cookie_t from, to;
      fsal_dirent_t entries[READDIR_SIZE];
      fsal_count_t number;
      fsal_boolean_t eod = FALSE;
      int error = FALSE;

      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_opendir(&root_handle, &op_ctx, &dir, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      printf("'/' attributes :\n");

      /* displaying attributes */
      printattributes(attribs);

      from = FSAL_READDIR_FROM_BEGINNING;

      while(!error && !eod)
        {
          fsal_dirent_t *curr;

          char cookiebuff[256];

          snprintCookie(cookiebuff, 256, &from);

          printf("\nReaddir cookie = %s\n", cookiebuff);

          if(FSAL_IS_ERROR(st = FSAL_readdir(&dir, from,
                                             mask, READDIR_SIZE * sizeof(fsal_dirent_t),
                                             entries, &to, &number, &eod)))
            {
              DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
              error = TRUE;
            }

          if(number > 0)
            {
              curr = entries;
              do
                {

                  snprintHandle(tracebuff, 256, &curr->handle);
                  snprintCookie(cookiebuff, 256, &curr->cookie);

                  printf("\t%s : %s (cookie %s)\n", tracebuff,
                         curr->name.name, cookiebuff);
                }
              while(curr = curr->nextentry);
            }
          /* preparing next call */
          from = to;

        }
      printf("Fin de boucle : error=%d ; eod=%d\n", error, eod);

    }
  else
/* TEST 6 */
  if(test[0] == '6')
    {

      /* readdir on root */
      fsal_dir_t dir;
      fsal_cookie_t from, to;
      fsal_dirent_t entries[READDIR_SIZE];
      fsal_count_t number;
      fsal_boolean_t eod = FALSE;
      int error = FALSE;

      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_opendir(&root_handle, &op_ctx, &dir, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      printf("'/' attributes :\n");

      /* displaying attributes */
      printattributes(attribs);

      from = FSAL_READDIR_FROM_BEGINNING;

      while(!error && !eod)
        {
          unsigned int i;

          snprintCookie(tracebuff, 256, &from);
          printf("\nReaddir cookie = %s\n", tracebuff);

          st = FSAL_readdir(&dir, from, mask,
                            READDIR_SIZE * sizeof(fsal_dirent_t),
                            entries, &to, &number, &eod);

          if(FSAL_IS_ERROR(st))
            {
              DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
              error = TRUE;
            }

          /* for each entry, we compare the result of FSAL_access
           * to FSAL_test_access. */
          for(i = 0; (!error) && (i < number); i++)
            {

              fsal_status_t st1, st2;
              char cookiebuff[256];

              snprintHandle(tracebuff, 256, &entries[i].handle);
              snprintCookie(cookiebuff, 256, &entries[i].cookie);

              printf("\t%s : %s (cookie %s)\n", tracebuff,
                     entries[i].name.name, cookiebuff);

              if(FSAL_IS_ERROR(st = ZFSFSAL_getattrs(&entries[i].handle, &op_ctx, &attribs)))
                {
                  DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
                }

              /* 1 - test R access */

              st1 = FSAL_access(&entries[i].handle, &op_ctx, FSAL_R_OK, NULL);

              st2 = FSAL_test_access(&op_ctx, FSAL_R_OK, &attribs);

              DisplayErrorJd(log_desc, ERR_FSAL, st1.major, st1.minor);
              DisplayErrorJd(log_desc, ERR_FSAL, st2.major, st2.minor);

              if(st1.major != st2.major)
                {
                  printf
                      ("Error : different access permissions given by FSAL_access and FSAL_test_access : %d <>%d\n",
                       st1.major, st2.major);
                }

              /* 2 - test W access */

              st1 = FSAL_access(&entries[i].handle, &op_ctx, FSAL_W_OK, NULL);

              st2 = FSAL_test_access(&op_ctx, FSAL_W_OK, &attribs);

              DisplayErrorJd(log_desc, ERR_FSAL, st1.major, st1.minor);
              DisplayErrorJd(log_desc, ERR_FSAL, st2.major, st2.minor);

              if(st1.major != st2.major)
                {
                  printf
                      ("Error : different access permissions given by FSAL_access and FSAL_test_access : %d <>%d\n",
                       st1.major, st2.major);
                }

              /* 3 - test X access */

              st1 = FSAL_access(&entries[i].handle, &op_ctx, FSAL_X_OK, NULL);

              st2 = FSAL_test_access(&op_ctx, FSAL_X_OK, &attribs);

              DisplayErrorJd(log_desc, ERR_FSAL, st1.major, st1.minor);
              DisplayErrorJd(log_desc, ERR_FSAL, st2.major, st2.minor);

              if(st1.major != st2.major)
                {
                  printf
                      ("Error : different access permissions given by FSAL_access and FSAL_test_access : %d <>%d\n",
                       st1.major, st2.major);
                }

            }

          /* preparing next call */
          from = to;

        }
      printf("Fin de boucle : error=%d ; eod=%d\n", error, eod);

    }
  else
/* TEST 7 */
  if(test[0] == '7')
    {

      /* test snprintmem and sscanmem */
      char test_string[] =
          "Ceci est une chaine d'essai.\nLes chiffres : 0123456789\nLes lettres : ABCDEFGHIJKLMNOPQRSTUVWXYZ";

      char buffer[256];
      char string[200];         /* 200 suffit car test_string fait <100 */

      int size1, size2, size3, i;

      /* we put bad values in string, to see if it is correctly set. */
      for(i = 0; i < 200; i++)
        string[i] = (char)i;

      printf("Initial data (%d Bytes) = <<%s>>\n", strlen(test_string), test_string);

      /* Write test_string to a buffer. */
      /* We don't give the final '\0'.  */
      snprintmem(buffer, 256, test_string, strlen(test_string));

      printf("Dest_Buffer (%d Bytes) = <<%s>>\n", strlen(buffer), buffer);

      /* read the value from the buffer */
      sscanmem(string, strlen(test_string), buffer);

      /* sets the final 0 to print the content of the buffer */
      printf("Retrieved string : following byte = %d\n",
             (int)string[strlen(test_string)]);
      string[strlen(test_string)] = '\0';

      printf("Retrieved string (%d Bytes) = <<%s>>\n", strlen(string), string);

      /* Automatic tests : */
      size1 = strlen(test_string);
      size2 = strlen(buffer);
      size3 = strlen(string);

      printf("-------------------------------------\n");

      if(size1 <= 0)
        printf("***** ERROR: source size=0 !!!\n");

      if(size1 != size3)
        printf("***** ERROR: source size <> target size\n");
      else
        printf("OK: source size = target size\n");

      if((size1 * 2) != size2)
        printf("***** ERROR: hexa size <> 2 * source size\n");
      else
        printf("OK: hexa size = 2 * source size\n");

      if(strcmp(test_string, string))
        printf("***** ERROR: source string <> target string\n");
      else
        printf("OK: source string = target string\n");

    }
  else
/* TEST 8 */
  if(test[0] == '8')
    {

      fsal_handle_t dir_hdl, subdir_hdl;
      fsal_name_t subdir_name;

      /* lookup on /cea/prot/S/lama/s8/leibovic */

      if(FSAL_IS_ERROR(st = FSAL_str2path("/cea/prot/S/lama/s8/leibovic", 40, &path)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookupPath(&path, &op_ctx, &handle, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 256, &handle);
      printf("/cea/prot/S/lama/s8/leibovic: handle = %s\n", tracebuff);

      sleep(1);

      /* creates a directory */
      printf("------- Create a directory -------\n");

      if(FSAL_IS_ERROR(st = FSAL_str2name("tests_GANESHA", 30, &name)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      attribs.asked_attributes = mask;

      if(FSAL_IS_ERROR(st = FSAL_mkdir(&handle, &name, &op_ctx,
                                       FSAL_MODE_RUSR | FSAL_MODE_WUSR
                                       | FSAL_MODE_XUSR | FSAL_MODE_RGRP
                                       | FSAL_MODE_WGRP, &dir_hdl, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          snprintHandle(tracebuff, 256, &dir_hdl);
          printf("newly created dir handle = %s\n", tracebuff);

          printattributes(attribs);

        }

      sleep(1);

      /* Try to create it again */
      printf("------- Try to create it again -------\n");

      if(FSAL_IS_ERROR(st = FSAL_mkdir(&handle, &name, &op_ctx,
                                       FSAL_MODE_RUSR | FSAL_MODE_WUSR
                                       | FSAL_MODE_XUSR | FSAL_MODE_RGRP
                                       | FSAL_MODE_WGRP, &dir_hdl, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          printf("**** Error: FSAL should have returned ERR_FSAL_EXIST\n");

        }

      sleep(1);

      /* creates a subdirectory */
      printf("------- Create a subdirectory -------\n");

      if(FSAL_IS_ERROR(st = FSAL_str2name("subdir_GANESHA", 30, &subdir_name)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      if(FSAL_IS_ERROR(st = FSAL_mkdir(&dir_hdl, &subdir_name, &op_ctx,
                                       FSAL_MODE_RUSR | FSAL_MODE_WUSR
                                       | FSAL_MODE_XUSR | FSAL_MODE_RGRP
                                       | FSAL_MODE_WGRP, &subdir_hdl, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          snprintHandle(tracebuff, 256, &subdir_hdl);
          printf("newly created subdir handle = %s\n", tracebuff);

          printattributes(attribs);

        }

      /* try to removes the parent directory */
      printf("------- Try to removes the parent directory -------\n");

      if(FSAL_IS_ERROR(st = FSAL_unlink(&handle, &name, &op_ctx, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          printf("FSAL should not have unlinked %s because it is not empty\n", name.name);

        }

      sleep(1);

      /* removes the subdirectory */
      printf("------- Removes the subdirectory -------\n");

      if(FSAL_IS_ERROR(st = FSAL_unlink(&dir_hdl, &subdir_name, &op_ctx, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          printf("New attributes for parent directory:\n");
          printattributes(attribs);

        }

      /* removes the parent directory */
      printf("------- Removes the parent directory -------\n");

      if(FSAL_IS_ERROR(st = FSAL_unlink(&handle, &name, &op_ctx, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          printf("Unlink %s OK\n", name.name);

        }

    }
/* TEST 9 */
  else if(test[0] == '9')
    {

      fsal_handle_t dir_hdl, subdir_hdl;
      fsal_name_t subdir_name;
      fsal_attrib_list_t attr_set;

      fsal_fsid_t set_fsid = { 1LL, 2LL };

#ifdef _LINUX
      struct tm jour_heure = { 56, 34, 12, 31, 12, 110, 0, 0, 0, 0, 0 };
#else
      struct tm jour_heure = { 56, 34, 12, 31, 12, 110, 0, 0, 0 };
#endif

      /* lookup on /cea/prot/S/lama/s8/leibovic */

      if(FSAL_IS_ERROR(st = FSAL_str2path("/cea/prot/S/lama/s8/leibovic", 40, &path)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookupPath(&path, &op_ctx, &handle, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 256, &handle);
      printf("/cea/prot/S/lama/s8/leibovic: handle = %s\n", tracebuff);

      sleep(1);

      /* creates a file */
      printf("------- Create a file -------\n");

      if(FSAL_IS_ERROR(st = FSAL_str2name("tests_GANESHA_setattrs", 30, &name)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      attribs.asked_attributes = mask;

      if(FSAL_IS_ERROR(st = FSAL_create(&handle, &name, &op_ctx,
                                        FSAL_MODE_RUSR | FSAL_MODE_WUSR
                                        | FSAL_MODE_XUSR | FSAL_MODE_RGRP
                                        | FSAL_MODE_WGRP, &dir_hdl, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          snprintHandle(tracebuff, 256, &dir_hdl);
          printf("newly created file handle = %s\n", tracebuff);

          printattributes(attribs);

        }

      sleep(1);

      printf("------- Try to change its attributes -------\n");

      /* Macro that try to change the value for an attribute */

#define CHANGE_ATTRS( str_nom, nom, flag, new_val ) do {\
  memset(&attr_set, 0, sizeof(fsal_attrib_list_t) );    \
  printf("\nTry to change '%s' :\n",str_nom);           \
  FSAL_SET_MASK( attr_set.asked_attributes , flag );    \
  attr_set.nom = new_val;                               \
  attribs.asked_attributes = attr_set.asked_attributes; \
/*  attribs.asked_attributes = mask;                      */\
  st = FSAL_setattrs( &dir_hdl, &op_ctx, &attr_set, &attribs );\
  if ( FSAL_IS_ERROR(st) )                              \
    DisplayErrorJd(log_desc,ERR_FSAL,st.major,st.minor);\
  else                                                  \
    printattributes( attribs );                         \
  } while(0)

      CHANGE_ATTRS("supported_attributes", supported_attributes,
                   FSAL_ATTR_SUPPATTR, FSAL_ATTRS_MANDATORY);

      CHANGE_ATTRS("type", type, FSAL_ATTR_TYPE, FSAL_TYPE_LNK);

      sleep(1);                 /* to see mtime modification by truncate */

      CHANGE_ATTRS("filesize", filesize, FSAL_ATTR_SIZE, (fsal_size_t) 12);

      sleep(1);                 /* to see mtime modification by truncate */

      CHANGE_ATTRS("fsid", fsid, FSAL_ATTR_FSID, set_fsid);

      /* @todo : ACLs */

      CHANGE_ATTRS("fileid", fileid, FSAL_ATTR_FILEID, (fsal_u64_t) 1234);

      CHANGE_ATTRS("mode", mode, FSAL_ATTR_MODE,
                   (FSAL_MODE_RUSR | FSAL_MODE_WUSR | FSAL_MODE_RGRP));

      CHANGE_ATTRS("numlinks", numlinks, FSAL_ATTR_NUMLINKS, 7);

      /* FSAL_ATTR_RAWDEV */

      CHANGE_ATTRS("atime", atime.seconds, FSAL_ATTR_ATIME, mktime(&jour_heure));

      jour_heure.tm_min++;

      CHANGE_ATTRS("creation", creation.seconds, FSAL_ATTR_CREATION, mktime(&jour_heure));

      jour_heure.tm_min++;

      CHANGE_ATTRS("mtime", mtime.seconds, FSAL_ATTR_MTIME, mktime(&jour_heure));

      jour_heure.tm_min++;

      CHANGE_ATTRS("ctime", ctime.seconds, FSAL_ATTR_CTIME, mktime(&jour_heure));

      CHANGE_ATTRS("spaceused", spaceused, FSAL_ATTR_SPACEUSED, (fsal_size_t) 12345);

      CHANGE_ATTRS("mounted_on_fileid", mounted_on_fileid,
                   FSAL_ATTR_MOUNTFILEID, (fsal_u64_t) 3210);

      CHANGE_ATTRS("owner", owner, FSAL_ATTR_OWNER, 3051);      /* deniel */

      CHANGE_ATTRS("group", group, FSAL_ATTR_GROUP, 5953);      /* sr */

      sleep(1);

      /* removes the parent directory */
      printf("------- Removes the directory -------\n");

      if(FSAL_IS_ERROR(st = FSAL_unlink(&handle, &name, &op_ctx, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          printf("Unlink %s OK\n", name.name);

        }

    }
  else if(test[0] == 'A')
    {

      char digest_buff[FSAL_DIGEST_SIZE_HDLV3];

      /* lookup on /cea/prot/S/lama/s8/leibovic */

      if(FSAL_IS_ERROR(st = FSAL_str2path("/cea/prot/S/lama/s8/leibovic", 40, &path)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookupPath(&path, &op_ctx, &handle, &attribs)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 256, &handle);
      printf("/cea/prot/S/lama/s8/leibovic: handle = %s\n", tracebuff);

      /* building digest */

      st = FSAL_DigestHandle(&export_ctx, FSAL_DIGEST_NFSV3, &handle, digest_buff);

      if(FSAL_IS_ERROR(st))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      else
        {
          /* print digest */
          snprintmem(tracebuff, 256, digest_buff, FSAL_DIGEST_SIZE_HDLV3);
          printf("/cea/prot/S/lama/s8/leibovic: handle_digest = %s\n", tracebuff);
        }

      memset(&handle, 0, sizeof(fsal_handle_t));

      /* expend digest */

      st = FSAL_ExpandHandle(&export_ctx, FSAL_DIGEST_NFSV3, digest_buff, &handle);

      if(FSAL_IS_ERROR(st))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
        }
      else
        {
          /* print expended handle */
          snprintHandle(tracebuff, 256, &handle);
          printf("/cea/prot/S/lama/s8/leibovic: handle expended = %s\n", tracebuff);
        }

    }
  else if(test[0] == 'B')
    {

      fsal_dynamicfsinfo_t dyninfo;

      if(FSAL_IS_ERROR(st = FSAL_dynamic_fsinfo(&root_handle, &op_ctx, &dyninfo)))
        {
          DisplayErrorJd(log_desc, ERR_FSAL, st.major, st.minor);
          exit(st.major);
        }

      printf("total_bytes = %llu\n", dyninfo.total_bytes);
      printf("free_bytes = %llu\n", dyninfo.free_bytes);
      printf("avail_bytes = %llu\n", dyninfo.avail_bytes);
      printf("total_files = %llu\n", dyninfo.total_files);
      printf("free_files = %llu\n", dyninfo.free_files);
      printf("avail_files = %llu\n", dyninfo.avail_files);
      printf("time_delta = %u.%u\n", dyninfo.time_delta.seconds,
             dyninfo.time_delta.nseconds);

    }
  else
    printf("%s : test inconnu\n", test);

  return 0;

}
