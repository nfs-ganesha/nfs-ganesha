#include "config.h"
#include <stddef.h>
#include <urcu/ref.h>
#include "gsh_refstr.h"
#include "abstract_mem.h"
#include "gsh_list.h"

struct gsh_refstr *gsh_refstr_alloc(size_t len)
{
	struct gsh_refstr *gr;

	gr = gsh_malloc(sizeof(*gr) + len);
	urcu_ref_init(&gr->gr_ref);
	return gr;
}

void gsh_refstr_release(struct urcu_ref *ref)
{
	struct gsh_refstr *gr = container_of(ref, struct gsh_refstr, gr_ref);

	gsh_free(gr);
}
