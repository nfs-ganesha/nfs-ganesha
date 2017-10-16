/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 */

#include "cache.h"
#include <inttypes.h>
#include <stdio.h>

void cache_init(struct cache_t *cache)
{
	PTHREAD_RWLOCK_init(&cache->lock, NULL);
	cache->offset = 0;
	cache->total_length = 0;
	glist_init(&cache->head);
}

void cache_destroy(struct cache_t *cache)
{
	PTHREAD_RWLOCK_destroy(&cache->lock);
}

int slice_compare(struct glist_head *s1, struct glist_head *s2)
{
	struct slice_t *node1 = glist_entry(s1, struct slice_t, node);
	struct slice_t *node2 = glist_entry(s2, struct slice_t, node);

	if (node1->offset == node2->offset) {
		if (node1->length == node2->length) {
			return 0;
		} else if (node1->length > node2->length) {
			return 1;
		} else {
			return -1;
		}
	} else if (node1->offset > node2->offset) {
		return 1;
	} else {
		return -1;
	}
}

void cache_put(struct cache_t *cache, struct slice_t *slice)
{
	PTHREAD_RWLOCK_wrlock(&cache->lock);
	glist_insert_sorted(&cache->head, &slice->node, slice_compare);
	cache->total_length += slice->length;
	PTHREAD_RWLOCK_unlock(&cache->lock);
}

int cache_empty(struct cache_t *cache)
{
	PTHREAD_RWLOCK_rdlock(&cache->lock);
	int result = glist_empty(&cache->head);

	PTHREAD_RWLOCK_unlock(&cache->lock);
	return result;
}

size_t cache_total_length_helper(struct cache_t *cache)
{
	size_t length = 0;
	struct glist_head *node = NULL;

	glist_for_each(node, &cache->head) {
		struct slice_t *slice = glist_entry(node, struct slice_t, node);

		length += slice->length;
	}
	return length;
}

size_t cache_total_length(struct cache_t *cache)
{
	PTHREAD_RWLOCK_rdlock(&cache->lock);
	size_t total_length = cache->total_length;

	PTHREAD_RWLOCK_unlock(&cache->lock);
	return total_length;
}

size_t cache_consecutive_length(struct cache_t *cache)
{
	PTHREAD_RWLOCK_rdlock(&cache->lock);
	size_t length = 0;
	struct glist_head *node = NULL;
	uint64_t last_offset = cache->offset;

	glist_for_each(node, &cache->head) {
		struct slice_t *slice = glist_entry(node, struct slice_t, node);

		if ((slice->offset <= last_offset) &&
				(slice->offset + slice->length
				 >= last_offset)) {
			last_offset = slice->offset + slice->length;
			length += slice->length;
		} else {
			break;
		}
	}
	PTHREAD_RWLOCK_unlock(&cache->lock);
	return length;
}

void cache_consecutive_get(struct cache_t *cache, struct cache_t *result)
{
	PTHREAD_RWLOCK_wrlock(&cache->lock);
	result->offset = cache->offset;
	glist_init(&result->head);

	uint64_t last_offset = cache->offset;
	struct glist_head *node = NULL;
	struct glist_head *last_node = NULL;

	glist_for_each(node, &cache->head) {
		struct slice_t *slice = glist_entry(node, struct slice_t, node);

		if ((slice->offset <= last_offset) &&
				(slice->offset + slice->length
				 >= last_offset)) {
			last_offset = slice->offset + slice->length;
			cache->offset = slice->offset + slice->length;
			last_node = node;
		} else {
			break;
		}
	}

	if (last_node != NULL) {
		if (last_node->next != &cache->head) {
			glist_split(&cache->head,
					&result->head, last_node->next);
		}
		glist_swap_lists(&cache->head, &result->head);

		/* recalculate total length */
		cache->total_length = cache_total_length_helper(cache);
		result->total_length = cache_total_length_helper(result);
	}
	PTHREAD_RWLOCK_unlock(&cache->lock);
}

void cache_print(struct cache_t *cache)
{
	PTHREAD_RWLOCK_rdlock(&cache->lock);
	printf("offset %" PRIu64 " total_length %zu ",
			cache->offset, cache->total_length);

	struct glist_head *node = NULL;

	glist_for_each(node, &cache->head) {
		struct slice_t *slice = glist_entry(node, struct slice_t, node);

		printf("[%" PRIu64 " %zu %p] ",
				slice->offset, slice->length, slice->data);
	}

	printf("\n");
	PTHREAD_RWLOCK_unlock(&cache->lock);
}
