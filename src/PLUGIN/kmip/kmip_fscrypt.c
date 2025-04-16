// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright IBM Corporation, 2025
 * Author: Marcus Watts <mwatts@ibm.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* main.c
 * Module core functions
 */

#include <stdlib.h>
#include <assert.h>
#include "gsh_list.h"
#include "log.h"
#include "FSAL/fsal_init.h"
#include "abstract_mem.h"
#include "config_parsing.h"
#include "conf_url.h"
#include "nfs_exports.h"
#include "kmip.h"
#include "kmip_bio.h"
#include "kmip_memset.h"
#include <openssl/err.h>
#include <openssl/store.h>

/* anti-bug pattern: does not take typename (considered unsafe) */
#define safe_sizeof(v) (size##of v)

#define BUSY_TIMEOUT 31 /* polling delay waiting for busy connection */

struct kmip_host_param {
	struct glist_head link;
	char *name; /* connect to this */
	unsigned short port;
	char *servername; /* via "sni"; indicate want this */
	char *verify_hostname; /* verify this in server cert that comes back */
};
struct kmip_params {
	char *kmip_cert;
	char *kmip_key;
	char *kmip_ca;
	char *kmip_chain_file;
	char *kmip_user;
	char *kmip_password;
	int kmip_version;
	uint32_t kmip_timeout;
	struct glist_head kmip_host;
};

struct export_kmip {
	char *kmip_key_id;
	uint16_t export_id;
};

struct kmip_params kmip_settings;
bool need_kmip_reload = FALSE;

void *kmip_host_init(void *, void *);
static void *kmip_block_init(void *link_mem, void *self_struct);
static int kmip_host_commit(void *, void *, void *, struct config_error_type *);
static int kmip_load_export_extension(struct export_extension *, config_file_t,
				      struct config_error_type *);
static int kmip_root_cb_func(struct exp_root_callback *,
			     struct fsal_obj_handle *obj);
static int kmip_root_cb_free(struct exp_root_callback *);
static void flush_kconns(void);

static struct config_item kmip_host_params[] = {
	CONF_ITEM_STR("addr", 0, 512, "localhost.", kmip_host_param, name),
	CONF_ITEM_UI16("port", 1, UINT16_MAX, 5696, kmip_host_param,
		       port), /* default is kmip */
	CONF_ITEM_STR("servername", 0, 512, NULL, kmip_host_param, servername),
	CONF_ITEM_STR("verify_hostname", 0, 512, NULL, kmip_host_param,
		      verify_hostname),
	CONFIG_EOL
};

static struct config_item_list kmip_protocols[] = {
	CONFIG_LIST_TOK("1.0", KMIP_1_0),
	CONFIG_LIST_TOK("1.1", KMIP_1_1),
	CONFIG_LIST_TOK("1.2", KMIP_1_2),
	CONFIG_LIST_TOK("1.3", KMIP_1_3),
	CONFIG_LIST_TOK("1.4", KMIP_1_4),
	CONFIG_LIST_TOK("2.0", KMIP_2_0),
	CONFIG_LIST_EOL
};

static struct config_item kmip_params[] = {
	CONF_ITEM_PATH("cert", 1, MAXPATHLEN, NULL, kmip_params, kmip_cert),
	CONF_ITEM_PATH("key", 1, MAXPATHLEN, NULL, kmip_params, kmip_key),
	CONF_ITEM_PATH("ca", 1, MAXPATHLEN, NULL, kmip_params, kmip_ca),
	CONF_ITEM_PATH("cert_chain", 1, MAXPATHLEN, NULL, kmip_params,
		       kmip_chain_file),
	CONF_ITEM_STR("user", 0, 512, NULL, kmip_params, kmip_user),
	CONF_ITEM_STR("password", 0, 512, NULL, kmip_params, kmip_password),
	CONF_ITEM_TOKEN("protocol", KMIP_1_1, kmip_protocols, kmip_params,
			kmip_version),
	CONF_ITEM_UI32("idle_timeout", 1, 3600, 7, kmip_params, kmip_timeout),
	CONF_ITEM_BLOCK_MULT("HOST", kmip_host_params, kmip_host_init,
			     kmip_host_commit, kmip_params, kmip_host),
	CONFIG_EOL
};

struct config_block kmip_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fscrypt.kmip",
	.blk_desc.name = "KMIP",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.flags = CONFIG_UNIQUE,
	.blk_desc.u.blk.init = kmip_block_init,
	.blk_desc.u.blk.params = kmip_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static struct config_item kmip_export_params[] = {
	CONF_ITEM_STR("kmip_key_id", 0, 512, NULL, export_kmip, kmip_key_id),
	CONF_MAND_UI16("Export_id", 0, UINT16_MAX, 1, export_kmip, export_id),

	CONFIG_EOL
};

/* this magic value means
 * "ignore existing encryption status on dir, don't add key"
 */
#define MAGIC_KMIP_KEY_ID "i-really-really-really-mean-this"

static int kmip_export_extension_commit(void *, void *, void *,
					struct config_error_type *);

struct config_block kmip_export_extensions = {
	.dbus_interface_name = "org.ganesha.nfsd.config.kmip.%d",
	.blk_desc.name = "EXPORT",
	.blk_desc.flags = CONFIG_RELAX,
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = kmip_export_params,
	.blk_desc.u.blk.commit = kmip_export_extension_commit
};

struct export_extension_sw kmip_extension_sw = { kmip_load_export_extension };

struct kmip_export_extension {
	struct export_extension extension;
} kmip_export_extension_st = { .extension = { .sw = &kmip_extension_sw } };

struct exp_root_callback_sw kmip_root_callback_sw = { kmip_root_cb_func,
						      kmip_root_cb_free };

struct kmip_callback {
	struct exp_root_callback callback;
	char *kmip_key_id;
};

void free_host_params(void)
{
	struct glist_head *host_list = &kmip_settings.kmip_host;
	struct kmip_host_param *host_p;

	if (glist_null(host_list)) {
		return;
	}
	while ((host_p = glist_first_entry(host_list, struct kmip_host_param,
					   link))) {
		glist_del(&host_p->link);
		gsh_free(host_p);
	}
}

int kmip_count_hosts(struct kmip_params *params)
{
	struct glist_head *gl;
	int r = 0;

	glist_for_each(gl, &params->kmip_host) {
		++r;
	}
	return r;
}

struct kmip_host_param *kmip_nth_host(struct kmip_params *params, int n)
{
	struct glist_head *gl;
	struct glist_head *host_list = &params->kmip_host;
	struct kmip_host_param *host_p;
	int i = 0;

	if (glist_null(host_list)) {
		return NULL;
	}
	glist_for_each(gl, host_list) {
		if (i == n) {
			host_p = glist_entry(gl, struct kmip_host_param, link);
			return host_p;
		}
		++i;
	}
	return NULL;
}

void *kmip_host_init(void *link_mem, void *self_struct)
{
	assert(link_mem || self_struct);
	if (!link_mem) {
		return self_struct;
	}
	if (!self_struct) {
		struct kmip_host_param *host_p;

		host_p = gsh_calloc(1, safe_sizeof(*host_p));
		return host_p;
	} else {
		gsh_free(self_struct);
	}
	return 0;
}

static int kmip_host_commit(void *node, void *link_mem, void *self_struct,
			    struct config_error_type *err_type)
{
	/* link_mem will be &kmip_settings.kmip_host
	 * self_struct will be a host_p
	 */
	struct glist_head *host_list = link_mem;
	struct kmip_host_param *host_p = self_struct;

	glist_add_tail(host_list, &host_p->link);
	return 0;
}

static void *kmip_block_init(void *link_mem, void *self_struct)
{
	/* exactly one of link_mem self_struct will be &kmip_settings */
	assert(link_mem != NULL || self_struct != NULL);
	struct kmip_params *settings = self_struct;

	if (link_mem == NULL) { /* call#1 0, v */
		if (glist_null(&settings->kmip_host)) {
			glist_init(&settings->kmip_host);
		} else {
			free_host_params();
			flush_kconns();
		}
		return self_struct;
	} else if (self_struct == NULL) { /* call#2 */
		return link_mem;
	} else
		return NULL;
}

int kmip_init_block(config_file_t config_struct,
		    struct config_error_type *err_type)
{
	int rc;

	rc = load_config_from_parse(config_struct, &kmip_block, &kmip_settings,
				    true, err_type);
	/*
	 * All kmip options are optional, so no kmip block
	 * is not necessarily bad.
	 */
	if (!config_error_is_harmless(err_type))
		LogDebug(COMPONENT_FSAL, "Parsing kmip block failed");
	if (rc > 0)
		rc = 0;
	return rc;
}

int load_kmip_export_extensions(config_file_t in_config,
				struct config_error_type *err_type)
{
	int rc;
	struct export_kmip st[1];

	memset(st, 0, safe_sizeof(*st));
	rc = load_config_from_parse(in_config, &kmip_export_extensions, st,
				    false, err_type);

	return rc;
}

struct kmip_plugin_module {
	struct gsh_config_provider config;
};

struct kmip_plugin_module
	kmip_plugin_static_t = { .config = {
					 .init_block = kmip_init_block,
				 } };

/**
 * @brief pass kmip_key_id to export root callback.
 */

static int kmip_export_extension_commit(void *node, void *link_mem,
					void *self_struct,
					struct config_error_type *err_type)
{
	struct kmip_callback *cb;
	struct export_kmip *st = self_struct;
	struct gsh_export *exp;
	int err_count = 0;

	exp = get_gsh_export(st->export_id);
	if (!exp) {
		LogCrit(COMPONENT_CONFIG, "Export %d does not exist",
			st->export_id);
		return ++err_count;
	}
	if (!st->kmip_key_id || strcmp(st->kmip_key_id, MAGIC_KMIP_KEY_ID)) {
		cb = gsh_calloc(1, safe_sizeof(*cb));
		if (st->kmip_key_id) {
			cb->kmip_key_id = gsh_strdup(st->kmip_key_id);
		}
		add_to_export_callbacks(exp, &kmip_root_callback_sw,
					&cb->callback);
	}
	put_gsh_export_config(exp);
	return 0;
}

/**
 * @brief look for kmip_key_id in all EXPORT blocks.
 */

static int kmip_load_export_extension(struct export_extension *ex,
				      config_file_t in_config,
				      struct config_error_type *err_type)
{
	int rc, rc0 = 0;
	struct export_kmip st[1];

	__attribute__((unused)) /* don't need for now; optimizer will delete */
	struct kmip_export_extension *extension_st;

	extension_st =
		container_of(ex, struct kmip_export_extension, extension);
	/* except during init, expicitly reread kmip block */
	if (need_kmip_reload) {
		rc0 = kmip_init_block(in_config, err_type);
	}
	need_kmip_reload = TRUE;
	memset(st, 0, safe_sizeof(*st));
	rc = load_config_from_parse(in_config, &kmip_export_extensions, st,
				    false, err_type);
	return rc ? rc : rc0;
}

/**
 * @brief log any queued ssl errors
 */

void log_ssl_errors(void)
{
	BUF_MEM *bptr;
	char *cp;
	BIO *mem = BIO_new(BIO_s_mem());

	ERR_print_errors(mem);
	BIO_get_mem_ptr(mem, &bptr);
	if (bptr->length) {
		cp = bptr->data + bptr->length;
		if (*--cp == '\n') {
			*cp = 0;
		}
	}
	LogCrit(COMPONENT_FSAL, "ssl error: <%s>", bptr->data);
	BIO_free(mem);
}

/*
 * @brief in case of error: log queued up line sources from libkmip.
 */

void log_kmip_stacked_errors(KMIP *ctx)
{
	ErrorFrame *ef;

	if (!ctx)
		return;
	for (ef = ctx->frame_index; ef >= ctx->errors; --ef)
		if (!ef->line)
			;
		else
			LogCrit(COMPONENT_FSAL, "- %s @ line: %d", ef->function,
				ef->line);
}

struct my_kmip_connection {
	SSL_CTX *ctx;
	SSL *ssl;
	BIO *bio;
	KMIP kmip_ctx[1];
	TextString textstrings[2];
	UsernamePasswordCredential upc[1];
	Credential credential[1];
	int need_to_free_kmip;
	size_t buffer_blocks, buffer_block_size, buffer_total_size;
	uint8 *encoding;
	int idle;
	struct timeval lastuse[1];
	int poison;
};
struct my_kmip_connection saved_kconn[1];
pthread_mutex_t kmip_connection_lock;
pthread_cond_t kmip_connection_wait;
pthread_t kmip_reaper_thread;
int kmip_shutting_down;
int saved_kmip_host_index;
#define NOT_CONNECTED(k) (!(k)->bio)
#define BUSY(k) (!(k)->idle)

/*
 * @brief free any resources associated with a kmip connection
 *
 * @param[in] kconn    the connection to be cleared.
 *
 * @return 0
 */

int kmip_free_handle_stuff(struct my_kmip_connection *kconn)
{
	if (kconn->encoding) {
		kmip_free_buffer(kconn->kmip_ctx, kconn->encoding,
				 kconn->buffer_total_size);
		kmip_set_buffer(kconn->kmip_ctx, NULL, 0);
		kconn->encoding = 0;
	}
	if (kconn->need_to_free_kmip) {
		kmip_destroy(kconn->kmip_ctx);
		kconn->need_to_free_kmip = 0;
	}
	if (kconn->bio) {
		BIO_free_all(kconn->bio);
		kconn->bio = 0;
	}
	if (kconn->ctx) {
		SSL_CTX_free(kconn->ctx);
		kconn->ctx = 0;
	}
	kconn->idle = 1;
	kconn->poison = 0;
	return 0;
}

int load_certs(SSL_CTX *ctx, char *chain_file, STACK_OF(X509) * *chain)
{
	int r = 0;

	STACK_OF(X509) *certs = 0;
	OSSL_STORE_CTX *store = NULL;
	int ncerts = 0;

	*chain = 0;
	certs = sk_X509_new_null();
	if (!certs) {
		LogCrit(COMPONENT_FSAL, "Out of memory loading");
		goto Done;
	}
	store = OSSL_STORE_open(chain_file, NULL, NULL, NULL, NULL);
	if (!store) {
		LogCrit(COMPONENT_FSAL, "Can't open file or uri for loading");
		goto Done;
	}
	while (!OSSL_STORE_eof(store)) {
		OSSL_STORE_INFO *info = OSSL_STORE_load(store);

		if (!info) {
			break;
		}
		int type = OSSL_STORE_INFO_get_type(info);
		int ok = 1;

		switch (type) {
		case OSSL_STORE_INFO_CERT: {
			ok = X509_add_cert(certs,
					   OSSL_STORE_INFO_get1_CERT(info),
					   X509_ADD_FLAG_DEFAULT);
			ncerts += ok;
		}; break;
		default:
			/* ignore any other type */
			break;
		}
		OSSL_STORE_INFO_free(info);
		if (!ok) {
			LogCrit(COMPONENT_FSAL,
				"Error reading entry from chain_file <%s>",
				chain_file);
			goto Done;
		}
	}
	if (!ncerts) {
		LogCrit(COMPONENT_FSAL, "Found no certs in chain file <%s>",
			chain_file);
		goto Done;
	}
	*chain = certs;
	certs = 0;
	r = 1;
Done:
	if (store)
		OSSL_STORE_close(store);
	if (certs) {
		sk_X509_pop_free(certs, X509_free);
	}
	return r;
}

/*
 * @brief retrieve an idle kmip handle.  It may or may not be connected.
 *
 * @return kconn  an idle connection.
 */

struct my_kmip_connection *get_kmip_handle(void)
{
	struct my_kmip_connection *kconn;

	pthread_mutex_lock(&kmip_connection_lock);
	for (;;) {
		kconn = saved_kconn;
		if (!kmip_shutting_down && !BUSY(kconn)) {
			kconn->idle = 0;
		} else {
			kconn = 0;
		}
		if (kmip_shutting_down || kconn)
			break;
		pthread_cond_wait(&kmip_connection_wait, &kmip_connection_lock);
	}
	pthread_mutex_unlock(&kmip_connection_lock);
	return kconn;
}

/*
 * @brief mark a connection available for reuse.
 *
 * The connection is left connected.  It will be cleared by
 * the reaper thread if it is not needed again soon.
 *
 * @param[in] kconn     connectino to be released.
 */

void release_kmip_handle(struct my_kmip_connection *kconn)
{
	if (!kconn)
		return;
	gettimeofday(kconn->lastuse, NULL);
	kconn->idle = 1;
	pthread_cond_broadcast(&kmip_connection_wait);
}

/*
 * @brief wait for any pending kmip calls.
 *
 * @return 0 if there is a pending call.
 * @return kconn an idle connectino.
 */

struct my_kmip_connection *timed_wait_kmip_busy(void)
{
	struct my_kmip_connection *kconn;
	struct timespec now_ts[1];

	clock_gettime(CLOCK_REALTIME, now_ts);
	now_ts->tv_sec += BUSY_TIMEOUT;
	pthread_mutex_lock(&kmip_connection_lock);
	for (;;) {
		kconn = saved_kconn;
		if (!BUSY(kconn)) {
			kconn->idle = 0;
		} else {
			kconn = 0;
		}
		if (kconn)
			break;
		if (pthread_cond_timedwait(&kmip_connection_wait,
					   &kmip_connection_lock, now_ts) < 0) {
			LogCrit(COMPONENT_FSAL,
				"%s: connection hung - go boom now?", __func__);
			break;
		}
	}
	pthread_mutex_unlock(&kmip_connection_lock);
	return kconn;
}

static void flush_kconns(void)
{
	saved_kconn->poison = 1;
}

/*
 * @brief connect to a named kmip host.
 *
 * @param[in] kconn holds all per-connection data.
 * @param[in] host  the dns host name to which to connect.
 * @param[in] portstring a string containing ascii digts
 *            of the port to connect to.
 *
 * @return 0 on success
 * @return 1 on failure
 */

int setup_kmip_connect(struct my_kmip_connection *kconn,
		       struct kmip_host_param *host_p)
{
	int r = 666;
	int i;
	size_t ns;
	TextString *up;

	STACK_OF(X509) *chain = 0;
	char portstring[8];
	const char *hostnm, *servername, *verify_hostname;

	/* generic initialization */

	memset(kconn, 0, safe_sizeof(*kconn));
	OPENSSL_init_ssl(0, NULL);
	kconn->ctx = SSL_CTX_new(TLS_client_method());
	if (!kmip_settings.kmip_cert)
		;
	else if (SSL_CTX_use_certificate_chain_file(
			 kconn->ctx, kmip_settings.kmip_cert) != 1) {
		LogCrit(COMPONENT_FSAL, "Can't load client cert from %s",
			kmip_settings.kmip_cert);
		log_ssl_errors();
		r = 1;
		goto Done;
	}
	if (!kmip_settings.kmip_key)
		;
	else if (SSL_CTX_use_PrivateKey_file(kconn->ctx, kmip_settings.kmip_key,
					     SSL_FILETYPE_PEM) != 1) {
		LogCrit(COMPONENT_FSAL, "Can't load client key from %s",
			kmip_settings.kmip_key);
		log_ssl_errors();
		r = 1;
		goto Done;
	}
	if (!kmip_settings.kmip_ca)
		;
	else if (SSL_CTX_load_verify_locations(
			 kconn->ctx, kmip_settings.kmip_ca, NULL) != 1) {
		LogCrit(COMPONENT_FSAL, "Can't load cacert %s",
			kmip_settings.kmip_ca);
		log_ssl_errors();
		r = 1;
		goto Done;
	}
	if (!kmip_settings.kmip_chain_file)
		;
	else if (!load_certs(kconn->ctx, kmip_settings.kmip_chain_file,
			     &chain)) {
		LogCrit(COMPONENT_FSAL, "Can't load chain_file <%s>",
			kmip_settings.kmip_chain_file);
		log_ssl_errors();
		r = 1;
		goto Done;
	} else if (!SSL_CTX_set1_chain(kconn->ctx, chain)) {
		LogCrit(COMPONENT_FSAL, "Can't set chain certs from <%s>",
			kmip_settings.kmip_chain_file);
		log_ssl_errors();
		r = 1;
		goto Done;
	}
	SSL_CTX_set_verify(kconn->ctx, SSL_VERIFY_PEER, NULL);
	kconn->bio = BIO_new_ssl_connect(kconn->ctx);
	if (!kconn->bio) {
		LogCrit(COMPONENT_FSAL, "BIO_new_ssl_connect failed");
		log_ssl_errors();
		r = 1;
		goto Done;
	}
	BIO_get_ssl(kconn->bio, &kconn->ssl);

	BIO_set_conn_hostname(kconn->bio, host_p->name);
	snprintf(portstring, safe_sizeof(portstring), "%d", host_p->port);
	BIO_set_conn_port(kconn->bio, portstring);
	hostnm = BIO_get_conn_hostname(kconn->bio);

	servername = host_p->servername;
	verify_hostname = host_p->verify_hostname;
	if (servername && !*servername) {
	} else {
		if (!servername)
			servername = hostnm;
		if (!SSL_set_tlsext_host_name(kconn->ssl, servername)) {
			LogCrit(COMPONENT_FSAL,
				"SSL_set_tlsext_host_name failed");
			log_ssl_errors();
			r = 1;
			goto Done;
		}
	}
	if (verify_hostname && !*verify_hostname) {
	} else {
		if (!verify_hostname)
			verify_hostname = *servername ? servername : hostnm;
		if (!SSL_set1_host(kconn->ssl, verify_hostname)) {
			LogCrit(COMPONENT_FSAL, "SSL_set1_host failed");
			log_ssl_errors();
			r = 1;
			goto Done;
		}
	}

	SSL_set_mode(kconn->ssl, SSL_MODE_AUTO_RETRY);

	/* connect to kmip host */

	if (BIO_do_connect(kconn->bio) != 1) {
		LogCrit(COMPONENT_FSAL, "BIO_do_connect failed to %s %s",
			hostnm, portstring);
		log_ssl_errors();
		r = 1;
		goto Done;
	}

	/* setup kmip */

	kmip_init(kconn->kmip_ctx, NULL, 0, kmip_settings.kmip_version);
	kconn->need_to_free_kmip = 1;
	kconn->buffer_blocks = 1;
	kconn->buffer_block_size = 1024;
	kconn->encoding =
		kconn->kmip_ctx->calloc_func(kconn->kmip_ctx->state,
					     kconn->buffer_blocks,
					     kconn->buffer_block_size);
	if (!kconn->encoding) {
		LogCrit(COMPONENT_FSAL, "kmip buffer alloc failed: %ld * %ld",
			kconn->buffer_blocks, kconn->buffer_block_size);
		r = 1;
		goto Done;
	}
	ns = kconn->buffer_blocks * kconn->buffer_block_size;
	kmip_set_buffer(kconn->kmip_ctx, kconn->encoding, ns);
	kconn->buffer_total_size = ns;

	/* add credential */

	up = kconn->textstrings;
	if (kmip_settings.kmip_user) {
		memset(kconn->upc, 0, safe_sizeof(*kconn->upc));
		up->value = kmip_settings.kmip_user;
		up->size = strlen(kmip_settings.kmip_user);
		kconn->upc->username = up++;
		if (kmip_settings.kmip_password) {
			up->value = kmip_settings.kmip_password;
			up->size = strlen(kmip_settings.kmip_password);
			kconn->upc->password = up++;
		}
		kconn->credential->credential_type =
			KMIP_CRED_USERNAME_AND_PASSWORD;
		kconn->credential->credential_value = kconn->upc;
		i = kmip_add_credential(kconn->kmip_ctx, kconn->credential);
		if (i != KMIP_OK) {
			LogCrit(COMPONENT_FSAL,
				"failed to add credential to kmip");
			r = 1;
			goto Done;
		}
	}
	r = 0;
Done:
	if (r) {
		kmip_free_handle_stuff(kconn);
	}
	return r;
}

/**
 * @brief return a connection to a kmip server.
 *
 * This routine will iterate through all HOST subblocks
 * specified in the KMIP block, until it is able to
 * complete a connection.  When reconnecting later,
 * it will try to reconnect to that host first.
 *
 * @return kconn a connected kmip connection.
 * @return 0 if there are no available kmip servers.
 */

struct my_kmip_connection *make_kmip_connect(void)
{
	struct my_kmip_connection *kconn;
	int i, j, rc;

	kconn = get_kmip_handle();
	rc = 0;
	if (!kconn) { /* probably shutting down... */
		LogCrit(COMPONENT_FSAL, "no free kmip handles%s",
			kmip_shutting_down ? ", shutting down" : "");
		return 0;
	}
	if (kconn->poison) {
		kmip_free_handle_stuff(kconn);
	}
	if (NOT_CONNECTED(kconn)) {
		int host_len = kmip_count_hosts(&kmip_settings);

		rc = 1;
		j = saved_kmip_host_index;
		for (i = 0; i < host_len; ++i) {
			j = (saved_kmip_host_index + i) % host_len;
			struct kmip_host_param *host_p =
				kmip_nth_host(&kmip_settings, j);
			rc = setup_kmip_connect(kconn, host_p);
			if (!rc) {
				saved_kmip_host_index = j;
				break;
			}
			LogCrit(COMPONENT_FSAL, "kmip can't connect to %s:%d",
				host_p->name, host_p->port);
		}
	}
	if (rc && kconn) {
		LogCrit(COMPONENT_FSAL, "no available kmip hosts");
		release_kmip_handle(kconn);
		kconn = 0;
	}
	return kconn;
}

/**
 * @brief fetch key contents from kmip
 *
 * @param[in] the key's unique id.
 * @param[out] the returned key block data, in memory that should be freed.
 * @param[out] length of the returned data.
 *
 * @return 0 on success
 * @return 1 on failure
 */

int kmip_get_keyvalue(char *unique_id, unsigned char **value_out,
		      size_t *value_size)
{
	struct my_kmip_connection *kconn;
	int need_to_free_response = 0;
	size_t ns;
	int i, r;
	char *response = NULL;
	int response_size = 0;

	*value_out = 0;
	*value_size = 0;
	kconn = make_kmip_connect();
	if (!kconn) {
		r = 1;
		goto Done;
	}

	/* build the request message */

	TextString uvalue[1];

	if (unique_id) {
		memset(uvalue, 0, safe_sizeof(*uvalue));
		uvalue->value = unique_id;
		uvalue->size = strlen(unique_id);
	}

	ProtocolVersion pv[1];

	memset(pv, 0, safe_sizeof(*pv));
	kmip_init_protocol_version(pv, kconn->kmip_ctx->version);

	RequestHeader rh[1];

	memset(rh, 0, safe_sizeof(*rh));
	kmip_init_request_header(rh);
	rh->protocol_version = pv;
	rh->maximum_response_size = kconn->kmip_ctx->max_message_size;
	rh->time_stamp = time(NULL);
	rh->batch_count = 1;

	GetRequestPayload get_req[1];
	RequestBatchItem rbi[1];

	memset(rbi, 0, safe_sizeof(*rbi));
	kmip_init_request_batch_item(rbi);

	memset(get_req, 0, safe_sizeof(*get_req));
	if (unique_id)
		get_req->unique_identifier = uvalue;
	;

	rbi->operation = KMIP_OP_GET;
	rbi->request_payload = get_req;

	RequestMessage rm[1];

	memset(rm, 0, safe_sizeof(*rm));
	rm->request_header = rh;
	rm->batch_items = rbi;
	rm->batch_count = 1;

	Authentication auth[1];

	memset(auth, 0, safe_sizeof(*auth));
	if (kconn->kmip_ctx->credential_list) {
		LinkedListItem *item = kconn->kmip_ctx->credential_list->head;

		if (item) {
			auth->credential = (Credential *)item->data;
			rh->authentication = auth;
		}
	}

	for (;;) {
		i = kmip_encode_request_message(kconn->kmip_ctx, rm);
		if (i != KMIP_ERROR_BUFFER_FULL)
			break;
		kmip_reset(kconn->kmip_ctx);
		kconn->kmip_ctx->free_func(kconn->kmip_ctx->state,
					   kconn->encoding);
		kconn->encoding = 0;
		++kconn->buffer_blocks;
		kconn->encoding =
			kconn->kmip_ctx->calloc_func(kconn->kmip_ctx->state,
						     kconn->buffer_blocks,
						     kconn->buffer_block_size);
		if (!kconn->encoding) {
			LogCrit(COMPONENT_FSAL,
				"kmip buffer alloc failed: %ld * %ld",
				kconn->buffer_blocks, kconn->buffer_block_size);
			r = 1;
			goto Done;
		}
		ns = kconn->buffer_blocks * kconn->buffer_block_size;
		kmip_set_buffer(kconn->kmip_ctx, kconn->encoding, ns);
		kconn->buffer_total_size = ns;
	}
	if (i != KMIP_OK) {
		LogCrit(COMPONENT_FSAL, "Can't encode request: %d %s", i,
			kconn->kmip_ctx->error_message);
		log_kmip_stacked_errors(kconn->kmip_ctx);
		r = 1;
		goto Done;
	}

	/*		kmip_print_request_message(rm);	*/

	i = kmip_bio_send_request_encoding(
		kconn->kmip_ctx, kconn->bio, (char *)kconn->encoding,
		kconn->kmip_ctx->index - kconn->kmip_ctx->buffer, &response,
		&response_size);
	if (i < 0) {
		LogCrit(COMPONENT_FSAL,
			"Problem sending request to create symmetric key: %d %s",
			i, kconn->kmip_ctx->error_message);
		log_kmip_stacked_errors(kconn->kmip_ctx);
		r = 1;
		goto Done;
	}
	kmip_free_buffer(kconn->kmip_ctx, kconn->encoding,
			 kconn->buffer_total_size);
	kconn->encoding = 0;
	kmip_set_buffer(kconn->kmip_ctx, response, response_size);
	ResponseMessage resp_m[1];

	memset(resp_m, 0, safe_sizeof(*resp_m));
	need_to_free_response = 1;
	i = kmip_decode_response_message(kconn->kmip_ctx, resp_m);
	if (i != KMIP_OK) {
		LogCrit(COMPONENT_FSAL, "Failed to decode get response %d: %s",
			i, kconn->kmip_ctx->error_message);
		log_kmip_stacked_errors(kconn->kmip_ctx);
		r = 1;
		goto Done;
	}
	/*		kmip_print_response_message(resp_m);	*/
	ResponseBatchItem *req = resp_m->batch_items;
	enum result_status rs = req->result_status;

	if (rs != KMIP_STATUS_SUCCESS) {
		LogCrit(COMPONENT_FSAL, "result is not success: %d", rs);
		; /* XXX do something here? */
	}
	GetResponsePayload *pld = (GetResponsePayload *)req->response_payload;

	if (pld) {
		switch (pld->object_type) {
		case KMIP_OBJTYPE_SYMMETRIC_KEY: {
			KeyBlock *kp = ((SymmetricKey *)pld->object)->key_block;
			ByteString *bp = 0;

			switch (kp->key_value_type) {
			case KMIP_TYPE_BYTE_STRING:
				bp = kp->key_value;
				break;
			case KMIP_TYPE_STRUCTURE: {
				KeyValue *kv = kp->key_value;

				switch (kp->key_format_type) {
				case KMIP_KEYFORMAT_RAW:
				case KMIP_KEYFORMAT_OPAQUE:
				case KMIP_KEYFORMAT_PKCS1:
				case KMIP_KEYFORMAT_PKCS8:
				case KMIP_KEYFORMAT_X509:
				case KMIP_KEYFORMAT_EC_PRIVATE_KEY:
					bp = kv->key_material;
					break;
				default:
					LogCrit(COMPONENT_FSAL,
						"unknown key material format type %d",
						kp->key_format_type);
				}
			} break;
			default:
				LogCrit(COMPONENT_FSAL,
					"undecipherable key value type %d",
					kp->key_value_type);
			}
			if (!bp) {
			} else if (*value_out) {
				LogCrit(COMPONENT_FSAL,
					"response has more than one result?");
				r = 1;
				goto Done;
			} else {
				unsigned char *outp;

				outp = malloc(bp->size);
				if (!outp) {
					LogCrit(COMPONENT_FSAL,
						"out of memory; cannot allocate %ld bytes",
						bp->size);
					r = 1;
					goto Done;
				}
				memcpy(outp, bp->value, bp->size);
				*value_out = outp;
				*value_size = bp->size;
				kmip_memset(bp->value, 0, bp->size);
			}
		} break;
		default:
			LogCrit(COMPONENT_FSAL, "Unknown object at %p",
				pld->object);
		}
	}
	r = 0;
Done:
	if (r && *value_out) {
		free(*value_out);
		*value_out = 0;
		*value_size = 0;
	}
	if (need_to_free_response)
		kmip_free_response_message(kconn->kmip_ctx, resp_m);

	release_kmip_handle(kconn);
	return r;
}

/**
 * @brief routine to be called after the export root object is set.
 *
 * @param[in] cb                   pointer to the export data
 * @param[in] obj                  root object
 *
 * @return 0 on success
 * @return EINVAL on failure.
 */

static int kmip_root_cb_func(struct exp_root_callback *cb,
			     struct fsal_obj_handle *obj)
{
	struct kmip_callback *data =
		container_of(cb, struct kmip_callback, callback);
	struct gsh_export *export = cb->_exp;
	fsal_status_t status;
	int rc = 0;
	unsigned char *value;
	size_t value_len, len;
	char *errmsg;

	struct io_fscrypt_setkey fscrypt_key;

	if (!data->kmip_key_id) {
		status = obj->obj_ops->control(obj,
					       FSCRYPT_VERIFY_NOT_ENCRYPTED,
					       NULL);
		if (FSAL_IS_SUCCESS(status)) {
			LogEvent(
				COMPONENT_FSAL,
				"no kmip key; unencrypted directory passes muster, export = %d",
				export->export_id);
		} else if (status.major == ERR_FSAL_NOTSUPP) {
			LogEvent(
				COMPONENT_FSAL,
				"no kmip key; no encryption support passes muster, export = %d, status = %d/%d",
				export->export_id, status.major, status.minor);
		} else {
			LogCrit(COMPONENT_FSAL,
				"no kmip key; encrypted directory, export = %d, status = %d/%d",
				export->export_id, status.major, status.minor);
			rc = status.minor;
			if (!rc)
				rc = EINVAL;
		}
		goto Done;
	}

	rc = kmip_get_keyvalue(data->kmip_key_id, &value, &value_len);

	if (rc) {
		LogCrit(COMPONENT_FSAL,
			"keyset callback: failed to get key for kmip_key_id = %s, export = %d",
			data->kmip_key_id, export->export_id);
		rc = EINVAL;
		goto Done;
	}

	if (!value || value_len < 32) {
		LogCrit(COMPONENT_FSAL,
			"keyset callback: runt/missing key for kmip_key_id = %s, export = %d; len=%ld",
			data->kmip_key_id, export->export_id, value_len);
		rc = EINVAL;
		goto Done;
	}

	memset(&fscrypt_key, 0, safe_sizeof(fscrypt_key));
	len = value_len;
	if (len > MAX_FSCRYPT_KEY_SIZE)
		len = MAX_FSCRYPT_KEY_SIZE;
	fscrypt_key.keylen = len;
	memcpy(fscrypt_key.data, value, len);
	kmip_memset(value, 0, value_len);
	free(value);

	status = obj->obj_ops->control(obj, FSCRYPT_SETKEY, &fscrypt_key);

	kmip_memset(&fscrypt_key, 0, safe_sizeof(fscrypt_key));

	if (FSAL_IS_SUCCESS(status)) {
		LogEvent(COMPONENT_FSAL,
			 "keyset success: kmip_key_id = %s, export = %d",
			 data->kmip_key_id, export->export_id);
	} else {
		switch (status.major) {
		case ERR_FSAL_EXIST:
			errmsg = " (existing policy mismatch)";
			break;
		case ERR_FSAL_NOTEMPTY:
			errmsg = " (directory not empty)";
			break;
		case ERR_FSAL_NOTSUPP:
			errmsg = " (no encryption support)";
			break;
		default:
			errmsg = "";
		}
		LogCrit(COMPONENT_FSAL,
			"keyset failed: kmip_key_id = %s, export = %d, error = %d/%d%s%s",
			data->kmip_key_id, export->export_id, status.major,
			status.minor, *errmsg ? " " : "", errmsg);
		rc = EINVAL;
	}

Done:
	kmip_root_cb_free(cb);
	return rc;
}

/**
 * @brief routine to return all memory allocated in a exp root callback.
 *
 * @param[in]     cb    callback to be freed.
 *
 * @return 0
 */

static int kmip_root_cb_free(struct exp_root_callback *cb)
{
	struct kmip_callback *data =
		container_of(cb, struct kmip_callback, callback);

	gsh_free(data->kmip_key_id);
	gsh_free(data);
	return 0;
}

/**
 * @brief Thread function to reap kmip connection after idle timeout
 *
 * @param[in] arg    Not used.
 *
 * @return "", always
 */

void *kmip_connection_reaper(void *a)
{
	useconds_t delay;
	(void)a;
	struct timeval idle[1], now[1], was[1], left[1];
	int saved;
	struct my_kmip_connection *kconn;

	for (;; usleep(delay)) {
		delay = kmip_settings.kmip_timeout * (1000000 / 2);
		if (BUSY(saved_kconn) || NOT_CONNECTED(saved_kconn))
			continue;
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &saved);
		pthread_mutex_lock(&kmip_connection_lock);
		idle->tv_sec = kmip_settings.kmip_timeout;
		idle->tv_usec = 0;
		gettimeofday(now, 0);
		timersub(now, idle, was);
		kconn = saved_kconn;
		if (BUSY(kconn) || NOT_CONNECTED(kconn)) {
			kconn = 0;
		} else if (!kconn->poison && timerisset(kconn->lastuse) &&
			   !timercmp(kconn->lastuse, was, <)) {
			timersub(kconn->lastuse, was, left);
			delay = left->tv_sec * 1000000 + left->tv_usec;
			kconn = 0;
		} else {
			kconn->idle = 0;
		}
		pthread_mutex_unlock(&kmip_connection_lock);
		if (kconn) {
			kmip_free_handle_stuff(kconn);
		}
		pthread_cond_broadcast(&kmip_connection_wait);
		pthread_setcancelstate(saved, NULL);
		if (kmip_shutting_down)
			break;
	}
	return "";
}

/**
 * @brief create reaper thread
 *
 * @return 0 always
 */

int start_kmip_connection_reaper(void)
{
	void *a1 = 0;

	saved_kconn->idle = 1;
	pthread_cond_init(&kmip_connection_wait, NULL);
	pthread_mutex_init(&kmip_connection_lock, NULL);
	pthread_create(&kmip_reaper_thread, NULL, kmip_connection_reaper, a1);

	return 0;
}

/**
 * @brief terminate the reaper thread
 *
 * This is called when the plugin is unloaded.
 *
 * @return 0 always
 */

void stop_kmip_reaper(void)
{
	void *thread_result;

	if (pthread_cancel(kmip_reaper_thread) < 0) {
		LogCrit(COMPONENT_FSAL, "Can't cancel reaper %d", errno);
		return;
	}
	if (pthread_join(kmip_reaper_thread, &thread_result) < 0) {
		LogCrit(COMPONENT_FSAL, "Can't join reaper %d", errno);
		return;
	}
	(void)thread_result;
}

void destroy_mutexes(void)
{
	pthread_cond_destroy(&kmip_connection_wait);
	pthread_mutex_destroy(&kmip_connection_lock);
}

/**
 * @brief Initialize kmip plugin
 */

MODULE_INIT void init(void)
{
	LogDebug(COMPONENT_FSAL, "kmip load");
	if (start_kmip_connection_reaper() < 0) {
		LogCrit(COMPONENT_FSAL, "Failed to start connection reaper");
	}
	if (register_config_locked(&kmip_plugin_static_t.config) != 0) {
		LogCrit(COMPONENT_FSAL, "Failed to register kmip plugin.");
	}
	add_export_extension(&kmip_export_extension_st.extension);
}

/**
 * @brief Release kmip plugin
 */

MODULE_FINI void finish(void)
{
	LogDebug(COMPONENT_FSAL, "kmip unload");
	struct my_kmip_connection *kconn;

	kmip_shutting_down = 1;
	remove_export_extension(&kmip_export_extension_st.extension);
	if (unregister_config_locked(&kmip_plugin_static_t.config) != 0)
		fprintf(stderr, "KMIP module failed to unregister");
	stop_kmip_reaper();
	kconn = timed_wait_kmip_busy();
	if (kconn && !NOT_CONNECTED(kconn)) {
		kmip_free_handle_stuff(kconn);
	}
	destroy_mutexes();
	free_host_params();
}
