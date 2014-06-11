
#ifndef _HANDLE_MAPPING_INTERNAL_H
#define _HANDLE_MAPPING_INTERNAL_H

#include "hashtable.h"

int handle_mapping_hash_add(hash_table_t *p_hash, uint64_t object_id,
			    unsigned int handle_hash, const void *data,
			    uint32_t datalen);
int snprintmem(char *target, size_t tgt_size, const void *source,
	       size_t mem_size);
int sscanmem(void *target, size_t tgt_size, const char *str_source);

#endif
