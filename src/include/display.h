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
 */

/**
 * @brief Descriptor for display buffers.
 *
 * This structure defines a display buffer.
 * Buffer may be allocated global, on the stack, or by malloc.
 */
struct display_buffer {
	size_t b_size;		/*< Size of the buffer, will hold b_size
				    - 1 chars plus a '\0' */
	char *b_current;	/*< Current position in the buffer, where the
				    next string will be appended */
	char *b_start;		/*< Start of the buffer */
};

int display_buffer_remain(struct display_buffer *dspbuf);

int display_start(struct display_buffer *dspbuf);

int display_finish(struct display_buffer *dspbuf);

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
	 * b_start.
	 */
	dspbuf->b_current = dspbuf->b_start;
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
static inline int display_buffer_len(struct display_buffer *dspbuf)
{
	int len = dspbuf->b_current - dspbuf->b_start;
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

int display_opaque_bytes(struct display_buffer *dspbuf, void *value, int len);

int convert_opaque_value_max_for_dir(struct display_buffer *dspbuf,
					void                   *value,
					int                     len,
					int                     max);

int display_opaque_value_max(struct display_buffer *dspbuf, void *value,
			     int len, int max);

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

int display_len_cat(struct display_buffer *dspbuf, char *str, int len);

/**
 * @brief Append a null delimited string to the buffer.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     str    The string
 *
 * @return the bytes remaining in the buffer.
 *
 */
static inline int display_cat(struct display_buffer *dspbuf, char *str)
{
	return display_len_cat(dspbuf, str, strlen(str));
}

int display_cat_trunc(struct display_buffer *dspbuf, char *str, size_t max);

/** @} */

#endif				/* _DISPLAY_H */
