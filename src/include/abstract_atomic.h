/*
 * Copyright © 2012 Linux Box Corporation
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

/**
 * @file   abstract_atomic.h
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @author Frank S. Filz <ffilz@us.ibm.com>
 * @brief  Shim for compiler or library supplied atomic operations
 *
 * This file provides inline functions that provide atomic operations
 * appropriate to the compiler being used.  (Someone can add support
 * for an appropriate library later on.)
 */

#ifndef _ABSTRACT_ATOMIC_H
#define _ABSTRACT_ATOMIC_H
#include <stddef.h>
#include <stdint.h>

#ifndef __GNUC__
#error Please edit abstract_atomic.h and impelment support for  \
       non-GNU compilers.
#else /* __GNUC__ */
#define ATOMIC_GCC_VERSION (__GNUC__ * 10000                           \
                            + __GNUC_MINOR__ * 100                     \
                            + __GNUC_PATCHLEVEL__)

#if ((ATOMIC_GCC_VERSION) >= 40700)
#define GCC_ATOMIC_FUNCTIONS 1
#elif ((ATOMIC_GCC_VERSION) >= 40100)
#define GCC_SYNC_FUNCTIONS 1
#else
#error This verison of GCC does not support atomics.
#endif /* Version check */
#endif /* __GNUC__ */

/*
 * Increment/decrement
 */

/**
 * @brief Atomically add to an int64_t
 *
 * This function atomically adds to the supplied value.
 *
 * @param[in,out] augend Number to be added to
 * @param[in]     addend Number to add
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int64_t
atomic_add_int64_t(int64_t *augend, uint64_t addend)
{
     return __atomic_add_fetch(augend, addend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int64_t
atomic_add_int64_t(int64_t *augend, uint64_t addend)
{
     return __sync_fetch_and_add(augend, addend);
}
#endif

/**
 * @brief Atomically increment an int64_t
 *
 * This function atomically adds 1 to the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline int64_t
atomic_inc_int64_t(int64_t *var)
{
     return atomic_add_int64_t(var, 1);
}

/**
 * @brief Atomically subtract from an int64_t
 *
 * This function atomically subtracts from the supplied value.
 *
 * @param[in,out] minuend    Number to be subtracted from
 * @param[in]     subtrahend Number to subtract
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int64_t
atomic_sub_int64_t(int64_t *minuend, uint64_t subtrahend)
{
     return __atomic_sub_fetch(minuend, subtrahend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int64_t
atomic_sub_int64_t(int64_t *minuend, uint64_t subtrahend)
{
     return __sync_fetch_and_sub(minuend, subtrahend);
}
#endif

/**
 * @brief Atomically decrement an int64_t
 *
 * This function atomically subtracts 1 from the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline int64_t
atomic_dec_int64_t(int64_t *var)
{
     return atomic_sub_int64_t(var, 1);
}

/**
 * @brief Atomically add to an uint64_t
 *
 * This function atomically adds to the supplied value.
 *
 * @param[in,out] augend Number to be added to
 * @param[in]     addend Number to add
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint64_t
atomic_add_uint64_t(uint64_t *augend, uint64_t addend)
{
     return __atomic_add_fetch(augend, addend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint64_t
atomic_add_uint64_t(uint64_t *augend, uint64_t addend)
{
     return __sync_fetch_and_add(augend, addend);
}
#endif

/**
 * @brief Atomically increment a uint64_t
 *
 * This function atomically adds 1 to the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline uint64_t
atomic_inc_uint64_t(uint64_t *var)
{
     return atomic_add_uint64_t(var, 1);
}

/**
 * @brief Atomically subtract from an uint64_t
 *
 * This function atomically subtracts from the supplied value.
 *
 * @param[in,out] minuend    Number to be subtracted from
 * @param[in]     subtrahend Number to subtract
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint64_t
atomic_sub_uint64_t(uint64_t *minuend, uint64_t subtrahend)
{
     return __atomic_sub_fetch(minuend, subtrahend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint64_t
atomic_sub_uint64_t(uint64_t *minuend, uint64_t subtrahend)
{
     return __sync_fetch_and_sub(minuend, subtrahend);
}
#endif

/**
 * @brief Atomically decrement a uint64_t
 *
 * This function atomically subtracts 1 from the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline uint64_t
atomic_dec_uint64_t(uint64_t *var)
{
     return atomic_sub_uint64_t(var, 1);
}

/**
 * @brief Atomically add to an int32_t
 *
 * This function atomically adds to the supplied value.
 *
 * @param[in,out] augend Number to be added to
 * @param[in]     addend Number to add
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int32_t
atomic_add_int32_t(int32_t *augend, uint32_t addend)
{
     return __atomic_add_fetch(augend, addend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int32_t
atomic_add_int32_t(int32_t *augend, uint32_t addend)
{
     return __sync_fetch_and_add(augend, addend);
}
#endif

/**
 * @brief Atomically increment an int32_t
 *
 * This function atomically adds 1 to the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline int32_t
atomic_inc_int32_t(int32_t *var)
{
     return atomic_add_int32_t(var, 1);
}

/**
 * @brief Atomically subtract from an int32_t
 *
 * This function atomically subtracts from the supplied value.
 *
 * @param[in,out] minuend    Number to be subtracted from
 * @param[in]     subtrahend Number to subtract
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int32_t
atomic_sub_int32_t(int32_t *minuend, uint32_t subtrahend)
{
     return __atomic_sub_fetch(minuend, subtrahend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int32_t
atomic_sub_int32_t(int32_t *minuend, uint32_t subtrahend)
{
     return __sync_fetch_and_sub(minuend, subtrahend);
}
#endif

/**
 * @brief Atomically decrement an int32_t
 *
 * This function atomically subtracts 1 from the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline int32_t
atomic_dec_int32_t(int32_t *var)
{
     return atomic_sub_int32_t(var, 1);
}

/**
 * @brief Atomically add to an uint32_t
 *
 * This function atomically adds to the supplied value.
 *
 * @param[in,out] augend Number to be added to
 * @param[in]     addend Number to add
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint32_t
atomic_add_uint32_t(uint32_t *augend, uint32_t addend)
{
     return __atomic_add_fetch(augend, addend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint32_t
atomic_add_uint32_t(uint32_t *augend, uint32_t addend)
{
     return __sync_fetch_and_add(augend, addend);
}
#endif

/**
 * @brief Atomically increment a uint32_t
 *
 * This function atomically adds 1 to the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline uint32_t
atomic_inc_uint32_t(uint32_t *var)
{
     return atomic_add_uint32_t(var, 1);
}

/**
 * @brief Atomically subtract from an uint32_t
 *
 * This function atomically subtracts from the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint32_t
atomic_sub_uint32_t(uint32_t *var, uint32_t sub)
{
     return __atomic_sub_fetch(var, sub, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint32_t
atomic_sub_uint32_t(uint32_t *var, uint32_t sub)
{
     return __sync_fetch_and_sub(var, sub);
}
#endif

/**
 * @brief Atomically decrement a uint32_t
 *
 * This function atomically subtracts 1 from the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline uint32_t
atomic_dec_uint32_t(uint32_t *var)
{
     return atomic_sub_uint32_t(var, 1);
}

/**
 * @brief Atomically add to an int16_t
 *
 * This function atomically adds to the supplied value.
 *
 * @param[in,out] augend Number to be added to
 * @param[in]     addend Number to add
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int16_t
atomic_add_int16_t(int16_t *augend, uint16_t addend)
{
     return __atomic_add_fetch(augend, addend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int16_t
atomic_add_int16_t(int16_t *augend, uint16_t addend)
{
     return __sync_fetch_and_add(augend, addend);
}
#endif

/**
 * @brief Atomically increment an int16_t
 *
 * This function atomically adds 1 to the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline int16_t
atomic_inc_int16_t(int16_t *var)
{
     return atomic_add_int16_t(var, 1);
}

/**
 * @brief Atomically subtract from an int16_t
 *
 * This function atomically subtracts from the supplied value.
 *
 * @param[in,out] minuend    Number to be subtracted from
 * @param[in]     subtrahend Number to subtract
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int16_t
atomic_sub_int16_t(int16_t *minuend, uint16_t subtrahend)
{
     return __atomic_sub_fetch(minuend, subtrahend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int16_t
atomic_sub_int16_t(int16_t *minuend, uint16_t subtrahend)
{
     return __sync_fetch_and_sub(minuend, subtrahend);
}
#endif

/**
 * @brief Atomically decrement an int16_t
 *
 * This function atomically subtracts 1 from the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline int16_t
atomic_dec_int16_t(int16_t *var)
{
     return atomic_sub_int16_t(var, 1);
}

/**
 * @brief Atomically add to an uint16_t
 *
 * This function atomically adds to the supplied value.
 *
 * @param[in,out] augend Number to be added to
 * @param[in]     addend Number to add
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint16_t
atomic_add_uint16_t(uint16_t *augend, uint16_t addend)
{
     return __atomic_add_fetch(augend, addend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint16_t
atomic_add_uint16_t(uint16_t *augend, uint16_t addend)
{
     return __sync_fetch_and_add(augend, addend);
}
#endif

/**
 * @brief Atomically increment a uint16_t
 *
 * This function atomically adds 1 to the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline uint16_t
atomic_inc_uint16_t(uint16_t *var)
{
     return atomic_add_uint16_t(var, 1);
}

/**
 * @brief Atomically subtract from an uint16_t
 *
 * This function atomically subtracts from the supplied value.
 *
 * @param[in,out] minuend    Number to be subtracted from
 * @param[in]     subtrahend Number to subtract
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint16_t
atomic_sub_uint16_t(uint16_t *minuend, uint16_t subtrahend)
{
     return __atomic_sub_fetch(minuend, subtrahend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint16_t
atomic_sub_uint16_t(uint16_t *minuend, uint16_t subtrahend)
{
     return __sync_fetch_and_sub(minuend, subtrahend);
}
#endif

/**
 * @brief Atomically decrement a uint16_t
 *
 * This function atomically subtracts 1 from the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline uint16_t
atomic_dec_uint16_t(uint16_t *var)
{
     return atomic_sub_uint16_t(var, 1);
}

/**
 * @brief Atomically add to an int8_t
 *
 * This function atomically adds to the supplied value.
 *
 * @param[in,out] augend Number to be added to
 * @param[in]     addend Number to add
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int8_t
atomic_add_int8_t(int8_t *augend, uint8_t addend)
{
     return __atomic_add_fetch(augend, addend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int8_t
atomic_add_int8_t(int8_t *augend, uint8_t addend)
{
     return __sync_fetch_and_add(augend, addend);
}
#endif

/**
 * @brief Atomically increment an int8_t
 *
 * This function atomically adds 1 to the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline int8_t
atomic_inc_int8_t(int8_t *var)
{
     return atomic_add_int8_t(var, 1);
}

/**
 * @brief Atomically subtract from an int8_t
 *
 * This function atomically subtracts from the supplied value.
 *
 * @param[in,out] minuend    Number to be subtracted from
 * @param[in]     subtrahend Number to subtract
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int8_t
atomic_sub_int8_t(int8_t *minuend, uint8_t subtrahend)
{
     return __atomic_sub_fetch(minuend, subtrahend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int8_t
atomic_sub_int8_t(int8_t *minuend, uint8_t subtrahend)
{
     return __sync_fetch_and_sub(minuend, subtrahend);
}
#endif

/**
 * @brief Atomically decrement an int8_t
 *
 * This function atomically subtracts 1 from the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline int8_t
atomic_dec_int8_t(int8_t *var)
{
     return atomic_sub_int8_t(var, 1);
}

/**
 * @brief Atomically add to an uint8_t
 *
 * This function atomically adds to the supplied value.
 *
 * @param[in,out] augend Number to be added to
 * @param[in]     addend Number to add
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint8_t
atomic_add_uint8_t(uint8_t *augend, uint8_t addend)
{
     return __atomic_add_fetch(augend, addend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint8_t
atomic_add_uint8_t(uint8_t *augend, uint8_t addend)
{
     return __sync_fetch_and_add(augend, addend);
}
#endif

/**
 * @brief Atomically increment a uint8_t
 *
 * This function atomically adds 1 to the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline uint8_t
atomic_inc_uint8_t(uint8_t *var)
{
     return atomic_add_uint8_t(var, 1);
}

/**
 * @brief Atomically subtract from an uint8_t
 *
 * This function atomically subtracts from the supplied value.
 *
 * @param[in,out] minuend    Number to be subtracted from
 * @param[in]     subtrahend Number to subtract
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint8_t
atomic_sub_uint8_t(uint8_t *minuend, uint8_t subtrahend)
{
     return __atomic_sub_fetch(minuend, subtrahend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint8_t
atomic_sub_uint8_t(uint8_t *minuend, uint8_t subtrahend)
{
     return __sync_fetch_and_sub(minuend, subtrahend);
}
#endif

/**
 * @brief Atomically decrement a uint8_t
 *
 * This function atomically subtracts 1 from the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline uint8_t
atomic_dec_uint8_t(uint8_t *var)
{
     return atomic_sub_uint8_t(var, 1);
}

/**
 * @brief Atomically add to an size_t
 *
 * This function atomically adds to the supplied value.
 *
 * @param[in,out] augend Number to be added to
 * @param[in]     addend Number to add
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline size_t
atomic_add_size_t(size_t *augend, size_t addend)
{
     return __atomic_add_fetch(augend, addend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline size_t
atomic_add_size_t(size_t *augend, size_t addend)
{
     return __sync_fetch_and_add(augend, addend);
}
#endif

/**
 * @brief Atomically increment a size_t
 *
 * This function atomically adds 1 to the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline size_t
atomic_inc_size_t(size_t *var)
{
     return atomic_add_size_t(var, 1);
}

/**
 * @brief Atomically subtract from an size_t
 *
 * This function atomically subtracts from the supplied value.
 *
 * @param[in,out] minuend    Number to be subtracted from
 * @param[in]     subtrahend Number to subtract
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline size_t
atomic_sub_size_t(size_t *minuend, size_t subtrahend)
{
     return __atomic_sub_fetch(minuend, subtrahend, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline size_t
atomic_sub_size_t(size_t *minuend, size_t subtrahend)
{
     return __sync_fetch_and_sub(minuend, subtrahend);
}
#endif

/**
 * @brief Atomically decrement a size_t
 *
 * This function atomically subtracts 1 from the supplied value.
 *
 * @param[in,out] var Pointer to the variable to modify
 */

static inline size_t
atomic_dec_size_t(size_t *var)
{
     return atomic_sub_size_t(var, 1);
}

/*
 * Bit manipulation.
 */

/**
 * @brief Atomically clear bits in a uint64_t
 *
 * This function atomic clears the bits indicated.
 *
 * @param[in,out] var  Pointer to the value to modify
 * @param[in]     bits Bits to clear
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint64_t
atomic_clear_uint64_t_bits(uint64_t *var,
                           uint64_t bits)
{
     return __atomic_and_fetch(var, ~bits, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint64_t
atomic_clear_uint64_t_bits(uint64_t *var,
                           uint64_t bits)
{
     return __sync_fetch_and_and(var, ~bits);
}
#endif

/**
 * @brief Atomically set bits in a uint64_t
 *
 * This function atomic clears the bits indicated.
 *
 * @param[in,out] var  Pointer to the value to modify
 * @param[in]     bits Bits to set
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint64_t
atomic_set_uint64_t_bits(uint64_t *var,
                         uint64_t bits)
{
     return __atomic_or_fetch(var, bits, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint64_t
atomic_set_uint64_t_bits(uint64_t *var,
                         uint64_t bits)
{
     return __sync_fetch_and_or(var, bits);
}
#endif

/**
 * @brief Atomically clear bits in a uint32_t
 *
 * This function atomic clears the bits indicated.
 *
 * @param[in,out] var  Pointer to the value to modify
 * @param[in]     bits Bits to clear
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint32_t
atomic_clear_uint32_t_bits(uint32_t *var,
                           uint32_t bits)
{
     return __atomic_and_fetch(var, ~bits, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint32_t
atomic_clear_uint32_t_bits(uint32_t *var,
                           uint32_t bits)
{
     return __sync_fetch_and_and(var, ~bits);
}
#endif

/**
 * @brief Atomically set bits in a uint32_t
 *
 * This function atomic clears the bits indicated.
 *
 * @param[in,out] var  Pointer to the value to modify
 * @param[in]     bits Bits to set
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint32_t
atomic_set_uint32_t_bits(uint32_t *var,
                         uint32_t bits)
{
     return __atomic_or_fetch(var, bits, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint32_t
atomic_set_uint32_t_bits(uint32_t *var,
                         uint32_t bits)
{
     return __sync_fetch_and_or(var, bits);
}
#endif

/**
 * @brief Atomically clear bits in a uint16_t
 *
 * This function atomic clears the bits indicated.
 *
 * @param[in,out] var  Pointer to the value to modify
 * @param[in]     bits Bits to clear
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint16_t
atomic_clear_uint16_t_bits(uint16_t *var,
                           uint16_t bits)
{
     return __atomic_and_fetch(var, ~bits, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint16_t
atomic_clear_uint16_t_bits(uint16_t *var,
                           uint16_t bits)
{
     return __sync_fetch_and_and(var, ~bits);
}
#endif

/**
 * @brief Atomically set bits in a uint16_t
 *
 * This function atomic clears the bits indicated.
 *
 * @param[in,out] var  Pointer to the value to modify
 * @param[in]     bits Bits to set
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint16_t
atomic_set_uint16_t_bits(uint16_t *var,
                         uint16_t bits)
{
     return __atomic_or_fetch(var, bits, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint16_t
atomic_set_uint16_t_bits(uint16_t *var,
                         uint16_t bits)
{
     return __sync_fetch_and_or(var, bits);
}
#endif

/**
 * @brief Atomically clear bits in a uint8_t
 *
 * This function atomic clears the bits indicated.
 *
 * @param[in,out] var  Pointer to the value to modify
 * @param[in]     bits Bits to clear
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint8_t
atomic_clear_uint8_t_bits(uint8_t *var,
                          uint8_t bits)
{
     return __atomic_and_fetch(var, ~bits, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint8_t
atomic_clear_uint8_t_bits(uint8_t *var,
                          uint8_t bits)
{
     return __sync_fetch_and_and(var, ~bits);
}
#endif

/**
 * @brief Atomically set bits in a uint8_t
 *
 * This function atomic clears the bits indicated.
 *
 * @param[in,out] var  Pointer to the value to modify
 * @param[in]     bits Bits to set
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint8_t
atomic_set_uint8_t_bits(uint8_t *var,
                        uint8_t bits)
{
     return __atomic_or_fetch(var, bits, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint8_t
atomic_set_uint8_t_bits(uint8_t *var,
                        uint8_t bits)
{
     return __sync_fetch_and_or(var, bits);
}
#endif

/*
 * Fetch and store
 */

/**
 * @brief Atomically fetch a size_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline size_t
atomic_fetch_size_t(size_t *var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline size_t
atomic_fetch_size_t(size_t *var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store a size_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_size_t(size_t *var, size_t val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_size_t(size_t *var, size_t val)
{
     __sync_lock_test_and_set(var, 0);
}
#endif

/**
 * @brief Atomically fetch a ptrdiff_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline ptrdiff_t
atomic_fetch_ptrdiff_t(ptrdiff_t *var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline ptrdiff_t
atomic_fetch_ptrdiff_t(ptrdiff_t *var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store a ptrdiff_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_ptrdiff_t(ptrdiff_t *var, ptrdiff_t val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_ptrdiff_t(ptrdiff_t *var, ptrdiff_t val)
{
     __sync_lock_test_and_set(var, 0);
}
#endif

/**
 * @brief Atomically fetch an int64_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int64_t
atomic_fetch_int64_t(int64_t *var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int64_t
atomic_fetch_int64_t(int64_t *var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store an int64_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_int64_t(int64_t *var, int64_t val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_int64_t(int64_t *var, int64_t val)
{
     __sync_lock_test_and_set(var, 0);
}
#endif

/**
 * @brief Atomically fetch a uint64_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint64_t
atomic_fetch_uint64_t(uint64_t *var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint64_t
atomic_fetch_uint64_t(uint64_t *var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store a uint64_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_uint64_t(uint64_t *var, uint64_t val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_uint64_t(uint64_t *var, uint64_t val)
{
     __sync_lock_test_and_set(var, 0);
}
#endif

/**
 * @brief Atomically fetch an int32_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int32_t
atomic_fetch_int32_t(int32_t *var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int32_t
atomic_fetch_int32_t(int32_t *var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store an int32_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_int32_t(int32_t *var, int32_t val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_int32_t(int32_t *var, int32_t val)
{
     __sync_lock_test_and_set(var, 0);
}
#endif

/**
 * @brief Atomically fetch a uint32_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint32_t
atomic_fetch_uint32_t(uint32_t *var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint32_t
atomic_fetch_uint32_t(uint32_t *var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store a uint32_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_uint32_t(uint32_t *var, uint32_t val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_uint32_t(uint32_t *var, uint32_t val)
{
     __sync_lock_test_and_set(var, 0);
}
#endif

/**
 * @brief Atomically fetch an int16_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int16_t
atomic_fetch_int16_t(int16_t *var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int16_t
atomic_fetch_int16_t(int16_t *var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store an int16_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_int16_t(int16_t *var, int16_t val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_int16_t(int16_t *var, int16_t val)
{
     __sync_lock_test_and_set(var, 0);
}
#endif

/**
 * @brief Atomically fetch a uint16_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint16_t
atomic_fetch_uint16_t(uint16_t *var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint16_t
atomic_fetch_uint16_t(uint16_t *var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store a uint16_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_uint16_t(uint16_t *var, uint16_t val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_uint16_t(uint16_t *var, uint16_t val)
{
     __sync_lock_test_and_set(var, 0);
}
#endif

/**
 * @brief Atomically fetch a int8_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline int8_t
atomic_fetch_int8_t(int8_t *var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline int8_t
atomic_fetch_int8_t(int8_t *var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store a int8_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_int8_t(int8_t *var, int8_t val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_int8_t(int8_t *var, int8_t val)
{
     __sync_lock_test_and_set(var, 0);
}
#endif

/**
 * @brief Atomically fetch a uint8_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to fetch
 *
 * @return the value pointed to by var.
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline uint8_t
atomic_fetch_uint8_t(uint8_t *var)
{
     return __atomic_load_n(var, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline uint8_t
atomic_fetch_uint8_t(uint8_t *var)
{
     return __sync_fetch_and_add(var, 0);
}
#endif

/**
 * @brief Atomically store a uint8_t
 *
 * This function atomically fetches the value indicated by the
 * supplied pointer.
 *
 * @param[in,out] var Pointer to the variable to modify
 * @param[in]     val The value to store
 */

#ifdef GCC_ATOMIC_FUNCTIONS
static inline void
atomic_store_uint8_t(uint8_t *var, uint8_t val)
{
     __atomic_store_n(var, val, __ATOMIC_SEQ_CST);
}
#elif defined(GCC_SYNC_FUNCTIONS)
static inline void
atomic_store_uint8_t(uint8_t *var, uint8_t val)
{
     __sync_lock_test_and_set(var, 0);
}
#endif
#endif /* !_ABSTRACT_ATOMIC_H */
