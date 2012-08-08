/*
 * Copyright (C) Paul Sheer, 2012
 * Author: Paul Sheer paulsheer@gmail.com
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
 * ------------- 
 */



#include <pthread.h>


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>


#ifndef SOL_TCP
#define SOL_TCP         6
#endif

#define MARSHALLER_PORT         31337


#include "sockbuf.h"
#include "nodedb.h"
#include "connection.h"
#include "ops.h"
#include "encoding.h"
#include "marshal.h"


struct marshal {
    int kill;
    int listen_sock;
    struct nodedb *db;
};


static void v_marshal_log_msg (int err, const char *fmt, va_list ap)
{
    char t[64];
    time_t now;
    time (&now);
    strftime (t, sizeof (t), "%Y-%m-%d %H:%M:%S", localtime (&now));
    printf ("%s: ", t);
    vfprintf (stdout, fmt, ap);
    if (err)
        printf (": [%s]", strerror (err));
    printf ("\n");
}

void marshal_log_msg (const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    v_marshal_log_msg (0, fmt, ap);
    va_end (ap);
}

void marshal_log_err (int err, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    v_marshal_log_msg (err, fmt, ap);
    va_end (ap);
}



static int listen_bind_socket (const char *address)
{
    struct sockaddr_in a;
    int s;
    int yes;
    if ((s = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        marshal_log_err(errno, "socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)");
        return -1;
    }
    yes = 1;
    if (setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (char *) &yes, sizeof (yes)) < 0) {
        marshal_log_err(errno, "setsockopt(SO_REUSEADDR)");
        close (s);
        return -1;
    }
    setsockopt (s, SOL_TCP, TCP_NODELAY, &yes, sizeof (int));
    memset (&a, '\0', sizeof (s));
    a.sin_addr.s_addr = inet_addr (address);
    a.sin_port = htons (MARSHALLER_PORT);
    a.sin_family = AF_INET;

    if (bind (s, (struct sockaddr *) &a, sizeof (a)) < 0) {
        marshal_log_err(errno, "bind(%s)", address);
        close (s);
        return -1;
    }
    listen (s, 10);
    return s;
}

static int connect_socket (const char *address)
{
    struct sockaddr_in a;
    int s, yes = 1;
    if ((s = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
        return -1;

    setsockopt (s, SOL_TCP, TCP_NODELAY, &yes, sizeof (int));

    memset (&a, '\0', sizeof (s));
    a.sin_addr.s_addr = inet_addr (address);
    a.sin_port = htons (MARSHALLER_PORT);
    a.sin_family = AF_INET;

    if (connect (s, (struct sockaddr *) &a, sizeof (a)) < 0) {
        close (s);
        return -1;
    }
    return s;
}

static struct connection *_connection_new (struct marshal *marshal, int sock)
{
    struct connection *c;
    c = (struct connection *) malloc (sizeof (*c));
    memset (c, '\0', sizeof (*c));

    c->marshal = marshal;
    c->db = marshal ? marshal->db : NULL;
    c->sock = sock;
    c->decode_errortext = "";

    sockbuf_init (&c->input, sock);
    sockbuf_init (&c->output, sock);

    return c;
}

struct connection *connection_new (void)
{
    int sock;
    sock = connect_socket ("127.0.0.1");
    if (sock < 0) {
        perror ("connect 127.0.0.1");
        return NULL;
    }
    return _connection_new (NULL, sock);
}

void connection_free (struct connection *c)
{
    sockbuf_free (&c->input);
    sockbuf_free (&c->output);

    if (c->sock >= 0) {
        shutdown (c->sock, 2);
        close (c->sock);
    }

    free (c);
}

int demarshal(struct connection *conn);

static void *connection_run (void *arg)
{
    int r;
    struct connection *c;
    c = (struct connection *) arg;

    while (!(r = demarshal (c))) {
/*         _nodedb_print(c->marshal->db); */
        encode_flush (c);
    }

    if (r != -1) {
        marshal_log_msg ("error: encode_error=%d, decode_error=%d, input_error=%d, output_error=%d, text=%s",
                 c->encode_error, c->decode_error, sockbuf_error (&c->input), sockbuf_error (&c->output),
                 c->decode_errortext);
    }

    marshal_log_msg ("disconnect");

    connection_free (c);

    return NULL;
}

struct marshal *marshal_new (struct nodedb *db)
{
    struct marshal *m;
    m = (struct marshal *) malloc (sizeof (*m));
    memset (m, '\0', sizeof (*m));
    m->listen_sock = listen_bind_socket ("127.0.0.1");
    if (m->listen_sock < 0)
        exit (1);
    m->db = db;
    return m;
}

static void marshal_kill (struct marshal *m)
{
    int sock;
    m->kill = 1;
    sock = connect_socket ("127.0.0.1");
    shutdown (sock, 2);
    close (sock);
}

void marshal_free (struct marshal *m)
{
    marshal_kill (m);
    while (m->kill != 2)
        usleep (100000);

    shutdown (m->listen_sock, 2);
    close (m->listen_sock);
    free (m);
}

void marshal_run (struct marshal *m)
{
    while (!m->kill) {
        int sock, yes = 1;
        struct sockaddr_in addr;
        socklen_t addrlen;
        addrlen = sizeof (addr);
        sock = accept (m->listen_sock, (struct sockaddr *) &addr, &addrlen);
        if (m->kill) {
            shutdown (sock, 2);
            close (sock);
            break;
        }
        if (sock < 0)
            continue;


        if (setsockopt (sock, SOL_TCP, TCP_NODELAY, &yes, sizeof (int)))
            marshal_log_err(errno, "setsockopt(TCP_NODELAY)");

        {
            pthread_t thread;
            pthread_attr_t attr;
            struct connection *c;

            pthread_attr_init (&attr);
            pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

            c = _connection_new (m, sock);
            if (pthread_create (&thread, &attr, connection_run, c))
                connection_free (c);
        }
    }
    m->kill = 2;
    marshal_log_msg("demarshaller done");
}





void marshal_create_process (void)
{
    int fd, new_fd, log_fd;

#define BG_LOGFILE              "/tmp/nfs-ganesha-demarshaller.log"

    printf ("creating background process with output redirected to " BG_LOGFILE "\n");

    fflush (stdout);
    fflush (stderr);

    if (fork ())
        return;

    setpgrp ();

    signal (SIGPIPE, SIG_IGN);
    signal (SIGTSTP, SIG_IGN);
    signal (SIGTTIN, SIG_IGN);
    signal (SIGTTOU, SIG_IGN);

    if ((fd = open ("/dev/null", O_RDWR)) < 0)
        _exit (1);
    if ((log_fd = open (BG_LOGFILE, O_CREAT | O_APPEND | O_WRONLY, 0600)) < 0)
        _exit (1);

    close (0);
    close (1);
    close (2);
    new_fd = dup (fd);
#if 1
    new_fd = dup (log_fd);
    new_fd = dup (log_fd);
#else
    new_fd = dup (fd);
    new_fd = dup (fd);
#endif
    (void) new_fd;
    close (fd);
    close (log_fd);

    setvbuf (stdout, malloc (BUFSIZ), _IOLBF, BUFSIZ);
    setvbuf (stderr, malloc (BUFSIZ), _IOLBF, BUFSIZ);

    marshal_log_msg ("starting demarshaller");

    marshal_run (marshal_new (nodedb_new ()));
    exit (0);
}





