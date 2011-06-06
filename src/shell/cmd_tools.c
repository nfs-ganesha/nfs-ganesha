/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    cmd_tools.c
 * \author  $Author: leibovic $
 * \date    $Date $
 * \version $Revision $
 * \brief   Header file for functions used by several layers.
 *
 *
 * $Log: cmd_tools.c,v $
 * Revision 1.24  2006/01/20 14:44:13  leibovic
 * altgroups support.
 *
 * Revision 1.23  2006/01/17 14:56:22  leibovic
 * Adaptation de HPSS 6.2.
 *
 * Revision 1.21  2005/09/28 09:08:00  leibovic
 * thread-safe version of localtime.
 *
 * Revision 1.20  2005/09/09 15:23:11  leibovic
 * Adding "cross" command for crossing junctions.
 *
 * Revision 1.19  2005/08/12 11:21:27  leibovic
 * Now, set cat concatenate strings.
 *
 * Revision 1.18  2005/07/27 14:19:08  leibovic
 * Changing fsal_time_t printing.
 *
 * Revision 1.17  2005/05/11 15:53:36  leibovic
 * Adding time function.
 *
 * Revision 1.16  2005/05/03 09:38:25  leibovic
 * Adding handle adressing support.
 *
 * Revision 1.15  2005/04/25 15:25:54  leibovic
 * Handling daylight saving.
 *
 * Revision 1.14  2005/04/25 12:57:48  leibovic
 * Implementing setattr.
 *
 * Revision 1.13  2005/04/14 11:21:56  leibovic
 * Changing command syntax.
 *
 * Revision 1.12  2005/04/13 09:28:05  leibovic
 * Adding unlink and mkdir calls.
 *
 * Revision 1.11  2005/03/15 15:30:07  leibovic
 * localtime_r -> localtime.
 *
 * Revision 1.10  2005/03/14 10:49:18  leibovic
 * Changing time printing.
 *
 * Revision 1.9  2005/03/09 15:43:25  leibovic
 * Multi-OS compiling.
 *
 * Revision 1.8  2005/03/04 08:01:32  leibovic
 * removing snprintmem (moved to FSAL layer)à.
 *
 * Revision 1.7  2005/01/31 14:09:03  leibovic
 * Portage to Linux.
 *
 * Revision 1.6  2005/01/21 13:31:26  leibovic
 * Code clenaning.
 *
 * Revision 1.5  2005/01/11 08:27:38  leibovic
 * Implementing Cache_inode layer tests.
 *
 * Revision 1.4  2005/01/07 09:12:13  leibovic
 * Improved Cache_inode shell version.
 *
 * Revision 1.3  2004/12/17 16:05:27  leibovic
 * Replacing %X with snprintmem for handles printing.
 *
 * Revision 1.2  2004/12/09 15:53:50  leibovic
 * A debug printf had been left.
 *
 * Revision 1.1  2004/12/09 15:46:22  leibovic
 * Tools externalisation.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <strings.h>
#include <string.h>
#include <errno.h>
#include <err_ghost_fs.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "cmd_tools.h"
#include <grp.h>

/* mutex for calling localtime... */
static pthread_mutex_t mutex_localtime = PTHREAD_MUTEX_INITIALIZER;

/* thread-safe and PORTABLE version of localtime... */
struct tm *Localtime_r(const time_t * p_time, struct tm *p_tm)
{
  struct tm *p_tmp_tm;

  if(!p_tm)
    {
      errno = EFAULT;
      return NULL;
    }

  pthread_mutex_lock(&mutex_localtime);

  p_tmp_tm = localtime(p_time);

  /* copy the result */
  (*p_tm) = (*p_tmp_tm);

  pthread_mutex_unlock(&mutex_localtime);

  return p_tm;
}

/**
 * my_atoi:
 * This function converts a string to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A negative value on error.
 *         Else, the converted integer.
 */
int my_atoi(char *str)
{

  int i;
  int out = 0;

  for(i = 0; str[i]; i++)
    {

      if((str[i] < '0') || (str[i] > '9'))
        return -1;              /* error */
      else
        {
          out *= 10;
          out += (int)(str[i] - '0');
        }
    }

  if(i == 0)
    return -1;

  return out;

}

/**
 * atomode:
 * This function converts a string to a unix access mode.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A negative value on error.
 *         Else, the access mode integer.
 */
int atomode(char *str)
{

  int i;
  int out = 0;

  for(i = 0; str[i]; i++)
    {

      if((str[i] < '0') || (str[i] > '7'))
        return -1;              /* error */
      else
        {
          out *= 8;
          out += (int)(str[i] - '0');
        }
    }

  if(i < 3)
    return -1;

  return out;

}

int ato64(char *str, unsigned long long *out64)
{

  int i;
  unsigned long long out = 0;

  if(!out64)
    return -1;

  for(i = 0; str[i]; i++)
    {

      if((str[i] < '0') || (str[i] > '9'))
        return -1;              /* error */
      else
        {
          out *= 10;
          out += (unsigned long long)(str[i] - '0');
        }
    }

  if(i == 0)
    return -1;

  *out64 = out;

  return 0;
}

/**
 * convert time from "YYYYMMDDHHMMSS" to time_t.
 */
time_t atotime(char *str)
{

  struct tm time_struct;

  char tmp_str[16];
  int i, j, rc;

  /* init tm */

  memset(&time_struct, 0, sizeof(struct tm));

  /* parsing year */

  j = 0;
  for(i = 0; i < 4; i++)
    {
      if(!str[i])
        return (time_t) - 1;
      tmp_str[j] = str[i];
      j++;
    }
  tmp_str[j] = '\0';
  j++;

  rc = my_atoi(tmp_str);
  if(rc == -1)
    return (time_t) - 1;

  time_struct.tm_year = rc - 1900;

/*  printf("Year: %d\n",rc);*/

  /* parsing month */

  j = 0;
  for(i = 4; i < 6; i++)
    {
      if(!str[i])
        return (time_t) - 1;
      tmp_str[j] = str[i];
      j++;
    }
  tmp_str[j] = '\0';
  j++;

  rc = my_atoi(tmp_str);
  if(rc == -1)
    return (time_t) - 1;

  time_struct.tm_mon = rc - 1;

/*  printf("Month: %d\n",rc);*/

  /* parsing day of month */

  j = 0;
  for(i = 6; i < 8; i++)
    {
      if(!str[i])
        return (time_t) - 1;
      tmp_str[j] = str[i];
      j++;
    }
  tmp_str[j] = '\0';
  j++;

  rc = my_atoi(tmp_str);
  if(rc == -1)
    return (time_t) - 1;

  time_struct.tm_mday = rc;

/*  printf("Day: %d\n",rc);*/

  /* parsing hour */

  j = 0;
  for(i = 8; i < 10; i++)
    {
      if(!str[i])
        return (time_t) - 1;
      tmp_str[j] = str[i];
      j++;
    }
  tmp_str[j] = '\0';
  j++;

  rc = my_atoi(tmp_str);
  if(rc == -1)
    return (time_t) - 1;

  time_struct.tm_hour = rc;

/*  printf("Hour: %d\n",rc);*/

  /* parsing minute */

  j = 0;
  for(i = 10; i < 12; i++)
    {
      if(!str[i])
        return (time_t) - 1;
      tmp_str[j] = str[i];
      j++;
    }
  tmp_str[j] = '\0';
  j++;

  rc = my_atoi(tmp_str);
  if(rc == -1)
    return (time_t) - 1;

  time_struct.tm_min = rc;

/*  printf("Min: %d\n",rc);*/

  /* parsing seconds */

  j = 0;
  for(i = 12; i < 14; i++)
    {
      if(!str[i])
        return (time_t) - 1;
      tmp_str[j] = str[i];
      j++;
    }
  tmp_str[j] = '\0';
  j++;

  rc = my_atoi(tmp_str);
  if(rc == -1)
    return (time_t) - 1;

  time_struct.tm_sec = rc;

/*  printf("Sec: %d\n",rc);*/

  /* too many char */
  if(str[i])
    return (time_t) - 1;

  /* actively determines whether it is daylight time or not. */
  time_struct.tm_isdst = -1;

  return mktime(&time_struct);

}

/**
 * split_path:
 * splits a path 'dir/dir/dir/obj' in two strings :
 * 'dir/dir/dir' and 'obj'.
 *
 * \param in_path (in/out char *)
 * \param p_path (out char **)
 * \param p_file (out char **)
 */
static char STR_ROOT_PATH[] = "/";
static char CURR_PATH[] = ".";

void split_path(char *in_path, char **p_path, char **p_file)
{

  size_t len, index;

  /* sanity check */
  if(!in_path || !p_path || !p_file)
    return;

  len = strlen(in_path);

  /* If the length is not 1 and the last char is '/' we remove it. */

  while((len > 1) && (in_path[len - 1] == '/'))
    {
      in_path[len - 1] = '\0';
      len--;
    }

  /* Now, we look for the last '/', if any. */

  index = len - 1;
  while((index > 0) && (in_path[index] != '/'))
    {
      index--;
    }

  /* possible cases :
   * /toto
   * xxx/toto
   * toto
   */

  if((index == 0) && (in_path[index] == '/'))
    {

      /* '/' is the first char */
      *p_path = STR_ROOT_PATH;
      *p_file = in_path + index + 1;
      return;

    }
  else if(in_path[index] == '/')
    {

      in_path[index] = '\0';
      *p_path = in_path;
      *p_file = in_path + index + 1;
      return;

    }
  else if(index == 0)
    {

      /* no '/' found */
      *p_path = CURR_PATH;
      *p_file = in_path;
      return;

    }

}

/* Time formatting routine */
char *time2str(time_t time_in, char *str_out)
{

#define TIME_STRLEN 30

  struct tm paramtm;
  time_t now;                   /* Now  */
  time_t jan_1;                 /* 01/01 of the current year */

  /*inits 'jan_1' for date printing */
  time(&now);
  Localtime_r(&now, &paramtm);
  paramtm.tm_mon = 0;
  paramtm.tm_mday = 1;
  paramtm.tm_hour = 1;
  paramtm.tm_min = 0;
  paramtm.tm_sec = 1;
  jan_1 = mktime(&paramtm);

  if(time_in < jan_1)
    {                           /* if dates back to last year : MM dd YYYY */
      strftime(str_out, TIME_STRLEN, "%b %e %Y ", Localtime_r(&time_in, &paramtm));
    }
  else
    {                           /* MM dd hh:mm */
      strftime(str_out, TIME_STRLEN, "%b %e %R", Localtime_r(&time_in, &paramtm));
    }
  return str_out;

}

/**
 * clean_path:
 * Transform a path to a cannonical path.
 * Remove //,  /./,  /../, final /
 * from a POSIX-like path.
 *
 * \param str (in/out char*) The path to be transformed.
 * \param len (in int)       The max path length.
 * \return Nothing.
 */
void clean_path(char *str, int len)
{

  int indexsrc = 0;
  int indexdest = 0;

  int length;

  char *sdd_index;              /* "slash dot dot" index */
  char *slash_index;            /* previous slash index */

  /* removes double slashes */
    /**************************/
  while(str[indexsrc] && (indexsrc + 1 < len))
    {
      while((indexsrc + 1 < len) && (str[indexsrc] == '/') && (str[indexsrc + 1] == '/'))
        indexsrc++;
      str[indexdest++] = str[indexsrc++];
    }
  if(!str[indexsrc])
    str[indexdest] = str[indexsrc];

  /* removes '/./' and '/.\0' */
    /****************************/

  /* if the path ends with /., we add a slash at the end,
     so '/./' will be detected in the next loop. */
#ifdef _TOTO
  length = strlen(str);
  if(length >= 2)
    {
      if((str[length - 1] == '.') && (str[length - 2] == '/'))
        {
          str[length] = '/';
          str[length + 1] = '\0';
        }
    }
#endif

  /* detects and removes '/./' */

  sdd_index = (char *)strstr(str, "/./");
  while(sdd_index)
    {

      /* we copy everything after "/./" to sdd_index */
      indexsrc = 3;             /* index in sdd_index */
      indexdest = 1;            /* index in sdd_index */
      /* BUG: I'm pretty sure this can't be right???? */
      while((sdd_index[indexdest] = sdd_index[indexsrc]))
        {
          indexdest++;
          indexsrc++;
        }

      /* inits the next loop */
      sdd_index = (char *)strstr(str, "/./");
    }

  /* removes '/../' and '/..\0' */
    /******************************/

  /* if the path ends with /.., we add a slash at the end,
     so '/../' will be detected in the next loop. */
  length = strlen(str);
  if(length >= 3)
    {
      if((str[length - 1] == '.') && (str[length - 2] == '.') && (str[length - 3] == '/'))
        {
          str[length] = '/';
          str[length + 1] = '\0';
        }
    }

  /* detects and removes '/../' */

  sdd_index = (char *)strstr(str, "/../");

  while(sdd_index)
    {

      /* look for the first '/' that preceeds sdd_index */
      for(slash_index = sdd_index - 1; (slash_index >= str) && (slash_index[0] != '/');
          slash_index--) ;

      /* if found, removes rep/../ path */

      if((slash_index[0] == '/') && (slash_index >= str))
        {

          /* we copy everything after "/../" to slash_index */
          indexsrc = 4;         /* index in sdd_index */
          indexdest = 1;        /* index in slash_index */
          while((slash_index[indexdest] = sdd_index[indexsrc]))
            {
              indexdest++;
              indexsrc++;
            }

        }
      else
        {

          /* if not found, it is '..' on the root directory. */

          /* If the path begins with a filehandle,
             we replace @handle/../ by @handle/..> */

          if(str[0] == '@')
            {

              sdd_index[3] = '>';

            }
          else
            {
              /* Else , we remove '/..' */

              indexsrc = 3;     /* index in str */
              indexdest = 0;    /* index in str */
              while((str[indexdest] = str[indexsrc]))
                {
                  indexdest++;
                  indexsrc++;
                }

            }                   /* end @ */

        }
      /* inits the next loop */
      sdd_index = (char *)strstr(str, "/../");
    }

  /* removes final slash */
    /***********************/
  length = strlen(str);
  if(length > 1)
    {
      if(str[length - 1] == '/')
        str[length - 1] = '\0';
    }

}

/**
 * print_fsal_status:
 * this function prints an fsal_status_t to a given output file.
 *
 * \param output (in FILE *) The output where the status is to be printed.
 * \param status (in status) The status to be printed.
 *
 * \return Nothing.
 */
void print_fsal_status(FILE * output, fsal_status_t status)
{

  char _str_[256];

#ifdef _USE_GHOSTFS

  log_snprintf(_str_, 256, "%J%r,%J%r",
               ERR_FSAL, status.major, ERR_GHOSTFS, status.minor);

#else

  log_snprintf(_str_, 256, "%J%r, filesystem status: %d",
               ERR_FSAL, status.major, status.minor);

#endif

  fprintf(output, "%s", _str_);
}

/**
 * fsal_status_to_string:
 * this function converts an fsal_status_t to a a string buffer
 *
 * \param output (in char *) The output where the status is to be printed.
 * \param status (in status) The status to be printed.
 *
 * \return Nothing.
 */
void fsal_status_to_string(char * output, fsal_status_t status)
{

#ifdef _USE_GHOSTFS

  log_snprintf(output, sizeof(output), "%J%r,%J%r",
               ERR_FSAL, status.major, ERR_GHOSTFS, status.minor);

#else

  log_snprintf(output, sizeof(output), "%J%r, filesystem status: %d",
               ERR_FSAL, status.major, status.minor);

#endif
}


/**
 * print_fsal_attrib_mask:
 * Print an fsal_attrib_mask_t to a given output file.
 *
 * \param mask (in fsal_attrib_mask_t) The mask to be printed.
 * \param output (in FILE *) The output where the mask is to be printed.
 * \return Nothing.
 */
void print_fsal_attrib_mask(fsal_attrib_mask_t mask, FILE * output)
{

  if(FSAL_TEST_MASK(mask, FSAL_ATTR_SUPPATTR))
    fprintf(output, "\tFSAL_ATTR_SUPPATTR\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_TYPE))
    fprintf(output, "\tFSAL_ATTR_TYPE\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_SIZE))
    fprintf(output, "\tFSAL_ATTR_SIZE\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_FSID))
    fprintf(output, "\tFSAL_ATTR_FSID\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_ACL))
    fprintf(output, "\tFSAL_ATTR_ACL \n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_FILEID))
    fprintf(output, "\tFSAL_ATTR_FILEID\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_MODE))
    fprintf(output, "\tFSAL_ATTR_MODE\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_NUMLINKS))
    fprintf(output, "\tFSAL_ATTR_NUMLINKS\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_OWNER))
    fprintf(output, "\tFSAL_ATTR_OWNER\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_GROUP))
    fprintf(output, "\tFSAL_ATTR_GROUP\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_RAWDEV))
    fprintf(output, "\tFSAL_ATTR_RAWDEV\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_ATIME))
    fprintf(output, "\tFSAL_ATTR_ATIME\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_CREATION))
    fprintf(output, "\tFSAL_ATTR_CREATION\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_CTIME))
    fprintf(output, "\tFSAL_ATTR_CTIME\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_MTIME))
    fprintf(output, "\tFSAL_ATTR_MTIME\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_SPACEUSED))
    fprintf(output, "\tFSAL_ATTR_SPACEUSED\n");
  if(FSAL_TEST_MASK(mask, FSAL_ATTR_MOUNTFILEID))
    fprintf(output, "\tFSAL_ATTR_MOUNTFILEID\n");

}

/**
 * strtype:
 * convert an FSAL object type to a string.
 *
 * \param type (in fsal_nodetype_t) The type to be printed.
 * \return The name (char *) for this FSAL object type.
 */
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
      return "FSAL_TYPE_JUNCTION  ";
    default:
      return "Unknown type   ";
    }
}

/**
 * print_fsal_attributes:
 * print an fsal_attrib_list_t to a given output file.
 *
 * \param attrs (in fsal_attrib_list_t) The attributes to be printed.
 * \param output (in FILE *) The file where the attributes are to be printed.
 * \return Nothing.
 */
void print_fsal_attributes(fsal_attrib_list_t attrs, FILE * output)
{

  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_TYPE))
    fprintf(output, "\tType : %s\n", strtype(attrs.type));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_SIZE))
    fprintf(output, "\tSize : %llu\n", attrs.filesize);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_FSID))
    fprintf(output, "\tfsId : %llu.%llu\n", attrs.fsid.major, attrs.fsid.minor);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ACL))
    fprintf(output, "\tACL List : (printing not implemented)\n");
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_FILEID))
    fprintf(output, "\tFileId : %#llx\n", attrs.fileid);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MODE))
    fprintf(output, "\tMode : %#o\n", attrs.mode);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_NUMLINKS))
    fprintf(output, "\tNumlinks : %u\n", (unsigned int)attrs.numlinks);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_OWNER))
    fprintf(output, "\tuid : %d\n", attrs.owner);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_GROUP))
    fprintf(output, "\tgid : %d\n", attrs.group);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_RAWDEV))
    fprintf(output, "\tRawdev ...\n");
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_ATIME))
    fprintf(output, "\tatime : %s", ctime((time_t *) & attrs.atime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_CREATION))
    fprintf(output, "\tcreation time : %s", ctime((time_t *) & attrs.creation.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_CTIME))
    fprintf(output, "\tctime : %s", ctime((time_t *) & attrs.ctime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MTIME))
    fprintf(output, "\tmtime : %s", ctime((time_t *) & attrs.mtime.seconds));
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_SPACEUSED))
    fprintf(output, "\tspaceused : %llu\n", attrs.spaceused);
  if(FSAL_TEST_MASK(attrs.asked_attributes, FSAL_ATTR_MOUNTFILEID))
    fprintf(output, "\tmounted_on_fileid : %#llx\n", attrs.mounted_on_fileid);

}

/**
 * print_item_line:
 * Prints a filesystem element on one line, like the Unix ls command.
 *
 * \param out (in FILE*) The file where the item is to be printed.
 * \param attrib (in fsal_attrib_list_t *) the attributes for the
 *        item.
 * \param name (in char *) The name of the item to be printed.
 * \param target (in char *) It the item is a symbolic link,
 *        this contains the link target.
 * \return Nothing.
 */
#define print_mask(_out,_mode,_mask,_lettre) do {    \
        if (_mode & _mask) fprintf(_out,_lettre);\
        else fprintf(_out,"-");                  \
      } while(0)

void print_item_line(FILE * out, fsal_attrib_list_t * attrib, char *name, char *target)
{

  char buff[256];

  if(FSAL_TEST_MASK(attrib->asked_attributes, FSAL_ATTR_FILEID))
    {
      /* print inode */
      fprintf(out, "%10llx ", attrib->fileid);
    }

  /* printing type (mandatory) */
  switch (attrib->type)
    {
    case FSAL_TYPE_FIFO:
      fprintf(out, "p");
      break;
    case FSAL_TYPE_CHR:
      fprintf(out, "c");
      break;
    case FSAL_TYPE_DIR:
      fprintf(out, "d");
      break;
    case FSAL_TYPE_BLK:
      fprintf(out, "b");
      break;
    case FSAL_TYPE_FILE:
      fprintf(out, "-");
      break;
    case FSAL_TYPE_LNK:
      fprintf(out, "l");
      break;
    case FSAL_TYPE_JUNCTION:
      fprintf(out, "j");
      break;
    default:
      fprintf(out, "?");
    }

  if(FSAL_TEST_MASK(attrib->asked_attributes, FSAL_ATTR_MODE))
    {

      /* printing rights */
      print_mask(out, attrib->mode, FSAL_MODE_RUSR, "r");
      print_mask(out, attrib->mode, FSAL_MODE_WUSR, "w");

      if(attrib->mode & FSAL_MODE_SUID)
        {
          if(attrib->mode & FSAL_MODE_XUSR)
            fprintf(out, "s");
          else
            fprintf(out, "S");
        }
      else
        {
          if(attrib->mode & FSAL_MODE_XUSR)
            fprintf(out, "x");
          else
            fprintf(out, "-");
        }

      print_mask(out, attrib->mode, FSAL_MODE_RGRP, "r");
      print_mask(out, attrib->mode, FSAL_MODE_WGRP, "w");

      if(attrib->mode & FSAL_MODE_SGID)
        {
          if(attrib->mode & FSAL_MODE_XGRP)
            fprintf(out, "s");
          else
            fprintf(out, "l");
        }
      else
        {
          if(attrib->mode & FSAL_MODE_XGRP)
            fprintf(out, "x");
          else
            fprintf(out, "-");
        }
      print_mask(out, attrib->mode, FSAL_MODE_ROTH, "r");
      print_mask(out, attrib->mode, FSAL_MODE_WOTH, "w");
      print_mask(out, attrib->mode, FSAL_MODE_XOTH, "x");
    }

  if(FSAL_TEST_MASK(attrib->asked_attributes, FSAL_ATTR_NUMLINKS))
    {
      /* print linkcount */
      fprintf(out, " %3u", (unsigned int)attrib->numlinks);
    }

  if(FSAL_TEST_MASK(attrib->asked_attributes, FSAL_ATTR_OWNER))
    {
      /* print uid */
      fprintf(out, " %8d", attrib->owner);
    }

  if(FSAL_TEST_MASK(attrib->asked_attributes, FSAL_ATTR_GROUP))
    {
      /* print gid */
      fprintf(out, " %8d", attrib->group);
    }

  if(FSAL_TEST_MASK(attrib->asked_attributes, FSAL_ATTR_SIZE))
    {
      /* print size */
      fprintf(out, " %15llu", attrib->filesize);
    }

  if(FSAL_TEST_MASK(attrib->asked_attributes, FSAL_ATTR_MTIME))
    {
      /* print mtime */
      fprintf(out, " %15s", time2str(attrib->mtime.seconds, buff));
    }

  /* print name */
  fprintf(out, " %s", name);

  if(attrib->type == FSAL_TYPE_LNK)
    fprintf(out, " -> %s", target);

  fprintf(out, "\n");
  return;

}

/**
 * this function converts peers (attr_name=attr_value,attr_name=attr_value,...)
 * to a fsal_attrib_list_t to be used in the FSAL_setattr call).
 * \return 0 if no error occured,
 *         a non null value else.
 */
int MkFSALSetAttrStruct(char *attribute_list, fsal_attrib_list_t * fsal_set_attr_struct)
{
  shell_attribute_t *current_attr;
  char attrib_list_tmp[2048];

  char *attrib_str;
  char *value_str;
  char *next_str = NULL;

  int rc;

  int param_32;
  unsigned long long param_64;
  time_t param_time;

  int *p_32;
  unsigned long long *p_64;
  time_t *p_time;

  /* sanity checks */

  if(!attribute_list || !fsal_set_attr_struct)
    return EFAULT;

  /* init output struct */

  memset(fsal_set_attr_struct, 0, sizeof(fsal_attrib_list_t));

  /* set attribute mask */

  FSAL_CLEAR_MASK(fsal_set_attr_struct->asked_attributes);

  /* temporary copy the attribute list */
  strncpy(attrib_list_tmp, attribute_list, 2048);
  attrib_list_tmp[2047] = '\0';

  /* get the first token */
  attrib_str = strtok_r(attrib_list_tmp, ",", &next_str);

  if(attrib_str == NULL)
    return EINVAL;

  while(attrib_str != NULL)
    {
      /* retrieving attribute value */
      attrib_str = strtok_r(attrib_str, "=", &value_str);

      if((attrib_str == NULL) || (value_str == NULL))
        return EINVAL;

      printf("Attribute: \"%s\", Value: \"%s\"\n", attrib_str, value_str);

      /* look for the attribute to be set. */

      for(current_attr = shell_attr_list;
          current_attr->attr_type != ATTR_NONE; current_attr++)
        {

          if(!strcasecmp(current_attr->attr_name, attrib_str))
            {

              /* exists loop */
              break;
            }

        }

      /* attribute not found */

      if(current_attr->attr_type == ATTR_NONE)
        return ENOENT;

      FSAL_SET_MASK(fsal_set_attr_struct->asked_attributes, current_attr->attr_mask);

      /* convert the attribute value to the correct type */

      switch (current_attr->attr_type)
        {
        case ATTR_32:

          param_32 = my_atoi(value_str);
          if(param_32 == -1)
            return EINVAL;
          p_32 = (int *)((caddr_t) fsal_set_attr_struct + current_attr->attr_offset);

          *p_32 = param_32;

          break;

        case ATTR_64:

          rc = ato64(value_str, &param_64);
          if(rc == -1)
            return EINVAL;
          p_64 =
              (unsigned long long *)((caddr_t) fsal_set_attr_struct +
                                     current_attr->attr_offset);

          *p_64 = param_64;

          break;

        case ATTR_OCTAL:       /* only for modes */

          param_32 = atomode(value_str);
          if(param_32 == -1)
            return EINVAL;
          p_32 = (int *)((caddr_t) fsal_set_attr_struct + current_attr->attr_offset);

          *p_32 = unix2fsal_mode(param_32);

          break;

        case ATTR_TIME:

          param_time = atotime(value_str);
          if(param_time == (time_t) - 1)
            return EINVAL;
          p_time =
              (time_t *) ((caddr_t) fsal_set_attr_struct + current_attr->attr_offset);

          *p_time = param_time;

          break;
        default:
          break;
        }

      /* now process the next attribute */

      attrib_str = next_str;

      next_str = NULL;          /* paranoid setting */
      value_str = NULL;         /* paranoid setting */

      if(attrib_str != NULL)
        attrib_str = strtok_r(attrib_str, ",", &next_str);

    }

  /* OK */
  return 0;

}

/* timer diffing function */

struct timeval time_diff(struct timeval time_from, struct timeval time_to)
{

  struct timeval result;

  if(time_to.tv_usec < time_from.tv_usec)
    {
      result.tv_sec = time_to.tv_sec - time_from.tv_sec - 1;
      result.tv_usec = 1000000 + time_to.tv_usec - time_from.tv_usec;
    }
  else
    {
      result.tv_sec = time_to.tv_sec - time_from.tv_sec;
      result.tv_usec = time_to.tv_usec - time_from.tv_usec;
    }

  return result;

}

/* used for concatenation in set */

char *concat(char *str1, char *str2, size_t max_len)
{
  size_t len1, len2;

  len1 = strlen(str1);
  len2 = strlen(str2);

  if(len1 + len2 + 1 > max_len)
    return NULL;

  return strcat(str1, str2);

}

/* Inspired from Free Software Foundation code. */
int getugroups(int maxcount, gid_t * grouplist, char *username, gid_t gid)
{
  struct group *grp;
  register char **cp;
  register int count = 0;

  if(gid != (gid_t) - 1)
    {
      if(maxcount != 0)
        grouplist[count] = gid;

      count++;
    }

  setgrent();
  while((grp = getgrent()) != 0)
    {
      for(cp = grp->gr_mem; *cp; ++cp)
        {
          int n;

          if(strcmp(username, *cp))
            continue;

          /* see if this group number is already in the list */
          for(n = 0; n < count; ++n)
            if(grouplist && grouplist[n] == grp->gr_gid)
              break;

          /* add the group to the list */
          if(n == count)
            {
              if(maxcount != 0)
                {
                  if(count >= maxcount)
                    {
                      endgrent();
                      return count;
                    }
                  grouplist[count] = grp->gr_gid;

                }
              count++;
            }

        }
    }
  endgrent();

  return count;
}
