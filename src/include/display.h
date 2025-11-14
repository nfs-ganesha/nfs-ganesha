/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright IBM Corporation, 2012
 *  Contributor: Frank Filz <ffilz@us.ibm.com>
 *
 * --------------------------
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
 *
 */

/**
 * @defgroup Display display_buffer implementation
 * @{
 */

/**
 * @file  display.h
 * @author Frank Filz <ffilz@us.ibm.com>
 * @brief Implementation of a buffer for constructing string messages.
 *
 * This file provides a buffer descriptor for string messages that
 * contains a current position as well as the buffer pointer and size.
 * A variety of functions are provided to manipulate the buffer and
 * append various strings to the buffer.
 */

#ifndef _DISPLAY_H
#define _DISPLAY_H

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @page Display Safe display buffers
 *
 * A struct display_buffer describes a string buffer and the current position
 * within it so that a string can be built of various components. This is
 * especially useful for nested display functions for data types, where
 * the top level display function may call display functions for sub-data types.
 *
 * While building a complex string, users SHOULD check the return value from
 * each display function and exit if it is <= 0, however, continuing to call
 * display functions will be totally safe.
 *
 * The only things that MUST be done if building a new display primitive is
 * to call display_start at the beginning and display_finish at the end. A
 * display primitive is a function that uses a non-display function (such as
 * strcat, memcpy, sprintf) to copy bytes into the buffer. Such primitives must
 * assure that any such routines do not overflow the buffer, and then the
 * primitive must manage the b_current. display_finish will handle proper
 * indication of a full buffer or buffer overflow.
 *
 * A display function that is not a primitive (only uses display functions
 * themselves) SHOULD call display_start to make sure the buffer isn't already
 * full. It also assures the buffer will not wind up without a NUL terminator
 * should it not actually make any display calls.
 *
 * The core routines:
 *
 * display_start validate and prepare to start appending to the buffer.
 * display_finish wrap up after appending to the buffer.
 * display_reset_buffer reset a buffer for re-use to build a new string.
 * display_printf append to the string using printf formatting
 * display_opaque_value format an opaque value into the buffer
 * display_cat append a simple string to the buffer
 *
 * There are variants of these functions.
 *
 * All display functions return the following set of values:
 *
 * -1 if there is some problem rendering the buffer unusable.
 * 0 if the buffer has overflowed.
 * >0 indicates the bytes remaining (including one byte for '\0').
 */

/**
 * @brief Descriptor for display buffers.
 *
 * This structure defines a display buffer.
 * Buffer may be allocated global, on the stack, or by malloc.
 */
struct display_buffer {
	size_t b_size; /*< Size of the buffer, will hold b_size
				    - 1 chars plus a '\0' */
	char *b_current; /*< Current position in the buffer, where the
				    next string will be appended */
	char *b_start; /*< Start of the buffer */
};

/**
 * @brief Compute the bytes remaining in a buffer.
 *
 * @param[in,out] dspbuf The buffer.
 *
 * @retval -1 if there is some problem rendering the buffer unusable.
 * @retval 0 if the buffer has overflowed.
 * @retval >0 indicates the bytes remaining (including one byte for '\0').
 */
int display_buffer_remain(struct display_buffer *dspbuf);

/**
 * @brief Prepare to append to buffer.
 *
 * @param[in,out] dspbuf The buffer.
 *
 * @return the bytes remaining in the buffer.
 *
 * This routine validates the buffer, then checks if the buffer is already full
 * in which case it will mark the buffer as overflowed and finish up the buffer.
 *
 */
int display_start(struct display_buffer *dspbuf);

/**
 * @brief Finish up a buffer after appending to it.
 *
 * @param[in,out] dspbuf The buffer.
 *
 * @return the bytes remaining in the buffer.
 *
 * After a buffer has been appended to, check for overflow.
 *
 * This should be called by every routine that actually copies bytes into a
 * display_buffer. It must not be called by routines that use other display
 * routines to build a buffer (since the last such routine executed will
 * have called this routine).
 *
 */
int display_finish(struct display_buffer *dspbuf);

/**
 * @brief Force overflow on a buffer after appending to it.
 *
 * @param[in,out] dspbuf The buffer.
 *
 * @return the bytes remaining in the buffer.
 *
 * After a buffer has been appended to, check for overflow.
 *
 */
int display_force_overflow(struct display_buffer *dspbuf);

/**
 * @brief Reset current position in buffer to start.
 *
 * @param[in,out] dspbuf The buffer.
 *
 */
static inline void display_reset_buffer(struct display_buffer *dspbuf)
{
	/* To re-use a buffer, all we need to do is roll b_current back to
	 * b_start and make it empty.
	 */
	dspbuf->b_current = dspbuf->b_start;
	*dspbuf->b_current = '\0';
}

/**
 * @brief Compute the string length of the buffer.
 *
 * @param[in] dspbuf The buffer to finish up.
 *
 * @return the length.
 *
 * This function is more efficient than strlen if the buffer hasn't overflowed.
 *
 */
static inline size_t display_buffer_len(struct display_buffer *dspbuf)
{
	size_t len = dspbuf->b_current - dspbuf->b_start;

	if (len == dspbuf->b_size) {
		/* Buffer has overflowed, due to forced overflow or partial
		 * UTF-8 fixup, the actual string length might actually be less
		 * than the full length of the buffer. Just use strlen.
		 */
		return strlen(dspbuf->b_start);
	} else {
		return len;
	}
}

/**
 * @brief Format a string into the buffer.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     fmt    The format string
 * @param[in]     args   The va_list args
 *
 * @return the bytes remaining in the buffer.
 *
 */
int display_vprintf(struct display_buffer *dspbuf, const char *fmt,
		    va_list args);

/**
 * @brief Format a string into the buffer.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     fmt    the format string
 * @param[in] ... the    args
 *
 * @return the bytes remaining in the buffer.
 *
 */
static inline int display_printf(struct display_buffer *dspbuf, const char *fmt,
				 ...)
{
	va_list args;
	int b_left;

	va_start(args, fmt);

	b_left = display_vprintf(dspbuf, fmt, args);

	va_end(args);

	return b_left;
}

#define OPAQUE_BYTES_SIZE(len) (MAX(len * 2 + 2 + 1, 32))

/* Indicate if use upper case (%02X) or lower case (%02x) */
#define OPAQUE_BYTES_UPPER 0x01

/* Indicate if to lead with 0x */
#define OPAQUE_BYTES_0x 0x02

/* Return -1 on invalid length */
#define OPAQUE_BYTES_INVALID_LEN 0x04

/* Return -1 on NULL pointer */
#define OPAQUE_BYTES_INVALID_NULL 0x08

/* Return -1 on EMPTTY target */
#define OPAQUE_BYTES_INVALID_EMPTY 0x10

/* Return -1 if len > max */
#define OPAQUE_BYTES_NO_TRUNC 0x20

/**
 * @brief Display a number of opaque bytes as a hex string.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     value  The bytes to display
 * @param[in]     len    The number of bytes to display
 * @param[in]     flags  Flags indicating options for display
 *
 * @return the bytes remaining in the buffer.
 *
 */
int display_opaque_bytes_flags(struct display_buffer *dspbuf, void *value,
			       int len, int flags);

/**
 * @brief Display a number of opaque bytes as a hex string.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     value  The bytes to display
 * @param[in]     len    The number of bytes to display
 *
 * @return the bytes remaining in the buffer.
 *
 */
static inline int display_opaque_bytes(struct display_buffer *dspbuf,
				       void *value, int len)
{
	return display_opaque_bytes_flags(dspbuf, value, len, OPAQUE_BYTES_0x);
}

/**
 * @brief Display a number of opaque bytes as a hex string, limiting the number
 *        of bytes used from the opaque value.
 *
 * @param[in,out] dspbuf   The buffer.
 * @param[in]     value    The bytes to display
 * @param[in]     len      The number of bytes to display
 * @param[in]     max      Max number of bytes from the opaque value to display
 * @param[in]     notprint Additional set of characters not considered printable
 * @param[in]     flags    Flags indicating options for display
 *
 * @return the bytes remaining in the buffer.
 *
 * This routine also attempts to detect a printable value and if so, displays
 * that instead of converting value to a hex string. It uses min(len,max) as
 * the number of bytes to use from the opaque value.
 *
 */
int display_opaque_value_max_impl(struct display_buffer *dspbuf, void *value,
				  int len, int max, char *notprint, int flags);

/**
 * @brief Display a number of opaque bytes as a hex string, limiting the number
 *        of bytes used from the opaque value.
 *
 * Wrapper for above function with no additional non-printable characters and
 * OPAQUE_BYTES_0x flag set.
 *
 * @param[in,out] dspbuf   The buffer.
 * @param[in]     value    The bytes to display
 * @param[in]     len      The number of bytes to display
 * @param[in]     max      Max number of bytes from the opaque value to display
 *
 * @return the bytes remaining in the buffer.
 *
 */
static inline int display_opaque_value_max(struct display_buffer *dspbuf,
					   void *value, int len, int max)
{
	return display_opaque_value_max_impl(dspbuf, value, len, max, NULL,
					     OPAQUE_BYTES_0x);
}

/**
 * @brief Display a number of opaque bytes as a hex string.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     value  The bytes to display
 * @param[in]     len    The number of bytes in the opaque value
 *
 * @return the bytes remaining in the buffer.
 *
 * This routine just calls display_opaque_value_max with max = len.
 *
 */
static inline int display_opaque_value(struct display_buffer *dspbuf,
				       void *value, int len)
{
	return display_opaque_value_max(dspbuf, value, len, len);
}

/**
 * @brief Append a length delimited string to the buffer.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     str    The string
 * @param[in]     len    The length of the string
 *
 * @return the bytes remaining in the buffer.
 *
 */
int display_len_cat(struct display_buffer *dspbuf, const char *str, int len);

/**
 * @brief Append a null delimited string to the buffer.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     str    The string
 *
 * @return the bytes remaining in the buffer.
 *
 */
static inline int display_cat(struct display_buffer *dspbuf, const char *str)
{
	return display_len_cat(dspbuf, str, strlen(str));
}

/**
 * @brief Append a null delimited string to the buffer, truncating it.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     str    The string
 * @param[in]     max    Truncate the string to this maximum length
 *
 * @return the bytes remaining in the buffer.
 *
 * This routine is useful when the caller wishes to append a string to
 * the buffer, but rather than truncating the string at the end of the buffer,
 * the caller desires the string to be truncated to some shorter length (max).
 *
 * If the string is truncated, that will be indicated with "..." characters.
 * Basically this routine makes a sub-display buffer of max+1 bytes and uses
 * display_cat to achieve the truncation.
 *
 */
int display_cat_trunc(struct display_buffer *dspbuf, char *str, size_t max);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _DISPLAY_H */
