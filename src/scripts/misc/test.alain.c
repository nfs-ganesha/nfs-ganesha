/* ---------------------------------
   a.out `home -p`/nom_de_repertoire
   --------------------------------- */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

main(int argc, char **argv)
{
  FILE *fic;
  int err, rc;
  char path[512], name[1024], cmde[2014];
  struct stat stbuf;
  time_t ltime;
  struct tm *tm;
  char dateheure[50];

  strcpy(path, argv[1]);
  sprintf(name, "%s/toto", path);

  err = stat(path, &stbuf);
  if (err == -1)
    {
      printf("repertoire vide : stat erreur\n");
    }
  else
    {
      printf("repertoire vide : stat OK\n");
    };

  if ((err == -1) || ((stbuf.st_mode & S_IFMT) != S_IFDIR))
    {
      printf("repertoire vide : pas un repertoire\n");
    }
  else
    {
      printf("repertoire vide : repertoire OK\n");
    };

  sprintf(cmde, "touch %s", name);      /* pour simuler la pre-existence du fichier */
  system(cmde);

  err = stat(path, &stbuf);
  if (err == -1)
    {
      printf("repertoire contenant 1 fichier : stat erreur\n");
    }
  else
    {
      printf("repertoire contenant 1 fichier : stat OK\n");
    };

  if ((err == -1) || ((stbuf.st_mode & S_IFMT) != S_IFDIR))
    {
      printf("repertoire contenant 1 fichier : pas un repertoire\n");
    }
  else
    {
      printf("repertoire contenant 1 fichier : repertoire OK\n");
    };

  unlink(name);

  rc = -1;
  while (rc != 0)
    {
      err = stat(path, &stbuf);
      if (err == -1)
        {
          printf("apres destruction du fichier : stat erreur\n");
        }
      else
        {
          printf("apres destruction du fichier : stat OK\n");
        };

      if ((err == -1) || ((stbuf.st_mode & S_IFMT) != S_IFDIR))
        {
          printf("apres destruction du fichier : pas un repertoire\n");
        }
      else
        {
          printf("apres destruction du fichier : repertoire OK\n");
          rc = 0;
        };

      sleep(3);
    };
}
