/*
 * vim:expandtab:shiftwidth=4:tabstop=8:
 */

/**
 * Common tools for printing, parsing, ....
 *
 *
 */

#ifndef _COMMON_UTILS_H
#define _COMMON_UTILS_H

#include <sys/types.h>          /* for caddr_t */

/**
 * BUILD_BUG_ON - break compile if a condition is true.
 * @condition: the condition which the compiler should know is false.
 *
 * If you have some code which relies on certain constants being equal, or
 * other compile-time-evaluated condition, you should use BUILD_BUG_ON to
 * detect if someone changes it.
 *
 * The implementation uses gcc's reluctance to create a negative array, but
 * gcc (as of 4.4) only emits that error for obvious cases (eg. not arguments
 * to inline functions).  So as a fallback we use the optimizer; if it can't
 * prove the condition is false, it will cause a link error on the undefined
 * "__build_bug_on_failed".  This error message can be harder to track down
 * though, hence the two different methods.
 *
 * Blatantly stolen from kernel source, include/linux/kernel.h:651
 */
#ifndef __OPTIMIZE__
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#else
extern int __build_bug_on_failed;
#define BUILD_BUG_ON(condition)                                 \
        do {                                                    \
                ((void)sizeof(char[1 - 2*!!(condition)]));      \
                if (condition) __build_bug_on_failed = 1;       \
        } while(0)
#endif


/* Most machines scandir callback requires a const. But not all */
#define SCANDIR_CONST           const

/* Most machines have mntent.h. */
#define HAVE_MNTENT_H           1

/**
 * This function converts a string to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A negative value on error.
 *         Else, the converted integer.
 */
int s_read_int(char *str);

/**
 * This function converts an octal to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A negative value on error.
 *         Else, the converted integer.
 */
int s_read_octal(char *str);

/**
 * This function converts a string to an integer.
 *
 * \param str (in char *) The string to be converted.
 *
 * \return A non null value on error.
 *         Else, 0.
 */
int s_read_int64(char *str, unsigned long long *out64);

int s_read_size(char *str, size_t * p_size);

/**
 * string to boolean convertion.
 * \return 1 for TRUE, 0 for FALSE, -1 on error
 */
int StrToBoolean(const char *str);

/**
 * snprintmem:
 * Print the content of a handle, a cookie,...
 * to an hexa string.
 *
 * \param target (output):
 *        The target buffer where memory is to be printed in ASCII.
 * \param tgt_size (input):
 *        Size (in bytes) of the target buffer.
 * \param source (input):
 *        The buffer to be printed.
 * \param mem_size (input):
 *        Size of the buffer to be printed.
 *
 * \return The number of bytes written in the target buffer.
 */
int snprintmem(char *target, int tgt_size, caddr_t source, int mem_size);

/**
 * snscanmem:
 * Read the content of a string and convert it to a handle, a cookie,...
 *
 * \param target (output):
 *        The target address where memory is to be written.
 * \param tgt_size (input):
 *        Size (in bytes) of the target memory buffer.
 * \param str_source (input):
 *        A hexadecimal string that represents
 *        the data to be stored into memory.
 *
 * \return - The number of bytes read in the source string.
 *         - -1 on error.
 */
int sscanmem(caddr_t target, int tgt_size, const char *str_source);

/* String parsing functions */

int find_space(char c);
int find_comma(char c);
int find_colon(char c);
int find_endLine(char c);
int find_slash(char c);

#ifndef HAVE_STRLCAT
extern size_t strlcat(char *dst, const char *src, size_t siz);
#endif

#ifndef HAVE_STRLCPY
extern size_t strlcpy(char *dst, const char *src, size_t siz);
#endif

#ifndef HAVE_STRNLEN
#define strnlen(a,b)            gsh_strnlen(a,b)
extern size_t gsh_strnlen(const char *s, size_t max);   /* prefix with gsh_ to prevent library conflict -- will fix properly with new build system */
#endif

#if defined(__APPLE__)
#  define clock_gettime(a,ts)     portable_clock_gettime(ts)
extern int portable_clock_gettime(struct timespec *ts);
#  define pthread_yield()         pthread_yield_np()
#  undef SCANDIR_CONST
#  define SCANDIR_CONST           
#  undef HAVE_MNTENT_H
#endif

/* My habit with mutex */
#define P( _mutex_ ) pthread_mutex_lock( &_mutex_ )
#define V( _mutex_ ) pthread_mutex_unlock( &_mutex_ )


#define PTHREAD_RWLOCK_wrlock(state_lock)                           \
  do {                                                              \
       int rc;                                                      \
                                                                    \
       LogFullDebug(COMPONENT_RW_LOCK, "get wr lock %p", state_lock);   \
       rc = pthread_rwlock_wrlock(state_lock);                      \
       if (rc == 0)                                                 \
         LogFullDebug(COMPONENT_RW_LOCK, "got wr lock %p", state_lock); \
       else                                                         \
         LogCrit(COMPONENT_RW_LOCK, "error %d wr lock %p", rc, state_lock); \
     } while(0)                                                      \

#define PTHREAD_RWLOCK_rdlock(state_lock)                           \
  do {                                                              \
       int rc;                                                      \
                                                                    \
       LogFullDebug(COMPONENT_RW_LOCK, "get rd lock %p", state_lock);   \
       rc = pthread_rwlock_rdlock(state_lock);                      \
       if (rc == 0)                                                 \
         LogFullDebug(COMPONENT_RW_LOCK, "got rd lock %p", state_lock); \
       else                                                         \
         LogCrit(COMPONENT_RW_LOCK, "error %d rd lock %p", rc, state_lock); \
     } while(0)                                                      \

#define PTHREAD_RWLOCK_unlock(state_lock)                           \
  do {                                                              \
       int rc;                                                      \
                                                                    \
       rc = pthread_rwlock_unlock(state_lock);                      \
       if (rc == 0)                                                 \
         LogFullDebug(COMPONENT_RW_LOCK, "unlock %p", state_lock);      \
       else                                                         \
         LogCrit(COMPONENT_RW_LOCK, "error %d unlock %p", rc, state_lock); \
     } while(0)                                                      \


#endif
