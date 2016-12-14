/* -*- mode: c; c-tab-always-indent: t; c-basic-offset: 8 -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Scality Inc., 2016
 * Author: Guillaume Gimenez ploki@blackmilk.fr
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

/* dbd_rest_client.c
 */

#include <time.h>
#include <curl/curl.h>
#include <jansson.h>

#include "scality_methods.h"
#include "dbd_rest_client.h"
#include "sproxyd_client.h"
#include "redis_client.h"
#include "random.h"

#define DEFAULT_CONTENT_TYPE "application/octet-stream"
#define DIRECTORY_CONTENT_TYPE "application/x-directory"
#define BUCKET_BASE_PATH "/default/bucket"
#define ATTRIBUTES_BASE_PATH "/default/attributes"
#define METADATA_INFORMATION_PATH "/default/metadataInformation"

struct dbd_response {
	long http_status;
	json_t *body;
};

struct dbd_get_parameters {
	const char *prefix;
	const char *marker;
	const char *delimiter;
	int maxkeys;
};

typedef struct dbd_response dbd_response_t;
typedef struct dbd_get_parameters dbd_get_parameters_t;

typedef struct dbd_dirent {
	char *name;
	dbd_dtype_t dtype;

	struct timespec last_modified;
	long long filesize;
} dirent_t;

static __thread struct dbd_dirent *listed_dirent = NULL;


static struct timespec
iso8601_str2timespec(const char* ts_str)
{
	struct timespec result = {};
	int y,M,d,h,m;
	double s;
	if (NULL != ts_str) {
		int ret = sscanf(ts_str, "%d-%d-%dT%d:%d:%lfZ",
				 &y, &M, &d, &h, &m, &s);
		if ( 6 != ret ) {
			LogCrit(COMPONENT_FSAL,
				"malformed ISO8601  date %s. got %d fields",
				ts_str, ret);
		} else {
			LogDebug(COMPONENT_FSAL,
				 "ISO8601 date: %d-%d-%d %d:%d:%f",
				 y, M, d, h, m, s);
			struct tm tm = {
				.tm_year = y-1900,
				.tm_mon = M-1,
				.tm_mday = d,
				.tm_hour = h,
				.tm_min = m,
				.tm_sec = (int)s,
				.tm_isdst = -1,
			};
			time_t t = timegm(&tm);
			result = (struct timespec) {
				.tv_sec = t,
				.tv_nsec = (s-(int)s)*1000000000,
			};
		}
	}
	else {
		LogCrit(COMPONENT_FSAL,
			"null ISO8601  timestamp");
	}
	return result;
}

void
dbd_response_free(dbd_response_t *response)
{
	if ( response ) {
		if (response->body)
			json_decref(response->body);
		gsh_free(response);
	}
}

static dbd_response_t *
dbd_get(struct scality_fsal_export *export,
	const char *base_path,
	const char *object,
	dbd_get_parameters_t *parameters) {

	if (object) {
		LogDebug(COMPONENT_FSAL,
			 "dbd_get(%s..%s)", base_path, object);
	}
	else if ( parameters && parameters->prefix ) {
		LogDebug(COMPONENT_FSAL,
			 "dbd_get(%s..prefix=%s, marker=%s, delimiter=%s)",
			 base_path, parameters->prefix,
			 parameters->marker?:"",
			 parameters->delimiter?:"");
	}
	if ( !object == !parameters ) {
		LogCrit(COMPONENT_FSAL,
			"BUG: Invalid parameters, object "
			"and parameters are both set");
		return NULL;
	}

	char url[MAX_URL_SIZE];
	char query_string[MAX_URL_SIZE] = "?";
	int pos = 1;
	char *tmp;
	CURL *curl = NULL;
	CURLcode curl_ret;
	FILE *body_stream = NULL;
	char *body_text = NULL;
	size_t body_len = 0;
	int ret;
	dbd_response_t *response = NULL;
	json_error_t json_error;
	int success = 0;

	curl = curl_easy_init();
	if ( NULL == curl ) {
		return NULL;

	}

	if ( NULL != parameters ) {
		if ( NULL != parameters->prefix ) {
			tmp = curl_easy_escape(curl, parameters->prefix,
					       strlen(parameters->prefix));
			pos += snprintf(query_string+pos,
					sizeof(query_string)-pos,
					   "prefix=%s&", tmp);
			free(tmp);
			if ( pos >= sizeof(query_string) ) {
				LogCrit(COMPONENT_FSAL, "buffer overrun");
				goto out;
			}
		}
		if ( NULL != parameters->marker ) {
			tmp = curl_easy_escape(curl, parameters->marker,
					       strlen(parameters->marker));
			pos += snprintf(query_string+pos,
					sizeof(query_string)-pos,
					"marker=%s&", tmp);
			if ( pos >= sizeof(query_string) ) {
				LogCrit(COMPONENT_FSAL, "buffer overrun");
				goto out;
			}
			free(tmp);
		}
		if ( NULL != parameters->delimiter ) {
			tmp = curl_easy_escape(curl, parameters->delimiter,
					       strlen(parameters->delimiter));
			pos += snprintf(query_string+pos,
					sizeof(query_string)-pos,
					   "delimiter=%s&", tmp);
			free(tmp);
			if ( pos >= sizeof(query_string) ) {
				LogCrit(COMPONENT_FSAL, "buffer overrun");
				goto out;
			}
		}
		if ( parameters->maxkeys > 0 ) {
			pos += snprintf(query_string+pos,
					sizeof(query_string)-pos,
					   "maxKeys=%d&", parameters->maxkeys);
			if ( pos >= sizeof(query_string) ) {
				LogCrit(COMPONENT_FSAL, "buffer overrun");
				goto out;
			}
		}
		//get rid of trailing '?' or '&' or '/'
		query_string[--pos]='\0';
	}

	if (object) {
		char *tmp = curl_easy_escape(curl, object, strlen(object));
		pos = snprintf(url, sizeof(url), "%s%s/%s/%s",
			       export->module->dbd_url, base_path,
			       export->bucket, tmp);
		free(tmp);
	}
	else {
		pos = snprintf(url, sizeof(url), "%s%s/%s%s",
			       export->module->dbd_url, base_path,
			       export->bucket, query_string);
	}


	if ( pos >= sizeof(url) ) {
		LogCrit(COMPONENT_FSAL, "buffer overrun");
		goto out;
	}

	response = gsh_calloc(1, sizeof(dbd_response_t));

	body_stream = open_memstream(&body_text, &body_len);
	if ( NULL == body_stream ) {
		LogCrit(COMPONENT_FSAL, "Failed to open memstream");
		goto out;
	}

	LogDebug(COMPONENT_FSAL,
		 "dbd_get(%s)",
		 url);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_stream);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300);

	curl_ret = curl_easy_perform(curl);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "curl(%s) failed", url);
		goto out;
	}

	curl_ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE,
				     &response->http_status);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL,
			"Unable to retrieve HTTP status for %s", url);
		goto out;
	}

	if ( response->http_status >= 200 && response->http_status < 300 ) {
		ret = fclose(body_stream);
		body_stream = NULL;
		if ( ret < 0 ) {
			LogCrit(COMPONENT_FSAL, "stream error at close");
			goto out;
		}
		response->body = json_loads(body_text,
					    JSON_REJECT_DUPLICATES,&json_error);
		if ( NULL == response->body ) {
			LogWarn(COMPONENT_FSAL,
				"json_load failed with error %s ...",
				json_error.text);
			if (object)
				LogWarn(COMPONENT_FSAL,
					"... parse error on object %s", object);
			else
				LogWarn(COMPONENT_FSAL,
					"... parse error on query string %s",
					query_string);

			goto out;
		}
	} else if ( response->http_status != 404 ) {
		LogCrit(COMPONENT_FSAL,
			 "curl(%s) => HTTP STATUS %ld",
			 url,
			 response->http_status);
	}
	success = 1;

 out:
	if (curl)
		curl_easy_cleanup(curl);
	if (body_stream)
		fclose(body_stream);
	if (body_text)
		free(body_text);
	if ( !success ) {
		dbd_response_free(response);
		response=NULL;
	}
	return response;
}

dbd_is_last_result_t
dbd_is_last(struct scality_fsal_export *export,
	    struct scality_fsal_obj_handle *dir_hdl)
{
	dbd_is_last_result_t result = DBD_LOOKUP_ERROR;
	char prefix[MAX_URL_SIZE];
	snprintf(prefix, sizeof prefix,"%s"S3_DELIMITER,dir_hdl->object);
	dbd_get_parameters_t parameters = {
		.prefix = prefix,
		.marker = NULL,
		.delimiter = S3_DELIMITER,
		// 2 because there is a bug in md. it returs truncated = true even if there is only one entry
		.maxkeys = 2,
	};
	dbd_response_t *response = dbd_get(export, BUCKET_BASE_PATH, NULL, &parameters);
	if ( NULL == response || response->http_status != 200 ) {
		result = DBD_LOOKUP_ERROR;
		goto end;
	}
	json_t *commonPrefixes = json_object_get(response->body, "CommonPrefixes");
	json_t *contents = json_object_get(response->body, "Contents");
	//bool isTruncated = json_equal(json_true(), json_object_get(response->body, "IsTruncated"));
	size_t contents_sz = json_array_size(contents);
	size_t commonPrefixes_sz = json_array_size(commonPrefixes);
	size_t element_count = contents_sz + commonPrefixes_sz;

	if (element_count > 1) {
		result = DBD_LOOKUP_IS_NOT_LAST;
		goto end;
	}
	if (0 == contents_sz) {
		result = DBD_LOOKUP_ENOENT;
		goto end;
	}
	assert(1 == contents_sz);
	json_t *content = json_array_get(contents, 0);
	json_t *key = content?json_object_get(content, "key"):NULL;
	const char *dent = key?json_string_value(key):NULL;
	if ( NULL == dent ) {
		result = DBD_LOOKUP_ERROR;
		goto end;
	}
	if ( 0 == strcmp(dent, prefix) ) {
		result = DBD_LOOKUP_IS_LAST;
	}
	else {
		result = DBD_LOOKUP_IS_NOT_LAST;
	}

 end:
	dbd_response_free(response);
	return result;
}

int dbd_lookup(struct scality_fsal_export *export,
	       struct scality_fsal_obj_handle *parent_hdl,
	       const char *name,
	       dbd_dtype_t *dtypep,
	       struct timespec *last_modifiedp,
	       long long *filesizep,
	       bool *attrs_loadedp)
{
	assert(NULL != dtypep);
	assert(NULL != last_modifiedp);
	assert(NULL != filesizep);
	assert(NULL != attrs_loadedp);
	char object[MAX_URL_SIZE];
	int len;

	if (listed_dirent) {
		*dtypep = listed_dirent->dtype;
		*last_modifiedp = listed_dirent->last_modified;
		*filesizep = listed_dirent->filesize;
		*attrs_loadedp = true;
		return 0;
	}
	*attrs_loadedp = false;

	len = snprintf(object, sizeof(object), "%s%s%s",
		       parent_hdl->object?:"",
		       parent_hdl->object &&
		       parent_hdl->object[0] != '\0' ?S3_DELIMITER:"", name);
	if ( len >= sizeof(object) ) {
		LogCrit(COMPONENT_FSAL, "object name too long");
		return -1;
	}
	return dbd_lookup_object(export, object, dtypep,
				 last_modifiedp,
				 filesizep,
				 attrs_loadedp);
}

static int
get_attributes_from_exact_match(json_t *body,
				const char *object,
				struct timespec *last_modifiedp,
				long long *filesizep)
{
	json_t *content_length = json_object_get(body, "content-length");
	if ( NULL == content_length ) {
		LogCrit(COMPONENT_FSAL,
			"content-length is not set on %s",
			object);
		return -1;
	}
	switch(json_typeof(content_length)) {
	case JSON_STRING:
		*filesizep = atoi(json_string_value(content_length));
		break;
	case JSON_INTEGER:
		*filesizep = json_integer_value(content_length);
		break;
	case JSON_REAL:
		*filesizep = json_real_value(content_length);
		break;
	default:
		*filesizep = 0;
	}
	json_t *last_modified = json_object_get(body, "last-modified");
	switch(json_typeof(last_modified)) {
	case JSON_STRING: {
		const char *ts_str = json_string_value(last_modified);
		*last_modifiedp = iso8601_str2timespec(ts_str);
		break;
	}
	default:
		LogCrit(COMPONENT_FSAL,
			"Unknown last-modified field type on %s",
			object);
		return -1;
		break;
	}
	return 0;
}

static int
get_attributes_from_listing_content(json_t *content,
				    struct timespec *last_modifiedp,
				    long long *filesizep)
{
	json_t *value = json_object_get(content, "value");
	json_t *last_modified = NULL;
	json_t *size = NULL;
	if (NULL != value) {
		last_modified = json_object_get(value,
						"LastModified");
		size = json_object_get(value,
				       "Size");
	}
	if (NULL == value ||
	    NULL == last_modified ||
	    NULL == size) {
		LogCrit(COMPONENT_FSAL,
			"Could not get entry value from listing");
		return -1;
	}
	if (last_modifiedp)
		*last_modifiedp =
			iso8601_str2timespec(json_string_value(last_modified));
	if (filesizep)
		*filesizep = json_integer_value(size);
	return 0;
}




int
dbd_lookup_object(struct scality_fsal_export *export,
		  const char *object,
		  dbd_dtype_t *dtypep,
		  struct timespec *last_modifiedp,
		  long long *filesizep,
		  bool *attrs_loadedp)
{
	int ret = -1;
	char prefix[MAX_URL_SIZE];
	int len;
	dbd_dtype_t dtype = DBD_DTYPE_IOERR;
	len = snprintf(prefix, sizeof(prefix), "%s", object);

	dbd_response_t *exact_match_response = NULL;

	LogDebug(COMPONENT_FSAL, "lookup exact match %s", object);
	exact_match_response = dbd_get(export, BUCKET_BASE_PATH, prefix, NULL);
	if ( NULL == exact_match_response )
		goto end;


	if ( 200 == exact_match_response->http_status ) {
		dtype = DBD_DTYPE_REGULAR;
		if (last_modifiedp && filesizep && attrs_loadedp) {
			ret = get_attributes_from_exact_match(exact_match_response->body,
							      object,
							      last_modifiedp,
							      filesizep);
			if (0 == ret )
				*attrs_loadedp = true;
		}
	} else if (404 == exact_match_response->http_status) {
		dbd_response_t *prefix_response = NULL;
		// add a trailing slash to lookup a common prefix
		snprintf(prefix+len, sizeof(prefix)-len, "%s", S3_DELIMITER);
		dbd_get_parameters_t parameters = {
			.prefix = prefix,
			.marker = NULL,
			.delimiter = S3_DELIMITER,
			.maxkeys = 1
		};
		LogDebug(COMPONENT_FSAL, "lookup prefix %s", prefix);
		prefix_response = dbd_get(export, BUCKET_BASE_PATH,
					  NULL, &parameters);
		if ( NULL == prefix_response ||
		     prefix_response->http_status != 200 )
			goto end;
		json_t *commonPrefixes = json_object_get(prefix_response->body,
							 "CommonPrefixes");
		json_t *contents = json_object_get(prefix_response->body,
						   "Contents");

		size_t commonPrefixes_sz = json_array_size(commonPrefixes);
		size_t contents_sz = json_array_size(contents);
		bool prefix_response_empty =
			( 0 == commonPrefixes_sz + contents_sz );
		if ( prefix_response_empty ) {
			dtype = DBD_DTYPE_ENOENT;
		} else {
			dtype = DBD_DTYPE_DIRECTORY;
			if (last_modifiedp && filesizep && attrs_loadedp &&
			    contents_sz) {
				json_t *content = json_array_get(contents, 0);
				json_t *js_key = json_object_get(content,
								 "Key");
				const char *key = json_string_value(js_key);
				if (key && 0 == strcmp(key, prefix)) {
					ret = get_attributes_from_listing_content(content, last_modifiedp, filesizep);
					if (attrs_loadedp && 0 == ret)
						*attrs_loadedp = true;
				}
			}
		}
		dbd_response_free(prefix_response);
	}
	ret = 0;
 end:
	dbd_response_free(exact_match_response);
	if (dtypep)
		*dtypep = dtype;
	return ret;
}

static size_t
noop_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	return size*nmemb;
}

int
dbd_delete(struct scality_fsal_export *export,
	   const char *object) {

	char url[MAX_URL_SIZE];
	int pos = 1;
	CURL *curl = NULL;
	CURLcode curl_ret;
	int success = 0;
	long http_status;

	curl = curl_easy_init();
	if ( NULL == curl ) {
		return -1;
	}

	char *tmp = curl_easy_escape(curl, object, strlen(object));
	pos = snprintf(url, sizeof(url), "%s"BUCKET_BASE_PATH"/%s/%s",
		       export->module->dbd_url, export->bucket, tmp);
	free(tmp);


	if ( pos >= sizeof(url) ) {
		LogCrit(COMPONENT_FSAL, "buffer overrun");
		goto out;
	}

	LogDebug(COMPONENT_FSAL,
		 "dbd_delete(%s)",
		 url);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, noop_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300);

	curl_ret = curl_easy_perform(curl);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "curl(%s) failed", url);
		goto out;
	}

	curl_ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);


	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL,
			"Unable to retrieve HTTP status for %s", url);
		goto out;
	}

	if ( http_status >= 200 && http_status < 300 ) {
	} else if ( http_status != 404 ) {
		LogCrit(COMPONENT_FSAL,
			 "curl(DELETE %s) => HTTP STATUS %ld",
			 url,
			 http_status);
	}
	success = 1;

 out:
	if (curl)
		curl_easy_cleanup(curl);

	return -!success;
}

void
dirent_init(dirent_t *dirent,
	    const char *name,
	    dbd_dtype_t dtype,
	    struct timespec last_modified,
	    long long filesize)
{
	dirent->name = gsh_strdup(name);
	dirent->dtype = dtype;
	dirent->last_modified = last_modified;
	dirent->filesize = filesize;
}

static void
dirent_deinit(dirent_t *dirents, int count)
{
	int i;
	for ( i = 0 ; i < count ; ++i ) {
		gsh_free(dirents[i].name);
	}
}

static int
dirent_cmp(const void *lhsp, const void *rhsp) {
	const dirent_t *lhs = lhsp;
	const dirent_t *rhs = rhsp;
	return strcmp(lhs->name, rhs->name);
}

static int
digest_entry(const char *prefix,
	     int prefix_len,
	     const char *entry,
	     char *dirent,
	     size_t dirent_len)
{
	if ( NULL == entry ) {
		LogCrit(COMPONENT_FSAL,
			"Null entry");
		return -1;
	}
	if ( 0 != strncmp(entry, prefix, prefix_len) ) {
		LogCrit(COMPONENT_FSAL,
			"Entry(%s) outside prefix(%s)",
			entry, prefix);
		return -1;
	}
	entry+=prefix_len;
	size_t entry_len = strlen(entry);
	if ( entry_len >= dirent_len) {
		LogCrit(COMPONENT_FSAL,
			"dirent buffer too small");
		return -1;
	}
	if ( 0 == entry_len ) {
		// a placeholder: when corresponding to the prefix,
		// it is listed in contents
		return -1;
	}

	memcpy(dirent, entry, entry_len);
	dirent[entry_len] = '\0';
	if ( dirent[entry_len-1] == '/' )
		dirent[entry_len-1] = '\0';
	if ( NULL != strrchr(dirent, *S3_DELIMITER) ) {
		LogCrit(COMPONENT_FSAL,
			"listing output filter defeated ");
		return -1;
	}
	return 0;
}

static int
dbd_dirents(struct scality_fsal_export* export,
	    struct scality_fsal_obj_handle *parent_hdl,
	    /* in/out */ char *marker,
	    /* out */  dirent_t *dirents,
	    /* out */ int *countp,
	    /* out */ int *is_lastp)
{
	dbd_response_t *response;
	char dirent[MAX_URL_SIZE];
	int count = 0;
	assert(NULL != countp);

	char prefix[MAX_URL_SIZE] = {};
	int prefix_len = 0;
	if ( parent_hdl->object[0] != '\0' )
		prefix_len = snprintf(prefix, sizeof prefix,
				      "%s"S3_DELIMITER, parent_hdl->object);
	if ( prefix_len > (int)sizeof(prefix)) {
		LogCrit(COMPONENT_FSAL, "Prefix too long: %d", prefix_len);
		return -1;
	}

	dbd_get_parameters_t parameters = {
		.marker =  '\0' == marker[0] ? NULL : marker,
		.prefix = prefix,
		.maxkeys = READDIR_MAX_KEYS,
		.delimiter = S3_DELIMITER,
	};

	response = dbd_get(export, BUCKET_BASE_PATH, NULL, &parameters);
	if ( NULL == response )
		return -1;

	json_t *commonPrefixes = json_object_get(response->body, "CommonPrefixes");
	json_t *contents = json_object_get(response->body, "Contents");

	size_t commonPrefixes_sz = json_array_size(commonPrefixes);
	size_t contents_sz = json_array_size(contents);

	*is_lastp = !json_equal(json_true(), json_object_get(response->body, "IsTruncated"));
	if ( commonPrefixes_sz ) {
		int i;
		for ( i = 0 ; i < json_array_size(commonPrefixes) ; ++i ) {
			const char *dent = json_string_value(json_array_get(commonPrefixes,i));
			if ( marker && strcmp(marker, dent) >= 0) {
				LogCrit(COMPONENT_FSAL,
					"got an unordered listing marker:%s >= dent:%s",
					marker, dent);
			}
			int ret = digest_entry(prefix, prefix_len,
					       dent,
					       dirent, sizeof dirent);
			if ( ret < 0 )
				continue;
			dirent_init(&dirents[count++], dirent, DBD_DTYPE_DIRECTORY, (struct timespec){}, 0);
		}
	}
	if ( contents_sz ) {
		int i;
		for ( i = 0 ; i < json_array_size(contents); ++i ) {
			json_t *content = json_array_get(contents,i);
			const char *dent = json_string_value(json_object_get(content,"key"));
			if ( marker && strcmp(marker, dent) >= 0) {
				LogCrit(COMPONENT_FSAL,
					"got an unordered listing marker:%s >= dent:%s",
					marker, dent);
			}
			int ret = digest_entry(prefix, prefix_len,
					       dent,
					       dirent, sizeof dirent);
			if ( ret < 0 )
				continue;
			struct timespec last_modified;
			long long filesize;
			ret = get_attributes_from_listing_content(content,
								  &last_modified,
								  &filesize);
			if ( ret < 0 )
				continue;
			dirent_init(&dirents[count++],
				    dirent, DBD_DTYPE_REGULAR,
				    last_modified,
				    filesize);
		}
	}

	if ( ! *is_lastp ) {
		const char *nextMarker = json_string_value(json_object_get(response->body, "NextMarker"));
		if ( NULL != nextMarker && marker  )
			strcpy(marker, nextMarker);
	}
	*countp = count;
	qsort(dirents, count, sizeof(dirent_t), dirent_cmp);
	dbd_response_free(response);
	return 0;
}

int
dbd_readdir(struct scality_fsal_export* export,
	    struct scality_fsal_obj_handle *myself,
	    const fsal_cookie_t *whence,
	    void *dir_state,
	    dbd_readdir_cb cb,
	    bool *eof)
{
	int ret = 0;
	char marker[MAX_URL_SIZE] = "";
	int is_last;
	dirent_t dirents[READDIR_MAX_KEYS] = {};
	int count;
	fsal_cookie_t seekloc = 0;

	if (whence != NULL)
		seekloc = *whence;

	if (seekloc) {
		int ret = redis_get_object((char*)&seekloc, sizeof seekloc,
					   marker, sizeof marker);
		if  ( 0 != ret ) {
			LogCrit(COMPONENT_FSAL, "Unable to retrieve handle for %s", marker);
			return -1;
		}
	}
	LogDebug(COMPONENT_FSAL,
		"readdir(%s, seekloc=%"PRIu64") begin",
		 myself->object, seekloc);
	if (!*eof)
		do {
			int ret = dbd_dirents(export, myself, marker,
					      dirents, &count,
					      &is_last);
			if ( ret < 0 ) {
				ret = -1;
				break;
			}

			int i;
			for (i = 0 ; i < count ; ++i ) {
				dirent_t *p = &dirents[i];
				listed_dirent = p;
				LogDebug(COMPONENT_FSAL,
					 "readdir dent: %s",
					 p->name);
				if (!cb(p->name, dir_state, &seekloc)) {
					listed_dirent = NULL;
					if ( is_last && i == count-1 )
						*eof = true;
					dirent_deinit(dirents, count);
					goto exit_loop;
				}
				listed_dirent = NULL;
			}
			dirent_deinit(dirents, count);
		} while ( !is_last );
	*eof = true;
	LogDebug(COMPONENT_FSAL,
		"readdir(%s) end",
		myself->object);
 exit_loop:
	return ret;
}

int
dbd_collect_bucket_attributes(struct scality_fsal_export *export)
{
	int ret = 0;
	dbd_response_t *response;
	response = dbd_get(export, ATTRIBUTES_BASE_PATH, "", NULL);
	if ( NULL != response && 200 == response->http_status ) {
		json_t *js_owner = json_object_get(response->body, "owner");
		json_t *js_owner_display_name = json_object_get(response->body, "ownerDisplayName");
		json_t *js_creation_date = json_object_get(response->body, "creationDate");
		const char *owner = NULL,
			*owner_display_name = NULL;
		if (js_creation_date)
			export->creation_date =
				iso8601_str2timespec(json_string_value(js_creation_date));
		if (js_owner)
			owner = json_string_value(js_owner);
		if (js_owner_display_name)
			owner_display_name = json_string_value(js_owner_display_name);
		if ( NULL != owner && NULL != owner_display_name ) {
			export->owner_id = strdup(owner);
			export->owner_display_name = strdup(owner_display_name);
			if (NULL == export->owner_id || NULL == export->owner_display_name)
				ret = -1;
		}
		else {
			LogCrit(COMPONENT_FSAL,
				"dbd_collect_bucket_attributes(%s) missing owner information",
				export->bucket);
			ret = -1;
		}
	}
	else {
		LogCrit(COMPONENT_FSAL,
			"dbd_collect_bucket_attributes(%s) request failed",
			export->bucket);
		ret = -1;
	}
	dbd_response_free(response);
	return ret;
}

static int
dbd_get_parts_size(struct scality_fsal_export *export,
		   struct scality_fsal_obj_handle *myself)
{
	size_t total = 0;
	struct avltree_node *node;
	int ret = 0;
	scality_content_lock(myself);
	for ( node = avltree_first(&myself->locations) ;
	      node != NULL ;
	      node = avltree_next(node) ) {
		struct scality_location *location;
		location = avltree_container_of(node,
						struct scality_location,
						avltree_node);
		size_t len;
		ret = sproxyd_head(export, location->key, &len);
		if ( ret < 0 )
			goto out;
		location->start = total;
		location->size = len;
		total += len;

		LogDebug(COMPONENT_FSAL,
			 "HEAD on part: key=%s, start=%zd, size=%zd",
			 location->key,
			 location->start,
			 location->size);
	}
 out:
	scality_content_unlock(myself);
	return ret;
}

static int
dbd_getattr_regular_file(struct scality_fsal_export* export,
			 struct scality_fsal_obj_handle *object_hdl);

static int
dbd_getattr_directory(struct scality_fsal_export* export,
		      struct scality_fsal_obj_handle *object_hdl)
{
	char prefix[MAX_URL_SIZE];
	int ret = 0;
	if ( '\0' == object_hdl->object[0] ) {
		//if this fails for the root handle, export is killed by ganesha
		return 0;
	}

	snprintf(prefix, sizeof(prefix), "%s"S3_DELIMITER, object_hdl->object);
	dbd_get_parameters_t parameters = {
		.prefix = prefix,
		.marker = NULL,
		.delimiter = S3_DELIMITER,
		.maxkeys = 1
	};
	dbd_response_t *response = NULL;
	LogDebug(COMPONENT_FSAL, "getattr_dir(%s)", object_hdl->object);
	response = dbd_get(export, BUCKET_BASE_PATH, NULL, &parameters);
	if ( NULL != response && response->http_status == 200 ) {

		json_t *commonPrefixes = json_object_get(response->body, "CommonPrefixes");
		json_t *contents = json_object_get(response->body, "Contents");
		size_t commonPrefixes_sz = json_array_size(commonPrefixes);
		size_t contents_sz = json_array_size(contents);
		bool response_empty = ( 0 == commonPrefixes_sz + contents_sz );

		if ( response_empty ) {
			//doesn't exist or prefix without the delimiter points to an object
			ret = -1;
		}
	} else {
		LogCrit(COMPONENT_FSAL,
			"dbd_getattr_directory(%s) request failed",
			object_hdl->object);
		ret = -1;
	}
	dbd_response_free(response);

	if ( 0 == ret ) {
		(void)dbd_getattr_regular_file(export, object_hdl);
	}
	return ret;
}

static int
dbd_getattr_regular_file(struct scality_fsal_export* export,
			 struct scality_fsal_obj_handle *object_hdl)
{
	LogDebug(COMPONENT_FSAL,
		 "%s: %p", object_hdl->object, object_hdl);

	int ret = 0;
	dbd_response_t *response = NULL;
	char object[MAX_URL_SIZE];
	bool directory;

	scality_content_lock(object_hdl);

	if (object_hdl->state > SCALITY_FSAL_OBJ_STATE_CLEAN) {
		ret = 0;
		goto out;
	}
	object_hdl->state = SCALITY_FSAL_OBJ_STATE_CLEAN;
	directory = DIRECTORY == object_hdl->attributes.type;

	snprintf(object, sizeof object,
		 directory
		 ? "%s"S3_DELIMITER
		 : "%s", object_hdl->object);

	response = dbd_get(export, BUCKET_BASE_PATH, object ,NULL);
	if ( NULL == response ) {
		ret = -1;
		goto out;
	}
	if ( 200 != response->http_status ) {
		ret = -1;
		goto out;
	}
	struct timespec last_modified;
	long long filesize;
	ret = get_attributes_from_exact_match(response->body,
					      object_hdl->object,
					      &last_modified,
					      &filesize);
	if (ret < 0) {
		goto out;
	}
	object_hdl->attributes.filesize = filesize;
	object_hdl->attributes.spaceused = filesize;

	object_hdl->attributes.mtime = last_modified;
	object_hdl->attributes.atime = object_hdl->attributes.mtime;
	object_hdl->attributes.ctime = object_hdl->attributes.mtime;
	object_hdl->attributes.chgtime = object_hdl->attributes.mtime;

	struct avltree_node *node;
	for ( node = avltree_first(&object_hdl->locations) ;
	      node != NULL ;
	      node = avltree_first(&object_hdl->locations) ) {
		struct scality_location *location;
		location = avltree_container_of(node,
						struct scality_location,
						avltree_node);
		avltree_remove(node, &object_hdl->locations);
		scality_location_free(location);
	}
	object_hdl->n_locations = 0;

	json_t *location = directory
		? NULL
		: json_object_get(response->body, "location");
	struct scality_location *new_location;
	switch(location? json_typeof(location) : JSON_NULL) {
	case JSON_STRING:
		new_location = scality_location_new(json_string_value(location),
						    0, filesize);

		avltree_insert(&new_location->avltree_node,
			       &object_hdl->locations);
		object_hdl->n_locations = 1;
		break;
	case JSON_ARRAY: {
		size_t i;
		size_t last_end = 0;
		object_hdl->n_locations = json_array_size(location);
		bool incomplete = false;
		for ( i = 0 ; i < object_hdl->n_locations ; ++i ) {
			json_t *item = json_array_get(location, i);

			switch (json_typeof(item)) {
			case JSON_STRING: {
				const char *key = json_string_value(item);
				new_location = scality_location_new(key,
								    -1, -1);
				avltree_insert(&new_location->avltree_node,
					       &object_hdl->locations);
				incomplete = true;
				break;
			}
			case JSON_OBJECT: {
				json_t *jkey = json_object_get(item, "key");
				json_t *jstart = json_object_get(item, "start");
				json_t *jsize = json_object_get(item, "size");
				size_t start = -1, size = -1;
				const char *key;
				if ( JSON_STRING != json_typeof(jkey) ) {
					LogCrit(COMPONENT_FSAL,
						"Key is not a string");
					ret = -1;
					goto out;
				}
				switch (json_typeof(jstart)) {
				case JSON_STRING:
					start = atoll(json_string_value(jstart));
					break;
				case JSON_INTEGER:
					start = json_integer_value(jstart);
					break;
				default:
					incomplete = true;
					break;
				}
				switch (json_typeof(jsize)) {
				case JSON_STRING:
					size = atoll(json_string_value(jsize));
					break;
				case JSON_INTEGER:
					size = json_integer_value(jsize);
					break;
				default:
					incomplete = true;
					break;
				}
				key = json_string_value(jkey);
				assert(last_end == start);
				last_end = start+size;
				new_location = scality_location_new(key,
								    start,
								    size);
				avltree_insert(&new_location->avltree_node,
					       &object_hdl->locations);
				LogDebug(COMPONENT_FSAL,
					 "key=%s, start=%zd, size=%zd",
					 key,
					 start,
					 size);
				}
				break;
			default:break;
			}
		}

		if ( incomplete ) {
			ret = dbd_get_parts_size(export,
						 object_hdl);
		}
		else {
			struct avltree_node *node;
			last_end = 0;
			int i = 0;
			for (node = avltree_first(&object_hdl->locations);
			     node != NULL ;
			     node = avltree_next(node) ) {
				struct scality_location *loc;
				loc = avltree_container_of(node,
							   struct
							   scality_location,
							   avltree_node);
				LogDebug(COMPONENT_FSAL,
					 "i: %d, "
					 "loc->start: %"PRIu64", "
					 "loc->size: %"PRIu64", ",
					 i, loc->start, loc->size);
				assert(last_end == loc->start);
				last_end = loc->start + loc->size;
				++i;
			}
		}
	}
	default:break;
	}

	struct scality_location * first_location;

	node = avltree_first(&object_hdl->locations);
	first_location = avltree_container_of(node,
					      struct scality_location,
					      avltree_node);
	if ( 0 != object_hdl->n_locations &&
	     NULL != first_location &&
	     first_location->size >= DEFAULT_PART_SIZE )
		object_hdl->part_size = first_location->size;
	else
		object_hdl->part_size = DEFAULT_PART_SIZE;

	scality_sanity_check_parts(export, object_hdl);

	assert(SCALITY_FSAL_OBJ_STATE_CLEAN == object_hdl->state);

 out:

	scality_content_unlock(object_hdl);
	dbd_response_free(response);
	return ret;
}


int
dbd_getattr(struct scality_fsal_export* export,
	    struct scality_fsal_obj_handle *object_hdl)
{
	if ( NULL == object_hdl->object )
		return -1;

	switch (object_hdl->obj_handle.type) {

	case DIRECTORY:
		return dbd_getattr_directory(export, object_hdl);

	case REGULAR_FILE:
		if (listed_dirent) {
			object_hdl->attributes.filesize =
			object_hdl->attributes.spaceused =
				listed_dirent->filesize;
			object_hdl->attributes.mtime =
				listed_dirent->last_modified;
			object_hdl->attributes.atime =
				object_hdl->attributes.mtime;
			object_hdl->attributes.ctime =
				object_hdl->attributes.mtime;
			object_hdl->attributes.chgtime =
				object_hdl->attributes.mtime;
			return 0;
		}
		return dbd_getattr_regular_file(export, object_hdl);

	default:
		LogCrit(COMPONENT_FSAL,
			"getattr on unsupported object %s", object_hdl->object);
		return -1;
	}
}

static char *
get_payload(struct scality_fsal_export* export,
	    struct scality_fsal_obj_handle *object_hdl)
{
	json_t *metadata = json_object();
	json_t *acl = json_object();
	char date[64];
	time_t time = object_hdl->attributes.mtime.tv_sec;
	struct tm tm;
	size_t ret;
	char md5[36];
	bool regular_file = REGULAR_FILE == object_hdl->attributes.type;
	bool directory = DIRECTORY == object_hdl->attributes.type;

	if (!regular_file && !directory)
		return NULL;

	gmtime_r(&time, &tm);

	ret = strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &tm);
	snprintf(date+ret, sizeof(date)-ret, ".%03ldZ", object_hdl->attributes.mtime.tv_nsec/1000000);

	random_hex(md5, sizeof(md5)-1);
	md5[32]='-';
	md5[35]='\0';

	json_object_set(metadata, "md-model-version", json_integer(2));
	json_object_set(metadata, "Date", json_string(date));
	json_object_set(metadata, "last-modified", json_string(date));
	json_object_set(metadata, "owner-display-name", json_string(export->owner_display_name));
	json_object_set(metadata, "owner-id", json_string(export->owner_id));
	json_object_set(metadata, "content-type", json_string(regular_file
							      ? DEFAULT_CONTENT_TYPE
							      : DIRECTORY_CONTENT_TYPE));
	//empty string md5
	json_object_set(metadata, "content-md5", json_string(md5));
	json_object_set(metadata, "x-amz-server-side-encryption", json_string(""));
	json_object_set(metadata, "x-amz-server-version-id", json_string(""));
	json_object_set(metadata, "x-amz-storage-class", json_string("STANDARD"));
	json_object_set(metadata, "x-amz-website-redirect-location", json_string(""));
	json_object_set(metadata, "x-amz-server-side-encryption-aws-kms-key-id", json_string(""));
	json_object_set(metadata, "x-amz-server-side-encryption-customer-algorithm", json_string(""));
	json_object_set(metadata, "x-amz-version-id", json_string("null"));

	json_object_set(metadata, "content-length",
			json_integer(regular_file
				     ? object_hdl->attributes.filesize
				     : 0));
	if ( object_hdl->n_locations ) {
		json_t *js_locations = json_array();
		struct avltree_node *node;
		for ( node = avltree_first(&object_hdl->locations) ;
		      node != NULL ;
		      node = avltree_next(node) ) {
			struct scality_location *location;
			location = avltree_container_of(node,
							struct scality_location,
							avltree_node);
			json_t *js_location = json_object();
			assert(NULL != location->key);
			json_object_set(js_location, "key",
					json_string(location->key));
			json_object_set(js_location, "start",
					json_integer(location->start));
			json_object_set(js_location, "size",
					json_integer(location->size));
			json_object_set(js_location, "dataStoreName",
					json_string("sproxyd"));
			json_array_append(js_locations, js_location);
		}
		json_object_set(metadata, "location", js_locations);
	} else {
		json_object_set(metadata, "location", json_null());
	}

	json_object_set(metadata, "acl", acl);
	json_object_set(acl, "Canned", json_string("private"));
	json_object_set(acl, "FULL_CONTROL", json_array());
	json_object_set(acl, "WRITE_ACP", json_array());
	json_object_set(acl, "READ", json_array());
	json_object_set(acl, "READ_ACP", json_array());

	char *payload_str = json_dumps(metadata, JSON_COMPACT);
	json_t* payload = NULL;
	if (export->metadata_version < 1) {
		payload = json_object();
		json_object_set(payload, "data", json_string(payload_str));
		free(payload_str);
		payload_str = json_dumps(payload, JSON_COMPACT);
	}

	json_decref(metadata);
	if (payload)
		json_decref(payload);

	return payload_str;
}

typedef struct
{
	size_t size;
	char *buf;
} buffer_t;

static size_t
read_callback(char *buffer, size_t size, size_t nitems, void *instream)
{
	size*=nitems;
	buffer_t *buf = instream;
	if ( size > buf->size )
		size = buf->size;
	if ( size > 0 ) {
		memcpy(buffer, buf->buf, size);
		buf->size -= size;
		buf->buf += size;
	}
	return size;
}

int
dbd_post(struct scality_fsal_export* export,
	 struct scality_fsal_obj_handle *object_hdl)
{
	LogDebug(COMPONENT_FSAL,
		 "%s: %p", object_hdl->object, object_hdl);

	char url[MAX_URL_SIZE];
	char *payload;
	CURL *curl = NULL;
	CURLcode curl_ret;
	long http_status;
	int ret = -1;

	scality_sanity_check_parts(export, object_hdl);

	payload = get_payload(export, object_hdl);
	if ( 0 == object_hdl->object[0]) {
		//FIXME
		//set bucket attributes must not be done here
		//does nothing and return success
		return 0;
	}

	if ( NULL == payload )
		return ret;

	curl = curl_easy_init();
	if ( NULL == curl ) {
		return ret;
	}

	buffer_t buf = {
		.buf = payload,
		.size = strlen(payload)
	};
	const char *format;
	switch (object_hdl->obj_handle.type) {
	case DIRECTORY:
		format = "%s"BUCKET_BASE_PATH"/%s/%s/";
		break;
	case REGULAR_FILE:
		format = "%s"BUCKET_BASE_PATH"/%s/%s";
		break;
	default:
		LogCrit(COMPONENT_FSAL, "Invalid object type");
		return -1;
	}
	char *tmp = curl_easy_escape(curl, object_hdl->object, strlen(object_hdl->object));
	snprintf(url, sizeof(url), format,
		 export->module->dbd_url, export->bucket, tmp);
	free(tmp);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, buf.size);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
	curl_easy_setopt(curl, CURLOPT_READDATA, &buf);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, noop_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300);

	curl_ret = curl_easy_perform(curl);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "curl(%s) failed: %s", url, curl_easy_strerror(curl_ret));
		goto out;
	}
	curl_ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "Unable to retrieve HTTP status for %s", url);
		goto out;
	}
	if ( http_status < 200 || http_status >= 300 ) {
		goto out;
	}
	ret = 0;

 out:
	if (curl)
		curl_easy_cleanup(curl);
	free(payload);
	return ret;
}


int
dbd_metadata_get_version(struct scality_fsal_export *export)
{
	int ret = 0;
	dbd_response_t *response = NULL;

	response = dbd_get(export, METADATA_INFORMATION_PATH, "", NULL);
	if ( NULL == response ) {
		ret = -1;
	} else  if ( response->http_status == 404) {
		export->metadata_version = 0;
	} else if ( response->http_status == 200 ) {
		json_t *metadata_version = json_object_get(response->body,
							   "metadataVersion");
		if ( json_typeof(metadata_version) != JSON_INTEGER )
			ret = -1;
		else
			export->metadata_version =
				json_integer_value(metadata_version);
	} else {
		return -1;
	}
	if (response)
		dbd_response_free(response);
	return ret;
}
