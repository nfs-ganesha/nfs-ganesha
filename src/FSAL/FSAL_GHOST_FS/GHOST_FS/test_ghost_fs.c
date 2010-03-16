/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
 * ---------------------------------------
 * \file     test_ghost_fs.c
 * \brief   Tests ghost fs.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "FSAL/FSAL_GHOST_FS/ghost_fs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libgen.h>

#define TRUE 1
#define FALSE 0

int is_num(char *str)
{

  int i;
  for (i = 0; str[i]; i++)
    if ((str[i] < '0') || (str[i] > '9'))
      return FALSE;

  return TRUE;

}

void Exit(int code, char *func)
{

  fprintf(stderr, "Error %d in GHOSTFS : %s\n", code, func);
  exit(code);

}

#define print_mask(_out,_mode,_mask,_lettre) do {    \
        if (_mode & _mask) fprintf(_out,_lettre);\
        else fprintf(_out,"-");                  \
      } while(0)

void print_item(FILE * out, GHOSTFS_Attrs_t * attrib, char *name, char *target)
{

  char buff[256];
  int i;

  if (!attrib || !name)
    {
      fprintf(stderr, "attrib=%p name=%p\n", attrib, name);
      exit(-1);
    }
  if ((attrib->type == GHOSTFS_LNK) && !target)
    {
      fprintf(stderr, "target=%p\n", target);
      exit(-1);
    }

  /* print inode */
  fprintf(out, "%10p ", attrib->inode);

  /* printing type */
  switch (attrib->type)
    {
    case GHOSTFS_DIR:
      fprintf(out, "d");
      break;
    case GHOSTFS_FILE:
      fprintf(out, "-");
      break;
    case GHOSTFS_LNK:
      fprintf(out, "l");
      break;
    default:
      fprintf(out, "?");
      break;
    }

  /* printing rights */
  print_mask(out, attrib->mode, 0400, "r");
  print_mask(out, attrib->mode, 0200, "w");
  print_mask(out, attrib->mode, 0100, "x");
  print_mask(out, attrib->mode, 0040, "r");
  print_mask(out, attrib->mode, 0020, "w");
  print_mask(out, attrib->mode, 0010, "x");
  print_mask(out, attrib->mode, 0004, "r");
  print_mask(out, attrib->mode, 0002, "w");
  print_mask(out, attrib->mode, 0001, "x");

  /* print linkcount */
  fprintf(out, " %3u", attrib->linkcount);

  /* print uid */
  fprintf(out, " %8d", attrib->uid);

  /* print gid */
  fprintf(out, " %8d", attrib->gid);

  /* print size */
  fprintf(out, " %15llu", attrib->size);

  /* print mtime */
#if ( defined( _AIX_4 ) ||  defined( _AIX_5 ) ||  defined( _LINUX ) )
  ctime_r(&attrib->mtime, buff);
#else
  ctime_r(&attrib->mtime, buff, 256);
#endif
  for (i = 0; (buff[i] != '\n') && (i < 255); i++) ;

  buff[i] = '\0';
  fprintf(out, " %25s", buff);

  /* print name */
  fprintf(out, " %s", name);

  if (attrib->type == GHOSTFS_LNK)
    fprintf(out, " -> %s", target);

  fprintf(out, "\n");

}

void print_dir_rec(FILE * out, GHOSTFS_handle_t dir_handle, char *fullpath,
		   unsigned int indent)
{

  char next_path[GHOSTFS_MAX_PATH];
  char link[256];
  char indent_str[80];
  dir_descriptor_t dir;
  int rc, i;
  GHOSTFS_dirent_t dirent;
  GHOSTFS_Attrs_t item_attr;

  /* build indent string */
  for (i = 0; (i < indent) && (i < 79); i++)
    indent_str[i] = ' ';
  indent_str[i] = '\0';

  /* directory name : */
  fprintf(out, "%s%s:\n", indent_str, fullpath);

  if (rc = GHOSTFS_Opendir(dir_handle, &dir))
    Exit(rc, "GHOSTFS_Opendir");

  /* read direntries */
  while (!(rc = GHOSTFS_Readdir(&dir, &dirent)))
    {

      /* indenting */
      fprintf(out, "%s", indent_str);

      /* getting attrs */
      if (rc = GHOSTFS_GetAttrs(dirent.handle, &item_attr))
	Exit(rc, "GHOSTFS_GetAttrs");

      switch (item_attr.type)
	{
	case GHOSTFS_LNK:
	  if (rc = GHOSTFS_ReadLink(dirent.handle, link, 256))
	    Exit(rc, "GHOSTFS_Readlink");
	  print_item(out, &item_attr, dirent.name, link);
	  break;
	case GHOSTFS_FILE:
	  print_item(out, &item_attr, dirent.name, NULL);
	  break;
	case GHOSTFS_DIR:
	  print_item(out, &item_attr, dirent.name, NULL);
	  if (strcmp(dirent.name, ".") && strcmp(dirent.name, ".."))
	    {
	      snprintf(next_path, GHOSTFS_MAX_PATH, "%s/%s", fullpath, dirent.name);
	      print_dir_rec(out, dirent.handle, next_path, indent + 1);
	    }
	  break;
	default:
	  print_item(out, &item_attr, dirent.name, NULL);
	}

    }

  if (rc != ERR_GHOSTFS_ENDOFDIR)
    Exit(rc, "GHOSTFS_Readdir");

  if (rc = GHOSTFS_Closedir(&dir))
    Exit(rc, "GHOSTFS_Closedir");

}

void *ls(void *out)
{				/* FILE * actually. */

  int rc;
  GHOSTFS_handle_t root_handle;
  GHOSTFS_Attrs_t root_attributes;

#ifndef _NO_BUDDY_SYSTEM
  BuddyInit(NULL);
#endif

  printf("Thread %p writing to FILE * %p\n", (caddr_t) pthread_self(), out);

  if (rc = GHOSTFS_GetRoot(&root_handle))
    Exit(rc, "GHOSTFS_GetRoot");

  /* printing root */
  if (rc = GHOSTFS_GetAttrs(root_handle, &root_attributes))
    Exit(rc, "GHOSTFS_GetAttrs");

  print_item(out, &root_attributes, "/", NULL);

  print_dir_rec(out, root_handle, "", 0);

  printf("Thread %p finished\n", (caddr_t) pthread_self());

  pthread_exit(NULL);

}

#define INIT_PTHREAD_ATTR(_attr_) do {                                     \
          pthread_attr_init(&(_attr_));                                    \
          pthread_attr_setscope(&(_attr_),PTHREAD_SCOPE_SYSTEM);           \
          pthread_attr_setdetachstate(&(_attr_),PTHREAD_CREATE_JOINABLE);  \
        } while(0)

void launch_ls(char *output1, char *output2)
{

  pthread_t workers[2];
  pthread_attr_t attrworkers[2];

  FILE *out1;
  FILE *out2;

  printf("Launching ls test -> %s %s\n", basename(output1), basename(output2));

  /* open outputs */
  if (!(out1 = fopen(output1, "w")))
    {
      perror("launch_ls");
      exit(errno);
    }
  if (!(out2 = fopen(output2, "w")))
    {
      perror("launch_ls");
      exit(errno);
    }

  /* Launch 2 threads that process
   *   ls -laiR "/"
   * and write the output to a file.
   * (tests thread safety).
   */

  INIT_PTHREAD_ATTR(attrworkers[0]);
  INIT_PTHREAD_ATTR(attrworkers[1]);

  if (pthread_create(&(workers[0]), &(attrworkers[0]), ls, (void *)out1))
    {
      printf("Error launching ls thread 1\n");
      exit(-1);
    }
  if (pthread_create(&(workers[1]), &(attrworkers[1]), ls, (void *)out2))
    {
      printf("Error launching ls thread 2\n");
      exit(-1);
    }

  /* waiting threads ending */
  pthread_join(workers[0], NULL);
  pthread_join(workers[1], NULL);

}

void usage(char *cmd)
{

  fprintf(stderr, "Usage :\n");
  fprintf(stderr, "  %s -ls <output1> <output2> \n", cmd);
  fprintf(stderr, "         launch a multi-threaded 'ls -l' on a ghost filesystem.\n");
  fprintf(stderr, "  %s -acces <path> <uid> <gid>\n", cmd);
  fprintf(stderr, "         test access on a file for a given couple (uid,gid).\n");
  fprintf(stderr, "  %s -mkdir <dir_name> <owner> <group>\n", cmd);
  fprintf(stderr, "         create a directory with the specified owner.\n");

}

GHOSTFS_handle_t Lookup(char *path)
{

  char *p_tok;
  char *p_tok_new;
  int rc;
  GHOSTFS_handle_t handle, handle_new;

  /* Looking up for path */

  if (path[0] != '/')
    {
      printf("Invalid path : %s\n", path);
      exit(-1);
    }

  if (rc = GHOSTFS_GetRoot(&handle))
    Exit(rc, "GHOSTFS_GetRoot");

  printf("Root = %p.%u\n", handle.inode, handle.magic);

  p_tok = &(path[1]);

  /* strtok with '/' */
  while (p_tok = (char *)strtok_r(p_tok, "/", &p_tok_new))
    {

      if (rc = GHOSTFS_Lookup(handle, p_tok, &handle_new))
	Exit(rc, "GHOSTFS_Lookup");

      printf("Lookup( %p.%u , '%s' ) = %p.%u\n", handle.inode, handle.magic,
	     p_tok, handle_new.inode, handle_new.magic);

      p_tok = p_tok_new;
      handle = handle_new;

    }

  return handle;

}

void launch_acces(char *path, int uid, int gid)
{

  GHOSTFS_handle_t handle;
  int rc;

  GHOSTFS_Attrs_t setting_mode_770;
  memset(&setting_mode_770, 0, sizeof(GHOSTFS_Attrs_t));

  setting_mode_770.mode = 0770;

  /* Lookup */
  handle = Lookup(path);

  /* Testing access rights */
  printf("Testing access for reading :\n");
  rc = GHOSTFS_Access(handle, GHOSTFS_TEST_READ, uid, gid);
  printf("GHOSTFS_Access returns %d\n", rc);

  printf("Testing access for writing :\n");
  rc = GHOSTFS_Access(handle, GHOSTFS_TEST_WRITE, uid, gid);
  printf("GHOSTFS_Access returns %d\n", rc);

  printf("Testing access for executing :\n");
  rc = GHOSTFS_Access(handle, GHOSTFS_TEST_EXEC, uid, gid);
  printf("GHOSTFS_Access returns %d\n", rc);

  /* changing access rights */
  printf("Setting mode 770 :\n");

  rc = GHOSTFS_SetAttrs(handle, SETATTR_MODE, setting_mode_770);
  printf("GHOSTFS_SetAttrs returns %d\n", rc);

  /* Testing access rights */
  printf("Testing access for reading :\n");
  rc = GHOSTFS_Access(handle, GHOSTFS_TEST_READ, uid, gid);
  printf("GHOSTFS_Access returns %d\n", rc);

  printf("Testing access for writing :\n");
  rc = GHOSTFS_Access(handle, GHOSTFS_TEST_WRITE, uid, gid);
  printf("GHOSTFS_Access returns %d\n", rc);

  printf("Testing access for executing :\n");
  rc = GHOSTFS_Access(handle, GHOSTFS_TEST_EXEC, uid, gid);
  printf("GHOSTFS_Access returns %d\n", rc);

}

void launch_mkdir(char *name, int uid, int gid)
{
  GHOSTFS_handle_t root_handle, new_handle, tmp_handle;
  int rc;

  /* get root handle */
  if (rc = GHOSTFS_GetRoot(&root_handle))
    Exit(rc, "GHOSTFS_GetRoot");

  if (rc = GHOSTFS_MkDir(root_handle, name, uid, gid, 0750, &new_handle, NULL))
    Exit(rc, "GHOSTFS_MkDir");

  /* filesystem content */
  printf("\nFilesystem content :\n");
  print_dir_rec(stdout, root_handle, "", 0);

  printf("\nTesting EEXIST error :\n");
  if (rc = GHOSTFS_MkDir(root_handle, name, uid, gid, 0750, &new_handle, NULL))
    printf("GHOSTFS_MkDir returned %d\n", rc);

  printf("\nCreating some subdirectories :\n");

  if (rc = GHOSTFS_MkDir(new_handle, "subdir.1", uid, gid, 0750, &tmp_handle, NULL))
    Exit(rc, "GHOSTFS_MkDir");
  if (rc = GHOSTFS_MkDir(new_handle, "subdir.2", uid, gid, 0750, &tmp_handle, NULL))
    Exit(rc, "GHOSTFS_MkDir");
  if (rc = GHOSTFS_MkDir(new_handle, "subdir.3", uid, gid, 0750, &tmp_handle, NULL))
    Exit(rc, "GHOSTFS_MkDir");

  printf("\nFilesystem content :\n");
  print_dir_rec(stdout, root_handle, "", 0);

}

static GHOSTFS_parameter_t config_ghostfs = {
  .root_mode = 0755,
  .root_owner = 0,
  .root_group = 0,
  .dot_dot_root_eq_root = 1,
  .root_access = 1
};

int main(int argc, char **argv)
{

  typedef enum action_t {
    ACTION_NULL,
    ACTION_LS,
    ACTION_ACCES,
    ACTION_MKDIR
  } action_t;

  action_t action = ACTION_NULL;

  char *output1;
  char *output2;
  char *lookup_path;
  char *str_uid;
  char *str_gid;
  int uid, gid;
  char newfile[1024];
  int rc;

  action = ACTION_NULL;

  if (argc > 1)
    {
      if (!strcmp(argv[1], "-acces"))
	action = ACTION_ACCES;
      else if (!strcmp(argv[1], "-ls"))
	action = ACTION_LS;
      else if (!strcmp(argv[1], "-mkdir"))
	action = ACTION_MKDIR;
    }

  if ((action == ACTION_ACCES || action == ACTION_MKDIR) && (argc == 5))
    {

      lookup_path = argv[2];
      str_uid = argv[3];
      str_gid = argv[4];

      if (!is_num(str_uid))
	{
	  printf("Invalid uid : %s\n", str_uid);
	  exit(-1);
	}
      uid = atoi(str_uid);

      if (!is_num(str_gid))
	{
	  printf("Invalid gid : %s\n", str_gid);
	  exit(-1);
	}
      gid = atoi(str_gid);

  } else if ((action == ACTION_LS) && (argc == 4))
    {

      output1 = argv[2];
      output2 = argv[3];

    } else
    {
      usage(basename(argv[0]));
      exit(EINVAL);
    }

#ifndef _NO_BUDDY_SYSTEM
  BuddyInit(NULL);
#endif

  /* Loads the filesystem structure */
  if (rc = GHOSTFS_Init(config_ghostfs))
    Exit(rc, "GHOSTFS_Init");

  switch (action)
    {

    case ACTION_LS:
      launch_ls(output1, output2);
      break;

    case ACTION_ACCES:
      launch_acces(lookup_path, uid, gid);
      break;

    case ACTION_MKDIR:
      launch_mkdir(lookup_path, uid, gid);
      break;
    }

  exit(0);

}
