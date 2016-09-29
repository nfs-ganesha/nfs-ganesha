
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

/* sproxyd_client.c
 */

#include <ctype.h>
#include <curl/curl.h>
#include "scality_methods.h"
#include "sproxyd_client.h"
#include "random.h"

int
sproxyd_head(struct scality_fsal_export* export,
	     const char *id,
	     size_t *lenp)
{
	CURL *curl = NULL;
	CURLcode curl_ret;
	char url[MAX_URL_SIZE];
	int ret = 0;
	long http_status;
	double body_len;

	curl = curl_easy_init();
	if ( NULL == curl ) {
		LogCrit(COMPONENT_FSAL, "Unable to init HTTP request");
		return -1;

	}

	snprintf(url, sizeof url, "%s/%s", export->module->sproxyd_url, id);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300);

	LogDebug(COMPONENT_FSAL, "Perform HEAD %s", url);
	curl_ret = curl_easy_perform(curl);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "Unable to perform HTTP request: HEAD %s", url);
		ret = -1;
		goto end;
	}

	curl_ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "Unable to retrieve HTTP status");
		ret = -1;
		goto end;
	}
	if ( 200 != http_status ) {
		LogCrit(COMPONENT_FSAL, "HTTP request failed with %ld status", http_status);
		ret = -1;
		goto end;
	}

	curl_ret = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &body_len);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "Unable to retrieve Content-Length");
		ret = -1;
		goto end;
	}

	//Success
	*lenp = body_len;

 end:
	curl_easy_cleanup(curl);

	return ret;
}


static int
sproxyd_get(struct scality_fsal_export* export,
	    const char *id,
	    const char *range,
	    char **bufp,
	    size_t *lenp)
{
	CURL *curl = NULL;
	CURLcode curl_ret;
	char url[MAX_URL_SIZE];
	FILE *body_stream = NULL;
	char *body_text = NULL;
	size_t body_len = 0;
	int ret = 0;
	long http_status;

	curl = curl_easy_init();
	if ( NULL == curl ) {
		LogCrit(COMPONENT_FSAL, "Unable to init HTTP request");
		return -1;
	}

	body_stream = open_memstream(&body_text, &body_len);
	if ( NULL == body_stream ) {
		LogCrit(COMPONENT_FSAL, "Unable to open memstream: %s", strerror(errno));
		ret = -1;
		goto end;
	}

	snprintf(url, sizeof url, "%s/%s", export->module->sproxyd_url, id);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	if ( NULL != range ) {
		LogDebug(COMPONENT_FSAL, "HTTP request with Range: bytes=%s", range);
		curl_easy_setopt(curl, CURLOPT_RANGE, range);
	}
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_stream);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300);

	LogDebug(COMPONENT_FSAL, "Perform GET %s", url);
	curl_ret = curl_easy_perform(curl);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "Unable to perform HTTP request: GET %s", url);
		ret = -1;
		goto end;
	}
	curl_ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "Unable to retrieve HTTP status");
		ret = -1;
	}
	if ( (range && 206 != http_status) || (!range && 200 != http_status) ) {
		LogCrit(COMPONENT_FSAL, "HTTP request failed with %ld status", http_status);
		ret = -1;
		goto end;
	}
	ret = fclose(body_stream);
	body_stream = NULL;
	if ( ret < 0 ) {
		LogCrit(COMPONENT_FSAL,
			"stream error at close");
		ret = -1;
		goto end;
	}

	//Success
	*bufp = body_text;
	*lenp = body_len;
	body_text = NULL;
 end:
	curl_easy_cleanup(curl);
	if (body_stream)
		fclose(body_stream);
	if (body_text)
		free(body_text);

	return ret;
}

int
sproxyd_read(struct scality_fsal_export* export,
	     struct scality_fsal_obj_handle *obj,
	     uint64_t offset,
	     size_t size, char *buf)
{
	char *frag;
	size_t frag_len;
	LogDebug(COMPONENT_FSAL, "sproxyd_read(%s, offset=%lu, size=%zu)",
		 obj->object, offset, size);

	struct avltree_node *node;
	for (node = avltree_first(&obj->locations) ;
	     node !=  NULL ;
	     node = avltree_next(node) ) {
		struct scality_location *loc;
		loc = avltree_container_of(node,
					   struct scality_location,
					   avltree_node);
		char range[200];
		int ret;

		if ( offset > loc->start &&
		     offset > loc->start+loc->size )
			continue;
		if ( 0 == size )
			break;

		size_t read_start = offset-loc->start;
		size_t read_size = loc->size-read_start;
		if ( read_size > size )
			read_size = size;
		if ( 0 == read_size )
			continue;
		snprintf(range, sizeof range, "%zu-%zu",
			 read_start, read_start+read_size-1);

		ret = sproxyd_get(export, loc->key, range,
				  &frag, &frag_len);
		if ( ret < 0 )
			return -1;
		if ( frag_len != read_size ) {
			free(frag);
			LogCrit(COMPONENT_FSAL, "read size mismatch expected %zu, got %zu",
				read_size, frag_len);
			return -1;
		}
		memcpy(buf, frag, frag_len);
		buf+=frag_len;
		offset+=frag_len;
		size-=frag_len;
		free(frag);
	}
	return 0;
}

static size_t
noop_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	return size*nmemb;
}

int
sproxyd_delete(struct scality_fsal_export* export,
	       const char *id)
{
	CURL *curl = NULL;
	CURLcode curl_ret;
	char url[MAX_URL_SIZE];
	int ret = 0;
	long http_status;

	curl = curl_easy_init();
	if ( NULL == curl ) {
		LogCrit(COMPONENT_FSAL, "Unable to init HTTP request");
		return -1;

	}

	snprintf(url, sizeof url, "%s/%s", export->module->sproxyd_url, id);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, noop_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300);

	LogDebug(COMPONENT_FSAL, "Perform DELETE %s", url);
	curl_ret = curl_easy_perform(curl);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "Unable to perform HTTP request: HEAD %s", url);
		ret = -1;
		goto end;
	}

	curl_ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
	if ( CURLE_OK != curl_ret ) {
		LogCrit(COMPONENT_FSAL, "Unable to retrieve HTTP status");
		ret = -1;
		goto end;
	}
	if ( 200 != http_status ) {
		LogCrit(COMPONENT_FSAL, "HTTP request failed with %ld status", http_status);
		ret = -1;
		goto end;
	}

	//Success

 end:
	curl_easy_cleanup(curl);

	return ret;
}

#define KEY_SIZE 40

char *
sproxyd_new_key(void)
{
	char buf[KEY_SIZE+1];
	int i;
	size_t ret;
	ret = random_hex(buf, KEY_SIZE);
	if ( KEY_SIZE != ret )
		return NULL;
	//SID
	buf[KEY_SIZE-7] = '5';
	buf[KEY_SIZE-6] = '9';
	//COS
	buf[KEY_SIZE-2] = '7';
	buf[KEY_SIZE-1] = '0';
	buf[KEY_SIZE-0] = '\0';
	for ( i = 0 ; i < KEY_SIZE ; ++i )
		buf[i]=toupper(buf[i]);
	return gsh_strdup(buf);
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
sproxyd_put(struct scality_fsal_export* export,
	    const char *id,
	    char *buf,
	    size_t size)
{
	char url[MAX_URL_SIZE];
	CURL *curl = NULL;
	CURLcode curl_ret;
	long http_status;
	int ret = -1;

	curl = curl_easy_init();
	if ( NULL == curl ) {
		return ret;
	}

	buffer_t data = {
		.buf = buf,
		.size = size
	};

	snprintf(url, sizeof(url), "%s/%s", export->module->sproxyd_url, id);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_PUT, 1);
	curl_easy_setopt(curl, CURLOPT_INFILESIZE, data.size);
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
	curl_easy_setopt(curl, CURLOPT_READDATA, &data);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, noop_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300);

	LogDebug(COMPONENT_FSAL, "curl_easy_perform(%zu) begin", data.size);
	curl_ret = curl_easy_perform(curl);
	LogDebug(COMPONENT_FSAL, "curl_easy_perform(%zu) end", data.size);
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
	return ret;

}
