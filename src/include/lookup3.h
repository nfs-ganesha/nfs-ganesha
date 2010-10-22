#ifndef _LOOKUP3_H
#define _LOOKUP3_H

#include <rbt_node.h>
#include <rbt_tree.h>
#include <pthread.h>
#include "RW_Lock.h"
#include "HashData.h"
#include "log_macros.h"

uint32_t Lookup3_hash_buff( char * str, uint32_t len ) ;

#endif                          /* _LOOKUP3_H */
