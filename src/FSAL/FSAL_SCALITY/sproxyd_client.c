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

static ssize_t
read_through(struct scality_fsal_export* export,
	     struct scality_location *loc,
	     uint64_t offset,
	     size_t size, char *buf)
{
	LogDebug(COMPONENT_FSAL, "offset=%lu, size=%zu)", offset, size);
	size_t start = offset;
	size_t length = 0;
	stencil_byte_t operation = STENCIL_READ;
	assert(NULL != loc->content);
	assert(NULL != loc->stencil);
	
	while (start < size && start < loc->size) {
		operation = loc->stencil[start];
		while ( start+length < loc->size &&
			start+length < size &&
			loc->stencil[start+length] == operation )
			++length;
		switch (operation) {
		case STENCIL_READ: {
			if ( NULL != loc->key ) {
				char range[200];
				size_t frag_len;
				char *frag;
				snprintf(range, sizeof(range), "%zu-%zu",
					 start, start+length-1);
				int ret = sproxyd_get(export, loc->key,
						      range, &frag, &frag_len);
				if ( ret < 0 ) {
					LogCrit(COMPONENT_FSAL,
						"sproxyd GET failed for this"
						" key (%s) and range (%s)",
						loc->key, range);
					return -1;
				}
				if ( frag_len != length ) {
					LogWarn(COMPONENT_FSAL,
						"sproxyd short GET for this"
						" key (%s) and range (%s)",
						loc->key, range);
					length = frag_len;
				}
				memcpy(buf, frag, length);
				free(frag);
			}
			else {
				memset(buf, 0, length);
			}
			break;
		}
		case STENCIL_COPY: {
			memcpy(buf, &loc->content[start], length);
			break;
		}
		case STENCIL_ZERO:
			memset(buf, 0, length);
			break;
		}
		buf += length;
		start += length;
		length = 0;
	}
	return start;
}

/**
 * @brief Read a slice from a file.
 * the requested slice may overlap several parts and this function has
 * the responsibility to pick data from the right place, whether it is
 * from the dirty range of a chunk or from the storage.
 *
 * @param export - export definition from which to get sproxyd parameters
 * @param obj - object to read
 * @param offset - start position of the read within the file
 * @param size - size to read (also match the buffer size)
 * @param[out]  buf - buffer from the caller in which to put data
 */
int
sproxyd_read(struct scality_fsal_export* export,
	     struct scality_fsal_obj_handle *obj,
	     uint64_t offset,
	     size_t size, char *buf)
{
	LogDebug(COMPONENT_FSAL, "sproxyd_read(%s, offset=%lu, size=%zu)",
		 obj->object, offset, size);

	size_t last_end = 0;
	int ret = -1;
	int i = 0;
	struct avltree_node *node;

	for (node = avltree_first(&obj->locations) ;
	     node !=  NULL && size != 0;
	     node = avltree_next(node) ) {

		struct scality_location *loc;
		loc = avltree_container_of(node,
					   struct scality_location,
					   avltree_node);
		LogDebug(COMPONENT_FSAL,
			 "i: %d, "
			 "loc->start: %"PRIu64", "
			 "loc->size: %"PRIu64", ",
			 i, loc->start, loc->size);
		assert(last_end == loc->start);
		last_end = loc->start + loc->size;
		++i;
		assert(offset >= loc->start);
		if (offset >= loc->start + loc->size) {
			//better chance on next part
			continue;
		}

		size_t read_start = offset - loc->start;
		size_t read_size = loc->size - read_start;
		if ( read_size > size )
			read_size = size;
		if ( 0 == read_size ) {
			//is it even possible?
			continue;
		}
		ssize_t bytes_read;

		if ( NULL == loc->content || NULL == loc->stencil ) {
			assert(NULL == loc->content && NULL == loc->stencil);
			if ( NULL != loc->key ) {
				int ret;
				char range[200];
				char *frag;
				size_t frag_len;
				snprintf(range, sizeof range, "%zu-%zu",
					 read_start, read_start+read_size-1);
				ret = sproxyd_get(export, loc->key, range,
						  &frag, &frag_len);
				if ( ret < 0 ) {
					LogCrit(COMPONENT_FSAL,
						"sproxyd_get failed");
					{
						size_t len;
						int ret2 = sproxyd_head(export,
									loc->key,
									&len);
						if (ret2<0) {
							LogCrit(COMPONENT_FSAL,
								"head failed on %s",
								loc->key);
						}
						else {
							LogDebug(COMPONENT_FSAL,
								 "%s is %"PRIu64
								 " bytes",
								 loc->key,
								 len);
						}
					}
					ret =  -1;
					goto out;
				}
				bytes_read = frag_len;
				memcpy(buf, frag, frag_len);
				free(frag);
			}
			else {
				bytes_read = read_size;
				memset(buf, 0, bytes_read);
			}
		}
		else {
			bytes_read = read_through(export,
						  loc,
						  read_start, read_size, buf);
			if ( bytes_read < 0 ) {
				ret = -1;
				goto out;
			}
		}
		if ( bytes_read != read_size ) {
			LogCrit(COMPONENT_FSAL,
				"read size mismatch expected %zu, got %zu",
				read_size, bytes_read);
			ret = -1;
			goto out;
		}
		buf+=bytes_read;
		offset+=bytes_read;
		size-=bytes_read;
	}
	ret = 0;
 out:
	return ret;
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
	LogDebug(COMPONENT_FSAL,
		 "sproxyd_delete(%s)", id);
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
	LogDebug(COMPONENT_FSAL,
		"sproxyd_put(%s)", id);
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
