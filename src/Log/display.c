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
 * @file  display.c
 * @author Frank Filz <ffilz@us.ibm.com>
 * @brief Implementation of a buffer for constructing string messages.
 *
 * A variety of functions are provided to manipulate display buffers and
 * append various strings to the buffer.
 */

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include "display.h"

/**
 * @brief Internal routine to compute the bytes remaining in a buffer.
 *
 * @param[in] dspbuf The buffer.
 *
 * @return the number of bytes remaining in the buffer.
 */
static inline int _display_buffer_remain(struct display_buffer * dspbuf)
{
  /* Compute number of bytes remaining in buffer (including space for null). */
  return dspbuf->b_size - (dspbuf->b_current - dspbuf->b_start);
}

/**
 * @brief Compute the bytes remaining in a buffer.
 *
 * @param[in,out] dspbuf The buffer.
 *
 * @retval -1 if there is some problem rendering the buffer unusable.
 * @retval 0 if the buffer has overflowed.
 * @retval >0 indicates the bytes remaining (inlcuding one byte for '\0').
 */
int display_buffer_remain(struct display_buffer * dspbuf)
{
  /* If no buffer, indicate problem. */
  if(dspbuf == NULL ||
     dspbuf->b_start == NULL ||
     dspbuf->b_size == 0)
    {
      errno = EFAULT;
      return -1;
    }

  /* If b_current is invalid, set it to b_start */
  if(dspbuf->b_current == NULL ||
     dspbuf->b_current < dspbuf->b_start ||
     dspbuf->b_current > (dspbuf->b_start + dspbuf->b_size))
    dspbuf->b_current = dspbuf->b_start;

  /* Buffer is too small, just make it an emptry string and mark the buffer
   * as overrun.
   */
  if(dspbuf->b_size < 4)
    {
      dspbuf->b_start[0] = '\0';
      dspbuf->b_current = dspbuf->b_start + dspbuf->b_size;
      return 0;
    }

  /* Compute number of bytes remaining in buffer (including space for null). */
  return _display_buffer_remain(dspbuf);
}

/**
 * @brief Finish up a buffer after overflowing it.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     ptr    The proposed position in the buffer for the "..." string.
 *
 * This routine will validate the final character that will remain in the buffer
 * prior to the "..." to make sure it is not a partial UTF-8 character. If so,
 * it will place the "..." such that it replaces the partial UTF-8 character.
 * The result will be a proper UTF-8 string (assuming the rest of the string is
 * valid UTF-8...).
 *
 * Caller will make sure sufficent room is available in buffer for the "..."
 * string. This leaves it up to the caller whether prt must be backed up
 * from b_current, or if the caller knows the next item won't fit in the buffer
 * but that there is room for the "..." string.
 */
void _display_complete_overflow(struct display_buffer * dspbuf, char * ptr)
{
  int    utf8len;
  char * end;

  /* end points after last byte that we will retain. */
  end = ptr + 1;

  /* Now ptr points to last byte that will remain part of string.
   * Next we need to check if this byte is the end of a valid UTF-8 character.
   */
  while((ptr > dspbuf->b_start) && ((*ptr & 0xc0) == 0x80))
    ptr--;

  /* Now ptr points to the start of a valid UTF-8 character or the string
   * was rather corrupt, there is no valid start of a UTF-8 character.
   */

  /* Compute the length of the last UTF-8 character */
  utf8len = end - ptr;

  /* Check if last character is valid UTF-8, for multibyte characters the first
   * byte is a string of 1 bits followed by a 0 bit. So for example, a 2 byte
   * character leads off with 110xxxxxxx, so we mask with 11100000 (0xe0) and
   * test for 11000000 (0xc0).
   */
  if((((*ptr & 0x80) == 0x00) && (utf8len == 1)) ||
     (((*ptr & 0xe0) == 0xc0) && (utf8len == 2)) ||
     (((*ptr & 0xf0) == 0xe0) && (utf8len == 3)) ||
     (((*ptr & 0xf8) == 0xf0) && (utf8len == 4)) ||
     (((*ptr & 0xfc) == 0xf8) && (utf8len == 5)) ||
     (((*ptr & 0xfe) == 0xfc) && (utf8len == 6)))
    {
      /* Last character before end is valid, increment ptr past it. */
      ptr = end;
    }
  /* else last character is not valid, leave ptr to strip it. */

  /* Now we know where to place the elipsis... */
  strcpy(ptr, "...");
}

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
int display_start(struct display_buffer * dspbuf)
{
  int b_left = display_buffer_remain(dspbuf);

  /* If buffer has already overflowed, just indicate no space is left. */
  if(b_left == 0)
    return 0;

  /* If buffer is already full, indicate overflow now, and indicate no space
   * is left (so caller doesn't bother to do anything.
   */
  if(b_left == 1)
    {
      /* Increment past end and finish buffer. */
      dspbuf->b_current++;
      b_left--;

      /* Back up 3 bytes before last byte (note that b_current points
       * PAST the last byte of the buffer since the buffer has overflowed).
       */
      _display_complete_overflow(dspbuf, dspbuf->b_current - 4);
    }

  /* Indicate buffer is ok by returning b_left. */
  return b_left;
}

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
int display_finish(struct display_buffer * dspbuf)
{
  /*
   * display_buffer_remain will return the current number of bytes left in the
   * buffer. If this is 0, and we just appended to the buffer (i.e.
   * display_buffer_remain was NOT 0 before appending), then the last append
   * just overflowed the buffer (note that if it exactly filled the buffer,
   * display_buffer_remain would have returned 1). Since the buffer just
   * overflowed, the overflow will be indicated by truncating the string to
   * allow space for a three character "..." sequence.
   */
  int b_left = display_buffer_remain(dspbuf);

  if(b_left != 0)
    return b_left;

  /* We validated above that buffer is at least 4 bytes... */

  /* Back up 3 bytes before last byte (note that b_current points
   * PAST the last byte of the buffer since the buffer has overflowed).
   */
  _display_complete_overflow(dspbuf, dspbuf->b_current - 4);

  return 0;
}

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
int display_force_overflow(struct display_buffer * dspbuf)
{
  int b_left = display_buffer_remain(dspbuf);

  if(b_left <= 0)
    return b_left;

  if(b_left < 3)
    {
      /* There aren't at least 3 characters left, back up to allow for them.
       * If there aren't room for 3 more non-0 bytes in the buffer, then (baring
       * multi-byte UTF-8 charts), the "..." will always be at the very end of
       * the buffer, that is determined by b_start + b_size, b_current is
       * currently b_start + b_size - b_left so instead of using b_current,
       * we just back up 4 bytes from the end of the buffer to make the space.
       * _display_complete_overflow will deal with the possibility that a UTF-8
       * character ended up truncated as a result.
       */
      _display_complete_overflow(dspbuf, dspbuf->b_start + dspbuf->b_size - 4);
    }
  else
    {
      /* Otherwise just put the "..." at b_current */
      _display_complete_overflow(dspbuf, dspbuf->b_current);
    }

  /* Mark buffer as overflowed. */
  dspbuf->b_current = dspbuf->b_start + dspbuf->b_size;

  return 0;
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
int display_vprintf(struct display_buffer * dspbuf,
                    const char            * fmt,
                    va_list                 args)
{
  int len;
  int b_left = display_start(dspbuf);

  if(b_left <= 0)
    return b_left;

  /* snprintf into the buffer no more than b_left bytes. snprintf assures the
   * buffer is null terminated (so will copy at most b_left characters).
   */
  len = vsnprintf(dspbuf->b_current, b_left, fmt, args);

  if(len >= b_left)
    {
      /* snprintf indicated that if the full string was printed, it would have
       * overflowed. By incrementing b_current by b_left, b_current now points
       * beyond the buffer and clearly marks the buffer as full.
       */
      dspbuf->b_current += b_left;
    }
  else
    {
      /* No overflow, move b_current to the end of the printf. */
      dspbuf->b_current += len;
    }

  /* Finish up */
  return display_finish(dspbuf);
}

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
int display_opaque_bytes(struct display_buffer * dspbuf, void * value, int len)
{
  unsigned int i = 0;
  int          b_left = display_start(dspbuf);

  if(b_left <= 0)
    return b_left;

  /* Check that the length is ok */
  if(len < 0)
    return display_printf(dspbuf, "(invalid len=%d)", len);

  /* If the value is NULL, display NULL value. */
  if(value == NULL)
    return display_cat(dspbuf, "(NULL)");

  /* If the value is empty, display EMPTY value. */
  if(len == 0)
    return display_cat(dspbuf, "(EMPTY)");

  /* Indicate the value is a hex string. */
  b_left = display_cat(dspbuf, "0x");

  /* Display the value one hex byte at a time. */
  for(i = 0; i < len && b_left > 0; i++)
    b_left = display_printf(dspbuf, "%02x", ((unsigned char *)value)[i]);

  /* Finish up */
  return display_finish(dspbuf);
}

/**
 * @brief convert clientid opaque bytes as a hex string for mkdir purpose.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     value  The bytes to display
 * @param[in]     len    The number of bytes to display
 *
 * @return the bytes remaining in the buffer.
 * 
 */
int convert_opaque_value_max_for_dir(struct display_buffer * dspbuf,
                             void                  * value,
                             int                     len,
                             int                     max)
{
  unsigned int i = 0;
  int          b_left = display_start(dspbuf);
  int          cpy = len;

  if(b_left <= 0)
    return 0;

  /* Check that the length is ok */
  if(len < 0)
    return 0;

  /* If the value is NULL, display NULL value. */
  if(value == NULL)
    return 0;

  /* If the value is empty, display EMPTY value. */
  if(len == 0)
    return 0;

  /* Display the length of the value. */
  b_left = display_printf(dspbuf, "(%d:", len);

  if(b_left <= 0)
    return 0;

  if(len > max)
    return 0;

  /* Determine if the value is entirely printable characters, */
  /* and it contains no slash character (reserved for filename) */
  for(i = 0; i < len; i++)
    if((!isprint(((char *)value)[i])) || (((char *)value)[i] == '/'))
      break;

  if(i == len)
    {
      /* Entirely printable character, so we will just copy the characters into
       * the buffer (to the extent there is room for them).
       */
      b_left = display_len_cat(dspbuf, value, cpy);
    }
  else
    {
      b_left = display_opaque_bytes(dspbuf, value, cpy);
    }

  if(b_left <= 0)
    return 0;

  return display_cat(dspbuf, ")");
}

/**
 * @brief Display a number of opaque bytes as a hex string, limiting the number
 *        of bytes used from the opaque value.
 *
 * @param[in,out] dspbuf The buffer.
 * @param[in]     value  The bytes to display
 * @param[in]     len    The number of bytes to display
 * @param[in]     max    The maximum number of bytes from the opaque value to display
 *
 * @return the bytes remaining in the buffer.
 *
 * This routine also attempts to detect a printable value and if so, displays
 * that instead of converting value to a hex string. It uses min(len,max) as
 * the number of bytes to use from the opaque value.
 * 
 */
int display_opaque_value_max(struct display_buffer * dspbuf,
                             void                  * value,
                             int                     len,
                             int                     max)
{
  unsigned int i = 0;
  int          b_left = display_start(dspbuf);
  int          cpy = len;

  if(b_left <= 0)
    return b_left;

  /* Check that the length is ok */
  if(len < 0)
    return display_printf(dspbuf, "(invalid len=%d)", len);

  /* If the value is NULL, display NULL value. */
  if(value == NULL)
    return display_cat(dspbuf, "(NULL)");

  /* If the value is empty, display EMPTY value. */
  if(len == 0)
    return display_cat(dspbuf, "(EMPTY)");

  /* Display the length of the value. */
  b_left = display_printf(dspbuf, "(%d:", len);

  if(b_left <= 0)
    return b_left;

  if(len > max)
    cpy = max;

  /* Determine if the value is entirely printable characters. */
  for(i = 0; i < len; i++)
    if(!isprint(((char *)value)[i]))
      break;

  if(i == len)
    {
      /* Entirely printable character, so we will just copy the characters into
       * the buffer (to the extent there is room for them).
       */
      b_left = display_len_cat(dspbuf, value, cpy);
    }
  else
    {
      b_left = display_opaque_bytes(dspbuf, value, cpy);
    }

  if(b_left <= 0)
    return 0;

  if(len > max)
    return display_cat(dspbuf, "...)");
  else
    return display_cat(dspbuf, ")");
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
int display_len_cat(struct display_buffer * dspbuf, char * str, int len)
{
  int b_left = display_start(dspbuf);
  int cpy;

  if(b_left <= 0)
    return b_left;

  /* Check if string would overflow dspbuf. */
  if(len >= b_left)
    {
      /* Don't copy more bytes than will fit. */
      cpy = b_left - 1;
    }
  else
    {
      /* Copy the entire string including null. */
      cpy = len;
    }

  /* Copy characters and null terminate. */
  memcpy(dspbuf->b_current, str, cpy);
  dspbuf->b_current[cpy] = '\0';
  

  if(len >= b_left)
    {
      /* Overflow, indicate by moving b_current past end of buffer. */
      dspbuf->b_current += b_left;
    }
  else
    {
      /* Didn't overflow, just increment b_current. */
      dspbuf->b_current += len;
    }

 return display_finish(dspbuf);
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
int display_cat_trunc(struct display_buffer * dspbuf, char * str, size_t max)
{
  struct display_buffer catbuf;
  int                   b_left = display_start(dspbuf);

  if(b_left <= 0)
    return b_left;

  /* If there isn't room left in dspbuf after max, then just use display_cat
   * so that dspbuf will be properly finished.
   */
  if((max + 1) >= b_left)
    return display_cat(dspbuf, str);

  /* Make a sub-buffer so we can properly truncate the string. */
  catbuf.b_current = dspbuf->b_current;
  catbuf.b_start   = dspbuf->b_current;
  catbuf.b_size    = max + 1;

  b_left = display_cat(&catbuf, str);

  /* Did we overflow the catbuf?
   * NOTE b_left can't be -1 because catbuf is declared on the stack.
   */
  if(b_left == 0)
    {
      /* Overflowed catbuf, catbuf.b_current points past the terminating null.
       * Roll back to point at the null.
       */
      dspbuf->b_current = catbuf.b_current - 1;
    }
  else
    {
      /* Update dspbuf */
      dspbuf->b_current = catbuf.b_current;
    }

  /* we know dspbuf itself can not have overflowed so just return the new
   * remaing buffer space.
   */
  return _display_buffer_remain(dspbuf);
}

/** @} */
