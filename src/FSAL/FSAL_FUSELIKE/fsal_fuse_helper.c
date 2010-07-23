#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"

/* Those functions are called from FUSE filesystems */

struct ganefuse_context *ganefuse_get_context(void)
{
  fusefsal_op_context_t *p_ctx;

  p_ctx = fsal_get_thread_context();

  if(p_ctx == NULL)
    return NULL;

  return &(p_ctx->ganefuse_context);
}
