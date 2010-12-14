#ifndef _LOOKUP3_H
#define _LOOKUP3_H

#include <rbt_node.h>
#include <rbt_tree.h>
#include <pthread.h>
#include "RW_Lock.h"
#include "HashData.h"
#include "log_macros.h"

uint32_t Lookup3_hash_buff( char * str, uint32_t len ) ;
void Lookup3_hash_buff_dual( char * str, uint32_t len, uint32_t * pval1, uint32_t *pval2 ) ;


#endif                          /* _LOOKUP3_H */
