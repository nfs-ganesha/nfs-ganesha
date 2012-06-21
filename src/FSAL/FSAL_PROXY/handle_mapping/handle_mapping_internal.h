
#ifndef _HANDLE_MAPPING_INTERNAL_H
#define _HANDLE_MAPPING_INTERNAL_H

#include "HashTable.h"

int handle_mapping_hash_add(hash_table_t * p_hash,
                            uint64_t object_id,
                            unsigned int handle_hash, const nfs_fh4 * p_handle);

#endif
