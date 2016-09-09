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


static struct timespec
iso8601_str2timespec(const char* ts_str)
{
	struct timespec result = {};
	int y,M,d,h,m;
	double s;
	if (NULL != ts_str) {
		int ret = sscanf(ts_str, "%d-%d-%dT%d:%d:%lfZ", &y, &M, &d, &h, &m, &s);
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


	if ( !object == !parameters ) {
		LogCrit(COMPONENT_FSAL, "BUG: Invalid parameters object and parameters are both set");
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
			tmp = curl_easy_escape(curl, parameters->prefix, strlen(parameters->prefix));
			pos += snprintf(query_string+pos, sizeof(query_string)-pos,
					   "prefix=%s&", tmp);
			free(tmp);
			if ( pos >= sizeof(query_string) ) {
				LogCrit(COMPONENT_FSAL, "buffer overrun");
				goto out;
			}
		}
		if ( NULL != parameters->marker ) {
			tmp = curl_easy_escape(curl, parameters->marker, strlen(parameters->marker));
			pos += snprintf(query_string+pos, sizeof(query_string)-pos,
					"marker=%s&", tmp);
			if ( pos >= sizeof(query_string) ) {
				LogCrit(COMPONENT_FSAL, "buffer overrun");
				goto out;
			}
			free(tmp);
		}
		if ( NULL != parameters->delimiter ) {
			tmp = curl_easy_escape(curl, parameters->delimiter, strlen(parameters->delimiter));
			pos += snprintf(query_string+pos, sizeof(query_string)-pos,
					   "delimiter=%s&", tmp);
			free(tmp);
			if ( pos >= sizeof(query_string) ) {
				LogCrit(COMPONENT_FSAL, "buffer overrun");
				goto out;
			}
		}
		if ( parameters->maxkeys > 0 ) {
			pos += snprintf(query_string+pos, sizeof(query_string)-pos,
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
			       export->module->dbd_url, base_path, export->bucket, tmp);
		free(tmp);
	}
	else {
		pos = snprintf(url, sizeof(url), "%s%s/%s%s",
			       export->module->dbd_url, base_path, export->bucket, query_string);
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

	curl_ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->http_status);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "Unable to retrieve HTTP status for %s", url);
		goto out;
	}

	if ( response->http_status >= 200 && response->http_status < 300 ) {
		ret = fclose(body_stream);
		body_stream = NULL;
		if ( ret < 0 ) {
			LogCrit(COMPONENT_FSAL, "stream error at close");
			goto out;
		}
		response->body = json_loads(body_text, JSON_REJECT_DUPLICATES,&json_error);
		if ( NULL == response->body ) {
			LogWarn(COMPONENT_FSAL,
				"json_load failed with error %s ...", json_error.text);
			if (object)
				LogWarn(COMPONENT_FSAL,
					"... parse error on object %s", object);
			else
				LogWarn(COMPONENT_FSAL,
					"... parse error on query string %s", query_string);
			
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
	       dbd_dtype_t *dtypep)
{
	char object[MAX_URL_SIZE];
	int len;
	len = snprintf(object, sizeof(object), "%s%s%s",
		       parent_hdl->object?:"",
		       parent_hdl->object && parent_hdl->object[0] != '\0' ?S3_DELIMITER:"", name);
	if ( len >= sizeof(object) ) {
		LogCrit(COMPONENT_FSAL, "object name too long");
		return -1;
	}
	return dbd_lookup_object(export, object, dtypep);
}

int
dbd_lookup_object(struct scality_fsal_export *export,
		  const char *object,
		  dbd_dtype_t *dtypep)
{
	int ret = -1;
	char prefix[MAX_URL_SIZE];
	int len;
	dbd_dtype_t dtype = DBD_DTYPE_IOERR;
	len = snprintf(prefix, sizeof(prefix), "%s", object);

	dbd_response_t *exact_match_response = NULL;
	dbd_response_t *prefix_response = NULL;

	exact_match_response = dbd_get(export, BUCKET_BASE_PATH, prefix, NULL);
	if ( NULL == exact_match_response )
		goto end;

	// add a trailing slash to lookup a common prefix
	snprintf(prefix+len, sizeof(prefix)-len, "%s", S3_DELIMITER);
	dbd_get_parameters_t parameters = {
		.prefix = prefix,
		.marker = NULL,
		.delimiter = S3_DELIMITER,
		.maxkeys = 1
	};

	prefix_response = dbd_get(export, BUCKET_BASE_PATH, NULL, &parameters);
	if ( NULL == prefix_response || prefix_response->http_status != 200 )
		goto end;

	json_t *commonPrefixes = json_object_get(prefix_response->body, "CommonPrefixes");
	json_t *contents = json_object_get(prefix_response->body, "Contents");
	
	size_t commonPrefixes_sz = json_array_size(commonPrefixes);
	size_t contents_sz = json_array_size(contents);
	bool prefix_response_empty = ( 0 == commonPrefixes_sz + contents_sz );

	if ( prefix_response_empty ) {
		if ( 404 == exact_match_response->http_status ) {
			dtype = DBD_DTYPE_ENOENT;
		}
		else if ( 200 == exact_match_response->http_status ) {
			dtype = DBD_DTYPE_REGULAR;
		}
	}
	else {
                dtype = DBD_DTYPE_DIRECTORY;
		if ( 200 == exact_match_response->http_status ) {
			LogWarn(COMPONENT_FSAL,
                                "an object is in the way of %s, it will not be visible",
				object);
			dtype = DBD_DTYPE_DIRECTORY;
		}
	}
	ret = 0;
 end:
	dbd_response_free(exact_match_response);
	dbd_response_free(prefix_response);
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
		LogCrit(COMPONENT_FSAL, "Unable to retrieve HTTP status for %s", url);
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



typedef struct dbd_dirent {
	char *name;
	dbd_dtype_t dtype;
	struct dbd_dirent *next;
} dirent_t;

static dirent_t *
dirent_new(const char *name, dbd_dtype_t dtype, dirent_t *next)
{
	dirent_t *dirent = gsh_malloc(sizeof *dirent);
	dirent->name = gsh_strdup(name);
	dirent->dtype = dtype;
	dirent->next = next;
	return dirent;
}
static void
dirent_free(dirent_t *dirent)
{
	dirent_t *d;
	while (dirent) {
		d = dirent->next;
		gsh_free(dirent->name);
		gsh_free(dirent);
		dirent = d;
	}
}


static int
dbd_dirents(struct scality_fsal_export* export,
	    struct scality_fsal_obj_handle *parent_hdl,
	    /* in/out */ char *marker,
	    /* out */  dirent_t **direntsp,
	    /* out */ int *is_lastp)
{
	dbd_response_t *response;
	dirent_t *dirents = NULL;
	char dirent[MAX_URL_SIZE];

	char prefix[MAX_URL_SIZE] = {};
	if ( parent_hdl->object[0] != '\0' )
		snprintf(prefix, sizeof prefix, "%s"S3_DELIMITER, parent_hdl->object);
		
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
				LogWarn(COMPONENT_FSAL,
					"got an unordered listing marker:%s >= dent:%s",
					marker, dent);
			}
			size_t dent_len = 0;
			if (NULL != dent)
				dent_len = strlen(dent);
			memcpy(dirent, dent, dent_len);
			dirent[dent_len-1] = '\0'; //remove trailing slash too
			char *p = strrchr(dirent, *S3_DELIMITER);
			if ( p ) {
				int l = strlen(dirent)-(p-dirent)-1;
				memmove(dirent, p+1, l);
				dirent[l]='\0';
			}
			dirents = dirent_new(dirent, DBD_DTYPE_DIRECTORY, dirents);
			LogDebug(COMPONENT_FSAL,
				"new dirent from commonPrefixes: %s",
				dirent);
		}
	}
	if ( contents_sz ) {
		int i;
		for ( i = 0 ; i < json_array_size(contents); ++i ) {
			json_t *content = json_array_get(contents,i);
			const char *dent = json_string_value(json_object_get(content,"key"));
			if ( marker && strcmp(marker, dent) >= 0) {
				LogWarn(COMPONENT_FSAL,
					"got an unordered listing marker:%s >= dent:%s",
					marker, dent);
			}
                        if ( 0 == strcmp(dent, prefix) ) {
                                //skip placeholder
                                continue;
                        }
			size_t dent_len = 0;
			if (NULL != dent)
				dent_len = strlen(dent);
			memcpy(dirent, dent, dent_len);		
			dirent[dent_len] = '\0';
			char *p = strrchr(dirent, *S3_DELIMITER);
			if ( p ) {
				int l = strlen(dirent)-(p-dirent)-1;
				memmove(dirent, p+1, l);
				dirent[l]='\0';
			}
			dirents = dirent_new(dirent, DBD_DTYPE_REGULAR, dirents);
			LogDebug(COMPONENT_FSAL,
				 "new dirent from contents: %s",
				 dirent);
		}
	}

	if ( ! *is_lastp ) {
		const char *nextMarker = json_string_value(json_object_get(response->body, "NextMarker"));
		if ( NULL != nextMarker && marker  )
			strcpy(marker, nextMarker);
	}

	*direntsp = dirents;
	dbd_response_free(response);
	return 0;
}

	
int
dbd_readdir(struct scality_fsal_export* export,
	    struct scality_fsal_obj_handle *myself,
	    fsal_cookie_t *whence,
	    void *dir_state,
	    fsal_readdir_cb cb,
	    bool *eof)
{
	int ret = 0;
	char marker[MAX_URL_SIZE] = "";
	int is_last;
	dirent_t *dirents = NULL;
	fsal_cookie_t seekloc;

	if (whence != NULL)
		seekloc = *whence;
	else
		seekloc = 0;

	if (seekloc) {
		int ret = redis_get_seekloc_marker(myself->object,
						   seekloc,
						   marker,
						   sizeof(marker));
		if  ( 0 != ret )
			return -1;
	}

	LogDebug(COMPONENT_FSAL,
		"readdir(%s) begin",
		myself->object);
	int count = 0;
	do {
		int ret = dbd_dirents(export, myself, marker, &dirents, &is_last);
		if ( ret < 0 ) {
			ret = -1;
			break;
		}

		dirent_t *p, *d = dirents;
		while (d) {
			p = d;
			d = d->next;
			LogDebug(COMPONENT_FSAL,
				"readdir dent: %s",
				p->name);
			if (!cb(p->name, dir_state, count++)) {
				snprintf(marker, sizeof marker, p->dtype == DBD_DTYPE_DIRECTORY
					 ? "%s"S3_DELIMITER"%s/"
					 : "%s"S3_DELIMITER"%s/",
					 myself->object, p->name);
				ret = redis_set_seekloc_marker(myself->object, marker, whence);
				if ( 0 != ret ) {
					ret = -1;
				}
				else {
					*eof = false;
				}
				goto exit_loop;
			}
		}
		dirent_free(dirents);
		dirents = NULL;
	} while ( !is_last );
	*eof = true;
	LogDebug(COMPONENT_FSAL,
		"readdir(%s) end",
		myself->object);
 exit_loop:
	dirent_free(dirents);
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
dbd_get_parts_size(struct scality_fsal_export *export,
		   struct scality_fsal_obj_handle *object_hdl)
{
	size_t i;
	size_t total = 0;
	for ( i = 0 ; i < object_hdl->n_locations ; ++i ) {
		size_t len;
		int ret;
		ret = sproxyd_head(export, object_hdl->locations[i].key, &len);
		if ( ret < 0 )
			return ret;
		object_hdl->locations[i].start = total;
		object_hdl->locations[i].size = len;
		total += len;

		LogDebug(COMPONENT_FSAL,
			 "HEAD on part: key=%s, start=%zd, size=%zd",
			 object_hdl->locations[i].key,
			 object_hdl->locations[i].start,
			 object_hdl->locations[i].size);
	}
	return 0;
}

static int
dbd_getattr_regular_file(struct scality_fsal_export* export,
			 struct scality_fsal_obj_handle *object_hdl)
{
	int ret = 0;
	dbd_response_t *response = NULL;
        char object[MAX_URL_SIZE];
        bool directory = DIRECTORY == object_hdl->attributes.type;

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
	json_t *content_length = json_object_get(response->body, "content-length");
	if ( NULL == content_length ) {
		LogCrit(COMPONENT_FSAL,
			"content-length is not set on %s",
			object_hdl->object);
		ret = -1;
		goto out;
	}
	long long filesize;
	switch(json_typeof(content_length)) {
	case JSON_STRING:
		filesize = atoi(json_string_value(content_length));
		break;
	case JSON_INTEGER:
		filesize = json_integer_value(content_length);
		break;
	case JSON_REAL:
		filesize = json_real_value(content_length);
		break;
	default:
		filesize = 0;
	}
	object_hdl->attributes.filesize = filesize;

	json_t *last_modified = json_object_get(response->body, "last-modified");
	switch(json_typeof(last_modified)) {
	case JSON_STRING: {
		const char *ts_str = json_string_value(last_modified);
		object_hdl->attributes.mtime = iso8601_str2timespec(ts_str);
		object_hdl->attributes.atime = object_hdl->attributes.mtime;
		object_hdl->attributes.ctime = object_hdl->attributes.mtime;
		object_hdl->attributes.chgtime = object_hdl->attributes.mtime;
		break;
	}
	default:
		LogCrit(COMPONENT_FSAL, "Unknown last-modified field type");
		break;
	}
	
	int i = 0;
	for ( i = 0 ; i < object_hdl->n_locations ; ++i ) {
		gsh_free(object_hdl->locations[i].key);
	}
	if ( object_hdl->locations )
		gsh_free(object_hdl->locations);
	object_hdl->locations = NULL;
	object_hdl->n_locations = 0;

	json_t *location = directory
                ? NULL
                : json_object_get(response->body, "location");

	switch(location? json_typeof(location) : JSON_NULL) {
	case JSON_STRING:
		object_hdl->locations = gsh_malloc(sizeof(struct scality_location));
		object_hdl->locations[0].start = 0;
		object_hdl->locations[0].size = filesize;
		object_hdl->locations[0].key = gsh_strdup(json_string_value(location));
		object_hdl->n_locations = 1;
		break;
	case JSON_ARRAY: {
		size_t i;
		object_hdl->n_locations = json_array_size(location);
		object_hdl->locations = gsh_calloc(object_hdl->n_locations,
						   sizeof(struct scality_location));
		bool incomplete = false;
		for ( i = 0 ; i < object_hdl->n_locations ; ++i ) {
			json_t *item = json_array_get(location, i);
			
			switch (json_typeof(item)) {
			case JSON_STRING: {
				object_hdl->locations[i].start = -1;
				object_hdl->locations[i].size = -1;
				object_hdl->locations[i].key = gsh_strdup(json_string_value(json_array_get(location, i)));
				incomplete = true;
				break;
			}
			case JSON_OBJECT: {
				json_t *key = json_object_get(item, "key");
				json_t *start = json_object_get(item, "start");
				json_t *size = json_object_get(item, "size");
				switch (json_typeof(start)) {
				case JSON_STRING:
					object_hdl->locations[i].start =
						atoll(json_string_value(start));
					break;
				case JSON_INTEGER:
					object_hdl->locations[i].start =
						json_integer_value(start);
					break;
				default:break;
				}
				switch (json_typeof(size)) {
				case JSON_STRING:
					object_hdl->locations[i].size =
						atoll(json_string_value(size));
					break;
				case JSON_INTEGER:
					object_hdl->locations[i].size =
						json_integer_value(size);
					break;
				default:break;
				}
				if ( JSON_STRING == json_typeof(key) ) {
					object_hdl->locations[i].key = gsh_strdup(json_string_value(key));
					LogDebug(COMPONENT_FSAL,
						 "key=%s, start=%zd, size=%zd",
						 object_hdl->locations[i].key,
						 object_hdl->locations[i].start,
						 object_hdl->locations[i].size);
					
				}
				break;
			}
			default:break;	
			}
		}
		if ( incomplete )
			ret = dbd_get_parts_size(export,
						 object_hdl);
	}	
	default:break;
	}

	if ( 0 != object_hdl->n_locations &&
	     object_hdl->locations[0].size >= DEFAULT_PART_SIZE )
		object_hdl->part_size = object_hdl->locations[0].size;
	else
		object_hdl->part_size = DEFAULT_PART_SIZE;

 out:
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
	char size[32];
	size_t ret;
	char md5[36];
        bool regular_file = REGULAR_FILE == object_hdl->attributes.type;
        bool directory = DIRECTORY == object_hdl->attributes.type;

        if (!regular_file && !directory)
                return NULL;

	gmtime_r(&time, &tm);
	
        snprintf(size, sizeof(size), "%zu", regular_file
                 ? object_hdl->attributes.filesize
                 : 0 );
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
	json_object_set(metadata, "content-length", json_string(size));
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

	if ( object_hdl->n_locations ) {
		json_t *locations = json_array();
		int i ;
		for ( i = 0 ; i < object_hdl->n_locations ; ++i ) {
			json_t *location = json_object();
			json_object_set(location, "key",
					json_string(object_hdl->locations[i].key));
			json_object_set(location, "start",
					json_integer(object_hdl->locations[i].start));
			json_object_set(location, "size",
					json_integer(object_hdl->locations[i].size));
			json_object_set(location, "dataStoreName", json_string("sproxyd"));
			json_array_append(locations, location);
		}
		json_object_set(metadata, "location", locations);
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
	json_t *payload = json_object();
	json_object_set(payload, "data", json_string(payload_str));
	free(payload_str);
	payload_str = json_dumps(payload, JSON_COMPACT);

	json_decref(metadata);
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
	char url[MAX_URL_SIZE];
	char *payload = get_payload(export, object_hdl);
	CURL *curl = NULL;
	CURLcode curl_ret;
	long http_status;
	int ret = -1;

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
