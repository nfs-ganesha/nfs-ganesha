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

/* redis_client.c
 */

#include <hiredis.h>

#include "scality_methods.h"
#include "random.h"

//redisContext are not thread-safe
static __thread redisContext *ctx__ = NULL;

//FIXME no context cleanup at exit

static void
reset_redis_context(void)
{
	if (ctx__)
		redisFree(ctx__);
	ctx__ = NULL;
}


static redisContext *
get_redis_context(void)
{

	if ( NULL != ctx__ )
		return ctx__;

	struct scality_fsal_export *export;
	export = container_of(op_ctx->fsal_export,
			      struct scality_fsal_export,
			      export);

	ctx__ = redisConnect(export->module->redis_host,
			     ntohs(export->module->redis_port));
	LogDebug(COMPONENT_FSAL,
		"REDIS CONNECT TO %s:%d", export->module->redis_host,
		ntohs(export->module->redis_port));
	
	if ( NULL == ctx__ ) {
		LogCrit(COMPONENT_FSAL,
			"Cannot allocate redis context");
	} else if ( ctx__->err ) {
		LogCrit(COMPONENT_FSAL,
			"Redis error: %s", ctx__->errstr);
		reset_redis_context();
		redisFree(ctx__);
		ctx__ = NULL;
	}
	return ctx__;
}

int
redis_get_object(char *buf, int buf_sz,
		 char *obj, int obj_sz)
{
	assert(buf_sz == V4_FH_OPAQUE_SIZE);
	int ret = 0;
	redisContext *ctx = get_redis_context();
	if ( NULL == ctx )
		return -1;

	redisReply *reply = redisCommand(ctx, "GET object:%b", buf, buf_sz);
	if ( NULL == reply ) {
		LogCrit(COMPONENT_FSAL,
			"Redis error: %s", ctx__->errstr);
		return -1;
	}
	switch(reply->type) {
	default:
		if ( REDIS_REPLY_ERROR == reply->type )
			LogCrit(COMPONENT_FSAL,
				"Redis reply error: %s", reply->str);
		else
			LogCrit(COMPONENT_FSAL,
				"Redis reply: unexpected data type");
	case REDIS_REPLY_NIL:
		ret = -1;
		break;
	case REDIS_REPLY_STRING:
		if ( reply->len >= obj_sz ) {
			LogCrit(COMPONENT_FSAL,
				"Object buffer too small");
			ret = -1;
			goto out;
		}
		struct scality_fsal_export *export;
		export = container_of(op_ctx->fsal_export,
				      struct scality_fsal_export,
				      export);
		size_t bucket_len = strlen(export->bucket);
		if ( 0 != strncmp(reply->str, export->bucket, bucket_len) ||
		     reply->str[bucket_len] != *S3_DELIMITER ) {
			LogCrit(COMPONENT_FSAL, "reply from wrong bucket: %s", reply->str);
			ret = -1;
			goto out;
		}
		memcpy(obj, reply->str+bucket_len+1, reply->len-bucket_len-1);
		obj[reply->len-bucket_len-1] = '\0';
	}
 out:
	freeReplyObject(reply);
	return ret;

}

int
redis_get_handle_key(const char *obj, char *buf, int buf_sz)
{
	assert(buf_sz == V4_FH_OPAQUE_SIZE);
	int ret = 0;
	redisContext *ctx = get_redis_context();
	if ( NULL == ctx )
		return -1;

	struct scality_fsal_export *export;
	export = container_of(op_ctx->fsal_export,
			      struct scality_fsal_export,
			      export);
	redisReply *reply = redisCommand(ctx, "GET handle:%s/%s", export->bucket, obj);
	if ( NULL == reply ) {
		LogCrit(COMPONENT_FSAL,
			"Redis error: %s", ctx__->errstr);
		return -1;
	}
	switch(reply->type) {
	default:
		if ( REDIS_REPLY_ERROR == reply->type )
			LogCrit(COMPONENT_FSAL,
				"Redis reply error: %s", reply->str);
		else
			LogCrit(COMPONENT_FSAL,
				"Redis reply: unexpected data type");
	case REDIS_REPLY_NIL:
		ret = -1;
		break;
	case REDIS_REPLY_STRING:
		if ( reply->len != buf_sz ) {
			LogCrit(COMPONENT_FSAL,
				"Redis reply: unexpected data size, got %d expected %d",
				reply->len, buf_sz);
			ret = -1;
		} else {
			memcpy(buf, reply->str, reply->len);
		}
	}
	freeReplyObject(reply);
	return ret;
}

void redis_remove(const char *obj)
{
	char buf[V4_FH_OPAQUE_SIZE];
	int ret;
	redisReply *reply;
	redisContext *ctx = get_redis_context();
	if ( NULL == ctx )
		return;
	ret = redis_get_handle_key(obj, buf, sizeof buf);
	if ( ret < 0 ) {
		return;
	}
	struct scality_fsal_export *export;
	export = container_of(op_ctx->fsal_export,
			      struct scality_fsal_export,
			      export);
	reply = redisCommand(ctx, "DEL handle:%s/%s", export->bucket, obj);
	if ( NULL == reply ) {
		LogCrit(COMPONENT_FSAL,
			"Redis error on delete: %s", ctx__->errstr);
		return;
	}
	switch(reply->type) {
	default:
		if ( REDIS_REPLY_ERROR == reply->type )
			LogCrit(COMPONENT_FSAL,
				"Redis reply error: %s", reply->str);
		else {
			LogCrit(COMPONENT_FSAL,
				"Redis reply: unexpected data type");
		}
	case REDIS_REPLY_INTEGER:
		break;
	}
	reply = redisCommand(ctx, "DEL object:%b", buf, sizeof buf);
	if ( NULL == reply ) {
		LogCrit(COMPONENT_FSAL,
			"Redis error on delete: %s", ctx__->errstr);
		return;
	}
	switch(reply->type) {
	default:
		if ( REDIS_REPLY_ERROR == reply->type )
			LogCrit(COMPONENT_FSAL,
				"Redis reply error: %s", reply->str);
		else {
			LogCrit(COMPONENT_FSAL,
				"Redis reply: unexpected data type");
		}
	case REDIS_REPLY_INTEGER:
		break;
	}
}

int
redis_create_handle_key(const char *obj, char *buf, int buf_sz)
{
	assert(buf_sz == V4_FH_OPAQUE_SIZE);

	redisContext *ctx = get_redis_context();
	if ( NULL == ctx )
		return -1;
	
	size_t n_bytes;	
	n_bytes = random_read(buf, buf_sz);
	if ( V4_FH_OPAQUE_SIZE != n_bytes ) {
		return -1;
	}

	struct scality_fsal_export *export;
	export = container_of(op_ctx->fsal_export,
			      struct scality_fsal_export,
			      export);
	redisReply *reply = redisCommand(ctx, "SET handle:%s/%s %b EX 86400",
					 export->bucket, obj,
					 buf, buf_sz);
	if ( NULL == reply ) {
		LogCrit(COMPONENT_FSAL,
			"Redis error: %s", ctx__->errstr);
		return -1;
	}

	int ret = 0;
	if ( REDIS_REPLY_ERROR == reply->type ) {
		LogCrit(COMPONENT_FSAL,
			"Redis reply error: %s", reply->str);
		ret = -1;
		goto out;
	}

	reply = redisCommand(ctx, "SET object:%b %s/%s EX 86400",
			     buf, buf_sz,
			     export->bucket, obj);
	if ( NULL == reply ) {
		LogCrit(COMPONENT_FSAL,
			"Redis error: %s", ctx__->errstr);
		return -1;
	}

	ret = 0;
	if ( REDIS_REPLY_ERROR == reply->type ) {
		LogCrit(COMPONENT_FSAL,
			"Redis reply error: %s", reply->str);
		ret = -1;
	}
	

 out:
	freeReplyObject(reply);
	return ret;
	
}
