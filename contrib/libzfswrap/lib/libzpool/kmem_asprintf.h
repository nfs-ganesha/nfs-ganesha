#ifndef _KMEM_ASPRINTF
#define _KMEM_ASPRINTF

#ifdef _KERNEL
/* In opensolaris they have added the definitions of kmem_asprintf and
 * strfree in a system lib. I'll add them here from zfs_context, and let's
 * hope they won't be needed elsewhere... */
#define	strfree(str) kmem_free((str), strlen(str)+1)

extern char *kmem_asprintf(const char *fmt, ...);
#endif

#endif
