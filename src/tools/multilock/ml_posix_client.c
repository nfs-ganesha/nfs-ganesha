/*
 * Copyright IBM Corporation, 2012
 *  Contributor: Frank Filz <ffilzlnx@mindspring.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 *
 */

#include "multilock.h"

/* command line syntax */

char options[] = "c:qdx:s:n:p:h?";
char usage[] =
	"Usage: ml_posix_client -s server -p port -n name [-q] [-d] [-c path]\n"
	"       ml_posix_client -x script [-q] [-d] [-c path]\n"
	"       ml_posix_client [-q] [-d] [-c path]\n" "\n"
	"  ml_posix_client may be run in three modes\n"
	"  - In the first mode, the client will be driven by a master.\n"
	"  - In the second mode, the client is driven by a script.\n"
	"  - In the third mode, the client interractive.\n" "\n"
	"  -s server - specify the master's hostname or IP address\n"
	"  -p port   - specify the master's port number\n"
	"  -n name   - specify the client's name\n"
	"  -x script - specify the name of a script to execute\n"
	"  -q        - specify quiet mode\n"
	"  -d        - specify dup errors mode (errors are sent to stdout and stderr)\n"
	"  -c path   - chdir\n";

char server[MAXSTR];
char name[MAXSTR];
char portstr[MAXSTR];
int port;
char line[MAXSTR * 2 + 3];
long int alarmtag;
int fno[MAXFPOS + 1];

void openserver()
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
	sprintf(resp.r_data, "%s", name);
	respond(&resp);
	return;
}

void command()
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
	int fd;

	if (fno[resp->r_fpos] != 0) {
		strcpy(errdetail, "fpos in use");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EINVAL;
		return;
	}

	if ((resp->r_flags & O_CREAT) == 0)
		fd = open(resp->r_data, resp->r_flags);
	else
		fd = open(resp->r_data, resp->r_flags, resp->r_mode);

	if (fd == -1) {
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = errno;
		sprintf(badtoken, "%s", resp->r_data);
	} else {
		fno[resp->r_fpos] = fd;
		resp->r_fno = fd;
		resp->r_status = STATUS_OK;
	}
}

void do_write(struct response *resp)
{
	long long int rc;

	if (resp->r_fpos != 0 && fno[resp->r_fpos] == 0) {
		strcpy(errdetail, "Invalid file number");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		return;
	}

	rc = write(fno[resp->r_fpos], resp->r_data, resp->r_length);

	if (rc == -1) {
		strcpy(errdetail, "Write failed");
		sprintf(badtoken, "%lld", resp->r_length);
		resp->r_status = STATUS_ERRNO;
		return;
	}

	if (rc != resp->r_length) {
		strcpy(errdetail, "Short write");
		sprintf(badtoken, "%lld", rc);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EIO;
		return;
	}

	resp->r_status = STATUS_OK;
}

void do_read(struct response *resp)
{
	long long int rc;

	if (resp->r_fpos != 0 && fno[resp->r_fpos] == 0) {
		strcpy(errdetail, "Invalid file number");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		return;
	}

	if (resp->r_length > MAXSTR)
		resp->r_length = MAXSTR;

	rc = read(fno[resp->r_fpos], resp->r_data, resp->r_length);

	if (rc == -1) {
		strcpy(errdetail, "Read failed");
		sprintf(badtoken, "%lld", resp->r_length);
		resp->r_status = STATUS_ERRNO;
		return;
	}

	resp->r_data[rc] = '\0';
	resp->r_length = strlen(resp->r_data);
	resp->r_status = STATUS_OK;
}

void do_seek(struct response *resp)
{
	int rc;

	if (resp->r_fpos != 0 && fno[resp->r_fpos] == 0) {
		strcpy(errdetail, "Invalid file number");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		return;
	}

	rc = lseek(fno[resp->r_fpos], resp->r_start, SEEK_SET);

	if (rc == -1) {
		strcpy(errdetail, "Seek failed");
		sprintf(badtoken, "%lld", resp->r_start);
		resp->r_status = STATUS_ERRNO;
		return;
	}

	resp->r_status = STATUS_OK;
}

void do_lock(struct response *resp)
{
	int rc;
	struct flock lock;
	int cmd;

	if (resp->r_cmd == CMD_LOCKW)
		cmd = F_SETLKW;
	else
		cmd = F_SETLK;

	if (resp->r_fpos != 0 && fno[resp->r_fpos] == 0) {
		strcpy(errdetail, "Invalid file number");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		return;
	}

	lock.l_whence = SEEK_SET;
	lock.l_type = resp->r_lock_type;
	lock.l_start = resp->r_start;
	lock.l_len = resp->r_length;

	if (cmd == F_SETLKW && alarmtag == 0) {
		/* Don't let a blocking lock block hang us
		 * Test case can set an alarm before LOCKW to specify
		 * a different time
		 */
		resp->r_secs = 30;
		do_alarm(resp);
	}

	rc = fcntl(fno[resp->r_fpos], cmd, &lock);

	if (rc == -1) {
		if (errno == EAGAIN) {
			resp->r_status = STATUS_DENIED;
		} else if (errno == EINTR) {
			resp->r_status = STATUS_CANCELED;
		} else if (errno == EDEADLK) {
			resp->r_status = STATUS_DEADLOCK;
		} else {
			strcpy(errdetail, "Lock failed");
			sprintf(badtoken, "%s %lld %lld",
				str_lock_type(lock.l_type), resp->r_start,
				resp->r_length);
			resp->r_status = STATUS_ERRNO;
			resp->r_errno = errno;
		}
	} else
		resp->r_status = STATUS_GRANTED;

	if (cmd == F_SETLKW && alarmtag == resp->r_tag) {
		/* cancel the alarm we set */
		alarm(0);
		alarmtag = 0;
	}
}

void do_hop(struct response *resp)
{
	int rc;
	int pos;
	struct flock lock;
	int cmd;

	cmd = F_SETLK;

	if (resp->r_fpos != 0 && fno[resp->r_fpos] == 0) {
		strcpy(errdetail, "Invalid file number");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		return;
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

		rc = fcntl(fno[resp->r_fpos], cmd, &lock);

		if (rc == -1) {
			if (errno == EAGAIN) {
				resp->r_start = lock.l_start;
				resp->r_length = 1;
				resp->r_status = STATUS_DENIED;
				break;
			} else {
				strcpy(errdetail, "Hop failed");
				sprintf(badtoken, "%s %ld",
					str_lock_type(resp->r_lock_type),
					lock.l_start);
				resp->r_status = STATUS_ERRNO;
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
		rc = fcntl(fno[resp->r_fpos], cmd, &lock);
		if (rc == -1) {
			strcpy(errdetail, "Hop Unlock failed");
			sprintf(badtoken, "%lld %lld", resp->r_start,
				resp->r_length);
			resp->r_status = STATUS_ERRNO;
		}
	}
}

void do_unhop(struct response *resp)
{
	int rc;
	int pos;
	struct flock lock;
	int cmd;

	cmd = F_SETLK;

	if (resp->r_fpos != 0 && fno[resp->r_fpos] == 0) {
		strcpy(errdetail, "Invalid file number");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		return;
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

		rc = fcntl(fno[resp->r_fpos], cmd, &lock);

		if (rc == -1) {
			strcpy(errdetail, "Unhop failed");
			sprintf(badtoken, "%s %ld",
				str_lock_type(resp->r_lock_type), lock.l_start);
			resp->r_status = STATUS_ERRNO;
			break;
		} else
			resp->r_status = STATUS_GRANTED;
	}

	if (resp->r_status != STATUS_GRANTED) {
		lock.l_whence = SEEK_SET;
		lock.l_type = F_UNLCK;
		lock.l_start = resp->r_start;
		lock.l_len = resp->r_length;
		rc = fcntl(fno[resp->r_fpos], cmd, &lock);
		if (rc == -1) {
			strcpy(errdetail, "Unhop Unlock failed");
			sprintf(badtoken, "%lld %lld", resp->r_start,
				resp->r_length);
			resp->r_status = STATUS_ERRNO;
		}
	}
}

void do_unlock(struct response *resp)
{
	int rc;
	struct flock lock;
	int cmd;

	cmd = F_SETLK;

	if (resp->r_fpos != 0 && fno[resp->r_fpos] == 0) {
		strcpy(errdetail, "Invalid file number");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		return;
	}

	lock.l_whence = SEEK_SET;
	lock.l_type = resp->r_lock_type;
	lock.l_start = resp->r_start;
	lock.l_len = resp->r_length;

	rc = fcntl(fno[resp->r_fpos], cmd, &lock);

	if (rc == -1) {
		strcpy(errdetail, "Unlock failed");
		sprintf(badtoken, "%lld %lld", resp->r_start, resp->r_length);
		resp->r_status = STATUS_ERRNO;
		return;
	}

	resp->r_status = STATUS_GRANTED;
}

void do_test(struct response *resp)
{
	int rc;
	struct flock lock;
	int cmd = F_GETLK;

	if (resp->r_fpos != 0 && fno[resp->r_fpos] == 0) {
		strcpy(errdetail, "Invalid file number");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		return;
	}

	lock.l_whence = SEEK_SET;
	lock.l_type = resp->r_lock_type;
	lock.l_start = resp->r_start;
	lock.l_len = resp->r_length;

	fprintf(stdout, "TEST lock type %s\n", str_lock_type(lock.l_type));

	rc = fcntl(fno[resp->r_fpos], cmd, &lock);

	if (rc == -1) {
		strcpy(errdetail, "Test failed");
		sprintf(badtoken, "%s %lld %lld", str_lock_type(lock.l_type),
			resp->r_start, resp->r_length);
		resp->r_status = STATUS_ERRNO;
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

	if (resp->r_fpos != 0 && fno[resp->r_fpos] == 0) {
		strcpy(errdetail, "Invalid file number");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		return;
	}

	rc = close(fno[resp->r_fpos]);
	fno[resp->r_fpos] = 0;

	if (rc == -1) {
		strcpy(errdetail, "Close failed");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		return;
	}

	resp->r_status = STATUS_OK;
}

static inline long long int lock_end(long long int start, long long int length)
{
	if (length == 0)
		return INT64_MAX;
	else
		return start + length;
}

struct test_list {
	struct test_list *tl_next;
	long long int tl_start;
	long long int tl_end;
};

struct test_list *tl_head;
struct test_list *tl_tail;

void remove_test_list_head()
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

	if (end == INT64_MAX)
		lock.l_len = 0;
	else
		lock.l_len = end - start;

	rc = fcntl(fno[resp->r_fpos], F_GETLK, &lock);

	if (rc == -1) {
		strcpy(errdetail, "Test failed");
		sprintf(badtoken, "%s %lld %lld", str_lock_type(lock.l_type),
			resp->r_start, resp->r_length);
		resp->r_errno = errno;
		resp->r_status = STATUS_ERRNO;
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

	conf_end = lock_end(lock.l_start, lock.l_len);

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

	if (resp->r_fpos != 0 && fno[resp->r_fpos] == 0) {
		strcpy(errdetail, "Invalid file number");
		sprintf(badtoken, "%ld", resp->r_fpos);
		resp->r_status = STATUS_ERRNO;
		resp->r_errno = EBADF;
		return;
	}

	resp->r_lock_type = F_WRLCK;

	make_test_item(start, lock_end(start, length));

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

int main(int argc, char **argv)
{
	int opt;
	int len, rc;
	struct sigaction sigact;
	char *rest;
	int oflags = 0;
	int no_tag;

	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = sighandler;

	if (sigaction(SIGALRM, &sigact, NULL) == -1)
		return 1;

	if (sigaction(SIGPIPE, &sigact, NULL) == -1)
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
			if (oflags > 7)
				show_usage(1, "Can not combine -x and -s\n");

			oflags |= 1;
			script = true;
			strncpy(server, optarg, MAXSTR);
			break;

		case 'x':
			if ((oflags & 7) != 0)
				show_usage(1,
					   "Can not combine -x and -s/-p/-n\n");

			oflags |= 8;
			script = true;
			input = fopen(optarg, "r");

			if (input == NULL)
				fatal("Could not open %s\n", optarg);
			break;

		case 'n':
			if (oflags > 7)
				show_usage(1, "Can not combine -x and -n\n");

			oflags |= 2;
			strncpy(name, optarg, MAXSTR);
			break;

		case 'p':
			if (oflags > 7)
				show_usage(1, "Can not combine -x and -p\n");

			oflags |= 4;
			strncpy(portstr, optarg, MAXSTR);
			port = atoi(optarg);
			break;

		case '?':
		case 'h':
		default:
			/* display the help */
			show_usage(0, "Help\n");
		}
	}

	if (oflags > 0 && oflags < 7)
		show_usage(1, "Must specify -s, -p, and -n together\n");

	if (oflags == 7)
		openserver();

	while (1) {
		len = readln(input, line, MAXSTR * 2);
		if (len < 0) {
			if (script)
				fatal("End of file on input\n");
			else
				break;
		} else {
			struct response resp;

			lno++;
			memset(&resp, 0, sizeof(resp));

			rest = SkipWhite(line, REQUIRES_MORE, "Invalid line");

			/* Skip totally blank line */
			if (rest == NULL)
				continue;

			if (script && !quiet)
				fprintf(stdout, "%s\n", rest);

			/* If line doesn't start with a tag, that's ok */
			no_tag = (!isdigit(*rest) && (*rest != '$'));

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
					do_lock(&resp);
					break;
				case CMD_LOCK:
					do_lock(&resp);
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

			respond(&resp);

			if (resp.r_cmd == CMD_QUIT)
				exit(0);
		}
	}

	return 0;
}
