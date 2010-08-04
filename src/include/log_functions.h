#ifndef _LOGS_H
#define _LOGS_H

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/param.h>
#include <syslog.h>

#ifndef LIBLOG_NO_THREAD
#include <errno.h>
#include <pthread.h>
#endif

/*
 * definition des codes d'error
 *
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
 *
 */

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#define LOCALMAXPATHLEN 1024

#define STR_LEN 256

/*
 * 
 * Constantes qui definissent les niveaux de gravite des affichages de LOG 
 *
 */

typedef struct loglev
{
  int value;
  char *str;
} log_level_t;

static log_level_t __attribute__ ((__unused__)) tabLogLevel[] =
{
#define NIV_NULL       0
  {
  NIV_NULL, "NIV_NULL"},
#define NIV_MAJ        1
  {
  NIV_MAJ, "NIV_MAJ"},
#define NIV_CRIT       2
  {
  NIV_CRIT, "NIV_CRIT"},
#define NIV_EVENT      3
  {
  NIV_EVENT, "NIV_EVENT"},
#define NIV_DEBUG      4
  {
  NIV_DEBUG, "NIV_DEBUG"},
#define NIV_FULL_DEBUG 5
  {
  NIV_FULL_DEBUG, "NIV_FULL_DEBUG"}
};

#define NB_LOG_LEVEL 6
#define NIV_MAJOR NIV_MAJ

/*
 *
 * La structure de definition des messages d'errors
 *
 */

#define LOG_MAX_STRLEN 2048
#define LOG_LABEL_LEN 50
#define LOG_MSG_LEN   255
typedef struct
{
  int numero;
  char label[LOG_LABEL_LEN];
  char msg[LOG_MSG_LEN];
} family_error_t;

/* Le type family d'erreurs */
typedef struct
{
  int num_family;
  char name_family[STR_LEN];
  family_error_t *tab_err;
} family_t;

typedef family_error_t status_t;
typedef family_error_t errctx_t;

typedef struct
{
  int err_family;
  int ctx_family;
  errctx_t contexte;
  status_t status;
} log_error_t;

/* les macros pour mettre dans les printf */
#define __E(variable) variable.numero, variable.label, variable.msg
#define __S(variable) variable.numero, variable.label, variable.msg
#define __format(variable) variable.numero, variable.label, variable.msg
#define __f(variable) variable.numero, variable.label, variable.msg
#define __AE(variable) variable.contexte.numero, variable.contexte.label, variable.contexte.msg, \
                       variable.status.numero,   variable.status.label,   variable.status.msg

#define ERR_NULL -1

/* les code d'error */
#define ERR_SYS 0
static errctx_t __attribute__ ((__unused__)) tab_systeme_err[] =
{
#define SUCCES                    0
#define ERR_SUCCES                0
#define ERR_NO_ERROR              0
  {
  ERR_SUCCES, "SUCCES", "Pas d'error"},
#define ERR_FAILURE               1
  {
  ERR_FAILURE, "FAILURE", "Une error est survenue"},
#define EVNT                      2
#define ERR_EVNT                  2
  {
  ERR_EVNT, "EVNT", "Evennement survenu"},
#define ERR_PTHREAD_KEY_CREATE    3
  {
  ERR_PTHREAD_KEY_CREATE, "ERR_PTHREAD_KEY_CREATE",
        "Error a la creation des pthread_keys"},
#define ERR_MALLOC                4
  {
  ERR_MALLOC, "ERR_MALLOC", "malloc impossible"},
#define ERR_SIGACTION             5
  {
  ERR_SIGACTION, "ERR_SIGACTION", "sigaction impossible"},
#define ERR_PTHREAD_ONCE          6
  {
  ERR_PTHREAD_ONCE, "ERR_PTHREAD_ONCE", "pthread_once impossible"},
#define ERR_FICHIER_LOG           7
  {
  ERR_FICHIER_LOG, "ERR_FICHIER_LOG", "impossible d'acceder au fichier de log"},
#define ERR_GETHOSTBYNAME         8
  {
  ERR_GETHOSTBYNAME, "ERR_GETHOSTBYNAME", "gethostbyname impossible"},
#define ERR_MMAP                  9
  {
  ERR_MMAP, "ERR_MMAP", "mmap impossible"},
#define ERR_SOCKET               10
  {
  ERR_SOCKET, "ERR_SOCKET", "socket impossible"},
#define ERR_BIND                 11
  {
  ERR_BIND, "ERR_BIND", "bind impossible"},
#define ERR_CONNECT              12
  {
  ERR_CONNECT, "ERR_CONNECT", "connect impossible"},
#define ERR_LISTEN               13
  {
  ERR_LISTEN, "ERR_LISTEN", "listen impossible"},
#define ERR_ACCEPT               14
  {
  ERR_ACCEPT, "ERR_ACCEPT", "accept impossible"},
#define ERR_RRESVPORT            15
  {
  ERR_RRESVPORT, "ERR_RRESVPORT", "rresvport impossible"},
#define ERR_GETHOSTNAME          16
  {
  ERR_GETHOSTNAME, "ERR_GETHOSTNAME", "gethostname impossible"},
#define ERR_GETSOCKNAME          17
  {
  ERR_GETSOCKNAME, "ERR_GETSOCKNAME", "getsockname impossible"},
#define ERR_IOCTL                18
  {
  ERR_IOCTL, "ERR_IOCTL", "ioctl impossible"},
#define ERR_UTIME                19
  {
  ERR_UTIME, "ERR_UTIME", "utime impossible"},
#define ERR_XDR                  20
  {
  ERR_XDR, "ERR_XDR", "Un appel XDR a echoue"},
#define ERR_CHMOD                21
  {
  ERR_CHMOD, "ERR_CHMOD", "chmod impossible"},
#define ERR_SEND                 22
  {
  ERR_SEND, "ERR_SEND", "send impossible"},
#define ERR_GETHOSTBYADDR        23
  {
  ERR_GETHOSTBYADDR, "ERR_GETHOSTBYADDR", "gethostbyaddr impossible"},
#define ERR_PREAD                24
  {
  ERR_PREAD, "ERR_PREAD", "pread impossible"},
#define ERR_PWRITE               25
  {
  ERR_PWRITE, "ERR_PWRITE", "pwrite impossible"},
#define ERR_STAT                 26
  {
  ERR_STAT, "ERR_STAT", "stat impossible"},
#define ERR_GETPEERNAME          27
  {
  ERR_GETPEERNAME, "ERR_GETPEERNAME", "getpeername impossible"},
#define ERR_FORK                 28
  {
  ERR_FORK, "ERR_FORK", "fork impossible"},
#define ERR_GETSERVBYNAME        29
  {
  ERR_GETSERVBYNAME, "ERR_GETSERVBYNAME", "getservbyname impossible"},
#define ERR_MUNMAP               30
  {
  ERR_MUNMAP, "ERR_MUNMAP", "munmap impossible"},
#define ERR_STATVFS              31
  {
  ERR_STATVFS, "ERR_STATVFS", "statvfs impossible"},
#define ERR_OPENDIR              32
  {
  ERR_OPENDIR, "ERR_OPENDIR", "opendir impossible"},
#define ERR_READDIR              33
  {
  ERR_READDIR, "ERR_READDIR", "readdir impossible"},
#define ERR_CLOSEDIR             34
  {
  ERR_CLOSEDIR, "ERR_CLOSEDIR", "closedir impossible"},
#define ERR_LSTAT                35
  {
  ERR_LSTAT, "ERR_LSTAT", "lstat impossible"},
#define ERR_GETWD                36
  {
  ERR_GETWD, "ERR_GETWD", "getwd impossible"},
#define ERR_CHDIR                37
  {
  ERR_CHDIR, "ERR_CHDIR", "chdir impossible"},
#define ERR_CHOWN                38
  {
  ERR_CHOWN, "ERR_CHOWN", "chown impossible"},
#define ERR_MKDIR                39
  {
  ERR_MKDIR, "ERR_MKDIR", "mkdir impossible"},
#define ERR_OPEN                 40
  {
  ERR_OPEN, "ERR_OPEN", "open impossible"},
#define ERR_READ                 41
  {
  ERR_READ, "ERR_READ", "read impossible"},
#define ERR_WRITE                42
  {
  ERR_WRITE, "ERR_WRITE", "write impossible"},
#define ERR_UTIMES               43
  {
  ERR_UTIMES, "ERR_UTIMES", "utimes impossible"},
#define ERR_READLINK             44
  {
  ERR_READLINK, "ERR_READLINK", "readlink impossible"},
#define ERR_SYMLINK              45
  {
  ERR_SYMLINK, "ERR_SYMLINK", "symlink impossible"},
#define ERR_SYSTEM               46
  {
  ERR_SYSTEM, "ERR_SYSTEM", "system impossible"},
#define ERR_POPEN                47
  {
  ERR_POPEN, "ERR_POPEN", "popen impossible"},
#define ERR_LSEEK                48
  {
  ERR_LSEEK, "ERR_LSEEK", "lseek impossible"},
#define ERR_PTHREAD_CREATE       49
  {
  ERR_PTHREAD_CREATE, "ERR_PTHREAD_CREATE", "pthread_create impossible"},
#define ERR_RECV                 50
  {
  ERR_RECV, "ERR_RECV", "recv impossible"},
#define ERR_FOPEN                51
  {
  ERR_FOPEN, "ERR_FOPEN", "fopen impossible"},
#define ERR_GETCWD               52
  {
  ERR_GETCWD, "ERR_GETCWD", "getcwd impossible"},
#define ERR_SETUID               53
  {
  ERR_SETUID, "ERR_SETUID", "setuid impossible"},
#define ERR_RENAME               54
  {
  ERR_RENAME, "ERR_RENAME", "rename impossible"},
#define ERR_UNLINK		           55
  {
  ERR_UNLINK, "ERR_UNLINK", "unlink impossible"},
#define ERR_SELECT               56
  {
  ERR_SELECT, "ERR_SELECT", "select impossible"},
#define ERR_WAIT                 57
  {
  ERR_WAIT, "ERR_WAIT", "wait impossible"},
#define ERR_SETSID               58
  {
  ERR_SETSID, "ERR_SETSID", "setsid impossible"},
#define ERR_SETGID		           59
  {
  ERR_SETGID, "ERR_SETGID", "setgid impossible"},
#define ERR_GETGROUPS            60
  {
  ERR_GETGROUPS, "ERR_GETGROUPS", "getgroups impossible"},
#define ERR_SETGROUPS            61
  {
  ERR_SETGROUPS, "ERR_SETGROUPS", "setgroups impossible"},
#define ERR_UMASK                62
  {
  ERR_UMASK, "ERR_UMASK", "umask impossible"},
#define ERR_CREAT                63
  {
  ERR_CREAT, "ERR_CREAT", "creat impossible"},
#define ERR_SETSOCKOPT           64
  {
  ERR_SETSOCKOPT, "ERR_SETSOCKOPT", "setsockopt impossible"},
#define ERR_DIRECTIO             65
  {
  ERR_DIRECTIO, "ERR_DIRECTIO", "appel a directio impossible"},
#define ERR_GETRLIMIT            66
  {
  ERR_GETRLIMIT, "ERR_GETRLIMIT", "appel a getrlimit impossible"},
#define ERR_SETRLIMIT            67
  {
  ERR_SETRLIMIT, "ERR_SETRLIMIT", "appel a setrlimit"},
#define ERR_TRUNCATE		 68
  {
  ERR_TRUNCATE, "ERR_TRUNCATE", "appel a truncate impossible"},
#define ERR_PTHREAD_MUTEX_INIT   69
  {
  ERR_PTHREAD_MUTEX_INIT, "ERR_PTHREAD_MUTEX_INIT", "init d'un mutex"},
#define ERR_PTHREAD_COND_INIT    70
  {
  ERR_PTHREAD_COND_INIT, "ERR_PTHREAD_COND_INIT", "init d'une variable de condition"},
#define ERR_FCNTL                 71
  {
  ERR_FCNTL, "ERR_FCNTL", "call to fcntl is impossible"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

#define ERR_POSIX 1

static status_t __attribute__ ((__unused__)) tab_systeme_status[] =
{
  {
  0, "NO_ERROR", "Pas d'error"},
  {
  EPERM, "EPERM", "Reservee a root"},
  {
  ENOENT, "ENOENT", "No such file or directory"},
  {
  ESRCH, "ESRCH", "No such process"},
  {
  EINTR, "EINTR", "interrupted system call"},
  {
  EIO, "EIO", "I/O error"},
  {
  ENXIO, "ENXIO", "No such device or address"},
  {
  E2BIG, "E2BIG", "Arg list too long"},
  {
  ENOEXEC, "ENOEXEC", "Exec format error"},
  {
  EBADF, "EBADF", "Bad file number"},
  {
  ECHILD, "ECHILD", "No children"},
  {
  EAGAIN, "EAGAIN", "Resource temporarily unavailable"},
  {
  ENOMEM, "ENOMEM", "Not enough core"},
  {
  EACCES, "ENOMEM", "Permission denied"},
  {
  EFAULT, "EFAULT", "Bad address"},
  {
  ENOTBLK, "ENOTBLK", "Block device required"},
  {
  EBUSY, "EBUSY", "Mount device busy"},
  {
  EEXIST, "EEXIST", "File exists"},
  {
  EXDEV, "EXDEV", "Cross-device link"},
  {
  ENODEV, "ENODEV", "No such device"},
  {
  ENOTDIR, "ENOTDIR", "Not a directory"},
  {
  EISDIR, "EISDIR", "Is a directory"},
  {
  EINVAL, "EINVAL", "Invalid argument"},
  {
  ENFILE, "ENFILE", "File table overflow"},
  {
  EMFILE, "EMFILE", "Too many open files"},
  {
  ENOTTY, "ENOTTY", "Inappropriate ioctl for device"},
  {
  ETXTBSY, "ETXTBSY", "Text file busy"},
  {
  EFBIG, "EFBIG", "File too large"},
  {
  ENOSPC, "ENOSPC", "No space left on device"},
  {
  ESPIPE, "ESPIPE", "Illegal seek"},
  {
  EROFS, "EROFS", "Read only file system"},
  {
  EMLINK, "EMLINK", "Too many links"},
  {
  EPIPE, "EPIPE", "Broken pipe"},
  {
  EDOM, "EDOM", "Math arg out of domain of func"},
  {
  ERANGE, "ERANGE", "Math result not representable"},
  {
  ENOMSG, "ENOMSG", "No message of desired type"},
  {
  EIDRM, "EIDRM", "Identifier removed"},
  {
  ERR_NULL, "ERR_NULL", ""}
};

/* other codes families */
#define ERR_LRU           10
#define ERR_HASHTABLE     11
#define ERR_RPC           12
#define ERR_FSAL          13
#define ERR_MFSL          14
#define ERR_GHOSTFS       15
#define ERR_CACHE_INODE   16
#define ERR_CACHE_CONTENT 17

/* les prototypes des fonctions de la lib */

int SetNamePgm(char *nom);
int SetNameHost(char *nom);
int SetNameFileLog(char *nom);
int SetNameFunction(char *nom); /* thread safe */
char *ReturnNamePgm();
char *ReturnNameHost();
char *ReturnNameFileLog();
char *ReturnNameFunction();     /* thread safe */

int DisplayErrorStringLine(char *tampon, int num_family, int num_error, int status,
                           int ma_ligne);
int DisplayErrorFluxLine(FILE * flux, int num_family, int num_error, int status,
                         int ma_ligne);
int DisplayErrorFdLine(int fd, int num_family, int num_error, int status, int ma_ligne);
int DisplayErrorLogLine(int num_family, int num_error, int status, int ma_ligne);

#define DisplayErrorLog( a, b, c ) DisplayErrorLogLine( a, b, c, __LINE__ )
#define DisplayErrorFlux( a, b, c, d ) DisplayErrorFluxLine( a, b, c, d, __LINE__ )
#define DisplayErrorString( a, b, c, d ) DisplayErrorStringLine( a, b, c, d, __LINE__ )
#define DisplayErrorFd(a, b, c, d ) DisplayErrorFdLine( a, b, c, d, __LINE__ )

int DisplayLogString(char *tampon, char *format, ...);
int DisplayLogStringLevel(char *tampon, int level, char *format, ...);

int DisplayLog(char *format, ...);
int DisplayLogLevel(int level, char *format, ...);

int DisplayLogFlux(FILE * flux, char *format, ...);
int DisplayLogFluxLevel(FILE * flux, int level, char *format, ...);

int DisplayLogPath(char *path, char *format, ...);
int DisplayLogPathLevel(char *path, int level, char *format, ...);

int DisplayLogFd(int fd, char *format, ...);
int DisplayLogFdLevel(int fd, int level, char *format, ...);

/* AddFamilyError : not thread safe */
int AddFamilyError(int num_family, char *nom_family, family_error_t * tab_err);

int RemoveFamilyError(int num_family);

char *ReturnNameFamilyError(int num_family);

int InitDebug(int niveau_debug);        /* not thread safe */

int SetLevelDebug(int level_to_set);    /* not thread safe */

int ReturnLevelDebug();
int ReturnLevelAscii(char *LevelEnAscii);
char *ReturnLevelInt(int level);

/* A present les types et les fonctions pour les descripteurs de journaux */
typedef enum type_voie
{ V_STREAM = 1, V_BUFFER, V_FILE, V_FD, V_SYSLOG } type_log_stream_t;

typedef enum niveau
{ GANESHA_LOG_RIEN = 0, GANESHA_LOG_MAJOR = 1, GANESHA_LOG_CRITICAL = 2, GANESHA_LOG_EVENT = 3, GANESHA_LOG_DEBUG = 4
} niveau_t;

typedef enum aiguillage
{ SUP = 1, EXACT = 0, INF = -1 } aiguillage_t;

typedef union desc_voie
{
  FILE *flux;
  char *buffer;
  char path[LOCALMAXPATHLEN];
  int fd;
} desc_log_stream_t;

typedef struct voie
{
  desc_log_stream_t desc;
  type_log_stream_t type;
  niveau_t niveau;
  aiguillage_t aiguillage;
  struct voie *suivante;
} log_stream_t;

typedef struct journal
{
  int nb_voies;
  log_stream_t *liste_voies;
  log_stream_t *fin_liste_voies;

} log_t;

#define LOG_INITIALIZER { 0, NULL, NULL }

int DisplayLogJd(log_t jd, char *format, ...);
int DisplayLogJdLevel(log_t jd, int level, char *format, ...);
int DisplayErrorJdLine(log_t jd, int num_family, int num_error, int status, int ma_ligne);

#define DisplayErrorJd(a, b, c, d ) DisplayErrorJdLine( a, b, c, d, __LINE__ )

int AddLogStreamJd(log_t * pjd,
                   type_log_stream_t type,
                   desc_log_stream_t desc_voie, niveau_t niveau, aiguillage_t aiguillage);

int log_vsnprintf(char *out, size_t n, char *format, va_list arguments);
#define log_vsprintf( out, format, arguments ) log_vsnprintf( out, (size_t)LOG_MAX_STRLEN, format, arguments )
int log_sprintf(char *out, char *format, ...);
int log_snprintf(char *out, size_t n, char *format, ...);
int log_vfprintf(FILE *, char *format, va_list arguments);
#define log_vprintf( format, arguments ) log_vfprintf( stdout, format, arguments )
int log_fprintf(FILE * file, char *format, ...);
int log_printf(char *format, ...);
#endif
