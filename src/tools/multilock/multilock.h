/*
 * Copyright IBM Corporation, 2012
 *  Contributor: Frank Filz  <ffilz@us.ibm.com>
 *
 *
 * This software is a server that implements the NFS protocol.
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
 *
 */

#ifndef _MULTILOCK_H
#define _MULTILOCK_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define MAXSTR 1024
#define MAXFPOS 16

int readln(FILE * in, char *buf, int buflen);

typedef struct response_t    response_t;
typedef struct command_def_t command_def_t;
typedef struct client_t      client_t;
typedef struct token_t       token_t;

struct token_t
{
  const char * t_name;
  int          t_len;
  int          t_value;
};

/* Commands to Client */
/* Responses use the same strings */
typedef enum commands_t
{
  CMD_OPEN,
  CMD_CLOSE,
  CMD_LOCKW,
  CMD_LOCK,
  CMD_UNLOCK,
  CMD_TEST,
  CMD_LIST,
  CMD_HOP,
  CMD_UNHOP,
  CMD_SEEK,
  CMD_READ,
  CMD_WRITE,
  CMD_COMMENT,
  CMD_ALARM,
  CMD_HELLO,
  CMD_QUIT,
  NUM_COMMANDS
} commands_t;

typedef enum requires_more_t
{
  REQUIRES_MORE,
  REQUIRES_NO_MORE,
  REQUIRES_EITHER,
} requires_more_t;

struct command_def_t
{
  const char      * cmd_name;
  int               cmd_len;
};

struct client_t
{
  client_t        * c_next;
  client_t        * c_prev;
  int               c_socket;
  struct sockaddr   c_addr;
  char              c_name[MAXSTR];
  FILE            * c_input;
  FILE            * c_output;
  int               c_refcount;
};

typedef enum status_t
{
  STATUS_OK,
  STATUS_AVAILABLE,
  STATUS_GRANTED,
  STATUS_DENIED,
  STATUS_DEADLOCK,
  STATUS_CONFLICT,
  STATUS_CANCELED,
  STATUS_COMPLETED,
  STATUS_ERRNO,
  STATUS_PARSE_ERROR,
  STATUS_ERROR     // must be last
} status_t;

extern char       errdetail[MAXSTR * 2];
extern char       badtoken[MAXSTR];
extern client_t * client_list;
extern FILE     * input;
extern FILE     * output;
extern int        script;
extern int        quiet;
extern int        duperrors;
extern int        strict;
extern int        error_is_fatal;
extern int        syntax;
extern long int   lno;
extern long int   global_tag;

long int get_global_tag(int increment);

#define fprintf_stderr(fmt, args...) \
  do {                               \
    if(duperrors)                    \
      fprintf(output, fmt, ## args); \
    fprintf(stderr, fmt, ## args);   \
  } while(0)

#define fatal(str, args...)       \
  do {                            \
    fprintf_stderr(str, ## args); \
    fprintf_stderr("FAIL\n");     \
    fflush(output);               \
    fflush(stderr);               \
    exit(1);                      \
  } while(0)

#define show_usage(ret, fmt, args...)  \
  do {                                 \
    fprintf_stderr(fmt, ## args);      \
    fprintf_stderr("%s", usage);       \
    fflush(stderr);                    \
    fflush(stdout);                    \
    exit(ret);                         \
  } while(0)

char * get_command(char * line, commands_t * cmd);
char * get_tag(char * line, response_t * resp, int required, requires_more_t requires_more);
char * get_rq_tag(char * line, response_t * req, int required, requires_more_t requires_more);
char * get_long(char * line, long int * value, requires_more_t requires_more, const char * invalid);
char * get_longlong(char * line, long long int * value, requires_more_t requires_more, const char * invalid);
char * get_fpos(char * line, long int * fpos, requires_more_t requires_more);
char * get_str(char * line, char * str, long long int * len, requires_more_t requires_more);
char * get_lock_type(char * line, int * type);
char * get_client(char * line, client_t ** pclient, int create, requires_more_t requires_more);
char * get_token(char * line, char ** token, int * len, int optional, requires_more_t requires_more, const char * invalid);
char * get_token_value(char * line, int * value, token_t * tokens, int optional, requires_more_t requires_more, const char * invalid);
char * get_status(char * line, response_t * resp);
char * get_open_opts(char * line, long int * fpos, int * flags, int * mode);
char * parse_response(char * line, response_t * resp);
char * parse_request(char * line, response_t * req, int no_tag);
char * get_on_off(char * line, int * value);
char * SkipWhite(char * line, requires_more_t requires_more, const char * who);

void respond(response_t * resp);
const char * str_lock_type(int type);
void sprintf_resp(char * line, const char * lead, response_t * resp);
void sprintf_req(char * line, const char * lead, response_t * req);
void send_cmd(response_t * req);

const char *str_status(status_t status);

const char * str_read_write_flags(int flags);
int sprintf_open_flags(char * line, int flags);

status_t parse_status(char * str, int len);

void free_response(response_t * resp, response_t ** list);
void free_client(client_t * client);

int compare_responses(response_t * expected, response_t * received);
void add_response(response_t * resp, response_t ** list);
response_t * check_expected_responses(response_t *expected_responses, response_t * client_resp);

struct response_t
{
  response_t    * r_next;
  response_t    * r_prev;
  client_t      * r_client;
  commands_t      r_cmd;
  status_t        r_status;
  long int        r_tag;
  long int        r_fpos;
  long int        r_fno;
  long int        r_secs;
  long long int   r_start;
  long long int   r_length;
  long int        r_pid;
  int             r_lock_type;
  int             r_flags;
  int             r_mode;
  long int        r_errno;
  char            r_data[MAXSTR * 2];
  char            r_original[MAXSTR * 3];
};

extern command_def_t commands[NUM_COMMANDS+1];

/* Command forms
 *
 * General format
 * tag cmd     [options] - tag is numeric sequence
 * tag OPEN    fpos {ro|wo|rw} [create] filename
 * tag CLOSE   fpos
 * tag LOCK    fpos type start length - type is READ or WRITE
 * tag LOCKW   fpos type start length
 * tag UNLOCK  fpos start length
 * tag TEST    fpos type start length
 * tag LIST    fpos start length
 * tag SEEK    fpos start
 * tag READ    fpos length
 * tag WRITE   fpos "string"
 * tag COMMENT "string"
 * tag ALARM   seconds
 * tag HELLO   "name" (command ignored, really just a response to server)
 * tag QUIT    (tag is optional, if not present, tag = -1)
 */

/* Response forms
 *
 * tag cmd     ERRNO value "string" - for all commands, result was an error
 * tag OPEN    OK fpos fd
 * tag CLOSE   OK fpos
 * tag LOCK    GRANTED fpos type start length
 * tag LOCK    DENIED fpos type start length
 * tag LOCKW   GRANTED fpos type start length
 * tag LOCKW   CANCELED fpos type start length
 * tag UNLOCK  GRANTED fpos type start length
 * tag TEST    GRANTED fpos type start length
 * tag TEST    CONFLICT fpos pid type start length
 * tag LIST    GRANTED fpos start length (returned if no locks to list)
 * tag LIST    DENIED fpos start length (returned if list had locks)
 * tag LIST    CONFLICT fpos pid type start length (returned for each lock in list)
 * tag SEEK    OK fpos
 * tag READ    OK fpos len "data"
 * tag WRITE   OK fpos len
 * tag COMMENT OK "string"
 * tag ALARM   OK seconds
 * tag ALARM   CANCELED remain
 * tag ALARM   COMPLETED
 * 0   HELLO   OK "name"
 * tag QUIT    OK
 */

#endif /* _MULTILOCK_H */
