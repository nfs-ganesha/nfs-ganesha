/*
 * Copyright (C) Red Hat  Inc., 2017
 *  Contributor: Frank Filz <ffilzlnx@mindspring.com>
 *  Contributor: Soumya koduri <skoduri@redhat.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 *
 */

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include <libgen.h>
#include "multilock.h"
#include "../../include/gsh_list.h"
#include <glusterfs/api/glfs.h>
#include <glusterfs/api/glfs-handles.h>

/* command line syntax */

char options[] = "c:qdx:s:n:p:v:g:h?";
char usage[] =
	"Usage: ml_glusterfs_client -s server -v volname -p port -n name -g glusterserver [-q] [-d] [-c path]\n"
	"       ml_glusterfs_client -v volname -g glusterserver -x script [-q] [-d] [-c path]\n"
	"       ml_glusterfs_client -v volname -g glusterserver [-q] [-d] [-c path]\n"
	"  ml_glusterfs_client may be run in three modes\n"
	"  - In the first mode, the client will be driven by a master.\n"
	"  - In the second mode, the client is driven by a script.\n"
	"  - In the third mode, the client interractive.\n"
	"  -s server      - specify the master's hostname or IP address\n"
	"  -p port        - specify the master's port number\n"
	"  -n name        - specify the client's name\n"
	"  -x script      - specify the name of a script to execute\n"
	"  -q             - specify quiet mode\n"
	"  -d             - specify dup errors mode (errors are sent to stdout and stderr)\n"
	"  -c path        - chdir\n"
	"  -g glusterserver -specify the hostname or IP address of glusterfs server\n"
	"  -v volname     - glusterfs volume name\n";

#define NUM_WORKER 4
#define POLL_DELAY 10

enum thread_type {
	THREAD_NONE,
	THREAD_MAIN,
	THREAD_WORKER,
	THREAD_POLL,
	THREAD_CANCEL
};

struct work_item {
	struct glist_head queue;
	struct glist_head fno_work;
	struct response resp;
	enum thread_type work_owner;
	pthread_t work_thread;
	time_t next_poll;
};

char server[MAXSTR];
char name[MAXSTR];
char portstr[MAXSTR];
char volname[MAXSTR];
char glusterserver[MAXSTR];
int port;
char line[MAXXFER];
long int alarmtag;
struct glfs_object *handles[MAXFPOS + 1];
struct glfs_fd *fds[MAXFPOS + 1];
enum lock_mode lock_mode[MAXFPOS + 1];

/* Global variables to manage work list */
struct glist_head fno_work[MAXFPOS + 1];
struct glist_head work_queue = GLIST_HEAD_INIT(work_queue);
struct glist_head poll_queue = GLIST_HEAD_INIT(poll_queue);
pthread_t threads[NUM_WORKER + 1];
pthread_mutex_t work_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t work_cond = PTHREAD_COND_INITIALIZER;

enum thread_type a_worker = THREAD_WORKER;
enum thread_type a_poller = THREAD_POLL;

/* The CephFS mount */
struct ceph_mount_info *cmount;


glfs_t  *fs;
struct glusterfs_fs *gl_fs;

void openserver(void)
{
	struct addrinfo *addr;
	int rc;
	struct addrinfo hint;
	int sock;
	struct response resp;

	if (!quiet)
		fprintf(stdout, "server=%s port=%d name=%s\n", server, port,
			name);

	memset(&hint, 0, sizeof(hint));
	hint.ai_family = AF_INET;
	hint.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo(server, portstr, &hint, &addr);

	if (rc != 0)
		fatal("getaddrinfo error %d \"%s\"\n", rc, gai_strerror(rc));

	rc = socket(addr[0].ai_family, SOCK_STREAM, 0);

	if (rc == -1)
		fatal("socket failed with ERRNO %d \"%s\"\n", errno,
		      strerror(errno));

	sock = rc;

	rc = connect(sock, addr[0].ai_addr, addr[0].ai_addrlen);

	if (rc == -1)
		fatal("connect failed with ERRNO %d \"%s\"\n", errno,
		      strerror(errno));

	input = fdopen(sock, "r");

	if (input == NULL)
		fatal(
		     "Could not create input stream from socket ERROr %d \"%s\"\n",
		     errno, strerror(errno));

	output = fdopen(sock, "w");

	if (output == NULL)
		fatal(
		     "Could not create output stream from socket ERROr %d \"%s\"\n",
		     errno, strerror(errno));

	if (!quiet)
		fprintf(stdout, "connected to server %s:%d\n", server, port);

	resp.r_cmd = CMD_HELLO;
	resp.r_status = STATUS_OK;
	resp.r_tag = 0;
	array_strcpy(resp.r_data, name);
	respond(&resp);
}

bool do_fork(struct response *resp, bool use_server)
{
	pid_t forked;

	if (!use_server) {
		fprintf_stderr("FORK may only be used in server mode\n");
		return false;
	}

	forked = fork();

	if (forked < 0) {
		/* Error */
		resp->r_status = STATUS_OK;
		resp->r_errno = errno;

		if (!quiet)
			fprintf(stdout, "fork failed %d (%s)\n",
				(int) resp->r_errno, strerror(resp->r_errno));

		return true;
	} else if (forked == 0) {
		/* Parent sends a FORK response */
		array_strcpy(resp->r_data, name);
		resp->r_status = STATUS_OK;
		if (!quiet)
			fprintf(stdout, "fork succeeded\n");
		return true;
	}

	if (!quiet)
		fprintf(stdout, "forked\n");

	/* This is the forked process */

	/* First close the old output. */
	fclose(output);

	/* Setup the new client name. */
	array_strcpy(name, resp->r_data);

	/* Then open a new connection to the server. */
	openserver();

	/* Response already sent. */
	return false;
}

void command(void)
{
}

void sighandler(int sig)
{
	struct response resp;

	switch (sig) {
	case SIGALRM:
		resp.r_cmd = CMD_ALARM;
		resp.r_tag = alarmtag;
		resp.r_status = STATUS_COMPLETED;
		alarmtag = 0;
		respond(&resp);
		break;

	case SIGPIPE:
	case SIGIO:
		break;
	}
}

void do_alarm(struct response *resp)
{
	unsigned int remain;

	remain = alarm(resp->r_secs);

	if (remain != 0) {
		struct response resp2;

		resp2.r_cmd = CMD_ALARM;
		resp2.r_tag = alarmtag;
		resp2.r_secs = remain;
		resp2.r_status = STATUS_CANCELED;
		respond(&resp2);
	}

	if (resp->r_secs != 0)
		alarmtag = resp->r_tag;
	else
		alarmtag = 0;

	resp->r_status = STATUS_OK;
}

void do_open(struct response *resp)
{
	struct glfs_object *glhandle = NULL, *parent = NULL;
	struct stat sb;
	struct glfs_fd *glfd = NULL;

	if (fds[resp->r_fpos] != NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EINVAL;
		array_strcpy(errdetail, "fpos in use");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	if ((resp->r_flags & O_CREAT) != 0) {
		char fullpath[PATH_MAX];
		char *name;
		char *path;

		array_strcpy(fullpath, resp->r_data);

		name = basename(fullpath);
		path = dirname(fullpath);
		fprintf_stderr("path = '%s' name = '%s'\n", path, name);

		parent = glfs_h_lookupat(fs, NULL, path, &sb, 0);

		if (parent != NULL) {
			glhandle = glfs_h_creat(fs, parent, name, resp->r_flags,
						resp->r_mode, &sb);

			/* Release the parent directory. */
			glfs_h_close(parent);

			if (!glhandle) {
				resp->r_status = STATUS_ERRNO;
				resp->r_errno = errno;
				array_strcpy(errdetail, "glfs_h_creat");
				return;
			}
		} else {
			resp->r_status = STATUS_ERRNO;
			resp->r_errno = errno;
			array_sprintf(errdetail, "glfs_h_lookupat %s", path);
			return;
		}
	} else {
		glhandle = glfs_h_lookupat(fs, NULL, resp->r_data, &sb, 0);
	}
	if (glhandle != NULL) {
		glfd = glfs_h_open(fs, glhandle, resp->r_flags);

		if (!glfd) {
			resp->r_status = STATUS_ERRNO;
			resp->r_errno = errno;
			array_strcpy(errdetail, "glfs_h_open");
			return;
		}
	} else {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = errno;
		array_strcpy(errdetail, "glfs_h_lookupat");
		return;
	}

	if (!glfd) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = errno;
		array_strcpy(badtoken, resp->r_data);
		return;
	}

	fds[resp->r_fpos] = glfd;
	handles[resp->r_fpos] = glhandle;
	lock_mode[resp->r_fpos] = (enum lock_mode) resp->r_lock_type;
	resp->r_fno = resp->r_fpos;
	resp->r_status = STATUS_OK;
}

void do_write(struct response *resp)
{
	long long int rc;

	if (resp->r_fpos != 0 && fds[resp->r_fpos] == NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		array_strcpy(errdetail, "Invalid file number");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	rc = glfs_write(fds[resp->r_fpos], resp->r_data, resp->r_length, 0);

	if (rc < 0) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = -rc;
		array_strcpy(errdetail, "Write failed");
		array_sprintf(badtoken, "%lld", resp->r_length);
		return;
	}

	if (rc != resp->r_length) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EIO;
		array_strcpy(errdetail, "Short write");
		array_sprintf(badtoken, "%lld", rc);
		return;
	}

	resp->r_status = STATUS_OK;
}

void do_read(struct response *resp)
{
	long long int rc;

	if (resp->r_fpos != 0 && fds[resp->r_fpos] == NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		array_strcpy(errdetail, "Invalid file number");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	if (resp->r_length > MAXSTR)
		resp->r_length = MAXSTR;
#ifdef USE_GLUSTER_STAT_FETCH_API
	rc = glfs_read(fds[resp->r_fpos], resp->r_data, resp->r_length,
			0, NULL);
#else
	rc = glfs_read(fds[resp->r_fpos], resp->r_data, resp->r_length, 0);
#endif
	if (rc < 0) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = -rc;
		array_strcpy(errdetail, "Read failed");
		array_sprintf(badtoken, "%lld", resp->r_length);
		return;
	}

	resp->r_data[rc] = '\0';
	resp->r_length = strlen(resp->r_data);
	resp->r_status = STATUS_OK;
}

void do_seek(struct response *resp)
{
	int rc;

	if (resp->r_fpos != 0 && fds[resp->r_fpos] == NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		array_strcpy(errdetail, "Invalid file number");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	rc = glfs_lseek(fds[resp->r_fpos], resp->r_start, SEEK_SET);

	if (rc < 0) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = -rc;
		array_strcpy(errdetail, "Seek failed");
		array_sprintf(badtoken, "%lld", resp->r_start);
		return;
	}

	resp->r_status = STATUS_OK;
}

void free_work(struct work_item *work)
{
	/* Make sure the item isn't on any queues */
	glist_del(&work->fno_work);
	glist_del(&work->queue);

	free(work);
}

void cancel_work_item(struct work_item *work)
{
	switch (work->work_owner) {
	case THREAD_NONE:
	case THREAD_MAIN:
	case THREAD_CANCEL:
		/* nothing special to do */
		break;

	case THREAD_WORKER:
	case THREAD_POLL:
		/* Mark the work item to be canceled */
		work->work_owner = THREAD_CANCEL;

		pthread_kill(work->work_thread, SIGIO);

		/* Wait for thread to be done or cancel the work */
		while (work->work_owner != THREAD_NONE)
			pthread_cond_wait(&work_cond, &work_mutex);
	}

	/* Done with the work item, free the memory. */
	free_work(work);
}

static inline long long int lock_end(struct response *req)
{
	if (req->r_length == 0)
		return LLONG_MAX;

	return req->r_start + req->r_length;
}

void cancel_work(struct response *req)
{
	struct glist_head cancel_work = GLIST_HEAD_INIT(cancel_work);
	struct glist_head *glist;
	struct work_item *work;
	bool start_over = true;

	pthread_mutex_lock(&work_mutex);

	while (start_over) {
		start_over = false;

		glist_for_each(glist, fno_work + req->r_fpos) {
			work = glist_entry(glist, struct work_item, fno_work);
			if (work->resp.r_start >= req->r_start &&
			    lock_end(&work->resp) <= lock_end(req)) {
				/* Do something */
				cancel_work_item(work);

				/* List may be messed up */
				start_over = true;
				break;
			}
		}
	}

	pthread_mutex_unlock(&work_mutex);
}

/* Must only be called from main thread...*/
int schedule_work(struct response *resp)
{
	struct work_item *work = calloc(1, sizeof(*work));

	if (work == NULL) {
		errno = ENOMEM;
		return -1;
	}

	pthread_mutex_lock(&work_mutex);

	work->resp = *resp;

	work->work_owner = THREAD_NONE;
	glist_add_tail(&work_queue, &work->queue);
	glist_add_tail(fno_work + resp->r_fpos, &work->fno_work);

	/* Signal to the worker and polling threads there is new work */
	pthread_cond_broadcast(&work_cond);

	pthread_mutex_unlock(&work_mutex);

	return 0;
}

bool do_lock(struct response *resp, enum thread_type thread_type)
{
	int rc;
	struct flock lock;
	bool retry = false;
	uint64_t owner;

	if (resp->r_fpos != 0 && fds[resp->r_fpos] == NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		array_strcpy(errdetail, "Invalid file number");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return true;
	}

	switch (lock_mode[resp->r_fpos]) {
	case LOCK_MODE_POSIX:
		if (resp->r_cmd == CMD_LOCKW) {
			retry = true;
		}
		owner = getpid();
		break;

	case LOCK_MODE_OFD:
		if (resp->r_cmd == CMD_LOCKW) {
			retry = true;
		}
		owner = resp->r_fpos;
		break;
	}

	lock.l_whence = SEEK_SET;
	lock.l_type = resp->r_lock_type;
	lock.l_start = resp->r_start;
	lock.l_len = resp->r_length;
	lock.l_pid = 0;

	rc = glfs_fd_set_lkowner(fds[resp->r_fpos], (void *)&owner,
				 sizeof(owner));
	if (rc < 0) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = -rc;
		array_strcpy(errdetail, "setting lkowner failed");
		array_sprintf(badtoken, "%lld %lld", resp->r_start,
			      resp->r_length);
		return false;
	}

	rc = glfs_posix_lock(fds[resp->r_fpos], F_SETLK, &lock);

	if (rc < 0)
		rc = -errno;

	if (rc == -EAGAIN && retry && thread_type == THREAD_MAIN) {
		/* We need to schedule OFD blocked lock */
		rc = schedule_work(resp);

		/* Check for scheduling success */
		if (rc >= 0)
			return false;
	}

	if (rc < 0) {
		if (rc == -EAGAIN) {
			if (retry) {
				/* Let caller know we didn't complete */
				return false;
			}
			resp->r_status = STATUS_DENIED;
		} else if (rc == -EINTR) {
			resp->r_status = STATUS_CANCELED;
		} else if (rc == -EDEADLK) {
			resp->r_status = STATUS_DEADLOCK;
		} else {
			resp->r_status = STATUS_ERRNO;
			resp->r_errno = -rc;
			array_strcpy(errdetail, "Lock failed");
			array_sprintf(badtoken, "%s %lld %lld",
				      str_lock_type(lock.l_type), resp->r_start,
				      resp->r_length);
		}
	} else
		resp->r_status = STATUS_GRANTED;

	return true;
}

void do_hop(struct response *resp)
{
	int rc;
	int pos;
	struct flock lock;
	uint64_t owner;

	if (resp->r_fpos != 0 && fds[resp->r_fpos] == NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		array_strcpy(errdetail, "Invalid file number");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	switch (lock_mode[resp->r_fpos]) {
	case LOCK_MODE_POSIX:
		owner = getpid();
		break;

	case LOCK_MODE_OFD:
		owner = resp->r_fpos;
		break;
	}

	for (pos = resp->r_start; pos < resp->r_start + resp->r_length; pos++) {
		if ((pos == 0) || (pos == (resp->r_start + resp->r_length - 1)))
			lock.l_start = pos;
		else if ((pos & 1) == 0)
			lock.l_start = pos - 1;
		else
			lock.l_start = pos + 1;

		lock.l_whence = SEEK_SET;
		lock.l_type = resp->r_lock_type;
		lock.l_len = 1;
		lock.l_pid = 0;

		rc = glfs_fd_set_lkowner(fds[resp->r_fpos], (void *)&owner,
					 sizeof(owner));
		if (rc < 0) {
			resp->r_status = STATUS_ERRNO;
			resp->r_errno = -rc;
			array_strcpy(errdetail, "setting lkowner failed");
			array_sprintf(badtoken, "%lld %lld", resp->r_start,
				      resp->r_length);
			return;
		}

		rc = glfs_posix_lock(fds[resp->r_fpos], F_SETLK, &lock);

		if (rc < 0)
			rc = -errno;
		if (rc < 0) {
			if (rc == -EAGAIN) {
				resp->r_start = lock.l_start;
				resp->r_length = 1;
				resp->r_status = STATUS_DENIED;
				break;
			} else {
				resp->r_status = STATUS_ERRNO;
				resp->r_errno = -rc;
				array_strcpy(errdetail, "Hop failed");
				array_sprintf(badtoken, "%s %ld",
					      str_lock_type(resp->r_lock_type),
					      lock.l_start);
				break;
			}
		} else
			resp->r_status = STATUS_GRANTED;
	}

	if (resp->r_status != STATUS_GRANTED) {
		lock.l_whence = SEEK_SET;
		lock.l_type = F_UNLCK;
		lock.l_start = resp->r_start;
		lock.l_len = resp->r_length;
		lock.l_pid = 0;

		rc = glfs_fd_set_lkowner(fds[resp->r_fpos], (void *)&owner,
					 sizeof(owner));
		if (rc < 0) {
			resp->r_status = STATUS_ERRNO;
			resp->r_errno = -rc;
			array_strcpy(errdetail, "setting lkowner failed");
			array_sprintf(badtoken, "%lld %lld", resp->r_start,
				      resp->r_length);
			return;
		}

		rc = glfs_posix_lock(fds[resp->r_fpos], F_SETLK, &lock);

		if (rc < 0)
			rc = -errno;
		if (rc < 0) {
			resp->r_status = STATUS_ERRNO;
			resp->r_errno = -rc;
			array_strcpy(errdetail, "Hop Unlock failed");
			array_sprintf(badtoken, "%lld %lld", resp->r_start,
				      resp->r_length);
		}
	}
}

void do_unhop(struct response *resp)
{
	int rc;
	int pos;
	struct flock lock;
	uint64_t owner;

	if (resp->r_fpos != 0 && fds[resp->r_fpos] == NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		array_strcpy(errdetail, "Invalid file number");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	switch (lock_mode[resp->r_fpos]) {
	case LOCK_MODE_POSIX:
		owner = getpid();
		break;

	case LOCK_MODE_OFD:
		owner = resp->r_fpos;
		break;
	}

	for (pos = resp->r_start; pos < resp->r_start + resp->r_length; pos++) {
		if ((pos == 0) || (pos == (resp->r_start + resp->r_length - 1)))
			lock.l_start = pos;
		else if ((pos & 1) == 0)
			lock.l_start = pos - 1;
		else
			lock.l_start = pos + 1;

		lock.l_whence = SEEK_SET;
		lock.l_type = resp->r_lock_type;
		lock.l_len = 1;
		lock.l_pid = 0;

		rc = glfs_posix_lock(fds[resp->r_fpos], F_SETLK, &lock);
		if (rc < 0)
			rc = -errno;

		if (rc < 0) {
			resp->r_status = STATUS_ERRNO;
			resp->r_errno = -rc;
			array_strcpy(errdetail, "Unhop failed");
			array_sprintf(badtoken, "%s %ld",
				      str_lock_type(resp->r_lock_type),
				      lock.l_start);
			break;
		} else
			resp->r_status = STATUS_GRANTED;
	}

	if (resp->r_status != STATUS_GRANTED) {
		lock.l_whence = SEEK_SET;
		lock.l_type = F_UNLCK;
		lock.l_start = resp->r_start;
		lock.l_len = resp->r_length;
		lock.l_pid = 0;

		rc = glfs_fd_set_lkowner(fds[resp->r_fpos], (void *)&owner,
					 sizeof(owner));
		if (rc < 0) {
			resp->r_status = STATUS_ERRNO;
			resp->r_errno = -rc;
			array_strcpy(errdetail, "setting lkowner failed");
			array_sprintf(badtoken, "%lld %lld", resp->r_start,
				      resp->r_length);
			return;
		}

		rc = glfs_posix_lock(fds[resp->r_fpos], F_SETLK, &lock);
		if (rc < 0)
			rc = -errno;
		if (rc < 0) {
			resp->r_status = STATUS_ERRNO;
			resp->r_errno = -rc;
			array_strcpy(errdetail, "Unhop Unlock failed");
			array_sprintf(badtoken, "%lld %lld", resp->r_start,
				      resp->r_length);
		}
	}
}

void do_unlock(struct response *resp)
{
	int rc;
	struct flock lock;
	uint64_t owner;

	if (resp->r_fpos != 0 && fds[resp->r_fpos] == NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		array_strcpy(errdetail, "Invalid file number");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	/* If this fpos has a blocking lock, cancel it. */
	cancel_work(resp);

	switch (lock_mode[resp->r_fpos]) {
	case LOCK_MODE_POSIX:
		owner = getpid();
		break;

	case LOCK_MODE_OFD:
		owner = resp->r_fpos;
		break;
	}

	lock.l_whence = SEEK_SET;
	lock.l_type = resp->r_lock_type;
	lock.l_start = resp->r_start;
	lock.l_len = resp->r_length;
	lock.l_pid = 0;

	rc = glfs_fd_set_lkowner(fds[resp->r_fpos], (void *)&owner,
				 sizeof(owner));
	if (rc < 0) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = -rc;
		array_strcpy(errdetail, "setting lkowner failed");
		array_sprintf(badtoken, "%lld %lld", resp->r_start,
			      resp->r_length);
		return;
	}

	rc = glfs_posix_lock(fds[resp->r_fpos], F_SETLK, &lock);

	if (rc < 0)
		rc = -errno;
	if (rc < 0) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = -rc;
		array_strcpy(errdetail, "Unlock failed");
		array_sprintf(badtoken, "%lld %lld",
			      resp->r_start, resp->r_length);
		return;
	}

	resp->r_status = STATUS_GRANTED;
}

void do_test(struct response *resp)
{
	int rc;
	struct flock lock;
	uint64_t owner;

	if (resp->r_fpos != 0 && fds[resp->r_fpos] == NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		array_strcpy(errdetail, "Invalid file number");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	switch (lock_mode[resp->r_fpos]) {
	case LOCK_MODE_POSIX:
		owner = getpid();
		break;

	case LOCK_MODE_OFD:
		owner = resp->r_fpos;
		break;
	}

	lock.l_whence = SEEK_SET;
	lock.l_type = resp->r_lock_type;
	lock.l_start = resp->r_start;
	lock.l_len = resp->r_length;
	lock.l_pid = 0;

	fprintf(stdout, "TEST lock type %s\n", str_lock_type(lock.l_type));

	rc = glfs_fd_set_lkowner(fds[resp->r_fpos], (void *)&owner,
				 sizeof(owner));
	if (rc < 0) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = -rc;
		array_strcpy(errdetail, "setting lkowner failed");
		array_sprintf(badtoken, "%lld %lld", resp->r_start,
			      resp->r_length);
		return;
	}

	rc = glfs_posix_lock(fds[resp->r_fpos], F_GETLK, &lock);

	if (rc < 0)
		rc = -errno;
	if (rc < 0) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = -rc;
		array_strcpy(errdetail, "Test failed");
		array_sprintf(badtoken, "%s %lld %lld",
			      str_lock_type(lock.l_type),
			      resp->r_start, resp->r_length);
		return;
	}

	if (lock.l_type == F_UNLCK) {
		fprintf(stdout, "GRANTED TEST lock type %s\n",
			str_lock_type(lock.l_type));
		resp->r_status = STATUS_GRANTED;
	} else {
		resp->r_lock_type = lock.l_type;
		resp->r_pid = lock.l_pid;
		resp->r_start = lock.l_start;
		resp->r_length = lock.l_len;
		resp->r_status = STATUS_CONFLICT;
	}
}

void do_close(struct response *resp)
{
	int rc;

	if (resp->r_fpos != 0 && fds[resp->r_fpos] == NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		array_strcpy(errdetail, "Invalid file number");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	rc = glfs_close(fds[resp->r_fpos]);

	glfs_h_close(handles[resp->r_fpos]);

	fds[resp->r_fpos] = NULL;
	handles[resp->r_fpos] = NULL;

	if (rc < 0) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = -rc;
		array_strcpy(errdetail, "Close failed");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	resp->r_status = STATUS_OK;
}

struct test_list {
	struct test_list *tl_next;
	long long int tl_start;
	long long int tl_end;
};

struct test_list *tl_head;
struct test_list *tl_tail;

void remove_test_list_head(void)
{
	struct test_list *item = tl_head;

	if (item == NULL)
		fatal("Corruption in test list\n");

	tl_head = item->tl_next;

	if (tl_tail == item)
		tl_tail = NULL;

	free(item);
}

void make_test_item(long long int start, long long int end)
{
	struct test_list *item = malloc(sizeof(*item));

	if (item == NULL)
		fatal("Could not allocate test list item\n");

	item->tl_next = NULL;
	item->tl_start = start;
	item->tl_end = end;

	if (tl_head == NULL)
		tl_head = item;

	if (tl_tail != NULL)
		tl_tail->tl_next = item;

	tl_tail = item;
}

int list_locks(long long int start, long long int end, struct response *resp)
{
	long long int conf_end;
	struct flock lock;
	int rc;

	lock.l_whence = SEEK_SET;
	lock.l_type = F_WRLCK;
	lock.l_start = start;
	lock.l_pid = 0;

	if (end == INT64_MAX)
		lock.l_len = 0;
	else
		lock.l_len = end - start;

	rc = glfs_posix_lock(fds[resp->r_fpos], F_GETLK, &lock);
	if (rc < 0)
		rc = -errno;

	if (rc < 0) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = -rc;
		array_strcpy(errdetail, "Test failed");
		array_sprintf(badtoken, "%s %lld %lld",
			      str_lock_type(lock.l_type),
			      resp->r_start, resp->r_length);
		respond(resp);
		return false;
	}

	/* Our test succeeded */
	if (lock.l_type == F_UNLCK)
		return false;

	resp->r_status = STATUS_CONFLICT;
	resp->r_lock_type = lock.l_type;
	resp->r_pid = lock.l_pid;
	resp->r_start = lock.l_start;
	resp->r_length = lock.l_len;

	respond(resp);

	conf_end = lock_end(resp);

	if (lock.l_start > start)
		make_test_item(start, lock.l_start);

	if (conf_end < end)
		make_test_item(conf_end, end);

	return true;
}

void do_list(struct response *resp)
{
	long long int start = resp->r_start;
	long long int length = resp->r_length;
	int conflict = false;

	if (resp->r_fpos != 0 && fds[resp->r_fpos] == NULL) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		array_strcpy(errdetail, "Invalid file number");
		array_sprintf(badtoken, "%ld", resp->r_fpos);
		return;
	}

	resp->r_lock_type = F_WRLCK;

	make_test_item(start, lock_end(resp));

	while (tl_head != NULL) {
		conflict |=
		    list_locks(tl_head->tl_start, tl_head->tl_end, resp);
		remove_test_list_head();
	}

	if (conflict)
		resp->r_status = STATUS_DENIED;
	else
		resp->r_status = STATUS_AVAILABLE;

	resp->r_lock_type = F_WRLCK;
	resp->r_start = start;
	resp->r_length = length;
}

struct work_item *get_work(enum thread_type thread_type)
{
	struct work_item *work, *poll;

	while (true) {
		/* Check poll list first */
		poll = glist_first_entry(&poll_queue, struct work_item, queue);
		work = poll;

		/* If we didn't find work on the poll list or this is a polling
		 * thread and the work on the poll list isn't due for polling
		 * yet, then look for work on the work list.
		 */
		if (work == NULL ||
		    (thread_type == THREAD_POLL &&
		     work->next_poll > time(NULL))) {
			/* Check work list now */
			work = glist_first_entry(&work_queue,
						 struct work_item,
						 queue);
		}

		/* Assign the work to ourself and remove from the queue and
		 * return to the caller.
		 */
		if (work != NULL) {
			work->work_owner = thread_type;
			work->work_thread = pthread_self();
			glist_del(&work->queue);
			return work;
		}

		/* No work, decide what kind of wait to do. */
		if (thread_type == THREAD_POLL && poll != NULL) {
			/* Since there is polling work to do, determine next
			 * time to poll and wait that long for signal.
			 */
			struct timespec twait = {poll->next_poll - time(NULL),
						 0};

			pthread_cond_timedwait(&work_cond,
					       &work_mutex,
					       &twait);
		} else {
			/* Wait for signal */
			pthread_cond_wait(&work_cond, &work_mutex);
		}
	}
}

void *worker(void *t_type)
{
	struct work_item *work = NULL;
	bool complete, cancelled = false;
	enum thread_type thread_type = *((enum thread_type *) t_type);

	pthread_mutex_lock(&work_mutex);

	while (true) {
		/* Look for work */
		work = get_work(thread_type);

		pthread_mutex_unlock(&work_mutex);

		assert(work != NULL);

		/* Do the work */
		switch (work->resp.r_cmd) {
		case CMD_LOCKW:
			complete = do_lock(&work->resp, thread_type);
			break;
		default:
			work->resp.r_status = STATUS_ERRNO;
			work->resp.r_errno = EINVAL;
			complete = true;
			break;
		}

		if (complete)
			respond(&work->resp);

		pthread_mutex_lock(&work_mutex);

		if (complete) {
			/* Remember if the main thread was trying to cancel
			 * the work.
			 */
			cancelled = work->work_owner == THREAD_CANCEL;

			/* Indicate this work is complete */
			work->work_owner = THREAD_NONE;
			work->work_thread = 0;

			if (cancelled) {
				/* Signal that work has been canceled or
				 * completed.
				 */
				pthread_cond_broadcast(&work_cond);
			} else {
				/* The work is done, and may be freed, except
				 * that if the work was being cancelled by
				 * cancel_work, then the main thread is waiting
				 * for this thread to be done with the work
				 * item, and thus we can not free it,
				 * cancel_work_item will be responsible to free
				 * the work item. This assures that the request
				 * that caused the cancellation is blocked until
				 * the cancellation has completed.
				 */
				free_work(work);
			}
		} else {
			/* This can ONLY happen for a polling thread.
			 * Put this work back in the queue, with a new
			 * next polling time.
			 */
			work->work_owner = THREAD_NONE;
			work->work_thread = 0;
			work->next_poll = time(NULL) + POLL_DELAY;
			glist_add_tail(&poll_queue, &work->queue);

			/* And let worker threads know there may be
			 * more work to do.
			 */
			pthread_cond_broadcast(&work_cond);
		}
	}
}

int main(int argc, char **argv)
{
	int opt;
	int len, rc, i;
	struct sigaction sigact;
	char *rest;
	int oflags = 0;
	int no_tag;

	/* Init the lists of work for each fno */
	for (i = 0; i <= MAXFPOS; i++)
		glist_init(fno_work + i);

	/* Start the worker and polling threads */
	for (i = 0; i <= NUM_WORKER; i++) {
		rc = pthread_create(&threads[i],
				    NULL,
				    worker,
				    i == 0 ? &a_poller : &a_worker);
		if (rc == -1) {
			fprintf(stderr,
				"pthread_create failed %s\n",
				strerror(errno));
			exit(1);
		}
	}

	/* Initialize the signal handling */
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = sighandler;

	if (sigaction(SIGALRM, &sigact, NULL) == -1)
		return 1;

	if (sigaction(SIGPIPE, &sigact, NULL) == -1)
		return 1;

	if (sigaction(SIGIO, &sigact, NULL) == -1)
		return 1;

	input = stdin;
	output = stdout;

	/* now parsing options with getopt */
	while ((opt = getopt(argc, argv, options)) != EOF) {
		switch (opt) {
		case 'c':
			rc = chdir(optarg);
			if (rc == -1) {
				fprintf(stderr,
					"Can not change dir to %s errno = %d \"%s\"\n",
					optarg, errno, strerror(errno));
				exit(1);
			}
			break;
		case 'q':
			quiet = true;
			break;

		case 'd':
			duperrors = true;
			break;

		case 's':
			if (oflags > 31)
				show_usage(1, "Can not combine -x and -s\n");

			oflags |= 1;
			script = true;
			array_strcpy(server, optarg);
			break;

		case 'x':
			if ((oflags & 31) != 0)
				show_usage(1,
					   "Can not combine -x and -s/-p/-n/-g/-v\n");

			oflags |= 32;
			script = true;
			input = fopen(optarg, "r");

			if (input == NULL)
				fatal("Could not open %s\n", optarg);
			break;

		case 'n':
			if (oflags > 31)
				show_usage(1, "Can not combine -x and -n\n");

			oflags |= 2;
			array_strcpy(name, optarg);
			break;

		case 'p':
			if (oflags > 31)
				show_usage(1, "Can not combine -x and -p\n");

			oflags |= 4;
			array_strcpy(portstr, optarg);
			port = atoi(optarg);
			break;

		case 'g':
			if (oflags > 31)
				show_usage(1, "Can not combine -x and -g\n");
			oflags |= 8;

			array_strcpy(glusterserver, optarg);
			break;

		case 'v':
			if (oflags > 31)
				show_usage(1, "Can not combine -x and -v\n");
			oflags |= 16;

			array_strcpy(volname, optarg);
			break;

		case '?':
		case 'h':
		default:
			/* display the help */
			show_usage(0, "Help\n");
		}
	}

	if (oflags > 0 && oflags < 31)
		show_usage(1, "Must specify -s, -p, -n, -g and -v together\n");

	if (oflags == 31)
		openserver();

	fs = glfs_new(volname);
	if (!fs) {
		fatal("Unable to create new glfs. Volume: %s",
		      volname);
	}

	rc = glfs_set_volfile_server(fs, "tcp", glusterserver, 24007);
	if (rc != 0) {
		fatal("Unable to set volume file. Volume: %s",
		      volname);
	}

	rc = glfs_set_logging(fs, "stdout", 7);
	if (rc != 0) {
		fatal("Unable to set logging. Volume: %s",
		      volname);
	}

	rc = glfs_init(fs);
	if (rc != 0) {
		fatal("Unable to initialize volume. Volume: %s",
		      volname);
	}

	while (1) {
		len = readln(input, line, sizeof(line));
		if (len < 0) {
			if (script)
				fatal("End of file on input\n");
			else
				break;
		} else {
			struct response resp;
			bool complete = true;

			lno++;
			memset(&resp, 0, sizeof(resp));

			rest = SkipWhite(line, REQUIRES_MORE, "Invalid line");

			/* Skip totally blank line */
			if (rest == NULL)
				continue;

			if (script && !quiet)
				fprintf(stdout, "%s\n", rest);

			/* If line doesn't start with a tag, that's ok */
			no_tag = (!isdigit(*rest) && (*rest != '$') &&
				  (*rest != '-'));

			/* Parse request into response structure */
			rest = parse_request(rest, &resp, no_tag);

			if (rest == NULL) {
				resp.r_status = STATUS_ERRNO;
				resp.r_errno = errno;
			} else {
				/* Make sure default status is ok */
				resp.r_status = STATUS_OK;

				if (*rest != '\0' && *rest != '#')
					fprintf_stderr(
					     "Command line not consumed, rest=\"%s\"\n",
					     rest);

				switch (resp.r_cmd) {
				case CMD_OPEN:
					do_open(&resp);
					break;
				case CMD_CLOSE:
					do_close(&resp);
					break;
				case CMD_LOCKW:
					complete = do_lock(&resp, THREAD_MAIN);
					break;
				case CMD_LOCK:
					complete = do_lock(&resp, THREAD_MAIN);
					break;
				case CMD_UNLOCK:
					do_unlock(&resp);
					break;
				case CMD_TEST:
					do_test(&resp);
					break;
				case CMD_LIST:
					do_list(&resp);
					break;
				case CMD_HOP:
					do_hop(&resp);
					break;
				case CMD_UNHOP:
					do_unhop(&resp);
					break;
				case CMD_SEEK:
					do_seek(&resp);
					break;
				case CMD_READ:
					do_read(&resp);
					break;
				case CMD_WRITE:
					do_write(&resp);
					break;
				case CMD_ALARM:
					do_alarm(&resp);
					break;
				case CMD_FORK:
					complete = do_fork(&resp, oflags == 7);
					break;

				case CMD_HELLO:
				case CMD_COMMENT:
				case CMD_QUIT:
					resp.r_status = STATUS_OK;
					break;

				case NUM_COMMANDS:
					fprintf_stderr("Invalid command %s\n",
						       line);
					continue;
				}
			}

			if (complete)
				respond(&resp);

			if (resp.r_cmd == CMD_QUIT)
				exit(0);
		}
	}

	return 0;
}
