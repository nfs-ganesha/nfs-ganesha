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
 * \file    shell_utils.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/17 14:56:22 $
 * \version $Revision: 1.8 $
 * \brief   Miscellaneous build-in commands for shell.
 *
 *
 * $Log: shell_utils.c,v $
 * Revision 1.8  2006/01/17 14:56:22  leibovic
 * Adaptation de HPSS 6.2.
 *
 * Revision 1.6  2005/10/10 12:39:08  leibovic
 * Using mnt/nfs free functions.
 *
 * Revision 1.5  2005/08/02 09:40:28  leibovic
 * stream error in diff.
 *
 * Revision 1.4  2005/05/11 15:53:37  leibovic
 * Adding time function.
 *
 * Revision 1.3  2005/05/11 07:25:45  leibovic
 * adding chomp util.
 *
 * Revision 1.2  2005/05/09 14:54:43  leibovic
 * Adding eq and ne.
 *
 * Revision 1.1  2005/05/09 12:23:55  leibovic
 * Version 2 of ganeshell.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>

#include "cmd_tools.h"
#include <errno.h>
#include "shell_utils.h"
#include "Getopt.h"
#include <string.h>

#include "stuff_alloc.h"

/* For mallinfo */
#ifdef _LINUX
#include <malloc.h>
#endif

#if( defined(  _APPLE ) && !defined( _FREEBSD )  )
#include <malloc/malloc.h>
#endif

#ifdef _SOLARIS
#include <malloc.h>
#endif

#include <sys/time.h>

/*--------------------------
 *    Timer management.
 *-------------------------*/

/* start time */
static struct timeval timer_start = { 0, 0 };

/* stop time */
static struct timeval timer_end = { 0, 0 };

/* timer state (0=OFF, 1=ON) */
static int timer_state = 0;

/* The timer command */

int util_timer(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    )
{

  if(argc != 2)
    {
      fprintf(output, "Usage: %s start|print|stop.\n", argv[0]);
      return -1;
    }

  /* Timer start command */

  if(!strcmp(argv[1], "start"))
    {

      if(timer_state)
        {
          fprintf(output, "Timer already started.\n");
          return -1;
        }

      if(gettimeofday(&timer_start, NULL) == -1)
        {
          fprintf(output, "Error retrieving system time.\n");
          return -1;
        }

      fprintf(output, "Timer start time: ");
      print_timeval(output, timer_start);

      timer_state = 1;
      return 0;
    }

  /* Timer stop command */

  if(!strcmp(argv[1], "stop"))
    {

      if(!timer_state)
        {
          fprintf(output, "Timer is not started.\n");
          return -1;
        }

      if(gettimeofday(&timer_end, NULL) == -1)
        {
          fprintf(output, "Error retrieving system time.\n");
          return -1;
        }

      fprintf(output, "Timer stop time: ");
      print_timeval(output, timer_end);

      timer_state = 0;
      return 0;
    }

  /* Timer print command */

  if(!strcmp(argv[1], "print"))
    {

      if(timer_state)
        {

          struct timeval timer_tmp;

          /* timer is running, print current enlapsed time */
          if(gettimeofday(&timer_tmp, NULL) == -1)
            {
              fprintf(output, "Error retrieving system time.\n");
              return -1;
            }

          timer_tmp = time_diff(timer_start, timer_tmp);
          print_timeval(output, timer_tmp);

        }
      else
        {

          struct timeval timer_tmp;

          timer_tmp = time_diff(timer_start, timer_end);
          print_timeval(output, timer_tmp);

        }
      return 0;
    }

  /* unknown timer command */
  fprintf(output, "Usage: %s start|print|stop.\n", argv[0]);
  return -1;

}                               /* util_timer */

/*--------------------------
 *      System utils.
 *-------------------------*/

int util_sleep(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    )
{

  unsigned long sleep_time;
  int rc;

  if(argc != 2)
    {
      fprintf(output, "Usage: %s <int value>\n", argv[0]);
      return -1;
    }

  /* conversion */
  rc = my_atoi(argv[1]);

  if(rc < 0)
    {
      fprintf(output, "Usage: %s <int value> (%s is not a positive integer)\n", argv[0],
              argv[1]);
      return -1;
    }

  sleep_time = (unsigned long)rc;

  fprintf(output, "sleep: suspending execution for %lu s...\n", sleep_time);

  /* sleeping */

  sleep(sleep_time);

  return 0;

}

int util_shell(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    )
{

  FILE *cmd_output;
  int i;
  char command_line[1024] = "";
  char buffer[1024];

  if(argc < 2)
    {
      fprintf(output, "Usage: %s <shell_cmd> [arg1 arg2 ...]\n", argv[0]);
      return -1;
    }

  /* builds the command line */

  for(i = 1; i < argc; i++)
    {
      strncat(command_line, argv[i], strlen(command_line) - 1024 - 1);
      if(i != argc - 1)
        strncat(command_line, " ", strlen(command_line) - 1024 - 1);
    }

  /* launch the command */

  cmd_output = popen(command_line, "r");

  if(cmd_output == NULL)
    {
      fprintf(output, "shell: popen error %d\n", errno);
      return -1;
    }

  /* copy the shell ouput to the command output stream */

  while(fgets(buffer, sizeof(buffer), cmd_output) != NULL)
    {
      fputs(buffer, output);
    }

  /* get returned status */

  return pclose(cmd_output);

}                               /* util_shell */

int util_meminfo(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    )
{
#if( !defined( _APPLE ) && !defined( _SOLARIS ) )

#ifndef _NO_BUDDY_SYSTEM
  buddy_stats_t bstats;
#endif

  /* standard mallinfo */

  struct mallinfo meminfo = mallinfo();

  fprintf(output, "Mallinfo:\n");

  fprintf(output, "   Total space in arena: %lu\n", (unsigned long)meminfo.arena);
  fprintf(output, "   Number of ordinary blocks: %d\n", meminfo.ordblks);
  fprintf(output, "   Number of small blocks: %d\n", meminfo.smblks);
  fprintf(output, "   Number of holding blocks: %d\n", meminfo.hblks);
  fprintf(output, "   Space in holding block headers: %d\n", meminfo.hblkhd);
  fprintf(output, "   Space in small blocks in use: %lu\n",
          (unsigned long)meminfo.usmblks);
  fprintf(output, "   Space in free small blocks: %lu\n", (unsigned long)meminfo.fsmblks);
  fprintf(output, "   Space in ordinary blocks in use: %lu\n",
          (unsigned long)meminfo.uordblks);
  fprintf(output, "   Space in free ordinary blocks: %lu\n",
          (unsigned long)meminfo.fordblks);
  fprintf(output, "   Cost of enabling keep option: %d\n", meminfo.keepcost);
  fprintf(output, "\n");

#ifndef _NO_BUDDY_SYSTEM

  BuddyGetStats(&bstats);

  fprintf(output, "Buddy info (thread %p):\n", (caddr_t) pthread_self());
  /* Buddy sytem info */

  fprintf(output, "Total Space in Arena: %lu  (Watermark: %lu)\n",
          (unsigned long)bstats.TotalMemSpace, (unsigned long)bstats.WM_TotalMemSpace);
  fprintf(output, "\n");

  fprintf(output, "Total Space for Standard Pages: %lu  (Watermark: %lu)\n",
          (unsigned long)bstats.StdMemSpace, (unsigned long)bstats.WM_StdMemSpace);

  fprintf(output, "      Nb Standard Pages: %lu\n", (unsigned long)bstats.NbStdPages);

  fprintf(output, "      Size of Std Pages: %lu\n", (unsigned long)bstats.StdPageSize);

  fprintf(output, "      Space Used inside Std Pages: %lu  (Watermark: %lu)\n",
          (unsigned long)bstats.StdUsedSpace, (unsigned long)bstats.WM_StdUsedSpace);

  fprintf(output, "      Nb of Std Pages Used: %lu  (Watermark: %lu)\n",
          (unsigned long)bstats.NbStdUsed, (unsigned long)bstats.WM_NbStdUsed);

  if(bstats.NbStdUsed > 0)
    {
      fprintf(output, "      Memory Fragmentation: %.2f %%\n",
              100.0 -
              (100.0 * bstats.StdUsedSpace /
               (1.0 * bstats.NbStdUsed * bstats.StdPageSize)));
    }

  fprintf(output, "\n");

#endif

#endif                          /* _APPLE */
  return 0;

}                               /* util_meminfo */

/*----------------------
 *    String utils.
 *----------------------*/

int util_cmp(int argc,          /* IN : number of args in argv */
             char **argv,       /* IN : arg list               */
             FILE * output      /* IN : output stream          */
    )
{

  static char *format = "hinv";
  static char *help_cmp =
      "Usage: %s [ -h | -i | -n | -v ]  <expr1> <expr2>\n"
      "     -h: print this help\n"
      "     -i: case insensitive comparison\n"
      "     -n: numerical comparison\n" "     -v: verbose mode\n";

  int err_flag = 0;             /* error parsing options */
  int flag_h = 0;               /* help */
  int flag_i = 0;               /* case insensitive compare */
  int flag_n = 0;               /* numerical compare */
  int flag_v = 0;               /* verbose */

  int option, rc = 0;

  char *str1 = NULL;            /* arg1 */
  char *str2 = NULL;            /* arg2 */

  /* the value to been returned, according to argv[0] */
  int value_if_equal = FALSE;

  if(!strcmp(argv[0], "eq"))
    value_if_equal = TRUE;
  else if(!strcmp(argv[0], "ne"))
    value_if_equal = FALSE;
  else if(!strcmp(argv[0], "cmp"))
    value_if_equal = FALSE;
  /* unexpected !!! */
  else
    exit(1);

  /* disables Getopt error message */
  Opterr = 0;

  /* reinits Getopt processing */
  Optind = 1;

  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'h':
          if(flag_h)
            fprintf(output,
                    "%s: warning: option 'h' has been specified more than once.\n",
                    argv[0]);
          else
            flag_h++;
          break;

        case 'i':
          if(flag_i)
            fprintf(output,
                    "%s: warning: option 'i' has been specified more than once.\n",
                    argv[0]);
          else
            flag_i++;
          break;

        case 'n':
          if(flag_n)
            fprintf(output,
                    "%s: warning: option 'n' has been specified more than once.\n",
                    argv[0]);
          else
            flag_n++;
          break;

        case 'v':
          if(flag_v)
            fprintf(output,
                    "%s: warning: option 'v' has been specified more than once.\n",
                    argv[0]);
          else
            flag_v++;
          break;

        case '?':
          fprintf(output, "%s: unknown option : %c\n", argv[0], Optopt);
          err_flag++;
          break;
        }
    }

  /* help flag */
  if(flag_h)
    {
      fprintf(output, help_cmp, argv[0]);
      return -1;
    }

  /* check conflicts */
  if(flag_i + flag_n > 1)
    {
      fprintf(output, "%s: conflict between options -i, -n\n", argv[0]);
      err_flag++;
    }

  /* check arg number */
  if(Optind != (argc - 2))
    {
      /* too much or not enough arguments */
      err_flag++;
    }
  else
    {
      str1 = argv[Optind];
      str2 = argv[Optind + 1];
    }

  /* error */
  if(err_flag)
    {
      fprintf(output, help_cmp, argv[0]);
      return -1;
    }

  if(!flag_i && !flag_n)
    {
      /* normal comparison */
      rc = strcmp(str1, str2);
    }
  else if(flag_i)
    {
      /* case insensitive comparison */
      rc = strcasecmp(str1, str2);
    }
  else if(flag_n)
    {
      int a, b;

      if(str1[0] == '-')
        a = my_atoi(str1 + 1);
      else
        a = my_atoi(str1);

      if(a < 0)
        {
          fprintf(output, "cmp: invalid integer value %s\n", str1);
          return -1;
        }

      if(str1[0] == '-')
        a = -a;

      if(str2[0] == '-')
        b = my_atoi(str2 + 1);
      else
        b = my_atoi(str2);

      if(b < 0)
        {
          fprintf(output, "cmp: invalid integer value %s\n", str2);
          return -1;
        }

      if(str2[0] == '-')
        b = -b;

      rc = b - a;

    }

  /* return values */

  if(rc == 0)
    {
      if(flag_v)
        fprintf(output, "arg1 = arg2\n");

      return value_if_equal;
    }
  else
    {
      if(flag_v)
        fprintf(output, "arg1 <> arg2\n");

      return (!value_if_equal);
    }

}                               /* util_cmp */

/* diff 2 strings line by line */

static void diff(FILE * output, char *str1, char *str2)
{

  /* current lines,chars */
  char *str_line1;
  char *char1;
  char *str_line2;
  char *char2;

  str_line1 = str1;
  str_line2 = str2;

  do
    {

      char1 = str_line1;
      char2 = str_line2;

      while(*char1 == *char2)
        {
          if((*char1 == '\0') || (*char1 == '\n'))
            break;

          char1++;
          char2++;
        }

      /* different ? */
      if(*char1 != *char2)
        {

          /* prints from the beggining of the line to the end */
          if(*str_line1)
            fprintf(output, "\t<- ");

          while((*str_line1) && (*str_line1 != '\n'))
            {
              putc(*str_line1, output);
              str_line1++;
            }

          /* skip the final \n  */
          if(*str_line1)
            str_line1++;

          if(*str_line2)
            fprintf(output, "\n\t-> ");

          while((*str_line2) && (*str_line2 != '\n'))
            {
              putc(*str_line2, output);
              str_line2++;
            }
          /* skip the final \n  */
          if(*str_line2)
            str_line2++;

          putc('\n', output);

        }
      else if(*char1 == '\n')
        {
          /* end of line */
          str_line1 = char1 + 1;
          str_line2 = char2 + 1;
        }
      else
        {
          /* end of file */
          break;
        }

    }
  while(1);

  return;

}

int util_diff(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output     /* IN : output stream          */
    )
{

  if(argc != 3)
    {
      fprintf(output, "Usage: %s <expr1> <expr2>\n", argv[0]);
      return -1;
    }

  diff(output, argv[1], argv[2]);

  return 0;

}

/* counts the number of char and lines in a string.*/
static void wc(FILE * output, char *str)
{

  int nb_char = 0;
  int nb_NL = 0;
  char *curr = str;

  while(*curr)
    {
      nb_char++;
      if(*curr == '\n')
        nb_NL++;
      curr++;
    }

  fprintf(output, "%d %d\n", nb_char, nb_NL);

  return;
}

int util_wc(int argc,           /* IN : number of args in argv */
            char **argv,        /* IN : arg list               */
            FILE * output       /* IN : output stream          */
    )
{

  if(argc != 2)
    {
      fprintf(output, "Usage: %s <expr>\n", argv[0]);
      return -1;
    }

  wc(output, argv[1]);

  return 0;

}

int util_chomp(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    )
{

  int len;
  char *in;
  char *out;

  if(argc != 2)
    {
      fprintf(output, "Usage: %s <expr>\n", argv[0]);
      return -1;
    }

  in = argv[1];

  len = strlen(in);

  if(in[len - 1] == '\n')
    {

      out = (char *)Mem_Alloc(len + 1);

      if(out == NULL)
        return Mem_Errno;

      /* local copy */
      strncpy(out, in, len + 1);

      out[len - 1] = '\0';

      fprintf(output, "%s", out);

      Mem_Free(out);

    }
  else
    {
      fprintf(output, "%s", in);
    }

  return 0;

}
