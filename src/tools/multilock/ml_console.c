// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "assert.h"

/* command line syntax */

char options[] = "ekdqfsp:h?x:";
char usage[] =
	"Usage: ml_console [-p port] [-s] [-f] [-q] [-x script] [-d]\n" "\n"
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

void open_socket(void)
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

void do_accept(void)
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

	array_sprintf(client->c_name, "<UNKNOWN_%d>", client->c_socket);

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

int receive(bool watchin, long timeout_secs)
{
	fd_set readfds, exceptfds;
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

		readfds = sockets;

		if (watchin)
			FD_SET(0, &readfds);

		exceptfds = sockets;

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

void error(void)
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
	char line[MAXXFER];

	client_resp = alloc_resp(client);

	len = readln(client->c_input, line, sizeof(line));

	if (len >= 0) {
		array_sprintf(client_resp->r_original, "%s %s",
			      client->c_name, line);
		fprintf(output, "%s\n", client_resp->r_original);

		rest = parse_response(line, client_resp);

		if (rest == NULL)
			return client_resp;

		if (client_resp->r_cmd == CMD_HELLO) {
			assert(client_resp->r_length < sizeof(client->c_name));
			array_strcpy(client->c_name, client_resp->r_data);
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

static void console_command(void);

struct response *receive_response(bool watchin, long timeout_secs)
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
			array_strcpy(resp->r_original, "-1 QUIT CANCELED");
			errno = 0;
			array_strcpy(errdetail,
				     "Receive interrupted - exiting...");
		} else {
			/* some other error occurred */
			fprintf_stderr("Receive failed ERRNO %d \"%s\"\n",
				       errno, strerror(errno));
			resp->r_cmd = CMD_QUIT;

			resp->r_errno = errno;
			resp->r_tag = -1;
			array_strcpy(resp->r_data, "Receive failed");

			array_sprintf(resp->r_original,
				      "-1 QUIT ERRNO %d \"%s\" \"Receive failed\"",
				      errno, strerror(errno));

			array_strcpy(errdetail, "Receive failed");
			array_strcpy(badtoken, "");
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

enum console_cmd {
	CCMD_QUIT,
	CCMD_STRICT,
	CCMD_CLIENT_CMD,
	CCMD_EXPECT,
	CCMD_FATAL,
	CCMD_SLEEP,
	CCMD_OPEN_BRACE,
	CCMD_CLOSE_BRACE,
	CCMD_SIMPLE_OK,
	CCMD_SIMPLE_AVAILABLE,
	CCMD_SIMPLE_GRANTED,
	CCMD_SIMPLE_DENIED,
	CCMD_SIMPLE_DEADLOCK,
	CCMD_CLIENTS,
	CCMD_FORK,
};

struct token console_commands[] = {
	{"QUIT", 4, CCMD_QUIT},
	{"STRICT", 6, CCMD_STRICT},
	{"EXPECT", 6, CCMD_EXPECT},
	{"FATAL", 5, CCMD_FATAL},
	{"SLEEP", 5, CCMD_SLEEP},
	{"{", 1, CCMD_OPEN_BRACE},
	{"}", 1, CCMD_CLOSE_BRACE},
	{"OK", 2, CCMD_SIMPLE_OK},
	{"AVAILABLE", 9, CCMD_SIMPLE_AVAILABLE},
	{"GRANTED", 7, CCMD_SIMPLE_GRANTED},
	{"DENIED", 6, CCMD_SIMPLE_DENIED},
	{"DEADLOCK", 8, CCMD_SIMPLE_DEADLOCK},
	{"CLIENTS", 7, CCMD_CLIENTS},
	{"FORK", 4, CCMD_FORK},
	{"", 0, CCMD_CLIENT_CMD}
};

static void handle_quit(void);

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
			array_strcpy(errdetail, "Unexpected response");
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

void handle_quit(void)
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

			array_sprintf(out, "%ld QUIT\n", ++global_tag);
			fputs(out, client->c_output);
			fflush(client->c_output);

			/* Build an EXPECT for -1 QUIT for this client */
			expect_resp = alloc_resp(client);
			expect_resp->r_cmd = CMD_QUIT;
			expect_resp->r_status = STATUS_OK;
			expect_resp->r_tag = global_tag;
			array_sprintf(expect_resp->r_original,
				      "EXPECT %s * QUIT OK",
				      client->c_name);
			add_response(expect_resp, &expected_responses);
			count++;

			/* Build an EXPECT for -2 QUIT for this client */
			expect_resp = alloc_resp(client);
			expect_resp->r_cmd = CMD_QUIT;
			expect_resp->r_status = STATUS_OK;
			expect_resp->r_tag = -2;
			array_sprintf(expect_resp->r_original,
				      "EXPECT %s -2 QUIT OK",
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

struct console_state {
	char *rest;
	char line[MAXXFER];
	char out[MAXXFER];
	char last[MAXXFER];	/* last command sent */
	struct client *client;
	int len;
	int cmd;
	struct response *expect_resp;
	struct response *client_cmd;
	bool inbrace;
	int count;
};

void ccmd_client_cmd(struct console_state *cs)
{
	cs->rest = get_client(cs->line, &cs->client, syntax, REQUIRES_MORE);

	if (cs->rest == NULL)
		return;

	if (script)
		array_sprintf(cs->last, "Line %4ld: %s", lno, cs->line);
	else
		array_strcpy(cs->last, cs->line);

	cs->client_cmd = alloc_resp(cs->client);

	cs->rest = parse_request(cs->rest, cs->client_cmd, false);

	if (cs->rest != NULL && !syntax)
		send_cmd(cs->client_cmd);

	free_response(cs->client_cmd, NULL);
}

void ccmd_sleep(struct console_state *cs)
{
	long secs;
	int t_end, t_now;

	cs->rest = get_long(cs->rest, &secs, true, "Invalid sleep time");

	if (cs->rest == NULL)
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
					cs->last,
					client_resp->r_original);

			array_strcpy(errdetail, "Unexpected response");

			cs->rest = NULL;

			free_response(client_resp, NULL);
		}

		/* If sleep 0 or we have run out, just want single iteration */
		if (t_now == t_end)
			break;
	}
}

void ccmd_open_brace(struct console_state *cs)
{
	if (cs->inbrace) {
		errno = 0;
		array_strcpy(errdetail, "Illegal nested brace");
		cs->rest = NULL;
	}

	cs->count = 0;
	cs->inbrace = true;
}

void ccmd_close_brace(struct console_state *cs)
{
	if (!cs->inbrace) {
		errno = 0;
		array_strcpy(errdetail, "Unmatched close brace");
		cs->rest = NULL;
	} else if (!syntax) {
		cs->inbrace = false;
		wait_for_expected_responses("responses",
					    cs->count, cs->last, true);
		fprintf(output,
			"All responses received OK\n");
		cs->count = 0;
	} else {
		cs->inbrace = false;
	}
}

void ccmd_clients(struct console_state *cs)
{
	if (cs->inbrace) {
		errno = 0;
		array_strcpy(errdetail,
			     "CLIENTS command not allowed inside brace");
		cs->rest = NULL;
		return;
	}

	while (cs->rest != NULL && cs->rest[0] != '\0'
	       && cs->rest[0] != '#') {
		/* Get the next client to expect */
		cs->rest = get_client(cs->rest,
				      &cs->client,
				      true,
				      REQUIRES_EITHER);

		if (cs->rest == NULL)
			return;

		/* Build an EXPECT client * HELLO OK "client" */
		cs->expect_resp = alloc_resp(cs->client);
		cs->expect_resp->r_cmd = CMD_HELLO;
		cs->expect_resp->r_tag = -1;
		cs->expect_resp->r_status = STATUS_OK;

		array_strcpy(cs->expect_resp->r_data, cs->client->c_name);

		array_sprintf(cs->expect_resp->r_original,
			      "EXPECT %s * HELLO OK \"%s\"",
			      cs->client->c_name, cs->client->c_name);

		cs->count++;

		if (syntax) {
			free_response(cs->expect_resp, NULL);
		} else {
			/* Add response to list of expected responses */
			add_response(cs->expect_resp, &expected_responses);
		}
	}

	if (cs->count == 0) {
		errno = 0;
		array_strcpy(errdetail, "Expected at least one client");
		cs->rest = NULL;
		return;
	}

	if (!syntax) {
		wait_for_expected_responses("clients",
					    cs->count,
					    cs->last,
					    true);
		fprintf(output,
			"All clients said HELLO OK\n");
	}

	cs->count = 0;
}

void ccmd_fork(struct console_state *cs)
{
	if (cs->inbrace) {
		errno = 0;
		array_strcpy(errdetail,
			     "FORK command not allowed inside brace");
		cs->rest = NULL;
		return;
	}

	/* Get the client to send FORK to */
	cs->rest = get_client(cs->rest,
			      &cs->client,
			      syntax,
			      REQUIRES_MORE);

	if (cs->rest == NULL)
		return;

	/* Build an EXPECT client * FORK OK "client" */
	cs->client_cmd = alloc_resp(cs->client);
	cs->client_cmd->r_cmd = CMD_FORK;
	cs->client_cmd->r_tag = -1;
	cs->client_cmd->r_status = STATUS_OK;

	cs->count++;

	/* Get the client that will be created */
	cs->rest = get_client(cs->rest,
			      &cs->client,
			      true,
			      REQUIRES_NO_MORE);

	if (cs->rest == NULL)
		return;

	/* Build an EXPECT client * HELLO OK "client" */
	cs->expect_resp = alloc_resp(cs->client);
	cs->expect_resp->r_cmd = CMD_HELLO;
	cs->expect_resp->r_tag = -1;
	cs->expect_resp->r_status = STATUS_OK;

	/* Use the created client's name as the FORK data */
	array_strcpy(cs->client_cmd->r_data,
		     cs->expect_resp->r_client->c_name);

	array_strcpy(cs->expect_resp->r_data,
		     cs->expect_resp->r_client->c_name);

	array_sprintf(cs->client_cmd->r_original,
		      "EXPECT %s * FORK OK \"%s\"",
		      cs->client->c_name, cs->client->c_name);

	array_sprintf(cs->expect_resp->r_original,
		      "EXPECT %s * HELLO OK \"%s\"",
		      cs->client->c_name, cs->client->c_name);

	cs->count++;

	if (syntax) {
		free_response(cs->client_cmd, NULL);
		free_response(cs->expect_resp, NULL);
	} else {
		/* Send the command */
		send_cmd(cs->client_cmd);

		/* Now fixup to expect client name as FORK OK data */
		array_strcpy(cs->client_cmd->r_data,
			     cs->client_cmd->r_client->c_name);

		/* Add responses to list of expected responses */
		add_response(cs->client_cmd, &expected_responses);
		add_response(cs->expect_resp, &expected_responses);
	}

	if (!syntax) {
		wait_for_expected_responses("clients",
					    cs->count,
					    cs->last,
					    true);
		fprintf(output, "All clients responded OK\n");
	}

	cs->count = 0;
}

void ccmd_expect(struct console_state *cs)
{
	cs->rest = get_client(cs->rest, &cs->client, true, REQUIRES_MORE);

	if (cs->rest == NULL)
		return;

	cs->expect_resp = alloc_resp(cs->client);

	if (script) {
		array_sprintf(cs->expect_resp->r_original,
			      "Line %4ld: EXPECT %s %s",
			      lno, cs->client->c_name, cs->rest);
	} else {
		array_sprintf(cs->expect_resp->r_original,
			      "EXPECT %s %s",
			      cs->client->c_name, cs->rest);
	}

	cs->rest = parse_response(cs->rest, cs->expect_resp);

	if (cs->rest == NULL || syntax) {
		free_response(cs->expect_resp, NULL);
	} else if (cs->inbrace) {
		add_response(cs->expect_resp, &expected_responses);
		cs->count++;
	} else if (expect_one_response(cs->expect_resp, cs->last))
		cs->rest = NULL;
}

void ccmd_simple(struct console_state *cs)
{
	array_strcpy(cs->last, cs->line);
	cs->rest = get_client(cs->rest, &cs->client, syntax, REQUIRES_MORE);

	if (cs->rest == NULL)
		return;

	cs->client_cmd = alloc_resp(cs->client);

	if (cs->cmd == CCMD_SIMPLE_OK)
		cs->client_cmd->r_status = STATUS_OK;
	else if (cs->cmd == CCMD_SIMPLE_AVAILABLE)
		cs->client_cmd->r_status = STATUS_AVAILABLE;
	else if (cs->cmd == CCMD_SIMPLE_GRANTED)
		cs->client_cmd->r_status = STATUS_GRANTED;
	else if (cs->cmd == CCMD_SIMPLE_DEADLOCK)
		cs->client_cmd->r_status = STATUS_DEADLOCK;
	else
		cs->client_cmd->r_status = STATUS_DENIED;

	cs->rest = parse_request(cs->rest, cs->client_cmd, true);

	if (cs->rest == NULL) {
		free_response(cs->client_cmd, NULL);
		return;
	}

	switch (cs->client_cmd->r_cmd) {
	case CMD_OPEN:
	case CMD_CLOSE:
	case CMD_SEEK:
	case CMD_WRITE:
	case CMD_COMMENT:
	case CMD_ALARM:
	case CMD_HELLO:
	case CMD_QUIT:
		if (cs->cmd != CCMD_SIMPLE_OK) {
			array_sprintf(errdetail,
				      "Simple %s command expects OK",
				      commands[cs->client_cmd->r_cmd].cmd_name);
			errno = 0;
			cs->rest = NULL;
		}
		break;

	case CMD_READ:
		if (cs->cmd != CCMD_SIMPLE_OK) {
			array_sprintf(errdetail,
				      "Simple %s command expects OK",
				      commands[cs->client_cmd->r_cmd].cmd_name);
			errno = 0;
			cs->rest = NULL;
		} else if (cs->client_cmd->r_length == 0
			   || cs->client_cmd->r_data[0] == '\0') {
			array_strcpy(errdetail,
				     "Simple READ must have compare data");
			errno = 0;
			cs->rest = NULL;
		}
		break;

	case CMD_LOCKW:
		if (cs->cmd != CCMD_SIMPLE_DEADLOCK) {
			array_sprintf(errdetail,
				      "%s command can not be a simple command",
				      commands[cs->client_cmd->r_cmd].cmd_name);
			errno = 0;
			cs->rest = NULL;
		}
		break;

	case CMD_LOCK:
	case CMD_HOP:
		if (cs->cmd != CCMD_SIMPLE_DENIED
		    && cs->cmd != CCMD_SIMPLE_GRANTED) {
			array_sprintf(errdetail,
				      "Simple %s command requires GRANTED or DENIED status",
				      commands[cs->client_cmd->r_cmd].cmd_name);
			errno = 0;
			cs->rest = NULL;
		}
		break;

	case CMD_TEST:
	case CMD_LIST:
		if (cs->cmd != CCMD_SIMPLE_AVAILABLE) {
			array_sprintf(errdetail,
				      "Simple %s command requires AVAILABLE status",
				      commands[cs->client_cmd->r_cmd].cmd_name);
			errno = 0;
			cs->rest = NULL;
		}
		break;

	case CMD_UNLOCK:
	case CMD_UNHOP:
		if (cs->cmd != CCMD_SIMPLE_GRANTED) {
			array_sprintf(errdetail,
				      "Simple %s command requires GRANTED status",
				      commands[cs->client_cmd->r_cmd].cmd_name);
			errno = 0;
			cs->rest = NULL;
		}
		break;

	case CMD_FORK:
		array_strcpy(errdetail,
			     "FORK not compatible with a simple command");
		break;

	case NUM_COMMANDS:
		array_strcpy(errdetail, "Invalid command");
		errno = 0;
		cs->rest = NULL;
		break;
	}

	if (cs->rest == NULL || syntax) {
		free_response(cs->client_cmd, NULL);
		return;
	}

	send_cmd(cs->client_cmd);

	/* We can't know what file descriptor will be returned */
	cs->client_cmd->r_fno = -1;
	sprintf_resp(cs->out, sizeof(cs->out), "EXPECT", cs->client_cmd);
	fprintf(output, "%s", cs->out);

	if (expect_one_response(cs->client_cmd, cs->last))
		cs->rest = NULL;
}

void console_command(void)
{
	struct console_state cs = {
		.inbrace = false,
		.count = 0,};

	cs.last[0] = '\0';

	while (1) {
		cs.len = readln(input, cs.line, sizeof(cs.line));
		lno++;

		if (cs.len < 0) {
			array_sprintf(cs.line, "QUIT");
			cs.len = strlen(cs.line);
			if (!syntax)
				fprintf(output, "QUIT\n");
		}

		cs.rest = SkipWhite(cs.line, REQUIRES_MORE, "Invalid line");

		/* Skip totally blank line and comments */
		if (cs.rest == NULL || cs.rest[0] == '#')
			continue;

		if (script && !syntax)
			fprintf(output, "Line %4ld: %s\n", lno, cs.line);

		cs.rest = get_token_value(cs.rest,
					  &cs.cmd,
					  console_commands,
					  true,
					  REQUIRES_EITHER,
					  "Invalid console command");

		if (cs.rest != NULL)
			switch ((enum console_cmd) cs.cmd) {
			case CCMD_QUIT:
				if (syntax)
					return;
				else
					handle_quit();
				break;

			case CCMD_STRICT:
				cs.rest = get_on_off(cs.rest, &strict);
				break;

			case CCMD_FATAL:
				cs.rest = get_on_off(cs.rest, &error_is_fatal);
				break;

			case CCMD_CLIENT_CMD:
				ccmd_client_cmd(&cs);
				break;

			case CCMD_SLEEP:
				ccmd_sleep(&cs);
				break;

			case CCMD_OPEN_BRACE:
				ccmd_open_brace(&cs);
				break;

			case CCMD_CLOSE_BRACE:
				ccmd_close_brace(&cs);
				break;

			case CCMD_CLIENTS:
				ccmd_clients(&cs);
				break;

			case CCMD_EXPECT:
				ccmd_expect(&cs);
				break;

			case CCMD_FORK:
				ccmd_fork(&cs);
				break;

			case CCMD_SIMPLE_OK:
			case CCMD_SIMPLE_AVAILABLE:
			case CCMD_SIMPLE_GRANTED:
			case CCMD_SIMPLE_DENIED:
			case CCMD_SIMPLE_DEADLOCK:
				ccmd_simple(&cs);
				break;

			}

		if (cs.rest == NULL) {
			error();

			if (syntax)
				fprintf(output,
					"Line %4ld: %s\n",
					lno, cs.line);

			if ((error_is_fatal && !syntax) || terminate)
				handle_quit();
		}

		if (!strict && !cs.inbrace && !script)
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

		console_command();

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

		console_command();
		/* Never returns */
	}

	while (1) {
		resp = receive_response(true, -1);

		if (strict && resp != NULL) {
			errno = 0;
			array_strcpy(errdetail, "Unexpected response");
			free_response(resp, NULL);
			error();
			if (error_is_fatal)
				handle_quit();
		}

		free_response(resp, NULL);
		console_command();
	}

	/* Never get here */
	return 0;
}
