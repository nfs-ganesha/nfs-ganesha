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

char options[] = "ekdqfsp:h?x:";
char usage[] =
	"Usage: ml_master [-p port] [-s] [-f] [-q] [-x script] [-d]\n" "\n"
	"  -p port   - specify the port to listen to clients on\n"
	"  -s        - specify strict mode (clients are not polled without EXPECT)\n"
	"  -f        - specify errors are fatal mode\n"
	"  -q        - speficy quiet mode\n"
	"  -d        - speficy dup errors mode (errors are sent to stdout and stderr)\n"
	"  -x script - specify script to run\n"
	"  -k        - syntax check only\n"
	"  -e        - non-fatal errors, full accounting of errors to stderr, everything\n"
	"              to stdout\n"
	;

int port, listensock;
struct sockaddr_in addr;
int maxfd;
struct response *expected_responses;
fd_set sockets;
int num_errors;
bool terminate;
bool err_accounting;
sigset_t full_signal_set;
sigset_t original_signal_set;

void open_socket()
{
	int rc;

	rc = socket(AF_INET, SOCK_STREAM, 0);
	if (rc == -1)
		fatal("socket failed with ERRNO %d \"%s\"\n",
		      errno, strerror(errno));

	listensock = rc;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;

	rc = bind(listensock, (struct sockaddr *)&addr, sizeof(addr));

	if (rc == -1)
		fatal("bind failed with ERRNO %d \"%s\"\n",
		      errno, strerror(errno));

	FD_ZERO(&sockets);
	FD_SET(listensock, &sockets);
	maxfd = listensock;

	rc = listen(listensock, 10);

	if (rc == -1)
		fatal("listen failed with ERRNO %d \"%s\"\n",
		      errno, strerror(errno));
}

void do_accept()
{
	struct client *client = malloc(sizeof(*client));
	socklen_t len;
	int rc;

	if (client == NULL)
		fatal("Accept malloc failed\n");

	memset(client, 0, sizeof(*client));

	len = sizeof(client->c_addr);

	client->c_socket = accept(listensock, &client->c_addr, &len);

	if (client->c_socket == -1)
		fatal("Accept failed ERRNO %d \"%s\"\n",
		      errno, strerror(errno));

	FD_SET(client->c_socket, &sockets);

	sprintf(client->c_name, "<UNKNOWN_%d>", client->c_socket);

	if (client->c_socket > maxfd)
		maxfd = client->c_socket;

	if (!quiet)
		fprintf(output, "Accept for socket %d\n", client->c_socket);

	client->c_input = fdopen(client->c_socket, "r");

	if (client->c_input == NULL)
		fatal("Accept fdopen for input failed ERRNO %d \"%s\"\n",
		      errno, strerror(errno));

	rc = setvbuf(client->c_input, NULL, _IONBF, 0);

	if (rc != 0)
		fatal("Accept setvbuf for input failed ERRNO %d \"%s\"\n",
		      errno, strerror(errno));

	client->c_output = fdopen(client->c_socket, "w");

	if (client->c_output == NULL)
		fatal("Accept fdopen for output failed ERRNO %d \"%s\"\n",
		      errno, strerror(errno));

	rc = setvbuf(client->c_output, NULL, _IONBF, 0);

	if (rc != 0)
		fatal("Accept setvbuf for output failed ERRNO %d \"%s\"\n",
		      errno, strerror(errno));

	client->c_refcount++;

	client->c_next = client_list;

	if (client_list != NULL)
		client_list->c_prev = client;

	client_list = client;
}

void close_client(struct client *client)
{
	close(client->c_socket);

	if (!quiet)
		fprintf(output, "Closed client socket %d\n", client->c_socket);

	FD_CLR(client->c_socket, &sockets);

	client->c_socket = 0;
	client->c_refcount--;
}

struct client *find_client_by_fd(int socket)
{
	struct client *client = client_list;

	while (client != NULL && client->c_socket != socket)
		client = client->c_next;

	return client;
}

struct client *find_client_by_name(const char *name)
{
	struct client *client = client_list;

	while (client != NULL && strcasecmp(client->c_name, name) != 0)
		client = client->c_next;

	return client;
}

int receive(bool watchin, long int timeout_secs)
{
	fd_set readfds, writefds, exceptfds;
	struct timespec timeout;
	int rc, i;
	int timeend = 0;

	if (timeout_secs > 0)
		timeend = time(NULL) + timeout_secs;

	while (1) {
		if (timeout_secs > 0) {
			timeout.tv_nsec = 0;
			timeout.tv_sec = timeend - time(NULL);
			if (timeout.tv_sec == 0)
				return -2;
		} else if (timeout_secs == 0) {
			timeout.tv_nsec = 0;
			timeout.tv_sec = 0;
		}

		memcpy(&readfds, &sockets, sizeof(sockets));

		if (watchin)
			FD_SET(0, &readfds);

		memcpy(&writefds, &sockets, sizeof(sockets));
		memcpy(&exceptfds, &sockets, sizeof(sockets));

		if (watchin)
			FD_SET(0, &exceptfds);

		if (watchin && !script) {
			fprintf(output, "> ");
			fflush(output);
		}

		if (!watchin && !quiet) {
			fprintf(output, "Waiting for clients\n");
			fflush(output);
		}

		if (timeout_secs >= 0) {
			fprintf(output, "About to sleep for %d secs\n",
				(int)timeout.tv_sec);
			rc = pselect(maxfd + 1, &readfds, NULL, &exceptfds,
				     &timeout, &original_signal_set);
		} else
			rc = pselect(maxfd + 1, &readfds, NULL, &exceptfds,
				     NULL, &original_signal_set);

		if (rc == -1) {
			if (watchin && !script) {
				fprintf(output, "\n");
				fflush(output);
			}

			if (errno == EINTR && !terminate) {
				if (timeout_secs >= 0)
					return -2;

				fprintf_stderr("select timed out\n");
				return -1;
			} else if (errno == EINTR) {
				fprintf_stderr("select terminated by signal\n");
				return -3;
			} else {
				fprintf_stderr
				    ("select failed with ERRNO %d \"%s\"\n",
				     errno, strerror(errno));
				return -1;
			}
		}

		for (i = 0; i <= maxfd; i++) {
			if (FD_ISSET(i, &readfds)) {
				if (watchin && !quiet && i != 0) {
					fprintf(output, "\n");
					fflush(output);
				}

				if (i == listensock)
					do_accept();
				else
					return i;
			}
			if (FD_ISSET(i, &exceptfds)) {
				fprintf_stderr(
				     "select received exception for socket %d\n",
				     i);
			}
		}
	}
}

void error()
{
	int len = strlen(errdetail);

	num_errors++;

	if (errdetail[len - 1] == '\n')
		errdetail[len - 1] = '\0';

	if (errno == 0)
		fprintf_stderr("%s\n", errdetail);
	else
		fprintf_stderr("ERRNO %d \"%s\" \"%s\" bad token \"%s\"\n",
			       errno, strerror(errno), errdetail, badtoken);
}

struct response *alloc_resp(struct client *client)
{
	struct response *resp = malloc(sizeof(*resp));

	if (resp == NULL)
		fatal("Could not allocate response\n");

	memset(resp, 0, sizeof(*resp));

	resp->r_client = client;

	if (client != NULL)
		client->c_refcount++;

	return resp;
}

struct response *process_client_response(struct client *client)
{
	int len;
	struct response *client_resp;
	char *rest;
	char line[MAXSTR * 2];

	client_resp = alloc_resp(client);

	len = readln(client->c_input, line, MAXSTR * 2);
	if (len >= 0) {
		sprintf(client_resp->r_original, "%s %s", client->c_name, line);
		fprintf(output, "%s\n", client_resp->r_original);

		rest = parse_response(line, client_resp);

		if (rest == NULL)
			return client_resp;

		if (client_resp->r_cmd == CMD_HELLO) {
			strncpy(client->c_name, client_resp->r_data,
				client_resp->r_length);
			client->c_name[client_resp->r_length] = '\0';
		}
	} else {
		fprintf(output, "%s -2 QUIT OK # socket closed\n",
			client->c_name);
		close_client(client);
		client_resp->r_cmd = CMD_QUIT;
		client_resp->r_tag = -2;
		client_resp->r_status = STATUS_OK;
	}

	return client_resp;
}

static void master_command();

struct response *receive_response(bool watchin, long int timeout_secs)
{
	int fd;
	struct client *client;

	fd = receive(watchin, timeout_secs);
	if (fd == -2 && timeout_secs >= 0) {
		/* Expected timeout */
		return NULL;
	} else if (fd < 0) {
		struct response *resp = alloc_resp(NULL);

		if (fd == -3) {
			/* signal interrupted select */
			fprintf_stderr("Receive interrupted - exiting...\n");
			resp->r_tag = -1;
			resp->r_cmd = CMD_QUIT;
			resp->r_status = STATUS_CANCELED;
			strcpy(resp->r_original, "-1 QUIT CANCELED");
			errno = 0;
			strcpy(errdetail, "Receive interrupted - exiting...");
		} else {
			/* some other error occurred */
			fprintf_stderr("Receive failed ERRNO %d \"%s\"\n",
				       errno, strerror(errno));
			resp->r_cmd = CMD_QUIT;

			resp->r_errno = errno;
			resp->r_tag = -1;
			strcpy(resp->r_data, "Receive failed");

			sprintf(resp->r_original,
				"-1 QUIT ERRNO %d \"%s\" \"Receive failed\"",
				errno, strerror(errno));

			strcpy(errdetail, "Receive failed");
			strcpy(badtoken, "");
		}
		return resp;
	} else if (watchin && fd == 0) {
		return NULL;
	} else {
		client = find_client_by_fd(fd);

		if (client == NULL)
			fatal("Could not find client for socket %d\n", fd);

		return process_client_response(client);
	}
}

enum master_cmd {
	MCMD_QUIT,
	MCMD_STRICT,
	MCMD_CLIENT_CMD,
	MCMD_EXPECT,
	MCMD_FATAL,
	MCMD_SLEEP,
	MCMD_OPEN_BRACE,
	MCMD_CLOSE_BRACE,
	MCMD_SIMPLE_OK,
	MCMD_SIMPLE_AVAILABLE,
	MCMD_SIMPLE_GRANTED,
	MCMD_SIMPLE_DENIED,
	MCMD_SIMPLE_DEADLOCK,
	MCMD_CLIENTS,
};

struct token master_commands[] = {
	{"QUIT", 4, MCMD_QUIT},
	{"STRICT", 6, MCMD_STRICT},
	{"EXPECT", 6, MCMD_EXPECT},
	{"FATAL", 5, MCMD_FATAL},
	{"SLEEP", 5, MCMD_SLEEP},
	{"{", 1, MCMD_OPEN_BRACE},
	{"}", 1, MCMD_CLOSE_BRACE},
	{"OK", 2, MCMD_SIMPLE_OK},
	{"AVAILABLE", 9, MCMD_SIMPLE_AVAILABLE},
	{"GRANTED", 7, MCMD_SIMPLE_GRANTED},
	{"DENIED", 6, MCMD_SIMPLE_DENIED},
	{"DEADLOCK", 8, MCMD_SIMPLE_DEADLOCK},
	{"CLIENTS", 7, MCMD_CLIENTS},
	{"", 0, MCMD_CLIENT_CMD}
};

static void handle_quit();

/*
 * wait_for_expected_responses
 *
 * Wait for a list of expected responses (in expected_responses). If any
 * unexpected response and this is not being called from handle_quit() force
 * fatal error.
 */
void wait_for_expected_responses(const char *label, int count,
				 const char *last, bool could_quit)
{
	struct response *expect_resp;
	struct response *client_resp;
	bool fatal = false;

	fprintf(output, "Waiting for %d %s...\n", count, label);
	while (expected_responses != NULL
	       && (client_list != NULL || could_quit)) {
		client_resp = receive_response(false, -1);

		if (terminate && could_quit) {
			free_response(client_resp, NULL);
			break;
		}

		expect_resp =
		    check_expected_responses(expected_responses, client_resp);

		if (expect_resp != NULL) {
			fprintf(output, "Matched %s\n",
				expect_resp->r_original);

			free_response(expect_resp, &expected_responses);
			free_response(client_resp, NULL);
		} else if (client_resp->r_cmd != CMD_QUIT) {
			errno = 0;
			if (err_accounting)
				fprintf(stderr, "%s\nResp:      %s\n", last,
					client_resp->r_original);

			free_response(client_resp, NULL);
			sprintf(errdetail, "Unexpected response");
			error();

			/* If not called from handle_quit() dump list of
			 * expected responses and quit if in error_is_fatal or
			 * in a script.
			 */
			if (could_quit) {
				/* Error must be fatal if script since script
				 * can't recover
				 */
				if (error_is_fatal || script)
					fatal = true;
				break;
			}
		}
	}

	/* Abandon any remaining responses */
	while (expected_responses != NULL) {
		fprintf_stderr("Abandoning %s\n",
			       expected_responses->r_original);

		free_response(expected_responses, &expected_responses);
	}

	if (fatal || terminate)
		handle_quit();
}

void handle_quit()
{
	struct response *expect_resp;
	struct client *client;
	int count = 0;
	char out[MAXSTR];

	if (client_list != NULL) {
		for (client = client_list; client != NULL;
		     client = client->c_next) {
			if (client->c_socket == 0)
				continue;

			sprintf(out, "%ld QUIT\n", ++global_tag);
			fputs(out, client->c_output);
			fflush(client->c_output);

			/* Build an EXPECT for -1 QUIT for this client */
			expect_resp = alloc_resp(client);
			expect_resp->r_cmd = CMD_QUIT;
			expect_resp->r_status = STATUS_OK;
			expect_resp->r_tag = global_tag;
			sprintf(expect_resp->r_original, "EXPECT %s * QUIT OK",
				client->c_name);
			add_response(expect_resp, &expected_responses);
			count++;

			/* Build an EXPECT for -2 QUIT for this client */
			expect_resp = alloc_resp(client);
			expect_resp->r_cmd = CMD_QUIT;
			expect_resp->r_status = STATUS_OK;
			expect_resp->r_tag = -2;
			sprintf(expect_resp->r_original, "EXPECT %s -2 QUIT OK",
				client->c_name);
			add_response(expect_resp, &expected_responses);
			count++;
		}

		wait_for_expected_responses("client_list", count, "QUIT",
					    false);
		fprintf(output, "All clients exited\n");
	}

	if (num_errors > 0) {
		fprintf_stderr("%d errors\n", num_errors);
		fprintf_stderr("FAIL\n");
	} else {
		fprintf_stderr("SUCCESS\n");
	}

	exit(num_errors > 0);
}

bool expect_one_response(struct response *expect_resp, const char *last)
{
	struct response *client_resp;
	bool result;

	client_resp = receive_response(false, -1);

	if (terminate)
		result = true;
	else
		result = !compare_responses(expect_resp, client_resp);

	if (result) {
		if (err_accounting)
			fprintf(stderr, "%s\n%s\nResp:      %s\n", last,
				expect_resp->r_original,
				client_resp->r_original);
	} else
		fprintf(output, "Matched\n");

	free_response(expect_resp, NULL);
	free_response(client_resp, NULL);

	return result;
}

struct master_state {
	char *rest;
	char line[MAXSTR * 2];
	char out[MAXSTR * 2];
	char last[MAXSTR * 2];	/* last command sent */
	struct client *client;
	int len;
	int cmd;
	struct response *expect_resp;
	struct response *client_cmd;
	bool inbrace;
	int count;
};

void mcmd_client_cmd(struct master_state *ms)
{
	ms->rest = get_client(ms->line, &ms->client, syntax, REQUIRES_MORE);

	if (ms->rest == NULL)
		return;

	if (script)
		sprintf(ms->last, "Line %4ld: %s", lno, ms->line);
	else
		strcpy(ms->last, ms->line);

	ms->client_cmd = alloc_resp(ms->client);

	ms->rest = parse_request(ms->rest, ms->client_cmd, false);

	if (ms->rest != NULL && !syntax)
		send_cmd(ms->client_cmd);

	free_response(ms->client_cmd, NULL);
}

void mcmd_sleep(struct master_state *ms)
{
	long int secs;
	int t_end, t_now;

	ms->rest = get_long(ms->rest, &secs, true, "Invalid sleep time");

	if (ms->rest == NULL)
		return;

	if (syntax)
		return;

	t_now = time(NULL);
	t_end = t_now + secs;

	while (t_now <= t_end && !terminate) {
		struct response *client_resp;

		client_resp = receive_response(false, t_end - t_now);
		t_now = time(NULL);

		if (client_resp != NULL) {
			errno = 0;

			if (err_accounting)
				fprintf(stderr,
					"%s\n%s\n",
					ms->last,
					client_resp->r_original);

			sprintf(errdetail,
				"Unexpected response");

			ms->rest = NULL;

			free_response(client_resp, NULL);
		}

		/* If sleep 0 or we have run out, just want single iteration */
		if (t_now == t_end)
			break;
	}
}

void mcmd_open_brace(struct master_state *ms)
{
	if (ms->inbrace) {
		errno = 0;
		strcpy(errdetail,
		       "Illegal nested brace");
		ms->rest = NULL;
	}

	ms->count = 0;
	ms->inbrace = true;
}

void mcmd_close_brace(struct master_state *ms)
{
	if (!ms->inbrace) {
		errno = 0;
		strcpy(errdetail,
		       "Unmatched close brace");
		ms->rest = NULL;
	} else if (!syntax) {
		ms->inbrace = false;
		wait_for_expected_responses("responses",
					    ms->count, ms->last, true);
		fprintf(output,
			"All responses received OK\n");
		ms->count = 0;
	} else {
		ms->inbrace = false;
	}
}

void mcmd_clients(struct master_state *ms)
{
	if (ms->inbrace) {
		errno = 0;
		strcpy(errdetail,
		       "CLIENTS command not allowed inside brace");
		ms->rest = NULL;
		return;
	}

	while (ms->rest != NULL && ms->rest[0] != '\0'
	       && ms->rest[0] != '#') {
		/* Get the next client to expect */
		ms->rest = get_client(ms->rest,
				      &ms->client,
				      true,
				      REQUIRES_EITHER);

		if (ms->rest == NULL)
			return;

		/* Build an EXPECT client * HELLO OK "client" */
		ms->expect_resp = alloc_resp(ms->client);
		ms->expect_resp->r_cmd = CMD_HELLO;
		ms->expect_resp->r_tag = -1;
		ms->expect_resp->r_status = STATUS_OK;

		strcpy(ms->expect_resp->r_data, ms->client->c_name);

		sprintf(ms->expect_resp->r_original,
			"EXPECT %s * HELLO OK \"%s\"",
			ms->client->c_name, ms->client->c_name);

		ms->count++;

		if (syntax) {
			free_response(ms->expect_resp, NULL);
		} else {
			/* Add response to list of expected responses */
			add_response(ms->expect_resp, &expected_responses);
		}
	}

	if (ms->count == 0) {
		errno = 0;
		strcpy(errdetail,
		       "Expected at least one client");
		ms->rest = NULL;
		return;
	}

	if (!syntax) {
		wait_for_expected_responses("clients",
					    ms->count,
					    ms->last,
					    true);
		fprintf(output,
			"All clients said HELLO OK\n");
	}

	ms->count = 0;
}

void mcmd_expect(struct master_state *ms)
{
	ms->rest = get_client(ms->rest, &ms->client, true, REQUIRES_MORE);

	if (ms->rest == NULL)
		return;

	ms->expect_resp = alloc_resp(ms->client);

	if (script)
		sprintf(ms->expect_resp->r_original,
			"Line %4ld: EXPECT %s %s",
			lno, ms->client->c_name, ms->rest);
	else
		sprintf(ms->expect_resp->r_original,
			"EXPECT %s %s",
			ms->client->c_name, ms->rest);

	ms->rest = parse_response(ms->rest, ms->expect_resp);

	if (ms->rest == NULL || syntax) {
		free_response(ms->expect_resp, NULL);
	} else if (ms->inbrace) {
		add_response(ms->expect_resp, &expected_responses);
		ms->count++;
	} else if (expect_one_response(ms->expect_resp, ms->last))
		ms->rest = NULL;
}

void mcmd_simple(struct master_state *ms)
{
	strcpy(ms->last, ms->line);
	ms->rest = get_client(ms->rest, &ms->client, syntax, REQUIRES_MORE);

	if (ms->rest == NULL)
		return;

	ms->client_cmd = alloc_resp(ms->client);

	if (ms->cmd == MCMD_SIMPLE_OK)
		ms->client_cmd->r_status = STATUS_OK;
	else if (ms->cmd == MCMD_SIMPLE_AVAILABLE)
		ms->client_cmd->r_status = STATUS_AVAILABLE;
	else if (ms->cmd == MCMD_SIMPLE_GRANTED)
		ms->client_cmd->r_status = STATUS_GRANTED;
	else if (ms->cmd == MCMD_SIMPLE_DEADLOCK)
		ms->client_cmd->r_status = STATUS_DEADLOCK;
	else
		ms->client_cmd->r_status = STATUS_DENIED;

	ms->rest = parse_request(ms->rest, ms->client_cmd, true);

	if (ms->rest == NULL) {
		free_response(ms->client_cmd, NULL);
		return;
	}

	switch (ms->client_cmd->r_cmd) {
	case CMD_OPEN:
	case CMD_CLOSE:
	case CMD_SEEK:
	case CMD_WRITE:
	case CMD_COMMENT:
	case CMD_ALARM:
	case CMD_HELLO:
	case CMD_QUIT:
		if (ms->cmd != MCMD_SIMPLE_OK) {
			sprintf(errdetail,
				"Simple %s command expects OK",
				commands[ms->client_cmd->r_cmd].cmd_name);
			errno = 0;
			ms->rest = NULL;
		}
		break;

	case CMD_READ:
		if (ms->cmd != MCMD_SIMPLE_OK) {
			sprintf(errdetail,
				"Simple %s command expects OK",
				commands[ms->client_cmd->r_cmd].cmd_name);
			errno = 0;
			ms->rest = NULL;
		} else if (ms->client_cmd->r_length == 0
			   || ms->client_cmd->r_data[0] == '\0') {
			strcpy(errdetail,
			       "Simple READ must have compare data");
			errno = 0;
			ms->rest = NULL;
		}
		break;

	case CMD_LOCKW:
		if (ms->cmd != MCMD_SIMPLE_DEADLOCK) {
			sprintf(errdetail,
				"%s command can not be a simple command",
				commands[ms->client_cmd->r_cmd].cmd_name);
			errno = 0;
			ms->rest = NULL;
		}
		break;

	case CMD_LOCK:
	case CMD_HOP:
		if (ms->cmd != MCMD_SIMPLE_DENIED
		    && ms->cmd != MCMD_SIMPLE_GRANTED) {
			sprintf(errdetail,
				"Simple %s command requires GRANTED or DENIED status",
				commands[ms->client_cmd->r_cmd].
				cmd_name);
			errno = 0;
			ms->rest = NULL;
		}
		break;

	case CMD_TEST:
	case CMD_LIST:
		if (ms->cmd != MCMD_SIMPLE_AVAILABLE) {
			sprintf(errdetail,
				"Simple %s command requires AVAILABLE status",
				commands[ms->client_cmd->r_cmd].cmd_name);
			errno = 0;
			ms->rest = NULL;
		}
		break;

	case CMD_UNLOCK:
	case CMD_UNHOP:
		if (ms->cmd != MCMD_SIMPLE_GRANTED) {
			sprintf(errdetail,
				"Simple %s command requires GRANTED status",
				commands[ms->client_cmd->r_cmd].cmd_name);
			errno = 0;
			ms->rest = NULL;
		}
		break;

	case NUM_COMMANDS:
		strcpy(errdetail, "Invalid command");
		errno = 0;
		ms->rest = NULL;
		break;
	}

	if (ms->rest == NULL || syntax) {
		free_response(ms->client_cmd, NULL);
		return;
	}

	send_cmd(ms->client_cmd);

	/* We can't know what file descriptor will be returned */
	ms->client_cmd->r_fno = -1;
	sprintf_resp(ms->out, "EXPECT", ms->client_cmd);
	fprintf(output, "%s", ms->out);

	if (expect_one_response(ms->client_cmd, ms->last))
		ms->rest = NULL;
}

void master_command()
{
	struct master_state ms = {
		.inbrace = false,
		.count = 0,};

	ms.last[0] = '\0';

	while (1) {
		ms.len = readln(input, ms.line, MAXSTR);
		lno++;

		if (ms.len < 0) {
			ms.len = sprintf(ms.line, "QUIT");
			if (!syntax)
				fprintf(output, "QUIT\n");
		}

		ms.rest = SkipWhite(ms.line, REQUIRES_MORE, "Invalid line");

		/* Skip totally blank line and comments */
		if (ms.rest == NULL || ms.rest[0] == '#')
			continue;

		if (script && !syntax)
			fprintf(output, "Line %4ld: %s\n", lno, ms.line);

		ms.rest = get_token_value(ms.rest,
					  &ms.cmd,
					  master_commands,
					  true,
					  REQUIRES_EITHER,
					  "Invalid master command");

		if (ms.rest != NULL)
			switch ((enum master_cmd) ms.cmd) {
			case MCMD_QUIT:
				if (syntax)
					return;
				else
					handle_quit();
				break;

			case MCMD_STRICT:
				ms.rest = get_on_off(ms.rest, &strict);
				break;

			case MCMD_FATAL:
				ms.rest = get_on_off(ms.rest, &error_is_fatal);
				break;

			case MCMD_CLIENT_CMD:
				mcmd_client_cmd(&ms);
				break;

			case MCMD_SLEEP:
				mcmd_sleep(&ms);
				break;

			case MCMD_OPEN_BRACE:
				mcmd_open_brace(&ms);
				break;

			case MCMD_CLOSE_BRACE:
				mcmd_close_brace(&ms);
				break;

			case MCMD_CLIENTS:
				mcmd_clients(&ms);
				break;

			case MCMD_EXPECT:
				mcmd_expect(&ms);
				break;

			case MCMD_SIMPLE_OK:
			case MCMD_SIMPLE_AVAILABLE:
			case MCMD_SIMPLE_GRANTED:
			case MCMD_SIMPLE_DENIED:
			case MCMD_SIMPLE_DEADLOCK:
				mcmd_simple(&ms);
				break;

			}

		if (ms.rest == NULL) {
			error();

			if (syntax)
				fprintf(output,
					"Line %4ld: %s\n",
					lno, ms.line);

			if ((error_is_fatal && !syntax) || terminate)
				handle_quit();
		}

		if (!strict && !ms.inbrace && !script)
			break;

		if (!script) {
			fprintf(output, "> ");
			fflush(output);
		}
	}
}

void sighandler(int sig)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
	case SIGUSR1:
		terminate = true;
		break;

	case SIGPIPE:
		terminate = true;
		break;
	}
}

int main(int argc, char **argv)
{
	int opt;
	struct response *resp;
	struct sigaction sigact;
	bool syntax_only = false;
	int rc;

	input = stdin;
	output = stdout;

	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = sighandler;

	rc = sigaction(SIGINT, &sigact, NULL);
	if (rc == -1)
		fatal(
		    "sigaction(SIGINT, &sigact, NULL) returned -1 errno %d \"%s\"\n",
		    errno, strerror(errno));

	rc = sigaction(SIGTERM, &sigact, NULL);
	if (rc == -1)
		fatal(
		    "sigaction(SIGTERM, &sigact, NULL) returned -1 errno %d \"%s\"\n",
		    errno, strerror(errno));

	rc = sigaction(SIGUSR1, &sigact, NULL);
	if (rc == -1)
		fatal(
		    "sigaction(SIGUSR1, &sigact, NULL) returned -1 errno %d \"%s\"\n",
		    errno, strerror(errno));

	rc = sigaction(SIGPIPE, &sigact, NULL);
	if (rc == -1)
		fatal(
		    "sigaction(SIGPIPE, &sigact, NULL) returned -1 errno %d \"%s\"\n",
		    errno, strerror(errno));

	rc = sigfillset(&full_signal_set);
	if (rc == -1)
		fatal(
		    "sigfillset(&full_signal_set) returned -1 errno %d \"%s\"\n",
		    errno, strerror(errno));

	sigprocmask(SIG_SETMASK, &full_signal_set, &original_signal_set);

	/* now parsing options with getopt */
	while ((opt = getopt(argc, argv, options)) != EOF) {
		switch (opt) {
		case 'e':
			duperrors = true;
			err_accounting = true;
			break;

		case 'd':
			duperrors = true;
			break;

		case 'p':
			port = atoi(optarg);
			break;

		case 'q':
			quiet = true;
			break;

		case 's':
			strict = true;
			break;

		case 'x':
			input = fopen(optarg, "r");
			if (input == NULL)
				fatal("Could not open %s\n", optarg);
			script = true;
			break;

		case 'k':
			syntax_only = true;
			break;

		case 'f':
			error_is_fatal = true;
			break;

		case '?':
		case 'h':
		default:
			/* display the help */
			fprintf(stderr, "%s", usage);
			fflush(stderr);
			exit(0);
			break;
		}
	}

	open_socket();

	if (script) {
		syntax = true;

		master_command();

		if (num_errors != 0) {
			fprintf(stdout, "Syntax checks fail\n");
			return 1;
		}

		if (syntax_only) {
			fprintf(stdout, "Syntax checks ok\n");
			return 0;
		}

		syntax = false;
		global_tag = lno;
		lno = 0;
		rewind(input);

		master_command();
		/* Never returns */
	}

	while (1) {
		resp = receive_response(true, -1);

		if (strict && resp != NULL) {
			errno = 0;
			sprintf(errdetail, "Unexpected response");
			free_response(resp, NULL);
			error();
			if (error_is_fatal)
				handle_quit();
		}

		free_response(resp, NULL);
		master_command();
	}

	/* Never get here */
	return 0;
}
