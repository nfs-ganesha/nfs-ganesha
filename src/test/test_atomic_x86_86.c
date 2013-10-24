#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#if 0
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
#endif

#if 1
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
#endif

#define inline

#include "../include/atomic_x86_64.h"

/* ADD 64 */
int64_t x1__atomic_postadd_int64_t(int64_t *p, uint64_t v)
{
	return __sync_fetch_and_add(p, v);
}

int64_t x2__atomic_postadd_int64_t(int64_t *p, uint64_t v)
{
	return atomic_postadd_int64_t(p, v);
}

int64_t x1__atomic_add_int64_t(int64_t *p, uint64_t v)
{
	return __sync_add_and_fetch(p, v);
}

int64_t x2__atomic_add_int64_t(int64_t *p, uint64_t v)
{
	return atomic_add_int64_t(p, v);
}

/* SUB 64 */
int64_t x1__atomic_postsub_int64_t(int64_t *p, uint64_t v)
{
	return __sync_fetch_and_sub(p, v);
}

int64_t x2__atomic_postsub_int64_t(int64_t *p, uint64_t v)
{
	return atomic_postsub_int64_t(p, v);
}

int64_t x1__atomic_sub_int64_t(int64_t *p, uint64_t v)
{
	return __sync_sub_and_fetch(p, v);
}

int64_t x2__atomic_sub_int64_t(int64_t *p, uint64_t v)
{
	return atomic_sub_int64_t(p, v);
}

/* ADD U64 */
uint64_t x1__atomic_postadd_uint64_t(uint64_t *p, uint64_t v)
{
	return __sync_fetch_and_add(p, v);
}

uint64_t x2__atomic_postadd_uint64_t(uint64_t *p, uint64_t v)
{
	return atomic_postadd_uint64_t(p, v);
}

uint64_t x1__atomic_add_uint64_t(uint64_t *p, uint64_t v)
{
	return __sync_add_and_fetch(p, v);
}

uint64_t x2__atomic_add_uint64_t(uint64_t *p, uint64_t v)
{
	return atomic_add_uint64_t(p, v);
}

/* SUB U64 */
uint64_t x1__atomic_postsub_uint64_t(uint64_t *p, uint64_t v)
{
	return __sync_fetch_and_sub(p, v);
}

uint64_t x2__atomic_postsub_uint64_t(uint64_t *p, uint64_t v)
{
	return atomic_postsub_uint64_t(p, v);
}

uint64_t x1__atomic_sub_uint64_t(uint64_t *p, uint64_t v)
{
	return __sync_sub_and_fetch(p, v);
}

uint64_t x2__atomic_sub_uint64_t(uint64_t *p, uint64_t v)
{
	return atomic_sub_uint64_t(p, v);
}

/* OR 64 */
uint64_t x1__atomic_postset_uint64_t_bits(uint64_t *p, uint64_t v)
{
	return __sync_fetch_and_or(p, v);
}

uint64_t x2__atomic_postset_uint64_t_bits(uint64_t *p, uint64_t v)
{
	return atomic_postset_uint64_t_bits(p, v);
}

uint64_t x1__atomic_set_uint64_t_bits(uint64_t *p, uint64_t v)
{
	return __sync_or_and_fetch(p, v);
}

uint64_t x2__atomic_set_uint64_t_bits(uint64_t *p, uint64_t v)
{
	return atomic_set_uint64_t_bits(p, v);
}

/* AND 64 */
uint64_t x1__atomic_postclear_uint64_t_bits(uint64_t *p, uint64_t v)
{
	return __sync_fetch_and_and(p, ~v);
}

uint64_t x2__atomic_postclear_uint64_t_bits(uint64_t *p, uint64_t v)
{
	return atomic_postclear_uint64_t_bits(p, v);
}

uint64_t x1__atomic_clear_uint64_t_bits(uint64_t *p, uint64_t v)
{
	return __sync_and_and_fetch(p, ~v);
}

uint64_t x2__atomic_clear_uint64_t_bits(uint64_t *p, uint64_t v)
{
	return atomic_clear_uint64_t_bits(p, v);
}

/* ADD 32 */
int32_t x1__atomic_postadd_int32_t(int32_t *p, uint32_t v)
{
	return __sync_fetch_and_add(p, v);
}

int32_t x2__atomic_postadd_int32_t(int32_t *p, uint32_t v)
{
	return atomic_postadd_int32_t(p, v);
}

int32_t x1__atomic_add_int32_t(int32_t *p, uint32_t v)
{
	return __sync_add_and_fetch(p, v);
}

int32_t x2__atomic_add_int32_t(int32_t *p, uint32_t v)
{
	return atomic_add_int32_t(p, v);
}

/* SUB 32 */
int32_t x1__atomic_postsub_int32_t(int32_t *p, uint32_t v)
{
	return __sync_fetch_and_sub(p, v);
}

int32_t x2__atomic_postsub_int32_t(int32_t *p, uint32_t v)
{
	return atomic_postsub_int32_t(p, v);
}

int32_t x1__atomic_sub_int32_t(int32_t *p, uint32_t v)
{
	return __sync_sub_and_fetch(p, v);
}

int32_t x2__atomic_sub_int32_t(int32_t *p, uint32_t v)
{
	return atomic_sub_int32_t(p, v);
}

/* ADD U32 */
uint32_t x1__atomic_postadd_uint32_t(uint32_t *p, uint32_t v)
{
	return __sync_fetch_and_add(p, v);
}

uint32_t x2__atomic_postadd_uint32_t(uint32_t *p, uint32_t v)
{
	return atomic_postadd_uint32_t(p, v);
}

uint32_t x1__atomic_add_uint32_t(uint32_t *p, uint32_t v)
{
	return __sync_add_and_fetch(p, v);
}

uint32_t x2__atomic_add_uint32_t(uint32_t *p, uint32_t v)
{
	return atomic_add_uint32_t(p, v);
}

/* SUB U32 */
uint32_t x1__atomic_postsub_uint32_t(uint32_t *p, uint32_t v)
{
	return __sync_fetch_and_sub(p, v);
}

uint32_t x2__atomic_postsub_uint32_t(uint32_t *p, uint32_t v)
{
	return atomic_postsub_uint32_t(p, v);
}

uint32_t x1__atomic_sub_uint32_t(uint32_t *p, uint32_t v)
{
	return __sync_sub_and_fetch(p, v);
}

uint32_t x2__atomic_sub_uint32_t(uint32_t *p, uint32_t v)
{
	return atomic_sub_uint32_t(p, v);
}

/* OR 32 */
uint32_t x1__atomic_postset_uint32_t_bits(uint32_t *p, uint32_t v)
{
	return __sync_fetch_and_or(p, v);
}

uint32_t x2__atomic_postset_uint32_t_bits(uint32_t *p, uint32_t v)
{
	return atomic_postset_uint32_t_bits(p, v);
}

uint32_t x1__atomic_set_uint32_t_bits(uint32_t *p, uint32_t v)
{
	return __sync_or_and_fetch(p, v);
}

uint32_t x2__atomic_set_uint32_t_bits(uint32_t *p, uint32_t v)
{
	return atomic_set_uint32_t_bits(p, v);
}

/* AND 32 */
uint32_t x1__atomic_postclear_uint32_t_bits(uint32_t *p, uint32_t v)
{
	return __sync_fetch_and_and(p, ~v);
}

uint32_t x2__atomic_postclear_uint32_t_bits(uint32_t *p, uint32_t v)
{
	return atomic_postclear_uint32_t_bits(p, v);
}

uint32_t x1__atomic_clear_uint32_t_bits(uint32_t *p, uint32_t v)
{
	return __sync_and_and_fetch(p, ~v);
}

uint32_t x2__atomic_clear_uint32_t_bits(uint32_t *p, uint32_t v)
{
	return atomic_clear_uint32_t_bits(p, v);
}

/* ADD 16 */
int16_t x1__atomic_postadd_int16_t(int16_t *p, uint16_t v)
{
	return __sync_fetch_and_add(p, v);
}

int16_t x2__atomic_postadd_int16_t(int16_t *p, uint16_t v)
{
	return atomic_postadd_int16_t(p, v);
}

int16_t x1__atomic_add_int16_t(int16_t *p, uint16_t v)
{
	return __sync_add_and_fetch(p, v);
}

int16_t x2__atomic_add_int16_t(int16_t *p, uint16_t v)
{
	return atomic_add_int16_t(p, v);
}

/* SUB 16 */
int16_t x1__atomic_postsub_int16_t(int16_t *p, uint16_t v)
{
	return __sync_fetch_and_sub(p, v);
}

int16_t x2__atomic_postsub_int16_t(int16_t *p, uint16_t v)
{
	return atomic_postsub_int16_t(p, v);
}

int16_t x1__atomic_sub_int16_t(int16_t *p, uint16_t v)
{
	return __sync_sub_and_fetch(p, v);
}

int16_t x2__atomic_sub_int16_t(int16_t *p, uint16_t v)
{
	return atomic_sub_int16_t(p, v);
}

/* ADD U16 */
uint16_t x1__atomic_postadd_uint16_t(uint16_t *p, uint16_t v)
{
	return __sync_fetch_and_add(p, v);
}

uint16_t x2__atomic_postadd_uint16_t(uint16_t *p, uint16_t v)
{
	return atomic_postadd_uint16_t(p, v);
}

uint16_t x1__atomic_add_uint16_t(uint16_t *p, uint16_t v)
{
	return __sync_add_and_fetch(p, v);
}

uint16_t x2__atomic_add_uint16_t(uint16_t *p, uint16_t v)
{
	return atomic_add_uint16_t(p, v);
}

/* SUB U16 */
uint16_t x1__atomic_postsub_uint16_t(uint16_t *p, uint16_t v)
{
	return __sync_fetch_and_sub(p, v);
}

uint16_t x2__atomic_postsub_uint16_t(uint16_t *p, uint16_t v)
{
	return atomic_postsub_uint16_t(p, v);
}

uint16_t x1__atomic_sub_uint16_t(uint16_t *p, uint16_t v)
{
	return __sync_sub_and_fetch(p, v);
}

uint16_t x2__atomic_sub_uint16_t(uint16_t *p, uint16_t v)
{
	return atomic_sub_uint16_t(p, v);
}

/* OR 16 */
uint16_t x1__atomic_postset_uint16_t_bits(uint16_t *p, uint16_t v)
{
	return __sync_fetch_and_or(p, v);
}

uint16_t x2__atomic_postset_uint16_t_bits(uint16_t *p, uint16_t v)
{
	return atomic_postset_uint16_t_bits(p, v);
}

uint16_t x1__atomic_set_uint16_t_bits(uint16_t *p, uint16_t v)
{
	return __sync_or_and_fetch(p, v);
}

uint16_t x2__atomic_set_uint16_t_bits(uint16_t *p, uint16_t v)
{
	return atomic_set_uint16_t_bits(p, v);
}

/* AND 16 */
uint16_t x1__atomic_postclear_uint16_t_bits(uint16_t *p, uint16_t v)
{
	return __sync_fetch_and_and(p, ~v);
}

uint16_t x2__atomic_postclear_uint16_t_bits(uint16_t *p, uint16_t v)
{
	return atomic_postclear_uint16_t_bits(p, v);
}

uint16_t x1__atomic_clear_uint16_t_bits(uint16_t *p, uint16_t v)
{
	return __sync_and_and_fetch(p, ~v);
}

uint16_t x2__atomic_clear_uint16_t_bits(uint16_t *p, uint16_t v)
{
	return atomic_clear_uint16_t_bits(p, v);
}

/* ADD 8 */
int8_t x1__atomic_postadd_int8_t(int8_t *p, uint8_t v)
{
	return __sync_fetch_and_add(p, v);
}

int8_t x2__atomic_postadd_int8_t(int8_t *p, uint8_t v)
{
	return atomic_postadd_int8_t(p, v);
}

int8_t x1__atomic_add_int8_t(int8_t *p, uint8_t v)
{
	return __sync_add_and_fetch(p, v);
}

int8_t x2__atomic_add_int8_t(int8_t *p, uint8_t v)
{
	return atomic_add_int8_t(p, v);
}

/* SUB 8 */
int8_t x1__atomic_postsub_int8_t(int8_t *p, uint8_t v)
{
	return __sync_fetch_and_sub(p, v);
}

int8_t x2__atomic_postsub_int8_t(int8_t *p, uint8_t v)
{
	return atomic_postsub_int8_t(p, v);
}

int8_t x1__atomic_sub_int8_t(int8_t *p, uint8_t v)
{
	return __sync_sub_and_fetch(p, v);
}

int8_t x2__atomic_sub_int8_t(int8_t *p, uint8_t v)
{
	return atomic_sub_int8_t(p, v);
}

/* ADD U8 */
uint8_t x1__atomic_postadd_uint8_t(uint8_t *p, uint8_t v)
{
	return __sync_fetch_and_add(p, v);
}

uint8_t x2__atomic_postadd_uint8_t(uint8_t *p, uint8_t v)
{
	return atomic_postadd_uint8_t(p, v);
}

uint8_t x1__atomic_add_uint8_t(uint8_t *p, uint8_t v)
{
	return __sync_add_and_fetch(p, v);
}

uint8_t x2__atomic_add_uint8_t(uint8_t *p, uint8_t v)
{
	return atomic_add_uint8_t(p, v);
}

/* SUB U8 */
uint8_t x1__atomic_postsub_uint8_t(uint8_t *p, uint8_t v)
{
	return __sync_fetch_and_sub(p, v);
}

uint8_t x2__atomic_postsub_uint8_t(uint8_t *p, uint8_t v)
{
	return atomic_postsub_uint8_t(p, v);
}

uint8_t x1__atomic_sub_uint8_t(uint8_t *p, uint8_t v)
{
	return __sync_sub_and_fetch(p, v);
}

uint8_t x2__atomic_sub_uint8_t(uint8_t *p, uint8_t v)
{
	return atomic_sub_uint8_t(p, v);
}

/* OR 8 */
uint8_t x1__atomic_postset_uint8_t_bits(uint8_t *p, uint8_t v)
{
	return __sync_fetch_and_or(p, v);
}

uint8_t x2__atomic_postset_uint8_t_bits(uint8_t *p, uint8_t v)
{
	return atomic_postset_uint8_t_bits(p, v);
}

uint8_t x1__atomic_set_uint8_t_bits(uint8_t *p, uint8_t v)
{
	return __sync_or_and_fetch(p, v);
}

uint8_t x2__atomic_set_uint8_t_bits(uint8_t *p, uint8_t v)
{
	return atomic_set_uint8_t_bits(p, v);
}

/* AND 8 */
uint8_t x1__atomic_postclear_uint8_t_bits(uint8_t *p, uint8_t v)
{
	return __sync_fetch_and_and(p, ~v);
}

uint8_t x2__atomic_postclear_uint8_t_bits(uint8_t *p, uint8_t v)
{
	return atomic_postclear_uint8_t_bits(p, v);
}

uint8_t x1__atomic_clear_uint8_t_bits(uint8_t *p, uint8_t v)
{
	return __sync_and_and_fetch(p, ~v);
}

uint8_t x2__atomic_clear_uint8_t_bits(uint8_t *p, uint8_t v)
{
	return atomic_clear_uint8_t_bits(p, v);
}

int main(int argc, char **argv)
{
	int i;

	int64_t p1 = 0;
	uint64_t p2 = 0;
	int32_t p3 = 0;
	uint32_t p4 = 0;
	int16_t p5 = 0;
	uint16_t p6 = 0;
	int8_t p7 = 0;
	uint16_t p8 = 0;

	atomic_store_int64_t(&p1, 55);
	atomic_store_uint64_t(&p2, 55);
	atomic_store_int32_t(&p3, 55);
	atomic_store_uint32_t(&p4, 55);
	atomic_store_int16_t(&p5, 55);
	atomic_store_uint16_t(&p6, 55);
	atomic_store_int8_t(&p7, 55);
	atomic_store_uint8_t(&p8, 55);

	assert(p1 == 55);
	assert(p2 == 55);
	assert(p3 == 55);
	assert(p4 == 55);
	assert(p5 == 55);
	assert(p6 == 55);
	assert(p7 == 55);
	assert(p8 == 55);

#define test(t,f) \
	do {\
		for (i = 0; i < 500000; i++) { \
			t res1; \
			t res2; \
			t x1; \
			t x2; \
			t f1; \
			t f2; \
			\
			res1 = res2 = random(); \
			x1 = x2 = random(); \
			f1 = f2 = random(); \
			\
			x1 = x1__##f(&res1, f1); \
			x2 = x2__##f(&res2, f2); \
			\
			fprintf(stderr, "%d:  %lld == %lld, %lld == %lld\n", \
			       __LINE__, (long long) x1, \
			       (long long) x2, (long long) res1, \
			       (long long) res2); \
			assert(x1 == x2); \
			assert(res1 == res2); \
		} \
	} while (0)

	test(int64_t, atomic_postadd_int64_t);
	test(int64_t, atomic_add_int64_t);
	test(int64_t, atomic_postsub_int64_t);
	test(int64_t, atomic_sub_int64_t);

	test(uint64_t, atomic_postadd_uint64_t);
	test(uint64_t, atomic_add_uint64_t);
	test(uint64_t, atomic_postsub_uint64_t);
	test(uint64_t, atomic_sub_uint64_t);

	test(uint64_t, atomic_postset_uint64_t_bits);
	test(uint64_t, atomic_set_uint64_t_bits);
	test(uint64_t, atomic_postclear_uint64_t_bits);
	test(uint64_t, atomic_clear_uint64_t_bits);

	test(int32_t, atomic_postadd_int32_t);
	test(int32_t, atomic_add_int32_t);
	test(int32_t, atomic_postsub_int32_t);
	test(int32_t, atomic_sub_int32_t);

	test(uint32_t, atomic_postadd_uint32_t);
	test(uint32_t, atomic_add_uint32_t);
	test(uint32_t, atomic_postsub_uint32_t);
	test(uint32_t, atomic_sub_uint32_t);

	test(uint32_t, atomic_postset_uint32_t_bits);
	test(uint32_t, atomic_set_uint32_t_bits);
	test(uint32_t, atomic_postclear_uint32_t_bits);
	test(uint32_t, atomic_clear_uint32_t_bits);

	test(int16_t, atomic_postadd_int16_t);
	test(int16_t, atomic_add_int16_t);
	test(int16_t, atomic_postsub_int16_t);
	test(int16_t, atomic_sub_int16_t);

	test(uint16_t, atomic_postadd_uint16_t);
	test(uint16_t, atomic_add_uint16_t);
	test(uint16_t, atomic_postsub_uint16_t);
	test(uint16_t, atomic_sub_uint16_t);

	test(uint16_t, atomic_postset_uint16_t_bits);
	test(uint16_t, atomic_set_uint16_t_bits);
	test(uint16_t, atomic_postclear_uint16_t_bits);
	test(uint16_t, atomic_clear_uint16_t_bits);

	test(int8_t, atomic_postadd_int8_t);
	test(int8_t, atomic_add_int8_t);
	test(int8_t, atomic_postsub_int8_t);
	test(int8_t, atomic_sub_int8_t);

	test(uint8_t, atomic_postadd_uint8_t);
	test(uint8_t, atomic_add_uint8_t);
	test(uint8_t, atomic_postsub_uint8_t);
	test(uint8_t, atomic_sub_uint8_t);

	test(uint8_t, atomic_postset_uint8_t_bits);
	test(uint8_t, atomic_set_uint8_t_bits);
	test(uint8_t, atomic_postclear_uint8_t_bits);
	test(uint8_t, atomic_clear_uint8_t_bits);

	return 0;
}
