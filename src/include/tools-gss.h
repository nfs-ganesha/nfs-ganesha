#ifndef _TOOLS_GSS_H
#define _TOOLS_GSS_H

/* Un tas d'include pour avoir les bindings standards */
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "aglae.h"              /* Mes routines de gestion des logs */
#include <dce/gssapi.h>         /* Header de la gssapi */

#define TOKEN_NOOP              (1<<0)
#define TOKEN_CONTEXT           (1<<1)
#define TOKEN_DATA              (1<<2)
#define TOKEN_MIC               (1<<3)
#define TOKEN_CONTEXT_NEXT      (1<<4)
#define TOKEN_WRAPPED           (1<<5)
#define TOKEN_ENCRYPTED         (1<<6)
#define TOKEN_SEND_MIC          (1<<7)
#define TOKEN_NOOP              (1<<0)
#define TOKEN_CONTEXT           (1<<1)
#define TOKEN_DATA              (1<<2)
#define TOKEN_MIC               (1<<3)
#define TOKEN_CONTEXT_NEXT      (1<<4)
#define TOKEN_WRAPPED           (1<<5)
#define TOKEN_ENCRYPTED         (1<<6)
#define TOKEN_SEND_MIC          (1<<7)

void sperror_gss(char *str, OM_uint32 major, OM_uint32 minor);

int write_tok(int s, gss_buffer_t tok);
int read_tok(int s, gss_buffer_t tok);
int recv_msg(int fd, char *msg, gss_ctx_id_t context, char *errbuf);
int send_msg(int fd, char *msg, gss_ctx_id_t context, char *errbuf);
int recv_token(int s, int *flags, gss_buffer_t tok);
int send_token(int s, int flags, gss_buffer_t tok)
#endif
