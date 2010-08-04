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
 *
 * ensemble des fonctions d'affichage et de gestion des erreurs
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>             /* for malloc */
#include <ctype.h>              /* for isdigit */
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#include "log_functions.h"

/* La longueur d'une chaine */
#define STR_LEN_TXT      2048
#define PATH_LEN         1024
#define MAX_STR_LEN      1024
#define MAX_NUM_FAMILY  50
#define UNUSED_SLOT -1

/* P et V pour etre Djikstra-compliant */
#define P( _mutex_ ) pthread_mutex_lock( &_mutex_)
#define V( _mutex_ ) pthread_mutex_unlock( &_mutex_ )

/* constants */
static int masque_log = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

/* Array of error families */

static family_t tab_family[MAX_NUM_FAMILY];

/* Global variables */

static char nom_programme[1024];
static char nom_host[256];
static char nom_fichier_log[PATH_LEN] = "/tmp/logfile";
static int niveau_debug = 2;
static int syslog_opened = 0 ;

/*
 * Variables specifiques aux threads.
 */

typedef struct ThreadLogContext_t
{

  char nom_fonction[STR_LEN];

} ThreadLogContext_t;

/* threads keys */
static pthread_key_t thread_key;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

#ifdef _DONT_HAVE_LOCALTIME_R

/* Localtime is not reentrant...
 * So we are obliged to have a mutex for calling it.
 * pffff....
 */
static pthread_mutex_t mutex_localtime = PTHREAD_MUTEX_INITIALIZER;

/* thread-safe and PORTABLE version of localtime */

static struct tm *Localtime_r(const time_t * p_time, struct tm *p_tm)
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
#else
# define Localtime_r localtime_r
#endif

/* forward declaration for diplaying errors. */
static int DisplayError(int num_error, int status);

/* Init of pthread_keys */
static void init_keys(void)
{
  if(pthread_key_create(&thread_key, NULL) == -1)
    DisplayError(ERR_PTHREAD_KEY_CREATE, errno);

  return;
}                               /* init_keys */

/**
 * GetThreadContext :
 * manages pthread_keys.
 */
static ThreadLogContext_t *Log_GetThreadContext()
{

  ThreadLogContext_t *p_current_thread_vars;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      DisplayError(ERR_PTHREAD_ONCE, errno);
      return NULL;
    }

  p_current_thread_vars = (ThreadLogContext_t *) pthread_getspecific(thread_key);

  /* we allocate the thread key if this is the first time */
  if(p_current_thread_vars == NULL)
    {

      /* allocates thread structure */
      p_current_thread_vars = (ThreadLogContext_t *) malloc(sizeof(ThreadLogContext_t));

#ifdef _DEBUG_LOG
      printf("malloc => %p\n", p_current_thread_vars);
#endif

      if(p_current_thread_vars == NULL)
        {
          DisplayError(ERR_MALLOC, errno);
          return NULL;
        }

      /* inits thread structures */
      p_current_thread_vars->nom_fonction[0] = '\0';

      /* set the specific value */
      pthread_setspecific(thread_key, (void *)p_current_thread_vars);

    }

  return p_current_thread_vars;

}                               /* GetThreadContext */

/* Affichage d'erreurs */
static int DisplayError(int num_error, int status)
{

  return fprintf(stderr, "Error %s status %d ==> %s\n", tab_systeme_err[num_error].label,
                 status, strerror(status));

}                               /* DisplayError */

/*
 * Fait la conversion nom du niveau de log
 * en ascii vers valeur numerique du niveau
 *
 */

int ReturnLevelAscii(char *LevelEnAscii)
{
  int i = 0;

  for(i = 0; i < NB_LOG_LEVEL; i++)
    if(!strcmp(tabLogLevel[i].str, LevelEnAscii))
      return tabLogLevel[i].value;

  /* Si rien n'est trouve on retourne -1 */
  return -1;
}                               /* ReturnLevelAscii */

char *ReturnLevelInt(int level)
{
  int i = 0;

  for(i = 0; i < NB_LOG_LEVEL; i++)
    if(tabLogLevel[i].value == level)
      return tabLogLevel[i].str;

  /* Si on n'a rien trouve on retourne NULL */
  return NULL;
}                               /* ReturnLevelInt */

/*
 * Set le nom du programme en cours.
 */
int SetNamePgm(char *nom)
{

  /* Cette fonction n'est pas thread-safe car le nom du programme
   * est commun a tous les threads */
  strcpy(nom_programme, nom);

  return 1;
}                               /* SetNamePgm */

/*
 * Set le nom d'host en cours 
 */
int SetNameHost(char *nom)
{
  strcpy(nom_host, nom);
  return 1;
}                               /* SetNameHost */

/*
 *
 * Set le nom de la fonction en cours
 *
 */
int SetNameFunction(char *nom)
{

  ThreadLogContext_t *context = Log_GetThreadContext();

  strcpy(context->nom_fonction, nom);

  return 1;
}                               /* SetNameFunction */

/* 
 * Set le fichier dans lequel vont etre loggues les messages
 */
int SetNameFileLog(char *nom)
{
  /* Cette fonction n'est pas thread-safe car le fichier de log
   * est commun a tous les threads du programme */
  strcpy(nom_fichier_log, nom);

  return 1;
}                               /* SetNameFileLog */

/*
 * Return le nom du programme en cours 
 */
char *ReturnNamePgm()
{
  return nom_programme;
}                               /* ReturnNamePgm */

/*
 * Return le nom du host en cours 
 */
char *ReturnNameHost()
{
  return nom_host;
}                               /* ReturnNameHost */

/*
 * Return le nom de la fonction en cours 
 */

char *ReturnNameFunction()
{
  ThreadLogContext_t *context = Log_GetThreadContext();

  return context->nom_fonction;
}                               /* ReturnNameFunction */

/*
 * Return le chemin du fichier de log
 */
char *ReturnNameFileLog()       /* Cette fonction n'est PAS MT-Safe */
{
  return nom_fichier_log;
}                               /* ReturnNameFileLog */

/*
 * Cette fonction permet d'installer un handler de signal 
 */

static void ArmeSignal(int signal, void (*action) ())
{
  struct sigaction act;         /* Soyons POSIX et puis signal() c'est pas joli */
  char buffer[1024];

  /* Mise en place des champs du struct sigaction */
  act.sa_flags = 0;
  act.sa_handler = action;
  sigemptyset(&act.sa_mask);

  if(sigaction(signal, &act, NULL) == -1)
    {
      DisplayError(ERR_SIGACTION, errno);
      snprintf(buffer, 1024, "Impossible de controler %d", signal);
      DisplayLogFlux(stdout, "%s", buffer);
    }
}                               /* ArmeSignal */

/*
 *
 * Cinq fonctions pour gerer le niveau de debug et le controler
 * a distance au travers de handlers de signaux
 *
 * InitDebug
 * IncrementeLevelDebug
 * DecrementeLevelDebug
 * SetLevelDebug
 * ReturnLevelDebug
 * 
 */

int ReturnLevelDebug()
{
  return niveau_debug;
}                               /* ReturnLevelDebug */

int SetLevelDebug(int level_to_set)
{
  if(level_to_set < 0)
    level_to_set = 0;

  niveau_debug = level_to_set;

  return niveau_debug;
}                               /* SetLevelDebug */

static void IncrementeLevelDebug()
{
  char buffer[1024];

  niveau_debug += 1;
  if(niveau_debug >= NB_LOG_LEVEL)
    niveau_debug = NB_LOG_LEVEL - 1;

  snprintf(buffer, 1024, "SIGUSR1 recu -> Level de debug: %s = %d ",
           ReturnLevelInt(niveau_debug), niveau_debug);

  DisplayLogFlux(stdout, "%s", buffer);
}                               /* IncrementeLevelDebug */

static void DecrementeLevelDebug()
{
  char buffer[1024];

  niveau_debug -= 1;
  if(niveau_debug < 0)
    niveau_debug = 0;

  snprintf(buffer, 1024, "SIGUSR2 recu -> Level de debug: %s = %d ",
           ReturnLevelInt(niveau_debug), niveau_debug);

  DisplayLogFlux(stdout, "%s", buffer);
}                               /* DecrementeLevelDebug */

int InitDebug(int level_to_set)
{
  int i = 0;

  /* Initialisation du tableau des familys */
  tab_family[0].num_family = 0;
  tab_family[0].tab_err = (family_error_t *) tab_systeme_err;
  strcpy(tab_family[0].name_family, "Errors Systeme UNIX");

  for(i = 1; i < MAX_NUM_FAMILY; i++)
    tab_family[i].num_family = UNUSED_SLOT;

  /* On impose le niveau de debug */
  SetLevelDebug(level_to_set);

  ArmeSignal(SIGUSR1, IncrementeLevelDebug);
  ArmeSignal(SIGUSR2, DecrementeLevelDebug);

  return 0;
}                               /* InitLevelDebug */

/* 
 *
 * La fonction qui fait l'entete pour tout le monde
 *
 */

static int FaireEntete(char *output)
{
  struct tm the_date;
  time_t heure;

  heure = time(NULL);
  Localtime_r(&heure, &the_date);

  return snprintf(output, STR_LEN,
                  "%.2d/%.2d/%.4d %.2d:%.2d:%.2d epoch=%ld : %s : %s-%d[%s] :",
                  the_date.tm_mday, the_date.tm_mon + 1, 1900 + the_date.tm_year,
                  the_date.tm_hour, the_date.tm_min, the_date.tm_sec, heure, nom_host,
                  nom_programme, getpid(), ReturnNameFunction());

}                               /* Faire_entete */

/*
 * Une fonction d'affichage tout a fait generique 
 */

static int DisplayLogString_valist(char *buff_dest, char *format, va_list arguments)
{
  char entete[STR_LEN];
  char texte[STR_LEN_TXT];

  /* Je mets en place l'entete */
  FaireEntete(entete);

  /* Ecriture sur le fichier choisi */
  log_vsnprintf(texte, STR_LEN_TXT, format, arguments);
  return snprintf(buff_dest, STR_LEN_TXT, "%s%s\n", entete, texte);
}                               /* DisplayLogString_valist */

/* 
 *
 * Display un message avec entete et avec le format (printf-like) indique 
 *
 */

int DisplayLogString(char *chaine, char *format, ...)
{
  int rc;
  va_list arguments;

  va_start(arguments, format);
  rc = DisplayLogString_valist(chaine, format, arguments);
  va_end(arguments);

  return rc;

}                               /* DisplayLogString */



static int DisplayLogSyslog_valist( char * format, va_list arguments )
{
  if( !syslog_opened )
   {
     openlog( "nfs-ganesha", LOG_PID, LOG_USER ) ;
     syslog_opened = 1 ;
   }

  vsyslog( LOG_ERR, format, arguments );
  return 1 ;
} /* DisplayLogSyslog_valist */

static int DisplayLogSyslog( char * format, ... )
{
  va_list arguments ;
  int rc ;

  va_start( arguments, format );
  rc = DisplayLogSyslog_valist( format, arguments ) ;
  va_end( arguments ) ;

  return rc ;
} /* DisplayLogSyslog_valist */

static int DisplayLogFd_valist(int fd, char *format, va_list arguments)
{
  char tampon[STR_LEN_TXT];

  DisplayLogString_valist(tampon, format, arguments);
  return write(fd, tampon, strlen(tampon));
}                               /* DisplayLogFd_valist */

int DisplayLogFd(int fd, char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);
  rc = DisplayLogFd_valist(fd, format, arguments);
  va_end(arguments);

  return rc;

}                               /* DisplayLogFd */

static int DisplayLogFlux_valist(FILE * flux, char *format, va_list arguments)
{
  char tampon[STR_LEN_TXT];

  DisplayLogString_valist(tampon, format, arguments);

  fprintf(flux, "%s", tampon);
  return fflush(flux);
}                               /* DisplayLogFlux_valist */

int DisplayLogFlux(FILE * flux, char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);
  rc = DisplayLogFlux_valist(flux, format, arguments);
  va_end(arguments);

  return rc;

}                               /* DisplayLogFlux */

static int DisplayLogPath_valist(char *path, char *format, va_list arguments)
{
  char tampon[STR_LEN_TXT];
  int fd, my_status;

  DisplayLogString_valist(tampon, format, arguments);

  if(path[0] != '\0')
    {
#ifdef _LOCK_LOG
      if((fd = open(path, O_WRONLY | O_SYNC | O_APPEND | O_CREAT, masque_log)) != -1)
        {
          /* un verrou sur fichier */
          struct flock lock_file;

          /* mise en place de la structure de verrou sur fichier */
          lock_file.l_type = F_WRLCK;
          lock_file.l_whence = SEEK_SET;
          lock_file.l_start = 0;
          lock_file.l_len = 0;

          if(fcntl(fd, F_SETLKW, (char *)&lock_file) != -1)
            {
              /* Si la prise du verrou est OK */
              write(fd, tampon, strlen(tampon));

              /* Relache du verrou sur fichier */
              lock_file.l_type = F_UNLCK;
              fcntl(fd, F_SETLKW, (char *)&lock_file);

              /* fermeture du fichier */
              close(fd);
              return SUCCES;
            }                   /* if fcntl */
          else
            {
              /* Si la prise du verrou a fait un probleme */
              my_status = errno;
              close(fd);
            }
        }

#else
      if((fd = open(path, O_WRONLY | O_NONBLOCK | O_APPEND | O_CREAT, masque_log)) != -1)
        {
          write(fd, tampon, strlen(tampon));
          /* fermeture du fichier */
          close(fd);
          return SUCCES;
        }
#endif
      else
        {
          /* Si l'ouverture du fichier s'est mal passee */
          my_status = errno;
        }
      fprintf(stderr, "Error %s : %s : status %d on file %s\n",
              tab_systeme_err[ERR_FICHIER_LOG].label,
              tab_systeme_err[ERR_FICHIER_LOG].msg, my_status, path);

      return ERR_FICHIER_LOG;
    }
  /* if path */
  return SUCCES;
}                               /* DisplayLogPath_valist */

int DisplayLogPath(char *path, char *format, ...)
{
  int rc;
  va_list arguments;

  va_start(arguments, format);
  rc = DisplayLogPath_valist(path, format, arguments);
  va_end(arguments);

  return rc;

}                               /* DisplayLogPath */

static int DisplayLog_valist(char *format, va_list arguments)
{
  return DisplayLogPath_valist(nom_fichier_log, format, arguments);
}                               /* DisplayLog_valist */

int DisplayLog(char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);
  rc = DisplayLog_valist(format, arguments);
  va_end(arguments);

  return rc;

}                               /* DisplayLog */

/*
 *
 * Les memes fonctions mais avec des considerations de niveau
 *
 */

int DisplayLogStringLevel(char *tampon, int level, char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);

  if(level <= niveau_debug)
    {
      rc = DisplayLogString_valist(tampon, format, arguments);
    }
  else
    rc = SUCCES;

  va_end(arguments);

  return rc;

}                               /* DisplayLogStringLevel */

int DisplayLogFluxLevel(FILE * flux, int level, char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);

  if(level <= niveau_debug)
    rc = DisplayLogFlux_valist(flux, format, arguments);
  else
    rc = SUCCES;

  va_end(arguments);

  return rc;

}                               /* DisplayLogFluxLevel */

int DisplayLogFdLevel(int fd, int level, char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);

  if(level <= niveau_debug)
    rc = DisplayLogFd_valist(fd, format, arguments);
  else
    rc = SUCCES;

  va_end(arguments);

  return rc;

}                               /* DisplayLogFdLevel */

int DisplayLogLevel(int level, char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);

  if(level <= niveau_debug)
    rc = DisplayLog_valist(format, arguments);
  else
    rc = SUCCES;

  va_end(arguments);

  return rc;

}                               /* DisplayLogLevel */

int DisplayLogPathLevel(char *path, int level, char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);

  if(level <= niveau_debug)
    rc = DisplayLogPath_valist(path, format, arguments);
  else
    rc = SUCCES;

  va_end(arguments);

  return rc;

}                               /* DisplayLogPathLevel */

/*
 *
 * Les routines de gestions des messages d'erreur 
 *
 */

int AddFamilyError(int num_family, char *name_family, family_error_t * tab_err)
{
  int i = 0;

  /* Le numero de la family est entre -1 et MAX_NUM_FAMILY */
  if((num_family < -1) || (num_family >= MAX_NUM_FAMILY))
    return -1;

  /* On n'occupe pas 0 car ce sont les erreurs du systeme */
  if(num_family == 0)
    return -1;

  /* On cherche une entree vacante */
  for(i = 0; i < MAX_NUM_FAMILY; i++)
    if(tab_family[i].num_family == UNUSED_SLOT)
      break;

  /* On verifie que la table n'est pas pleine */
  if(i == MAX_NUM_FAMILY)
    return -1;

  tab_family[i].num_family = (num_family != -1) ? num_family : i;
  tab_family[i].tab_err = tab_err;
  strcpy(tab_family[i].name_family, name_family);

  return tab_family[i].num_family;
}                               /* AddFamilyError */

int RemoveFamilyError(int num_family)
{
  int i = 0;

  for(i = 0; i < MAX_NUM_FAMILY; i++)
    if(tab_family[i].num_family == num_family)
      {
        tab_family[i].num_family = UNUSED_SLOT;
        return i;
      }

  /* Sinon on retourne -1 */
  return -1;
}                               /* RemoveFamilyError */

char *ReturnNameFamilyError(int num_family)
{
  int i = 0;

  for(i = 0; i < MAX_NUM_FAMILY; i++)
    if(tab_family[i].num_family == num_family)
      {
        /* A quoi sert cette ligne ??????!!!!!! */
        /* tab_family[i].num_family = UNUSED_SLOT ; */
        return tab_family[i].name_family;
      }

  /* Sinon on retourne NULL */
  return NULL;
}                               /* ReturnFamilyError */

/* Cette fonction trouve une family dans le tabelau des familys d'erreurs */
static family_error_t *TrouveTabErr(int num_family)
{
  int i = 0;

  for(i = 0; i < MAX_NUM_FAMILY; i++)
    {
      if(tab_family[i].num_family == num_family)
        {
          return tab_family[i].tab_err;
        }
    }

  /* Sinon on retourne NULL */
  return NULL;
}                               /* TrouveTabErr */

static family_error_t TrouveErr(family_error_t * tab_err, int num)
{
  int i = 0;
  family_error_t returned_err;

  do
    {

      if(tab_err[i].numero == num || tab_err[i].numero == ERR_NULL)
        {
          returned_err = tab_err[i];
          break;
        }

      i += 1;
    }
  while(1);

  return returned_err;
}                               /* TrouveErr */

static int FaireLogError(char *buffer, int num_family, int num_error, int status,
                         int ma_ligne)
{
  family_error_t *tab_err = NULL;
  family_error_t the_error;

  /* Find the family */
  if((tab_err = TrouveTabErr(num_family)) == NULL)
    return -1;

  /* find the error */
  the_error = TrouveErr(tab_err, num_error);

  if(status == 0)
    {
      return sprintf(buffer, "Error %s : %s : status %d : Line %d",
                     the_error.label, the_error.msg, status, ma_ligne);
    }
  else
    {
      return sprintf(buffer, "Error %s : %s : status %d : %s : Line %d",
                     the_error.label, the_error.msg, status, strerror(status), ma_ligne);
    }
}                               /* FaireLogError */

int DisplayErrorStringLine(char *tampon, int num_family, int num_error, int status,
                           int ma_ligne)
{
  char buffer[STR_LEN_TXT];

  if(FaireLogError(buffer, num_family, num_error, status, ma_ligne) == -1)
    return -1;

  return DisplayLogString(tampon, "%s", buffer);
}                               /* DisplayErrorStringLine */

int DisplayErrorFluxLine(FILE * flux, int num_family, int num_error, int status,
                         int ma_ligne)
{
  char buffer[STR_LEN_TXT];

  if(FaireLogError(buffer, num_family, num_error, status, ma_ligne) == -1)
    return -1;

  return DisplayLogFlux(flux, "%s", buffer);
}                               /* DisplayErrorFluxLine */

int DisplayErrorFdLine(int fd, int num_family, int num_error, int status, int ma_ligne)
{
  char buffer[STR_LEN_TXT];

  if(FaireLogError(buffer, num_family, num_error, status, ma_ligne) == -1)
    return -1;

  return DisplayLogFd(fd, "%s", buffer);
}                               /* DisplayErrorFdLine */

int DisplayErrorLogLine(int num_family, int num_error, int status, int ma_ligne)
{
  char buffer[STR_LEN_TXT];

  if(FaireLogError(buffer, num_family, num_error, status, ma_ligne) == -1)
    return -1;

  return DisplayLog("%s", buffer);
}                               /* DisplayErrorLogLine */

/* Les routines de gestion du descripteur de journal */
int AddLogStreamJd(log_t * pjd, type_log_stream_t type, desc_log_stream_t desc_voie,
                   niveau_t niveau, aiguillage_t aiguillage)
{
  log_stream_t *nouvelle_voie;

  /* On alloue une nouvelle voie */
  if((nouvelle_voie = (log_stream_t *) malloc(sizeof(log_stream_t))) == NULL)
    return -1;

  nouvelle_voie->desc = desc_voie;

  nouvelle_voie->type = type;
  nouvelle_voie->niveau = niveau;
  nouvelle_voie->aiguillage = aiguillage;
  nouvelle_voie->suivante = NULL;

  if(pjd->nb_voies == 0)
    {
      pjd->liste_voies = nouvelle_voie;
      pjd->fin_liste_voies = nouvelle_voie;
    }
  else
    {
      pjd->fin_liste_voies->suivante = nouvelle_voie;
      pjd->fin_liste_voies = nouvelle_voie;
    }

  pjd->nb_voies += 1;
  return 0;
}

int DisplayLogJdLevel(log_t jd, int level, char *format, ...)
{
  va_list arguments;
  int ecrire_msg = 0;
  log_stream_t *pvoie = NULL;

  /* On regarde pour toutes les voies */
  for(pvoie = jd.liste_voies; pvoie != NULL; pvoie = pvoie->suivante)
    {
      /* restart from first arg for each log stream */
      va_start(arguments, format);

      ecrire_msg = 0;

      switch (pvoie->aiguillage)
        {
        case SUP:
          /* Attention a l'ordre des gravites (inverse) */
          if((unsigned int)level <= pvoie->niveau)
            ecrire_msg = 1;
          break;

        case INF:
          /* Attention, l'ordre est inverse */
          if((unsigned int)level >= pvoie->niveau)
            ecrire_msg = 1;

          break;
        case EXACT:
          if((unsigned int)level == pvoie->niveau)
            ecrire_msg = 1;
          break;
        default:
          break;
        }                       /* switch */

      /* Ecriture du message */
      if(ecrire_msg == 1)
        {
          switch (pvoie->type)
            {
            case V_STREAM:
              DisplayLogFlux_valist(pvoie->desc.flux, format, arguments);
              break;

            case V_BUFFER:
              DisplayLogString_valist(pvoie->desc.buffer, format, arguments);
              break;

            case V_FILE:
              DisplayLogPath_valist(pvoie->desc.path, format, arguments);
              break;

            case V_FD:
              DisplayLogFd_valist(pvoie->desc.fd, format, arguments);
              break;

            case V_SYSLOG:
	      DisplayLogSyslog_valist( format, arguments ) ;
              break ;

            default:
              break;
            }                   /* switch */
        }

      /* if */
      /* restart from first arg for each log stream */
      va_end(arguments);

    }                           /* for */

  return SUCCES;
}                               /* DisplayLogFdLevel */

/* Comme la precedente, mais on ecrit a tous les coups */
int DisplayLogJd(log_t jd, char *format, ...)
{
  va_list arguments;
  log_stream_t *pvoie = NULL;

  /* On regarde pour toutes les voies */
  for(pvoie = jd.liste_voies; pvoie != NULL; pvoie = pvoie->suivante)
    {
      /* restart from first arg for each log stream. */
      va_start(arguments, format);

      switch (pvoie->type)
        {
        case V_STREAM:
          DisplayLogFlux_valist(pvoie->desc.flux, format, arguments);
          break;

        case V_BUFFER:
          DisplayLogString_valist(pvoie->desc.buffer, format, arguments);
          break;

        case V_FILE:
          DisplayLogPath_valist(pvoie->desc.path, format, arguments);
          break;

        case V_FD:
          DisplayLogFd_valist(pvoie->desc.fd, format, arguments);
          break;

        case V_SYSLOG:
	  DisplayLogSyslog_valist( format, arguments ) ;
          break ;

        default:
          break;
        }                       /* switch */

      /* restart from first arg for each log stream. */
      va_end(arguments);

    }                           /* for */

  return SUCCES;
}                               /* DisplayLogFdLevel */

int DisplayErrorJdLine(log_t jd, int num_family, int num_error, int status, int ma_ligne)
{
  char buffer[STR_LEN_TXT];

  if(FaireLogError(buffer, num_family, num_error, status, ma_ligne) == -1)
    return -1;

  return DisplayLogJd(jd, "%s", buffer);
}                               /* DisplayErrorFluxLine */

/* Un sprintf personnalisé */
/* Cette macro est utilisee a chaque fois que l'on avance d'un pas dans le parsing */
#define ONE_STEP  do { iterformat +=1 ; len += 1; } while(0)

#define NO_TYPE       0
#define INT_TYPE      1
#define LONG_TYPE     2
#define CHAR_TYPE     3
#define STRING_TYPE   4
#define FLOAT_TYPE    5
#define DOUBLE_TYPE   6
#define POINTEUR_TYPE 7

/* Type specifiques a la log */
#define EXTENDED_TYPE 8

#define STATUS_SHORT       1
#define STATUS_LONG        2
#define CONTEXTE_SHORT     3
#define CONTEXTE_LONG      4
#define ERREUR_SHORT       5
#define ERREUR_LONG        6
#define ERRNUM_SHORT       7
#define ERRNUM_LONG        8
#define ERRCTX_SHORT       9
#define ERRCTX_LONG        10
#define CHANGE_ERR_FAMILY  11
#define CHANGE_CTX_FAMILY  12

#define NO_LONG 0
#define SHORT_LG 1
#define LONG_LG 2
#define LONG_LONG_LG 3

#define MAX_STR_TOK LOG_MAX_STRLEN

int log_vsnprintf(char *out, size_t taille, char *format, va_list arguments)
{
  char *iterformat = NULL;
  char subformat[MAX_STR_TOK];
  char tmpout[MAX_STR_TOK];
  int type = NO_TYPE;
  int typelg = NO_LONG;
  int type_ext = 0;
  char *ptrsub = NULL;
  char *endofstr = NULL;
  int len = 0;
  int precision_in_valist = 0;
  int retval = 0;

  int err_family = ERR_POSIX;
  int ctx_family = ERR_SYS;
  int numero;
  char *label;
  char *msg;

  int tmpnumero;
  char *tmplabel;
  char *tmpmsg;
  family_error_t *tab_err;
  family_error_t the_error;

#ifdef _DEBUG_LOG
  printf("##### Le format est #%s#\n", format);
#endif

  /* Phase d'init */
  out[0] = '\0';
  iterformat = format;
  ptrsub = subformat;

  do
    {
      /* reinit var length for each %<smthg> */
      type = NO_TYPE;
      typelg = NO_LONG;

      ptrsub = iterformat;
      endofstr = iterformat;
      len = 0;

      /* On affiche d'abord tout ce qui est avant un % */
      while(*iterformat != '\0' && *iterformat != '%')
        {
#ifdef _DEBUG_LOG
          printf("##### iterformat = #%s#\n", iterformat);
#endif
          ONE_STEP;
        }

      if(*iterformat == '\0')
        break;
      else
        endofstr = iterformat;

      /* Je vire le premier caractere (qui est forcement un '%') */
      ONE_STEP;

      /* On traite les arguments positionnels */
      while(isdigit(*iterformat) || *iterformat == '$')
        {
          ONE_STEP;
        }

      /* 
       * une boucle qui traite des caracterers de formatage, infini sauf quand un caractere qui n'est pas de 
       * formattage est rencontree qui produit un break ;
       */
      do
        {

          switch (*iterformat)
            {
              /* ATTENTION 
               *  /!\ Une serie de case sans break !!!! 
               */
            case ' ':
              /* Un espace est utilise a la place d'un signe. */
            case '+':
              /* Un signe + explicite */
            case '-':
              /* un signe - */
            case '#':
              /* utilisation d'une forme alternative */
            case '0':
              /* Pour forcer l'emploi de padding par des 0 sur des champs de taille fixe */
            case 'I':
              /* Pour les formats internationaux */
              ONE_STEP;
              continue;         /* On repasse dans la boucle */

            default:
              /* Fin de ce type de caracteres */
              break;
            }
          break;
        }
      while(1);                 /* Pas vraiment une boucle infinie en fait */

      /* La taille du champ */
      if(*iterformat == '*')
        {
          /* On garde le '*' */
          ONE_STEP;

          /* on lit la precision dans les arguments */
          precision_in_valist += 1;

          /* On garde la taille du champs */
          while(isdigit(*iterformat) || *iterformat == '$')
            {
              ONE_STEP;
            }
        }

      /* if( *iterformat == '*' ) */
      /* La precision */
      if(*iterformat == '.')
        {
          /* On garde le '.' */
          ONE_STEP;

          if(*iterformat == '*')
            {
              /* On garde le '*' */
              ONE_STEP;

              /* On garde la taille du champs */
              while(isdigit(*iterformat) || *iterformat == '$')
                {
                  ONE_STEP;
                }
            }                   /* if( *iterformat == '*' ) */
          else
            {
              if(isdigit(*iterformat))
                {

                  while(isdigit(*iterformat) || *iterformat == '$')
                    {
                      ONE_STEP;
                    }
                }               /*  if( isdigit( *iterformat ) */
              else
                {
                  /* Le cas %.? est traite comme %.0? */
                  ONE_STEP;
                }

            }                   /* if( *iterformat == '.' )  */
        }

      /* On s'occupe a present des doubles quantificateurs, types ll */
      switch (*iterformat)
        {
        case 'h':
          /* ce sont des short ints ou bien des char */
          typelg = SHORT_LG;
          ONE_STEP;
          break;
        case 'L':
          /* long doubles et long ints */
          typelg = LONG_LG;
          ONE_STEP;
          break;
        case 'q':
          /* long long */
          typelg = LONG_LG;
          ONE_STEP;
          break;
        case 'z':
        case 'Z':
          /* Les entiers sont des size */
          typelg = LONG_LG;
          ONE_STEP;
          break;
        case 't':
        case 'j':
          typelg = LONG_LG;
          ONE_STEP;
          break;
        case 'l':
          /* long int, peut etre double */
          ONE_STEP;
          typelg = LONG_LG;
          if(*iterformat == 'l')
            {
              ONE_STEP;
              typelg = LONG_LONG_LG;
            }
          break;
        default:
          break;
        }

      /* A present, l'identificateur de type */
      type = NO_TYPE;

      switch (*iterformat)
        {
        case 'i':
        case 'd':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
          /* Des entiers */
          type = INT_TYPE;
          ONE_STEP;
          break;
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
          /* Des flottants */
          type = DOUBLE_TYPE;
          ONE_STEP;
          break;
        case 'c':
        case 'C':
          /* Des caracteres */
          type = CHAR_TYPE;
          ONE_STEP;
          break;
        case 's':
        case 'S':
          /* Des chaines de caracteres a zero terminal */
          type = STRING_TYPE;
          ONE_STEP;
          break;
        case 'p':
          /* La chose en un pointeur */
          type = POINTEUR_TYPE;
          ONE_STEP;
          break;
        case 'n':
          /* Le monstrueux passage de pointeur, trou beant pour les shellcodes, je ne le gere pas volontairement */
          type = POINTEUR_TYPE;
          ONE_STEP;
          break;
        case '%':
          /* le caractere %, tout simplement */
          type = NO_TYPE;
          ONE_STEP;
          break;
        case 'b':
          /* Un status, dans sa version courte */
          type = EXTENDED_TYPE;
          type_ext = STATUS_SHORT;
          ONE_STEP;
          break;
        case 'B':
          /* Un status, dans sa version longue */
          type = EXTENDED_TYPE;
          type_ext = STATUS_LONG;
          ONE_STEP;
          break;
        case 'h':
          /* Un contexte, dans sa version courte */
          type = EXTENDED_TYPE;
          type_ext = CONTEXTE_SHORT;
          ONE_STEP;
          break;
        case 'H':
          /* Un contexte, dans sa version longue */
          type = EXTENDED_TYPE;
          type_ext = CONTEXTE_LONG;
          ONE_STEP;
          break;
        case 'y':
          /* Une log_error, en version courte */
          type = EXTENDED_TYPE;
          type_ext = ERREUR_SHORT;
          ONE_STEP;
          break;
        case 'Y':
          /* Une log_error, en version longue */
          type = EXTENDED_TYPE;
          type_ext = ERREUR_LONG;
          ONE_STEP;
          break;
        case 'r':
          /* Un  numero d'erreur, en version courte */
          type = EXTENDED_TYPE;
          type_ext = ERRNUM_SHORT;
          ONE_STEP;
          break;
        case 'R':
          /* Un  numero d'erreur, en version longue */
          type = EXTENDED_TYPE;
          type_ext = ERRNUM_LONG;
          ONE_STEP;
          break;
        case 'v':
          /* Un contexte d'erreur, en version courte */
          type = EXTENDED_TYPE;
          type_ext = ERRCTX_SHORT;
          ONE_STEP;
          break;
        case 'V':
          /* Un contexte d'erreur, en version longue */
          type = EXTENDED_TYPE;
          type_ext = ERRCTX_LONG;
          ONE_STEP;
          break;
        case 'J':
          /* Changement de family par defaut, pour les erreurs */
          type = EXTENDED_TYPE;
          type_ext = CHANGE_ERR_FAMILY;
          ONE_STEP;
          break;
        case 'K':
          /* Changement de family par defaut, pour les contexte */
          type = EXTENDED_TYPE;
          type_ext = CHANGE_CTX_FAMILY;
          ONE_STEP;
          break;
        case 'm':
        default:
          break;
        }

      strncpy(subformat, ptrsub, len);
      subformat[len] = '\0';
#ifdef _DEBUG_LOG
      printf("##### iterformat +1  = #%s# \n", iterformat);
      printf("subformat = #%s# len =%d\n", subformat, len);
#endif

      if(type != EXTENDED_TYPE)
        {
          va_list tmp_args;

          /* vsnprintf may modify arguments */
          va_copy(tmp_args, arguments);

          /* Composition de la sortie avec vsprintf */
          retval += vsnprintf(tmpout, taille, subformat, tmp_args);

          /* free this temporary copy */
          va_end(tmp_args);

          /*
           * Si un truc genre %*.*d est utilise, on doit consommer un entier qui contient la precision pour chaque "*"
           */
          while(precision_in_valist != 0)
            {
              va_arg(arguments, int);
              precision_in_valist -= 1;
            }

          /* Je vais prendre un argument dans la va_list selon le format */
          switch (typelg)
            {
            case SHORT_LG:
              va_arg(arguments, int);
              break;

            case NO_LONG:
              switch (type)
                {
                case INT_TYPE:
                  va_arg(arguments, int);
                  break;

                case LONG_TYPE:
                  va_arg(arguments, long);
                  break;

                case FLOAT_TYPE:
                  /* float is promoted to double when passed through "..." */
                  va_arg(arguments, double);
                  break;

                case DOUBLE_TYPE:
                  va_arg(arguments, double);
                  break;

                case CHAR_TYPE:
                  /* char is promoted to int when passed through "..." */
                  va_arg(arguments, int);
                  break;

                case POINTEUR_TYPE:
                  va_arg(arguments, void *);
                  break;

                case STRING_TYPE:
                  va_arg(arguments, char *);
                  break;
                }

              break;

            case LONG_LG:
              switch (type)
                {
                case INT_TYPE:
                  va_arg(arguments, long int);
                  break;

                case LONG_TYPE:
                  va_arg(arguments, long long int);
                  break;

                case FLOAT_TYPE:
                  va_arg(arguments, double);
                  break;

                case DOUBLE_TYPE:
                  va_arg(arguments, long double);
                  break;
                }

            case LONG_LONG_LG:
              switch (type)
                {
                case INT_TYPE:
                case LONG_TYPE:
                  va_arg(arguments, long long int);
                  break;

                case FLOAT_TYPE:
                case DOUBLE_TYPE:
                  va_arg(arguments, long double);
                  break;
                }

              break;
            }
        }                       /* if( type != EXTENDED_TYPE ) */
      else
        {
          /* Cette macro (un peu crade), extrait une family_error_t des arguments variables */
#define VA_ARG_ERREUR_T numero = va_arg( arguments, long ) ; label  = va_arg( arguments, char * ) ; msg    = va_arg( arguments, char *)

#ifdef _DEBUG_LOG
          printf("====> Un type etendu \n");
          printf("====> subformat = #%s#\n", subformat);
          printf("====> out = #%s#\n", out);
#endif

          /* si le subformat est de type "toto titi tutu %X",
           * on ajoute le "toto titi tutu " dans la chaine de sortie */
          if(strlen(subformat) > 2)
            strncat(out, subformat, strlen(subformat) - 2);

          switch (type_ext)
            {
            case STATUS_SHORT:
            case CONTEXTE_SHORT:
              VA_ARG_ERREUR_T;
              snprintf(tmpout, MAX_STR_TOK, "%s(%d)", label, numero);
              break;

            case STATUS_LONG:
            case CONTEXTE_LONG:
              VA_ARG_ERREUR_T;
              snprintf(tmpout, MAX_STR_TOK, "%s(%d) : '%s'", label, numero, msg);
              break;

            case ERREUR_SHORT:
              VA_ARG_ERREUR_T;
              tmpnumero = numero;
              tmplabel = label;
              tmpmsg = msg;
              VA_ARG_ERREUR_T;
              snprintf(tmpout, MAX_STR_TOK, "%s %s(%d)", tmplabel, label, numero);
              break;

            case ERREUR_LONG:
              VA_ARG_ERREUR_T;
              tmpnumero = numero;
              tmplabel = label;
              tmpmsg = msg;
              VA_ARG_ERREUR_T;
              snprintf(tmpout, MAX_STR_TOK, "%s(%d) : '%s' -> %s(%d) : '%s'",
                       tmplabel, tmpnumero, tmpmsg, label, numero, msg);
              break;

            case CHANGE_ERR_FAMILY:
              /* On assigne un nouvelle family d'erreur */
              err_family = va_arg(arguments, int);
              tmpout[0] = '\0'; /* Chaine vide */
              break;

            case CHANGE_CTX_FAMILY:
              /* On assigne une nouvelle family de contexte */
              ctx_family = va_arg(arguments, int);
              tmpout[0] = '\0';
              break;

            case ERRNUM_SHORT:
              /* Un numero d'erreur dans la family courante (ERR_POSIX par defaut) */
              if((tab_err = TrouveTabErr(err_family)) == NULL)
                {
                  snprintf(tmpout, MAX_STR_TOK, "?");
                  break;
                }
              numero = va_arg(arguments, int);
              the_error = TrouveErr(tab_err, numero);
              snprintf(tmpout, MAX_STR_TOK, "%s(%d)", the_error.label, the_error.numero);
              break;

            case ERRNUM_LONG:
              /* Un numero d'erreur dans la family courante (ERR_POSIX par defaut) */
              if((tab_err = TrouveTabErr(err_family)) == NULL)
                {
                  snprintf(tmpout, MAX_STR_TOK, "?");
                  break;
                }
              numero = va_arg(arguments, int);
              the_error = TrouveErr(tab_err, numero);
              snprintf(tmpout, MAX_STR_TOK, "%s(%d) : '%s'", the_error.label,
                       the_error.numero, the_error.msg);
              break;

            case ERRCTX_SHORT:
              /* Un numero de contexte dans la family courante (ERR_SYS par defaut) */
              if((tab_err = TrouveTabErr(ctx_family)) == NULL)
                {
                  snprintf(tmpout, MAX_STR_TOK, "?");
                  break;
                }
              numero = va_arg(arguments, int);
              the_error = TrouveErr(tab_err, numero);
              snprintf(tmpout, MAX_STR_TOK, "%s(%d)", the_error.label, the_error.numero);
              break;

            case ERRCTX_LONG:
              /* Un numero de contexte dans la family courante (ERR_SYS par defaut) */
              if((tab_err = TrouveTabErr(ctx_family)) == NULL)
                {
                  snprintf(tmpout, MAX_STR_TOK, "?");
                  break;
                }
              numero = va_arg(arguments, int);
              the_error = TrouveErr(tab_err, numero);
              snprintf(tmpout, MAX_STR_TOK, "%s(%d) : '%s'", the_error.label, numero,
                       the_error.msg);
              break;

            }                   /* switch */

        }                       /* else */
#ifdef _DEBUG_LOG
      printf("====> tmpout = #%s#\n", tmpout);
#endif
      strncat(out, tmpout, taille);

#ifdef _DEBUG_LOG
      printf("====> out = #%s#\n", out);
      printf("=================\n");
#endif
    }
  while(*iterformat != '\0');

  /* Le pas oublie la fin du format, qui est tout a la queue */
#ifdef _DEBUG_LOG
  printf("DEBUG : ptrsub = #%s#   endofstr = #%s#\n", ptrsub, endofstr);
#endif
  if(*endofstr == '%')
    ptrsub = iterformat;        /* Pour clore l'iteration */
  strncat(out, ptrsub, taille);

  return retval;
}                               /* mon_vsprintf */

int log_sprintf(char *out, char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);
  rc = log_vsprintf(out, format, arguments);
  va_end(arguments);

  return rc;
}

int log_snprintf(char *out, size_t n, char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);
  rc = log_vsnprintf(out, n, format, arguments);
  va_end(arguments);

  return rc;
}

int log_fprintf(FILE * file, char *format, ...)
{
  va_list arguments;
  char tmpstr[LOG_MAX_STRLEN];
  int rc;

  va_start(arguments, format);
  memset(tmpstr, 0, LOG_MAX_STRLEN);
  rc = log_vsnprintf(tmpstr, LOG_MAX_STRLEN, format, arguments);
  va_end(arguments);
  fputs(tmpstr, file);
  return rc;
}

int log_vfprintf(FILE * file, char *format, va_list arguments)
{
  char tmpstr[LOG_MAX_STRLEN];
  int rc;

  memset(tmpstr, 0, LOG_MAX_STRLEN);
  rc = log_vsnprintf(tmpstr, LOG_MAX_STRLEN, format, arguments);
  fputs(tmpstr, file);
  return rc;
}

int log_printf(char *format, ...)
{
  va_list arguments;
  int rc;

  va_start(arguments, format);
  rc = log_vfprintf(stdout, format, arguments);
  va_end(arguments);

  return rc;
}

  /* 
   * Pour info : Les tags de printf dont on peut se servir:
   * w DMNOPQTUWX 
   */
