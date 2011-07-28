#ifdef _KERNEL
#include <sys/zfs_context.h>
#include <stdarg.h>

char *
kmem_asprintf(const char *fmt, ...)
{
	int size;
	va_list adx;
	char *buf;

	va_start(adx, fmt);
	size = vsnprintf(NULL, 0, fmt, adx) + 1;
	va_end(adx);

	buf = kmem_alloc(size, KM_SLEEP);

	va_start(adx, fmt);
	size = vsnprintf(buf, size, fmt, adx);
	va_end(adx);

	return (buf);
}

#endif
