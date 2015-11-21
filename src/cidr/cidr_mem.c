/*
 * Various libcidr memory-related functions
 */
#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "abstract_mem.h"
#include "cidr.h"

/* Allocate a struct cidr_addr */
CIDR *cidr_alloc(void)
{
	return gsh_calloc(1, sizeof(CIDR));
}

/* Duplicate a CIDR */
CIDR *cidr_dup(const CIDR * src)
{
	CIDR *toret;

	toret = cidr_alloc();

	memcpy(toret, src, sizeof(CIDR));

	return (toret);
}

/* Free a struct cidr_addr */
void cidr_free(CIDR * tofree)
{
	gsh_free(tofree);
}
