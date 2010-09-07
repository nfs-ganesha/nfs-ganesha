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
    LogTest("FSAL_ATTR_SUPPATTR");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_TYPE))
    LogTest("FSAL_ATTR_TYPE");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_SIZE))
    LogTest("FSAL_ATTR_SIZE");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_FSID))
    LogTest("FSAL_ATTR_FSID");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_ACL))
    LogTest("FSAL_ATTR_ACL ");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_FILEID))
    LogTest("FSAL_ATTR_FILEID");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_MODE))
    LogTest("FSAL_ATTR_MODE");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_NUMLINKS))
    LogTest("FSAL_ATTR_NUMLINKS");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_OWNER))
    LogTest("FSAL_ATTR_OWNER");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_GROUP))
    LogTest("FSAL_ATTR_GROUP");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_RAWDEV))
    LogTest("FSAL_ATTR_RAWDEV");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_ATIME))
    LogTest("FSAL_ATTR_ATIME");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_CREATION))
    LogTest("FSAL_ATTR_CREATION");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_CTIME))
    LogTest("FSAL_ATTR_CTIME");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_CHGTIME))
    LogTest("FSAL_ATTR_CHGTIME");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_MTIME))
    LogTest("FSAL_ATTR_MTIME");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_SPACEUSED))
    LogTest("FSAL_ATTR_SPACEUSED");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_MOUNTFILEID))
    LogTest("FSAL_ATTR_MOUNTFILEID");

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
    LogTest("FSAL_ATTR_RDATTR_ERR");

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_TYPE))
    LogTest("Type : %s", strtype(attrs.type));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_SIZE))
    LogTest("Size : %llu", attrs.filesize);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_FSID))
    LogTest("fsId : %llu.%llu", attrs.fsid.major, attrs.fsid.minor);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ACL))
    LogTest("ACL List ...");
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_FILEID))
    LogTest("FileId : %llu", attrs.fileid);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    LogTest("Mode : %#o", attrs.mode);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_NUMLINKS))
    LogTest("Numlinks : %u", (unsigned int)attrs.numlinks);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER))
    LogTest("uid : %d", attrs.owner);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_GROUP))
    LogTest("gid : %d", attrs.group);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_RAWDEV))
    LogTest("Rawdev ...");
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME))
    LogTest("atime : %s", ctime((time_t *) & attrs.atime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_CREATION))
    LogTest("creation time : %s", ctime((time_t *) & attrs.creation.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_CTIME))
    LogTest("ctime : %s", ctime((time_t *) & attrs.ctime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME))
    LogTest("mtime : %s", ctime((time_t *) & attrs.mtime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_CHGTIME))
    LogTest("chgtime : %s", ctime((time_t *) & attrs.chgtime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_SPACEUSED))
    LogTest("spaceused : %llu", attrs.spaceused);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MOUNTFILEID))
    LogTest("mounted_on_fileid : %llu", attrs.mounted_on_fileid);

}

void usage()
{
  LogTest("Usage :\n\ttest_fsal <no_test>");
  LogTest("\ttests :");
  LogTest("\t\t1 - getattrs");
  LogTest("\t\t2 - lookup");
  LogTest("\t\t3 - lookupPath");
  LogTest("\t\t4 - readdir (acces par tableau)");
  LogTest("\t\t5 - readdir (acces liste chainee)");
  LogTest("\t\t6 - access/test_access");
  LogTest("\t\t7 - snprintmem/sscanmem");
  LogTest("\t\t8 - mkdir/rmdir");
  LogTest("\t\t9 - setattr");
  LogTest("\t\tA - digest/expend handle");
  LogTest("\t\tB - dynamic fs info");
  return;
}

int main(int argc, char **argv)
{

  char localmachine[256];
  char *test;
  fsal_parameter_t init_param;
  fsal_status_t st;
  uid_t uid;
  snmpfsal_export_context_t export_ctx;
  snmpfsal_op_context_t op_ctx;
  snmpfsal_handle_t root_handle, handle;
  fsal_name_t name;
  fsal_path_t path;
  fsal_attrib_list_t attribs;
  fsal_attrib_mask_t mask;
  char tracebuff[2048];

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
  SetDefaultLogging("TEST");
  SetNameFunction("main");
  InitLogging();

  /* Obtention du nom de la machine */
  if(gethostname(localmachine, sizeof(localmachine)) != 0)
    {
      LogError(COMPONENT_FSAL, ERR_SYS, ERR_GETHOSTNAME, errno);
      exit(1);
    }
  else
    SetNameHost(localmachine);

  AddFamilyError(ERR_FSAL, "FSAL related Errors", tab_errstatus_FSAL);

  /* prepare fsal_init */

  /* 1 - fs specific info */

  FSAL_SetDefault_FS_specific_parameter(&init_param);

  strcpy(init_param.fs_specific_info.snmp_server, "scratchy");

  /* 2-common info (default) */
  FSAL_SetDefault_FS_common_parameter(&init_param);

  /* 3- fsal info */
  FSAL_SetDefault_FSAL_parameter(&init_param);

  init_param.fsal_info.max_fs_calls = 0;

  /* Init */
  if(FSAL_IS_ERROR(st = FSAL_Init(&init_param)))
    {
      LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
    }

  /* getting creds */
  uid = getuid();
  LogTest("uid = %d", uid);

#define TEST_SNMP_OID "/"
  FSAL_str2path(TEST_SNMP_OID, strlen(TEST_SNMP_OID) + 1, &path);

  st = FSAL_BuildExportContext(&export_ctx, &path, NULL);
  if(FSAL_IS_ERROR(st))
    LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);

  st = FSAL_InitClientContext(&op_ctx);

  if(FSAL_IS_ERROR(st))
    LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);

  st = FSAL_GetClientContext(&op_ctx, &export_ctx, uid, -1, NULL, 0);

  if(FSAL_IS_ERROR(st))
    LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);

  /* getting root handle */

  if(FSAL_IS_ERROR(st = FSAL_lookup(NULL, NULL, &op_ctx, &root_handle, NULL)))
    {
      LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
    }

  snprintHandle(tracebuff, 2048, &root_handle);
  LogTest("Root handle (size=%u, root_type=%d, object_type=%d) = %s",
         sizeof(snmpfsal_handle_t), (int)FSAL_NODETYPE_ROOT,
         root_handle.object_type_reminder, tracebuff);

  /* getting what are the supported attributes */

  attribs.asked_attributes = 0;
  FSAL_SET_MASK(attribs.asked_attributes, FSAL_ATTR_SUPPATTR);
  LogTest("asked attributes :");
  printmask(attribs.asked_attributes);

  if(FSAL_IS_ERROR(st = FSAL_getattrs(&root_handle, &op_ctx, &attribs)))
    {
      LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
    }

  LogTest("supported attributes :");
  printmask(attribs.supported_attributes);

  mask = attribs.supported_attributes;

/* TEST 1 */

  if(test[0] == '1')
    {

      attribs.asked_attributes = 0;
      FSAL_SET_MASK(attribs.asked_attributes, FSAL_ATTR_SUPPATTR);
      LogTest("asked attributes :");
      printmask(attribs.asked_attributes);

      if(FSAL_IS_ERROR(st = FSAL_getattrs(&root_handle, &op_ctx, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      LogTest("supported attributes :");

      /* getting all spported attributes of root */
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_getattrs(&root_handle, &op_ctx, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      printattributes(attribs);

    }
  else
/* TEST 2 */
  if(test[0] == '2')
    {

      /* getting handle and attributes for subdirectory "iso" */
      if(FSAL_IS_ERROR(st = FSAL_str2name("iso", 4, &name)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookup(&root_handle, &name, &op_ctx, &handle, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 2048, &handle);
      LogTest("/iso handle = %s", tracebuff);

      /* displaying attributes */
      printattributes(attribs);

      /* getting handle and attributes for subdirectory "org" */
      if(FSAL_IS_ERROR(st = FSAL_str2name("org", 4, &name)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      root_handle = handle;
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookup(&root_handle, &name, &op_ctx, &handle, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 2048, &handle);
      LogTest("/iso/org handle = %s", tracebuff);

      /* displaying attributes */
      printattributes(attribs);

      /* getting handle and attributes for "dod" */
      if(FSAL_IS_ERROR(st = FSAL_str2name("6", 2, &name)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      root_handle = handle;
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookup(&root_handle, &name, &op_ctx, &handle, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 2048, &handle);
      LogTest("/iso/org/dod handle = %s", tracebuff);

      /* displaying attributes */
      printattributes(attribs);

    }
  else
/* TEST 3 */
  if(test[0] == '3')
    {

      /* lookup root */
      if(FSAL_IS_ERROR(st = FSAL_str2path("/", 2, &path)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookupPath(&path, &op_ctx, &handle, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 2048, &handle);
      LogTest("/ handle = %s", tracebuff);

      /* displaying attributes */
      printattributes(attribs);

#define MY_SNMP_VAR "/iso/org/dod/internet/mgmt/mib-2/system/sysUpTime/0"
      /* lookup path */
      if(FSAL_IS_ERROR(st = FSAL_str2path(MY_SNMP_VAR, strlen(MY_SNMP_VAR) + 1, &path)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookupPath(&path, &op_ctx, &handle, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 2048, &handle);
      LogTest("%s handle = %s", MY_SNMP_VAR, tracebuff);

      /* displaying attributes */
      printattributes(attribs);

    }
  else
/* TEST 4 */
  if(test[0] == '4')
    {

      /* readdir on root */
      snmpfsal_dir_t dir;
      snmpfsal_cookie_t from, to;
      fsal_dirent_t entries[READDIR_SIZE];
      fsal_count_t number;
      fsal_boolean_t eod = FALSE;
      int error = FALSE;

      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_opendir(&root_handle, &op_ctx, &dir, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      LogTest("'/' attributes :");

      /* displaying attributes */
      printattributes(attribs);

      from = FSAL_READDIR_FROM_BEGINNING;

      while(!error && !eod)
        {
          unsigned int i;
          char cookiebuff[2048];

          snprintCookie(cookiebuff, 2048, &from);
          LogTest("\nReaddir cookie = %s", cookiebuff);
          if(FSAL_IS_ERROR(st = FSAL_readdir(&dir, from,
                                             mask, READDIR_SIZE * sizeof(fsal_dirent_t),
                                             entries, &to, &number, &eod)))
            {
              LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
              error = TRUE;
            }

          for(i = 0; (!error) && (i < number); i++)
            {

              snprintHandle(tracebuff, 2048, &entries[i].handle);

              snprintCookie(cookiebuff, 2048, &entries[i].cookie);

              LogTest("\t%s : %s (cookie %s)", tracebuff,
                     entries[i].name.name, cookiebuff);
            }
          /* preparing next call */
          from = to;

        }
      LogTest("Fin de boucle : error=%d ; eod=%d", error, eod);

    }
  else
/* TEST 5 */
  if(test[0] == '5')
    {

      /* readdir on root */
      snmpfsal_dir_t dir;
      snmpfsal_cookie_t from, to;
      fsal_dirent_t entries[READDIR_SIZE];
      fsal_count_t number;
      fsal_boolean_t eod = FALSE;
      int error = FALSE;

      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_opendir(&root_handle, &op_ctx, &dir, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      LogTest("'/' attributes :");

      /* displaying attributes */
      printattributes(attribs);

      from = FSAL_READDIR_FROM_BEGINNING;

      while(!error && !eod)
        {
          fsal_dirent_t *curr;

          char cookiebuff[2048];

          snprintCookie(cookiebuff, 2048, &from);

          LogTest("\nReaddir cookie = %s", cookiebuff);

          if(FSAL_IS_ERROR(st = FSAL_readdir(&dir, from,
                                             mask, READDIR_SIZE * sizeof(fsal_dirent_t),
                                             entries, &to, &number, &eod)))
            {
              LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
              error = TRUE;
            }

          if(number > 0)
            {
              curr = entries;
              do
                {

                  snprintHandle(tracebuff, 2048, &curr->handle);
                  snprintCookie(cookiebuff, 2048, &curr->cookie);

                  LogTest("\t%s : %s (cookie %s)", tracebuff,
                         curr->name.name, cookiebuff);
                }
              while(curr = curr->nextentry);
            }
          /* preparing next call */
          from = to;

        }
      LogTest("Fin de boucle : error=%d ; eod=%d", error, eod);

    }
  else
/* TEST 6 */
  if(test[0] == '6')
    {

      /* readdir on root */
      snmpfsal_dir_t dir;
      snmpfsal_cookie_t from, to;
      fsal_dirent_t entries[READDIR_SIZE];
      fsal_count_t number;
      fsal_boolean_t eod = FALSE;
      int error = FALSE;

      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = SNMPFSAL_opendir(&root_handle, &op_ctx, &dir, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      LogTest("'/' attributes :");

      /* displaying attributes */
      printattributes(attribs);

      from = FSAL_READDIR_FROM_BEGINNING;

      while(!error && !eod)
        {
          unsigned int i;

          snprintCookie(tracebuff, 2048, &from);
          LogTest("\nReaddir cookie = %s", tracebuff);

          st = FSAL_readdir(&dir, from, mask,
                            READDIR_SIZE * sizeof(fsal_dirent_t),
                            entries, &to, &number, &eod);

          if(FSAL_IS_ERROR(st))
            {
              LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
              error = TRUE;
            }

          /* for each entry, we compare the result of FSAL_access
           * to FSAL_test_access. */
          for(i = 0; (!error) && (i < number); i++)
            {

              fsal_status_t st1, st2;
              char cookiebuff[2048];

              snprintHandle(tracebuff, 2048, &entries[i].handle);
              snprintCookie(cookiebuff, 2048, &entries[i].cookie);

              LogTest("\t%s : %s (cookie %s)", tracebuff,
                     entries[i].name.name, cookiebuff);

              if(FSAL_IS_ERROR(st = FSAL_getattrs(&entries[i].handle, &op_ctx, &attribs)))
                {
                  LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
                }

              /* 1 - test R access */

              st1 = FSAL_access(&entries[i].handle, &op_ctx, FSAL_R_OK, NULL);

              st2 = FSAL_test_access(&op_ctx, FSAL_R_OK, &attribs);

              LogError(COMPONENT_FSAL, ERR_FSAL, st1.major, st1.minor);
              LogError(COMPONENT_FSAL, ERR_FSAL, st2.major, st2.minor);

              if(st1.major != st2.major)
                {
                  LogTest(
                       "Error : different access permissions given by FSAL_access and FSAL_test_access : %d <>%d",
                       st1.major, st2.major);
                }

              /* 2 - test W access */

              st1 = FSAL_access(&entries[i].handle, &op_ctx, FSAL_W_OK, NULL);

              st2 = FSAL_test_access(&op_ctx, FSAL_W_OK, &attribs);

              LogError(COMPONENT_FSAL, ERR_FSAL, st1.major, st1.minor);
              LogError(COMPONENT_FSAL, ERR_FSAL, st2.major, st2.minor);

              if(st1.major != st2.major)
                {
                  LogTest(
                       "Error : different access permissions given by FSAL_access and FSAL_test_access : %d <>%d",
                       st1.major, st2.major);
                }

              /* 3 - test X access */

              st1 = FSAL_access(&entries[i].handle, &op_ctx, FSAL_X_OK, NULL);

              st2 = FSAL_test_access(&op_ctx, FSAL_X_OK, &attribs);

              LogError(COMPONENT_FSAL, ERR_FSAL, st1.major, st1.minor);
              LogError(COMPONENT_FSAL, ERR_FSAL, st2.major, st2.minor);

              if(st1.major != st2.major)
                {
                  LogTest(
                       "Error : different access permissions given by FSAL_access and FSAL_test_access : %d <>%d",
                       st1.major, st2.major);
                }

            }

          /* preparing next call */
          from = to;

        }
      LogTest("Fin de boucle : error=%d ; eod=%d", error, eod);

    }
  else
/* TEST 7 */
  if(test[0] == '7')
    {

      /* test snprintmem and sscanmem */
      char test_string[] =
          "Ceci est une chaine d'essai.\nLes chiffres : 0123456789\nLes lettres : ABCDEFGHIJKLMNOPQRSTUVWXYZ";

      char buffer[2048];
      char string[200];         /* 200 suffit car test_string fait <100 */

      int size1, size2, size3, i;

      /* we put bad values in string, to see if it is correctly set. */
      for(i = 0; i < 200; i++)
        string[i] = (char)i;

      LogTest("Initial data (%d Bytes) = <<%s>>", strlen(test_string), test_string);

      /* Write test_string to a buffer. */
      /* We don't give the final '\0'.  */
      snprintmem(buffer, 2048, test_string, strlen(test_string));

      LogTest("Dest_Buffer (%d Bytes) = <<%s>>", strlen(buffer), buffer);

      /* read the value from the buffer */
      sscanmem(string, strlen(test_string), buffer);

      /* sets the final 0 to print the content of the buffer */
      LogTest("Retrieved string : following byte = %d",
             (int)string[strlen(test_string)]);
      string[strlen(test_string)] = '\0';

      LogTest("Retrieved string (%d Bytes) = <<%s>>", strlen(string), string);

      /* Automatic tests : */
      size1 = strlen(test_string);
      size2 = strlen(buffer);
      size3 = strlen(string);

      LogTest("-------------------------------------");

      if(size1 <= 0)
        LogTest("***** ERROR: source size=0 !!!");

      if(size1 != size3)
        LogTest("***** ERROR: source size <> target size");
      else
        LogTest("OK: source size = target size");

      if((size1 * 2) != size2)
        LogTest("***** ERROR: hexa size <> 2 * source size");
      else
        LogTest("OK: hexa size = 2 * source size");

      if(strcmp(test_string, string))
        LogTest("***** ERROR: source string <> target string");
      else
        LogTest("OK: source string = target string");

    }
  else
/* TEST 8 */
  if(test[0] == '8')
    {

      snmpfsal_handle_t dir_hdl, subdir_hdl;
      fsal_name_t subdir_name;

      /* lookup on /iso/org/dod/internet/mgmt/mib-2/system */

#define TEST8_PATH "/iso/org/dod/internet/mgmt/mib-2/system"

      if(FSAL_IS_ERROR(st = FSAL_str2path(TEST8_PATH, sizeof(TEST8_PATH), &path)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookupPath(&path, &op_ctx, &handle, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 2048, &handle);
      LogTest("%s: handle = %s", TEST8_PATH, tracebuff);

      sleep(1);

      /* creates a directory */
      LogTest("------- Create a directory -------");

      if(FSAL_IS_ERROR(st = FSAL_str2name("tests_GANESHA", 30, &name)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      attribs.asked_attributes = mask;

      if(FSAL_IS_ERROR(st = FSAL_mkdir(&handle, &name, &op_ctx,
                                       FSAL_MODE_RUSR | FSAL_MODE_WUSR
                                       | FSAL_MODE_XUSR | FSAL_MODE_RGRP
                                       | FSAL_MODE_WGRP, &dir_hdl, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          snprintHandle(tracebuff, 2048, &dir_hdl);
          LogTest("newly created dir handle = %s", tracebuff);

          printattributes(attribs);

        }

      sleep(1);

      /* Try to create it again */
      LogTest("------- Try to create it again -------");

      if(FSAL_IS_ERROR(st = FSAL_mkdir(&handle, &name, &op_ctx,
                                       FSAL_MODE_RUSR | FSAL_MODE_WUSR
                                       | FSAL_MODE_XUSR | FSAL_MODE_RGRP
                                       | FSAL_MODE_WGRP, &dir_hdl, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          LogTest("**** Error: FSAL should have returned ERR_FSAL_EXIST");

        }

      sleep(1);

      /* creates a subdirectory */
      LogTest("------- Create a subdirectory -------");

      if(FSAL_IS_ERROR(st = FSAL_str2name("subdir_GANESHA", 30, &subdir_name)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      if(FSAL_IS_ERROR(st = FSAL_mkdir(&dir_hdl, &subdir_name, &op_ctx,
                                       FSAL_MODE_RUSR | FSAL_MODE_WUSR
                                       | FSAL_MODE_XUSR | FSAL_MODE_RGRP
                                       | FSAL_MODE_WGRP, &subdir_hdl, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          snprintHandle(tracebuff, 2048, &subdir_hdl);
          LogTest("newly created subdir handle = %s", tracebuff);

          printattributes(attribs);

        }

      /* try to removes the parent directory */
      LogTest("------- Try to removes the parent directory -------");

      if(FSAL_IS_ERROR(st = FSAL_unlink(&handle, &name, &op_ctx, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          LogTest("FSAL should not have unlinked %s because it is not empty", name.name);

        }

      sleep(1);

      /* removes the subdirectory */
      LogTest("------- Removes the subdirectory -------");

      if(FSAL_IS_ERROR(st = FSAL_unlink(&dir_hdl, &subdir_name, &op_ctx, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          LogTest("New attributes for parent directory:");
          printattributes(attribs);

        }

      /* removes the parent directory */
      LogTest("------- Removes the parent directory -------");

      if(FSAL_IS_ERROR(st = FSAL_unlink(&handle, &name, &op_ctx, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          LogTest("Unlink %s OK", name.name);

        }

    }
/* TEST 9 */
  else if(test[0] == '9')
    {

      snmpfsal_handle_t dir_hdl, subdir_hdl;
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
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookupPath(&path, &op_ctx, &handle, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 2048, &handle);
      LogTest("/cea/prot/S/lama/s8/leibovic: handle = %s", tracebuff);

      sleep(1);

      /* creates a file */
      LogTest("------- Create a file -------");

      if(FSAL_IS_ERROR(st = FSAL_str2name("tests_GANESHA_setattrs", 30, &name)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      attribs.asked_attributes = mask;

      if(FSAL_IS_ERROR(st = FSAL_create(&handle, &name, &op_ctx,
                                        FSAL_MODE_RUSR | FSAL_MODE_WUSR
                                        | FSAL_MODE_XUSR | FSAL_MODE_RGRP
                                        | FSAL_MODE_WGRP, &dir_hdl, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          snprintHandle(tracebuff, 2048, &dir_hdl);
          LogTest("newly created file handle = %s", tracebuff);

          printattributes(attribs);

        }

      sleep(1);

      LogTest("------- Try to change its attributes -------");

      /* Macro that try to change the value for an attribute */

#define CHANGE_ATTRS( str_nom, nom, flag, new_val ) do {\
  memset(&attr_set, 0, sizeof(fsal_attrib_list_t) );    \
  LogTest("\nTry to change '%s' :",str_nom);           \
  FSAL_SET_MASK( attr_set.asked_attributes , flag );    \
  attr_set.nom = new_val;                               \
  attribs.asked_attributes = attr_set.asked_attributes; \
/*  attribs.asked_attributes = mask;                      */\
  st = FSAL_setattrs( &dir_hdl, &op_ctx, &attr_set, &attribs );\
  if ( FSAL_IS_ERROR(st) )                              \
    LogError(COMPONENT_FSAL,ERR_FSAL,st.major,st.minor);\
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
      LogTest("------- Removes the directory -------");

      if(FSAL_IS_ERROR(st = FSAL_unlink(&handle, &name, &op_ctx, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      else
        {

          LogTest("Unlink %s OK", name.name);

        }

    }
  else if(test[0] == 'A')
    {

      char digest_buff[FSAL_DIGEST_SIZE_HDLV3];

      /* lookup on MY_SNMP_VAR */

      if(FSAL_IS_ERROR(st = FSAL_str2path(MY_SNMP_VAR, sizeof(MY_SNMP_VAR), &path)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      attribs.asked_attributes = mask;
      if(FSAL_IS_ERROR(st = FSAL_lookupPath(&path, &op_ctx, &handle, &attribs)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }

      snprintHandle(tracebuff, 2048, &handle);
      LogTest("%s: handle = %s", MY_SNMP_VAR, tracebuff);

      /* building digest */

      st = FSAL_DigestHandle(&export_ctx, FSAL_DIGEST_NFSV3, &handle, digest_buff);

      if(FSAL_IS_ERROR(st))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      else
        {
          /* print digest */
          snprintmem(tracebuff, 2048, digest_buff, FSAL_DIGEST_SIZE_HDLV3);
          LogTest("%s: handle_digest = %s", MY_SNMP_VAR, tracebuff);
        }

      memset(&handle, 0, sizeof(snmpfsal_handle_t));

      /* expend digest */

      st = FSAL_ExpandHandle(&export_ctx, FSAL_DIGEST_NFSV3, digest_buff, &handle);

      if(FSAL_IS_ERROR(st))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
        }
      else
        {
          /* print expended handle */
          snprintHandle(tracebuff, 2048, &handle);
          LogTest("%s: handle expended = %s", MY_SNMP_VAR, tracebuff);
        }

    }
  else if(test[0] == 'B')
    {

      fsal_dynamicfsinfo_t dyninfo;

      if(FSAL_IS_ERROR(st = FSAL_dynamic_fsinfo(&root_handle, &op_ctx, &dyninfo)))
        {
          LogError(COMPONENT_FSAL, ERR_FSAL, st.major, st.minor);
          exit(st.major);
        }

      LogTest("total_bytes = %llu", dyninfo.total_bytes);
      LogTest("free_bytes = %llu", dyninfo.free_bytes);
      LogTest("avail_bytes = %llu", dyninfo.avail_bytes);
      LogTest("total_files = %llu", dyninfo.total_files);
      LogTest("free_files = %llu", dyninfo.free_files);
      LogTest("avail_files = %llu", dyninfo.avail_files);
      LogTest("time_delta = %u.%u", dyninfo.time_delta.seconds,
             dyninfo.time_delta.nseconds);

    }
  else
    LogTest("%s : test inconnu", test);

  return 0;

}
