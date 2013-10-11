/*
 * Copyright © 2012 Paul Sheer
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

#define atomic_fetch_size_t(a)          atomic_postadd_size_t(a,0)
#define atomic_fetch_int64_t(a)         atomic_postadd_int64_t(a,0)
#define atomic_fetch_uint64_t(a)        atomic_postadd_uint64_t(a,0)
#define atomic_fetch_int32_t(a)         atomic_postadd_int32_t(a,0)
#define atomic_fetch_uint32_t(a)        atomic_postadd_uint32_t(a,0)
#define atomic_fetch_int16_t(a)         atomic_postadd_int16_t(a,0)
#define atomic_fetch_uint16_t(a)        atomic_postadd_uint16_t(a,0)
#define atomic_fetch_int8_t(a)          atomic_postadd_int8_t(a,0)
#define atomic_fetch_uint8_t(a)         atomic_postadd_uint8_t(a,0)

#if defined(__LP64__) || defined(__LP64)

/* ADD U64 */
static inline size_t atomic_postadd_size_t(size_t * p, size_t v)
{
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline size_t atomic_add_size_t(size_t * p, size_t v)
{
	size_t _v = v;
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* SUB U64 */
static inline size_t atomic_postsub_size_t(size_t * p, size_t v)
{
	v = -v;
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline size_t atomic_sub_size_t(size_t * p, size_t v)
{
	size_t _v;
	v = -v;
	_v = v;
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

#else

#error what is the sizeof your size_t?

#endif

/* STORE 64 */
static inline void atomic_store_int64_t(int64_t * p, int64_t v)
{
	asm volatile ("lock\n\txchgq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
}

/* ADD 64 */
static inline int64_t atomic_postadd_int64_t(int64_t * p, uint64_t v)
{
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline int64_t atomic_add_int64_t(int64_t * p, uint64_t v)
{
	uint64_t _v = v;
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* SUB 64 */
static inline int64_t atomic_postsub_int64_t(int64_t * p, uint64_t v)
{
	v = -v;
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline int64_t atomic_sub_int64_t(int64_t * p, uint64_t v)
{
	uint64_t _v;
	v = -v;
	_v = v;
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* STORE U64 */
static inline void atomic_store_uint64_t(uint64_t * p, uint64_t v)
{
	asm volatile ("lock\n\txchgq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
}

/* ADD U64 */
static inline uint64_t atomic_postadd_uint64_t(uint64_t * p, uint64_t v)
{
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline uint64_t atomic_add_uint64_t(uint64_t * p, uint64_t v)
{
	uint64_t _v = v;
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* SUB U64 */
static inline uint64_t atomic_postsub_uint64_t(uint64_t * p, uint64_t v)
{
	v = -v;
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline uint64_t atomic_sub_uint64_t(uint64_t * p, uint64_t v)
{
	uint64_t _v;
	v = -v;
	_v = v;
	asm volatile ("lock\n\txaddq %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* OR 64 */
static inline uint64_t atomic_postset_uint64_t_bits(uint64_t * p, uint64_t v)
{
	uint64_t t;
	asm volatile ("\n\tmovq %0, %1\n\
1:\n\
\tmovq %1, %%rcx\n\
\tmovq %1, %%rdx\n\
\torq %2, %%rdx\n\
\tlock cmpxchgq	%%rdx, %0\n\
\tjne 1b\n\
\tmovq %%rcx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%rcx", "%rdx");
	return t;
}

static inline uint64_t atomic_set_uint64_t_bits(uint64_t * p, uint64_t v)
{
	uint64_t t;
	asm volatile ("\n\tmovq %0, %1\n\
1:\n\
\tmovq %1, %%rdx\n\
\torq %2, %%rdx\n\
\tlock cmpxchgq	%%rdx, %0\n\
\tjne 1b\n\
\tmovq %%rdx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%rdx");
	return t;
}

/* AND 64 */
static inline uint64_t atomic_postclear_uint64_t_bits(uint64_t * p, uint64_t v)
{
	uint64_t t;
	v = ~v;
	asm volatile ("\n\tmovq %0, %1\n\
1:\n\
\tmovq %1, %%rcx\n\
\tmovq %1, %%rdx\n\
\tandq %2, %%rdx\n\
\tlock cmpxchgq	%%rdx, %0\n\
\tjne 1b\n\
\tmovq %%rcx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%rcx", "%rdx");
	return t;
}

static inline uint64_t atomic_clear_uint64_t_bits(uint64_t * p, uint64_t v)
{
	uint64_t t;
	v = ~v;
	asm volatile ("\n\tmovq %0, %1\n\
1:\n\
\tmovq %1, %%rdx\n\
\tandq %2, %%rdx\n\
\tlock cmpxchgq	%%rdx, %0\n\
\tjne 1b\n\
\tmovq %%rdx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%rdx");
	return t;
}

/* STORE 32 */
static inline void atomic_store_int32_t(int32_t * p, int32_t v)
{
	*p = v;			/* is atomic on Intel */
}

/* ADD 32 */
static inline int32_t atomic_postadd_int32_t(int32_t * p, uint32_t v)
{
	asm volatile ("lock\n\txaddl %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline int32_t atomic_add_int32_t(int32_t * p, uint32_t v)
{
	uint32_t _v = v;
	asm volatile ("lock\n\txaddl %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* SUB 32 */
static inline int32_t atomic_postsub_int32_t(int32_t * p, uint32_t v)
{
	v = -v;
	asm volatile ("lock\n\txaddl %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline int32_t atomic_sub_int32_t(int32_t * p, uint32_t v)
{
	uint32_t _v;
	v = -v;
	_v = v;
	asm volatile ("lock\n\txaddl %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* STORE U32 */
static inline void atomic_store_uint32_t(uint32_t * p, uint32_t v)
{
	*p = v;			/* is atomic on Intel */
}

/* ADD U32 */
static inline uint32_t atomic_postadd_uint32_t(uint32_t * p, uint32_t v)
{
	asm volatile ("lock\n\txaddl %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline uint32_t atomic_add_uint32_t(uint32_t * p, uint32_t v)
{
	uint32_t _v = v;
	asm volatile ("lock\n\txaddl %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* SUB U32 */
static inline uint32_t atomic_postsub_uint32_t(uint32_t * p, uint32_t v)
{
	v = -v;
	asm volatile ("lock\n\txaddl %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline uint32_t atomic_sub_uint32_t(uint32_t * p, uint32_t v)
{
	uint32_t _v;
	v = -v;
	_v = v;
	asm volatile ("lock\n\txaddl %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* OR 32 */
static inline uint32_t atomic_postset_uint32_t_bits(uint32_t * p, uint32_t v)
{
	uint32_t t;
	asm volatile ("\n\tmovl %0, %1\n\
1:\n\
\tmovl %1, %%ecx\n\
\tmovl %1, %%edx\n\
\torl %2, %%edx\n\
\tlock cmpxchgl	%%edx, %0\n\
\tjne 1b\n\
\tmovl %%ecx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%ecx", "%edx");
	return t;
}

static inline uint32_t atomic_set_uint32_t_bits(uint32_t * p, uint32_t v)
{
	uint32_t t;
	asm volatile ("\n\tmovl %0, %1\n\
1:\n\
\tmovl %1, %%edx\n\
\torl %2, %%edx\n\
\tlock cmpxchgl	%%edx, %0\n\
\tjne 1b\n\
\tmovl %%edx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%edx");
	return t;
}

/* AND 32 */
static inline uint32_t atomic_postclear_uint32_t_bits(uint32_t * p, uint32_t v)
{
	uint32_t t;
	v = ~v;
	asm volatile ("\n\tmovl %0, %1\n\
1:\n\
\tmovl %1, %%ecx\n\
\tmovl %1, %%edx\n\
\tandl %2, %%edx\n\
\tlock cmpxchgl	%%edx, %0\n\
\tjne 1b\n\
\tmovl %%ecx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%ecx", "%edx");
	return t;
}

static inline uint32_t atomic_clear_uint32_t_bits(uint32_t * p, uint32_t v)
{
	uint32_t t;
	v = ~v;
	asm volatile ("\n\tmovl %0, %1\n\
1:\n\
\tmovl %1, %%edx\n\
\tandl %2, %%edx\n\
\tlock cmpxchgl	%%edx, %0\n\
\tjne 1b\n\
\tmovl %%edx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%edx");
	return t;
}

/* STORE 16 */
static inline void atomic_store_int16_t(int16_t * p, int16_t v)
{
	*p = v;			/* is atomic on Intel */
}

/* ADD 16 */
static inline int16_t atomic_postadd_int16_t(int16_t * p, uint16_t v)
{
	asm volatile ("lock\n\txaddw %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline int16_t atomic_add_int16_t(int16_t * p, uint16_t v)
{
	uint16_t _v = v;
	asm volatile ("lock\n\txaddw %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* SUB 16 */
static inline int16_t atomic_postsub_int16_t(int16_t * p, uint16_t v)
{
	v = -v;
	asm volatile ("lock\n\txaddw %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline int16_t atomic_sub_int16_t(int16_t * p, uint16_t v)
{
	uint16_t _v;
	v = -v;
	_v = v;
	asm volatile ("lock\n\txaddw %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* STORE U16 */
static inline void atomic_store_uint16_t(uint16_t * p, uint16_t v)
{
	*p = v;			/* is atomic on Intel */
}

/* ADD U16 */
static inline uint16_t atomic_postadd_uint16_t(uint16_t * p, uint16_t v)
{
	asm volatile ("lock\n\txaddw %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline uint16_t atomic_add_uint16_t(uint16_t * p, uint16_t v)
{
	uint16_t _v = v;
	asm volatile ("lock\n\txaddw %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* SUB U16 */
static inline uint16_t atomic_postsub_uint16_t(uint16_t * p, uint16_t v)
{
	v = -v;
	asm volatile ("lock\n\txaddw %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline uint16_t atomic_sub_uint16_t(uint16_t * p, uint16_t v)
{
	uint16_t _v;
	v = -v;
	_v = v;
	asm volatile ("lock\n\txaddw %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* OR 16 */
static inline uint16_t atomic_postset_uint16_t_bits(uint16_t * p, uint16_t v_)
{
	uint32_t t, v;
	v = v_;
	asm volatile ("\n\tmovzwl %0, %1\n\
1:\n\
\tmovl %1, %%ecx\n\
\tmovl %1, %%edx\n\
\torl %2, %%edx\n\
\tlock cmpxchgw	%%dx, %0\n\
\tjne 1b\n\
\tmovl %%ecx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%ecx", "%edx");
	return t;
}

static inline uint16_t atomic_set_uint16_t_bits(uint16_t * p, uint16_t v_)
{
	uint32_t t, v;
	v = v_;
	asm volatile ("\n\tmovzwl %0, %1\n\
1:\n\
\tmovl %1, %%edx\n\
\torl %2, %%edx\n\
\tlock cmpxchgw	%%dx, %0\n\
\tjne 1b\n\
\tmovl %%edx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%edx");
	return t;
}

/* AND 16 */
static inline uint16_t atomic_postclear_uint16_t_bits(uint16_t * p, uint16_t v_)
{
	uint32_t t, v;
	v = ~v_;
	asm volatile ("\n\tmovzwl %0, %1\n\
1:\n\
\tmovl %1, %%ecx\n\
\tmovl %1, %%edx\n\
\tandl %2, %%edx\n\
\tlock cmpxchgw	%%dx, %0\n\
\tjne 1b\n\
\tmovl %%ecx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%ecx", "%edx");
	return t;
}

static inline uint16_t atomic_clear_uint16_t_bits(uint16_t * p, uint16_t v_)
{
	uint32_t t, v;
	v = ~v_;
	asm volatile ("\n\tmovzwl %0, %1\n\
1:\n\
\tmovl %1, %%edx\n\
\tandl %2, %%edx\n\
\tlock cmpxchgw	%%dx, %0\n\
\tjne 1b\n\
\tmovl %%edx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%edx");
	return t;
}

/* STORE 8 */
static inline void atomic_store_int8_t(int8_t * p, int8_t v)
{
	*p = v;			/* is atomic on Intel */
}

/* ADD 8 */
static inline int8_t atomic_postadd_int8_t(int8_t * p, uint8_t v)
{
	asm volatile ("lock\n\txaddb %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline int8_t atomic_add_int8_t(int8_t * p, uint8_t v)
{
	uint8_t _v = v;
	asm volatile ("lock\n\txaddb %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* SUB 8 */
static inline int8_t atomic_postsub_int8_t(int8_t * p, uint8_t v)
{
	v = -v;
	asm volatile ("lock\n\txaddb %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline int8_t atomic_sub_int8_t(int8_t * p, uint8_t v)
{
	uint8_t _v;
	v = -v;
	_v = v;
	asm volatile ("lock\n\txaddb %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* STORE U8 */
static inline void atomic_store_uint8_t(uint16_t * p, uint8_t v)
{
	*p = v;			/* is atomic on Intel */
}

/* ADD U8 */
static inline uint8_t atomic_postadd_uint8_t(uint8_t * p, uint8_t v)
{
	asm volatile ("lock\n\txaddb %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline uint8_t atomic_add_uint8_t(uint8_t * p, uint8_t v)
{
	uint8_t _v = v;
	asm volatile ("lock\n\txaddb %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* SUB U8 */
static inline uint8_t atomic_postsub_uint8_t(uint8_t * p, uint8_t v)
{
	v = -v;
	asm volatile ("lock\n\txaddb %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v;
}

static inline uint8_t atomic_sub_uint8_t(uint8_t * p, uint8_t v)
{
	uint8_t _v;
	v = -v;
	_v = v;
	asm volatile ("lock\n\txaddb %0,%1":"=r" (v), "=m"(*p)
		      :"0"(v)
		      :"memory");
	return v + _v;
}

/* OR 8 */
static inline uint8_t atomic_postset_uint8_t_bits(uint8_t * p, uint8_t v_)
{
	uint32_t t, v;
	v = v_;
	asm volatile ("\n\tmovzbl %0, %1\n\
1:\n\
\tmovl %1, %%ecx\n\
\tmovl %1, %%edx\n\
\torl %2, %%edx\n\
\tlock cmpxchgb	%%dl, %0\n\
\tjne 1b\n\
\tmovl %%ecx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%ecx", "%edx");
	return t;
}

static inline uint8_t atomic_set_uint8_t_bits(uint8_t * p, uint8_t v_)
{
	uint32_t t, v;
	v = v_;
	asm volatile ("\n\tmovzbl %0, %1\n\
1:\n\
\tmovl %1, %%edx\n\
\torl %2, %%edx\n\
\tlock cmpxchgb	%%dl, %0\n\
\tjne 1b\n\
\tmovl %%edx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%edx");
	return t;
}

/* AND 8 */
static inline uint8_t atomic_postclear_uint8_t_bits(uint8_t * p, uint8_t v_)
{
	uint32_t t, v;
	v = ~v_;
	asm volatile ("\n\tmovzbl %0, %1\n\
1:\n\
\tmovl %1, %%ecx\n\
\tmovl %1, %%edx\n\
\tandl %2, %%edx\n\
\tlock cmpxchgb	%%dl, %0\n\
\tjne 1b\n\
\tmovl %%ecx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%ecx", "%edx");
	return t;
}

static inline uint8_t atomic_clear_uint8_t_bits(uint8_t * p, uint8_t v_)
{
	uint32_t t, v;
	v = ~v_;
	asm volatile ("\n\tmovzbl %0, %1\n\
1:\n\
\tmovl %1, %%edx\n\
\tandl %2, %%edx\n\
\tlock cmpxchgb	%%dl, %0\n\
\tjne 1b\n\
\tmovl %%edx, %1\n":"=m" (*p), "=a"(t), "=r"(v)
		      :"2"(v)
		      :"memory", "%edx");
	return t;
}
