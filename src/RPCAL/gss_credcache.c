// SPDX-License-Identifier: BSD-3-Clause
/*
  Copyright (c) 2004 The Regents of the University of Michigan.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
  3. Neither the name of the University nor the names of its
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* GSS Credential Cache, redacted from gssd. */

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <gssapi/gssapi.h>
#include <rpc/rpc.h>
#if defined(HAVE_KRB5) && !defined(GSS_C_NT_HOSTBASED_SERVICE)
#include <gssapi/gssapi_generic.h>
#define GSS_C_NT_HOSTBASED_SERVICE gss_nt_service_name
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#ifdef HAVE_COM_ERR_H
#include <com_err.h>
#endif

#include <sys/socket.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <gssapi/gssapi.h>
#include <limits.h>
#ifdef USE_PRIVATE_KRB5_FUNCTIONS
#include <gssapi/gssapi_krb5.h>
#endif
#include <krb5.h>
#include <rpc/auth_gss.h>

#include "log.h"
#include "common_utils.h"
#include "abstract_mem.h"
#include "gss_credcache.h"
#include "nfs_core.h"

/*
 * Hide away some of the MIT vs. Heimdal differences
 * here with macros...
 */

#ifdef HAVE_KRB5
#define k5_free_unparsed_name(ctx, name)	\
	krb5_free_unparsed_name((ctx), (name))
#define k5_free_default_realm(ctx, realm)	\
	krb5_free_default_realm((ctx), (realm))
#define k5_free_kt_entry(ctx, kte)			\
	krb5_free_keytab_entry_contents((ctx), (kte))
#else				/* Heimdal */
#define k5_free_unparsed_name(ctx, name)	\
	gsh_free(name)
#define k5_free_default_realm(ctx, realm)	\
	gsh_free(realm)
#define k5_free_kt_entry(ctx, kte)		\
	krb5_kt_free_entry((ctx), (kte))

#undef USE_GSS_KRB5_CCACHE_NAME
#define USE_GSS_KRB5_CCACHE_NAME 1
#endif

#define GSSD_DEFAULT_CRED_PREFIX "krb5cc_"
#define GSSD_DEFAULT_MACHINE_CRED_SUFFIX "machine"
#define GSSD_MAX_CCACHE_SEARCH 16

struct gssd_k5_kt_princ {
	struct gssd_k5_kt_princ *next;
	krb5_principal princ;
	char *ccname;
	char *realm;
	krb5_timestamp endtime;
};

typedef void (*gssd_err_func_t)(const char *, ...);


static int use_memcache;
static struct gssd_k5_kt_princ *gssd_k5_kt_princ_list;
static pthread_mutex_t ple_mtx = PTHREAD_MUTEX_INITIALIZER;

static char *gssd_k5_err_msg(krb5_context context, krb5_error_code code);
static int gssd_get_single_krb5_cred(krb5_context context, krb5_keytab kt,
				     struct gssd_k5_kt_princ *ple,
				     int nocache);
static void gssd_set_krb5_ccache_name(char *ccname);

/* Global list of principals/cache file names for machine credentials */

/*
 * Obtain credentials via a key in the keytab given
 * a keytab handle and a gssd_k5_kt_princ structure.
 * Checks to see if current credentials are expired,
 * if not, uses the keytab to obtain new credentials.
 *
 * Returns:
 *	0 => success (or credentials have not expired)
 *	nonzero => error
 */
static int gssd_get_single_krb5_cred(krb5_context context, krb5_keytab kt,
				     struct gssd_k5_kt_princ *ple, int nocache)
{
#if HAVE_KRB5_GET_INIT_CREDS_OPT_SET_ADDRESSLESS
	krb5_get_init_creds_opt * init_opts = NULL;
#else
	krb5_get_init_creds_opt options;
#endif
	krb5_get_init_creds_opt * opts;
	krb5_creds my_creds;
	krb5_ccache ccache = NULL;
	char kt_name[BUFSIZ];
	char cc_name[BUFSIZ];
	int code;
	time_t now = time(0);
	char *cache_type;
	char *pname = NULL;
	char *k5err = NULL;

	memset(&my_creds, 0, sizeof(my_creds));

	if (ple->ccname && ple->endtime > now && !nocache) {
		LogFullDebug(COMPONENT_NFS_CB,
			 "INFO: Credentials in CC '%s' are good until %d",
			 ple->ccname, ple->endtime);
		code = 0;
		goto out;
	}

	code = krb5_kt_get_name(context, kt, kt_name, BUFSIZ);
	if (code != 0) {
		LogCrit(COMPONENT_NFS_CB,
			 "ERROR: Unable to get keytab name in %s", __func__);
		goto out;
	}

	if ((krb5_unparse_name(context, ple->princ, &pname)))
		pname = NULL;

#if HAVE_KRB5_GET_INIT_CREDS_OPT_SET_ADDRESSLESS
	code = krb5_get_init_creds_opt_alloc(context, &init_opts);
	if (code) {
		k5err = gssd_k5_err_msg(context, code);
		LogCrit(COMPONENT_NFS_CB,
			"ERROR: %s allocating gic options", k5err);
		goto out;
	}
	if (krb5_get_init_creds_opt_set_addressless(context, init_opts, 1))
		LogWarn(COMPONENT_NFS_CB,
			 "WARNING: Unable to set option for addressless tickets.  May have problems behind a NAT.");
#ifdef TEST_SHORT_LIFETIME
	/* set a short lifetime (for debugging only!) */
	LogCrit(COMPONENT_NFS_CB,
		"WARNING: Using (debug) short machine cred lifetime!");
	krb5_get_init_creds_opt_set_tkt_life(init_opts, 5 * 60);
#endif
	opts = init_opts;

#else /* HAVE_KRB5_GET_INIT_CREDS_OPT_SET_ADDRESSLESS */

	krb5_get_init_creds_opt_init(&options);
	krb5_get_init_creds_opt_set_address_list(&options, NULL);
#ifdef TEST_SHORT_LIFETIME
	/* set a short lifetime (for debugging only!) */
	LogCrit(COMPONENT_NFS_CB,
		"WARNING: Using (debug) short machine cred lifetime!");
	krb5_get_init_creds_opt_set_tkt_life(&options, 5 * 60);
#endif
	opts = &options;
#endif

	code = krb5_get_init_creds_keytab(context, &my_creds,
					  ple->princ, kt, 0,
					  NULL, opts);
	if (code != 0) {
		k5err = gssd_k5_err_msg(context, code);
		LogWarn(COMPONENT_NFS_CB,
			 "WARNING: %s while getting initial ticket for principal '%s' using keytab '%s'",
			 k5err,
			 pname ? pname : "<unparsable>", kt_name);
		goto out;
	}

	/*
	 * Initialize cache file which we're going to be using
	 */

	if (use_memcache)
		cache_type = "MEMORY";
	else
		cache_type = "FILE";
	code = snprintf(cc_name, sizeof(cc_name), "%s:%s/%s%s_%s", cache_type,
			ccachesearch[0], GSSD_DEFAULT_CRED_PREFIX,
			GSSD_DEFAULT_MACHINE_CRED_SUFFIX, ple->realm);
	if (code < 0) {
		/* code and errno already set */
		goto out;
	} else if (code >= sizeof(cc_name)) {
		code = -1;
		errno = EINVAL;
		goto out;
	}
	ple->endtime = my_creds.times.endtime;
	if (ple->ccname != NULL)
		gsh_free(ple->ccname);
	ple->ccname = gsh_strdup(cc_name);

	code = krb5_cc_resolve(context, cc_name, &ccache);
	if (code != 0) {
		k5err = gssd_k5_err_msg(context, code);
		LogCrit(COMPONENT_NFS_CB,
			 "ERROR: %s while opening credential cache '%s'",
			 k5err, cc_name);
		goto out;
	}
	code = krb5_cc_initialize(context, ccache, ple->princ);
	if (code != 0) {
		k5err = gssd_k5_err_msg(context, code);
		LogCrit(COMPONENT_NFS_CB,
			 "ERROR: %s while initializing credential cache '%s'",
			 k5err, cc_name);
		goto out;
	}
	code = krb5_cc_store_cred(context, ccache, &my_creds);
	if (code != 0) {
		k5err = gssd_k5_err_msg(context, code);
		LogCrit(COMPONENT_NFS_CB,
			 "ERROR: %s while storing credentials in '%s'",
			 k5err, cc_name);
		goto out;
	}
	/* if we get this far, let gss mech know */
	gssd_set_krb5_ccache_name(cc_name);
	code = 0;
	LogFullDebug(COMPONENT_NFS_CB,
		 "Successfully obtained machine credentials for principal '%s' stored in ccache '%s'",
		 pname, cc_name);
 out:
#if HAVE_KRB5_GET_INIT_CREDS_OPT_SET_ADDRESSLESS
	if (init_opts)
		krb5_get_init_creds_opt_free(context, init_opts);
#endif
	if (pname)
		k5_free_unparsed_name(context, pname);
	if (ccache)
		krb5_cc_close(context, ccache);
	krb5_free_cred_contents(context, &my_creds);
	gsh_free(k5err);
	return code;
}

/*
 * Depending on the version of Kerberos, we either need to use
 * a private function, or simply set the environment variable.
 */
static void gssd_set_krb5_ccache_name(char *ccname)
{
#ifdef USE_GSS_KRB5_CCACHE_NAME
	u_int maj_stat, min_stat;

	LogFullDebug(COMPONENT_NFS_CB,
		 "using gss_krb5_ccache_name to select krb5 ccache %s",
		 ccname);
	maj_stat = gss_krb5_ccache_name(&min_stat, ccname, NULL);
	if (maj_stat != GSS_S_COMPLETE) {
		LogCrit(COMPONENT_NFS_CB,
			 "WARNING: gss_krb5_ccache_name with name '%s' failed (%s)",
			 ccname, error_message(min_stat));
	}
#else
	/*
	 * Set the KRB5CCNAME environment variable to tell the krb5 code
	 * which credentials cache to use.  (Instead of using the private
	 * function above for which there is no generic gssapi
	 * equivalent.)
	 */
	LogFullDebug(COMPONENT_NFS_CB,
		 "using environment variable to select krb5 ccache %s",
		 ccname);
	setenv("KRB5CCNAME", ccname, 1);
#endif
}

/*
 * Given a principal, find a matching ple structure
 */
static struct gssd_k5_kt_princ *find_ple_by_princ(krb5_context context,
						  krb5_principal princ)
{
	struct gssd_k5_kt_princ *ple;

	for (ple = gssd_k5_kt_princ_list; ple != NULL; ple = ple->next) {
		if (krb5_principal_compare(context, ple->princ, princ))
			return ple;
	}
	/* no match found */
	return NULL;
}

/*
 * Create, initialize, and add a new ple structure to the global list
 */
static struct gssd_k5_kt_princ *new_ple(krb5_context context,
					krb5_principal princ)
{
	struct gssd_k5_kt_princ *ple = NULL, *p;
	krb5_error_code code;
	char *default_realm;
	int is_default_realm = 0;

	ple = gsh_calloc(1, sizeof(struct gssd_k5_kt_princ));

#ifdef HAVE_KRB5
	ple->realm = gsh_malloc(princ->realm.length + 1);
	memcpy(ple->realm, princ->realm.data, princ->realm.length);
	ple->realm[princ->realm.length] = '\0';
#else
	ple->realm = gsh_strdup(princ->realm);
#endif
	code = krb5_copy_principal(context, princ, &ple->princ);

	if (code) {
		gsh_free(ple->realm);
		gsh_free(ple);
		return NULL;
	}

	/*
	 * Add new entry onto the list (if this is the default
	 * realm, always add to the front of the list)
	 */

	code = krb5_get_default_realm(context, &default_realm);
	if (code == 0) {
		if (strcmp(ple->realm, default_realm) == 0)
			is_default_realm = 1;
		k5_free_default_realm(context, default_realm);
	}

	if (is_default_realm) {
		ple->next = gssd_k5_kt_princ_list;
		gssd_k5_kt_princ_list = ple;
	} else {
		p = gssd_k5_kt_princ_list;
		while (p != NULL && p->next != NULL)
			p = p->next;
		if (p == NULL)
			gssd_k5_kt_princ_list = ple;
		else
			p->next = ple;
	}

	return ple;
}

/*
 * Given a principal, find an existing ple structure, or create one
 */
static struct gssd_k5_kt_princ *get_ple_by_princ(krb5_context context,
						 krb5_principal princ)
{
	struct gssd_k5_kt_princ *ple;

	PTHREAD_MUTEX_lock(&ple_mtx);

	ple = find_ple_by_princ(context, princ);
	if (ple == NULL)
		ple = new_ple(context, princ);

	PTHREAD_MUTEX_unlock(&ple_mtx);

	return ple;
}

/*
 * Given a (possibly unqualified) hostname,
 * return the fully qualified (lower-case!) hostname
 */
static int get_full_hostname(const char *inhost, char *outhost, int outhostlen)
{
	struct addrinfo *addrs = NULL;
	struct addrinfo hints;
	int retval;
	char *c;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = AI_CANONNAME;

	/* Get full target hostname */
	retval = gsh_getaddrinfo(inhost, NULL, &hints, &addrs,
			nfs_param.core_param.enable_AUTHSTATS);

	if (retval) {
		LogWarn(COMPONENT_NFS_CB,
			 "%s while getting full hostname for '%s'",
			 gai_strerror(retval), inhost);
		return retval;
	}

	if (strlcpy(outhost, addrs->ai_canonname, outhostlen) >= outhostlen) {
		retval = -1;
		goto out;
	}

	for (c = outhost; *c != '\0'; c++)
		*c = tolower(*c);

	LogFullDebug(COMPONENT_NFS_CB,
		     "Full hostname for '%s' is '%s'", inhost, outhost);
	retval = 0;

 out:

	freeaddrinfo(addrs);
	return retval;
}

/*
 * If principal matches the given realm and service name,
 * and has *any* instance (hostname), return 1.
 * Otherwise return 0, indicating no match.
 */
#ifdef HAVE_KRB5
static int realm_and_service_match(krb5_principal p, const char *realm,
				   const char *service)
{
	/* Must have two components */
	if (p->length != 2)
		return 0;

	if ((strlen(realm) == p->realm.length)
	    && (strncmp(realm, p->realm.data, p->realm.length) == 0)
	    && (strlen(service) == p->data[0].length)
	    && (strncmp(service, p->data[0].data, p->data[0].length) == 0))
		return 1;

	return 0;
}
#else
static int realm_and_service_match(krb5_context context, krb5_principal p,
				   const char *realm, const char *service)
{
	const char *name, *inst;

	if (p->name.name_string.len != 2)
		return 0;

	name = krb5_principal_get_comp_string(context, p, 0);
	inst = krb5_principal_get_comp_string(context, p, 1);
	if (name == NULL || inst == NULL)
		return 0;
	if ((strcmp(realm, p->realm) == 0)
	    && (strcmp(service, name) == 0))
		return 1;

	return 0;
}
#endif

/*
 * Search the given keytab file looking for an entry with the given
 * service name and realm, ignoring hostname (instance).
 *
 * Returns:
 *	0 => No error
 *	non-zero => An error occurred
 *
 * If a keytab entry is found, "found" is set to one, and the keytab
 * entry is returned in "kte".  Otherwise, "found" is zero, and the
 * value of "kte" is unpredictable.
 */
static int gssd_search_krb5_keytab(krb5_context context, krb5_keytab kt,
				   const char *realm, const char *service,
				   int *found, krb5_keytab_entry *kte)
{
	krb5_kt_cursor cursor;
	krb5_error_code code;
	struct gssd_k5_kt_princ *ple;
	int retval = -1, status;
	char kt_name[BUFSIZ];
	char *pname;
	char *k5err = NULL;

	if (found == NULL) {
		retval = EINVAL;
		goto out;
	}
	*found = 0;

	/*
	 * Look through each entry in the keytab file and determine
	 * if we might want to use it as machine credentials.  If so,
	 * save info in the global principal list (gssd_k5_kt_princ_list).
	 */
	code = krb5_kt_get_name(context, kt, kt_name, BUFSIZ);
	if (code != 0) {
		k5err = gssd_k5_err_msg(context, code);
		LogCrit(COMPONENT_NFS_CB,
			 "ERROR: %s attempting to get keytab name",
			 k5err);
		gsh_free(k5err);
		retval = code;
		goto out;
	}
	code = krb5_kt_start_seq_get(context, kt, &cursor);
	if (code != 0) {
		k5err = gssd_k5_err_msg(context, code);
		LogCrit(COMPONENT_NFS_CB,
			 "ERROR: %s while beginning keytab scan for keytab '%s'",
			 k5err, kt_name);
		gsh_free(k5err);
		retval = code;
		goto out;
	}

	while ((code = krb5_kt_next_entry(context, kt, kte, &cursor)) == 0) {
		code = krb5_unparse_name(context, kte->principal, &pname);
		if (code != 0) {
			k5err = gssd_k5_err_msg(context, code);
			LogCrit(COMPONENT_NFS_CB,
				 "WARNING: Skipping keytab entry because we failed to unparse principal name: %s",
				 k5err);
			k5_free_kt_entry(context, kte);
			gsh_free(k5err);
			continue;
		}
		LogFullDebug(COMPONENT_NFS_CB,
			     "Processing keytab entry for principal '%s'",
			     pname);
		/* Use the first matching keytab entry found */
#ifdef HAVE_KRB5
		status =
		    realm_and_service_match(kte->principal, realm, service);
#else
		status =
		    realm_and_service_match(context, kte->principal, realm,
					    service);
#endif
		if (status) {
			LogFullDebug(COMPONENT_NFS_CB,
				     "We WILL use this entry (%s)", pname);
			ple = get_ple_by_princ(context, kte->principal);
			/*
			 * Return, don't free, keytab entry if
			 * we were successful!
			 */
			if (unlikely(ple == NULL)) {
				retval = ENOMEM;
				k5_free_kt_entry(context, kte);
				k5_free_unparsed_name(context, pname);
				(void) krb5_kt_end_seq_get(
					context, kt, &cursor);
				goto out;
			} else {
				retval = 0;
				*found = 1;
			}
			k5_free_unparsed_name(context, pname);
			break;
		} else {
			LogFullDebug(COMPONENT_NFS_CB,
				     "We will NOT use this entry (%s)", pname);
		}
		k5_free_unparsed_name(context, pname);
		k5_free_kt_entry(context, kte);
	}

	code = krb5_kt_end_seq_get(context, kt, &cursor);
	if (code != 0) {
		k5err = gssd_k5_err_msg(context, code);
		LogCrit(COMPONENT_NFS_CB,
			 "WARNING: %s while ending keytab scan for keytab '%s'",
			 k5err, kt_name);
		gsh_free(k5err);
	}

	retval = 0;
 out:
	return retval;
}

/*
 * Find a keytab entry to use for a given target hostname.
 * Tries to find the most appropriate keytab to use given the
 * name of the host we are trying to connect with.
 */
static int find_keytab_entry(krb5_context context, krb5_keytab kt,
			     const char *hostname, krb5_keytab_entry *kte,
			     const char **svcnames)
{
	krb5_error_code code;
	char **realmnames = NULL;
	char myhostname[NI_MAXHOST], targethostname[NI_MAXHOST];
	char myhostad[NI_MAXHOST + 1];
	int i, j, retval;
	char *default_realm = NULL;
	char *realm;
	char *k5err = NULL;
	int tried_all = 0, tried_default = 0;
	krb5_principal princ;

	/* Get full target hostname */
	retval =
	    get_full_hostname(hostname, targethostname, sizeof(targethostname));
	if (retval)
		goto out;

	/* Get full local hostname */
	retval = gsh_gethostname(myhostname, sizeof(myhostname),
			nfs_param.core_param.enable_AUTHSTATS);
	if (retval) {
		k5err = gssd_k5_err_msg(context, retval);
		LogWarn(COMPONENT_NFS_CB,
			"%s while getting local hostname", k5err);
		gsh_free(k5err);
		goto out;
	}

	/* Compute the active directory machine name HOST$ */
	strcpy(myhostad, myhostname);
	for (i = 0; myhostad[i] != 0; ++i)
		myhostad[i] = toupper(myhostad[i]);
	myhostad[i] = '$';
	myhostad[i + 1] = 0;

	retval = get_full_hostname(myhostname, myhostname, sizeof(myhostname));
	if (retval)
		goto out;

	code = krb5_get_default_realm(context, &default_realm);
	if (code) {
		retval = code;
		k5err = gssd_k5_err_msg(context, code);
		LogWarn(COMPONENT_NFS_CB,
			"%s while getting default realm name", k5err);
		gsh_free(k5err);
		goto out;
	}

	/*
	 * Get the realm name(s) for the target hostname.
	 * In reality, this function currently only returns a
	 * single realm, but we code with the assumption that
	 * someday it may actually return a list.
	 */
	code = krb5_get_host_realm(context, targethostname, &realmnames);
	if (code) {
		k5err = gssd_k5_err_msg(context, code);
		LogCrit(COMPONENT_NFS_CB,
			 "ERROR: %s while getting realm(s) for host '%s'",
			 k5err, targethostname);
		gsh_free(k5err);
		retval = code;
		goto out;
	}

	/*
	 * Try the "appropriate" realm first, and if nothing found for that
	 * realm, try the default realm (if it hasn't already been tried).
	 */
	i = 0;
	realm = realmnames[i];
	while (1) {
		if (realm == NULL) {
			tried_all = 1;
			if (!tried_default)
				realm = default_realm;
		}
		if (tried_all && tried_default)
			break;
		if (strcmp(realm, default_realm) == 0)
			tried_default = 1;
		for (j = 0; svcnames[j] != NULL; j++) {
			char spn[1028];

			/*
			 * The special svcname "$" means 'try the active
			 * directory machine account'
			 */
			if (strcmp(svcnames[j], "$") == 0) {
				retval = snprintf(spn, sizeof(spn), "%s@%s",
						  myhostad, realm);
				if (retval < 0) {
					goto out;
				} else if (retval >= sizeof(spn)) {
					retval = -1;
					goto out;
				}
				code = krb5_build_principal_ext(
					context, &princ, strlen(realm), realm,
					strlen(myhostad), myhostad, NULL);
			} else {
				retval = snprintf(spn, sizeof(spn), "%s/%s@%s",
						  svcnames[j], myhostname,
						  realm);
				if (retval < 0) {
					goto out;
				} else if (retval >= sizeof(spn)) {
					retval = -1;
					goto out;
				}
				code = krb5_build_principal_ext(
					context, &princ, strlen(realm), realm,
					strlen(svcnames[j]), svcnames[j],
					strlen(myhostname), myhostname, NULL);
			}

			if (code) {
				k5err = gssd_k5_err_msg(context, code);
				LogWarn(COMPONENT_NFS_CB,
					 "%s while building principal for '%s'",
					 k5err, spn);
				gsh_free(k5err);
				continue;
			}
			code = krb5_kt_get_entry(context, kt, princ, 0, 0, kte);
			krb5_free_principal(context, princ);
			if (code) {
				k5err = gssd_k5_err_msg(context, code);
				LogFullDebug(COMPONENT_NFS_CB,
					 "%s while getting keytab entry for '%s'",
					 k5err, spn);
				gsh_free(k5err);
			} else {
				LogFullDebug(COMPONENT_NFS_CB,
					 "Success getting keytab entry for '%s'",
					 spn);
				retval = 0;
				goto out;
			}
			retval = code;
		}
		/*
		 * Nothing found with our hostname instance, now look for
		 * names with any instance (they must have an instance)
		 */
		for (j = 0; svcnames[j] != NULL; j++) {
			int found = 0;

			if (strcmp(svcnames[j], "$") == 0)
				continue;
			code =
			    gssd_search_krb5_keytab(context, kt, realm,
						    svcnames[j], &found, kte);
			if (!code && found) {
				LogFullDebug(COMPONENT_NFS_CB,
					 "Success getting keytab entry for %s/*@%s",
					 svcnames[j], realm);
				retval = 0;
				goto out;
			}
		}
		if (!tried_all) {
			i++;
			realm = realmnames[i];
		}
	}
 out:
	if (default_realm)
		k5_free_default_realm(context, default_realm);
	if (realmnames)
		krb5_free_host_realm(context, realmnames);

	return retval;
}

/*
 * A common routine for getting the Kerberos error message
 */
static char *gssd_k5_err_msg(krb5_context context, krb5_error_code code)
{
#if HAVE_KRB5_GET_ERROR_MESSAGE
	if (context != NULL) {
		const char *origmsg;
		char *msg = NULL;

		origmsg = krb5_get_error_message(context, code);
		msg = gsh_strdup(origmsg);
		krb5_free_error_message(context, origmsg);
		return msg;
	}
#endif
#if HAVE_KRB5
	return gsh_strdup(error_message(code));
#else
	if (context != NULL)
		return gsh_strdup(krb5_get_err_text(context, code));
	else
		return gsh_strdup(error_message(code));
#endif
}

/* Public Interfaces */

char *ccachesearch[GSSD_MAX_CCACHE_SEARCH + 1];

/*
 * Obtain (or refresh if necessary) Kerberos machine credentials
 */
int gssd_refresh_krb5_machine_credential(char *hostname,
					 struct gssd_k5_kt_princ *ple,
					 char *service)
{
	krb5_error_code code = 0;
	krb5_context context;
	krb5_keytab kt = NULL;
	int retval = 0;
	char *k5err = NULL;
	const char *svcnames[5] = { "$", "root", "nfs", "host", NULL };
	char *keytabfile = nfs_param.krb5_param.keytab;

	/*
	 * If a specific service name was specified, use it.
	 * Otherwise, use the default list.
	 */
	if (service != NULL && strcmp(service, "*") != 0) {
		svcnames[0] = service;
		svcnames[1] = NULL;
	}
	if (hostname == NULL && ple == NULL)
		return EINVAL;

	code = krb5_init_context(&context);
	if (code) {
		k5err = gssd_k5_err_msg(NULL, code);
		LogCrit(COMPONENT_NFS_CB,
			 "ERROR: %s: %s while initializing krb5 context",
			 __func__, k5err);
		retval = code;
		gsh_free(k5err);
		goto out_wo_context;
	}

	code = krb5_kt_resolve(context, keytabfile, &kt);
	if (code != 0) {
		k5err = gssd_k5_err_msg(context, code);
		LogCrit(COMPONENT_NFS_CB,
			 "ERROR: %s: %s while resolving keytab '%s'",
			 __func__, k5err, keytabfile);
		gsh_free(k5err);
		goto out;
	}

	if (ple == NULL) {
		krb5_keytab_entry kte;

		code = find_keytab_entry(context, kt, hostname, &kte,
					 svcnames);
		if (code) {
			LogCrit(COMPONENT_NFS_CB,
				 "ERROR: %s: no usable keytab entry found in keytab %s for connection with host %s",
				 __func__, keytabfile, hostname);
			retval = code;
			goto out;
		}

		ple = get_ple_by_princ(context, kte.principal);
		k5_free_kt_entry(context, &kte);
		if (ple == NULL) {
			char *pname;

			if ((krb5_unparse_name(context, kte.principal,
					       &pname))) {
				pname = NULL;
			}
			LogCrit(COMPONENT_NFS_CB,
				 "ERROR: %s: Could not locate or create ple struct for principal %s for connection with host %s",
				 __func__,
				 pname ? pname : "<unparsable>", hostname);
			if (pname)
				k5_free_unparsed_name(context, pname);
			goto out;
		}
	}
	retval = gssd_get_single_krb5_cred(context, kt, ple, 0);
 out:
	if (kt)
		krb5_kt_close(context, kt);
	krb5_free_context(context);
 out_wo_context:
	return retval;
}

int gssd_check_mechs(void)
{
	u_int32_t maj_stat, min_stat;
	gss_OID_set supported_mechs = GSS_C_NO_OID_SET;
	int retval = -1;

	maj_stat = gss_indicate_mechs(&min_stat, &supported_mechs);
	if (maj_stat != GSS_S_COMPLETE) {
		LogCrit(COMPONENT_NFS_CB,
			 "Unable to obtain list of supported mechanisms. Check that gss library is properly configured.");
		goto out;
	}
	if (supported_mechs == GSS_C_NO_OID_SET ||
	    supported_mechs->count == 0) {
		LogCrit(COMPONENT_NFS_CB,
			 "Unable to obtain list of supported mechanisms. Check that gss library is properly configured.");
		goto out;
	}
	maj_stat = gss_release_oid_set(&min_stat, &supported_mechs);
	retval = 0;
out:
	return retval;
}
