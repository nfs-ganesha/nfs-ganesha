/*
 * vim:noexpandtab:shiftwidth=4:tabstop=8:
 */

/**
 * Common tools for printing, parsing, ....
 *
 *
 */
#include "config.h"

#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "common_utils.h"

/**
 * @brief Convert a decimal string to an integer
 *
 * @param[in] str The string to be converted.
 *
 * @return The converted integer, negative on error.
 */
int s_read_int(const char *str)
{
	int i;
	int out = 0;

	for (i = 0; str[i]; i++) {
		if ((str[i] < '0') || (str[i] > '9'))
			return -1; /* error */
		else {
			out *= 10;
			out += (int)(str[i] - '0');
		}
	}

	if (i == 0)
		return -1;

	return out;

}

/**
 * @brief Convert an octal string to an integer
 *
 * @param[in] str The string to be converted.
 *
 * @return The converted integer, negative on error.
 */
int s_read_octal(const char *str)
{

	int i;
	int out = 0;

	for (i = 0; str[i]; i++) {

		if ((str[i] < '0') || (str[i] > '7'))
			return -1;	/* error */
		else {
			out *= 8;
			out += (int)(str[i] - '0');
		}
	}

	if (i == 0)
		return -1;

	return out;

}

/**
 * @brief Convert a decimal string to a 64-bit integer.
 *
 * @param[in]  str   The string to be converted.
 * @param[out] out64 Converted integer
 *
 * @retval True on success
 * @retval False on error
 */
bool s_read_uint64(const char *str, uint64_t *out64)
{

	int i;
	unsigned long long out = 0;

	if (!out64)
		return -1;

	for (i = 0; str[i]; i++) {

		if ((str[i] < '0') || (str[i] > '9'))
			return -1;	/* error */
		else {
			out *= 10;
			out += (unsigned long long)(str[i] - '0');
		}
	}

	if (i == 0)
		return false;

	*out64 = out;

	return true;
}

/**
 * @brief Convert a string to a boolean
 *
 * @retval 1 for true
 * @retval 0 for false
 * @retval -1 for error
 */
int str_to_bool(const char *str)
{
	if (!strcasecmp(str, "1") || !strcasecmp(str, "TRUE")
	    || !strcasecmp(str, "YES"))
		return 1;

	if (!strcasecmp(str, "0") || !strcasecmp(str, "FALSE")
	    || !strcasecmp(str, "NO"))
		return 0;

	return -1;
}

/**
 * @brief Print memory to a a hex string
 *
 * @param[out] target   Buffer where memory is to be printed
 * @param[in]  tgt_size Size of the target buffer
 * @param[in]  source   Buffer to be printed
 * @param[in]  mem_size Size of the buffer
 *
 * @return The number of bytes written in the target buffer.
 */
int
snprintmem(char *target, size_t tgt_size, const void *source,
	   size_t mem_size)
{

	const unsigned char *c = '\0';	/* the current char to be printed */
	char *str = target;	/* the current position in target buffer */
	int wrote = 0;

	for (c = (const unsigned char *)source;
	     c < ((const unsigned char *)source + mem_size); c++) {
		int tmp_wrote = 0;

		if (wrote >= tgt_size) {
			target[tgt_size - 1] = '\0';
			break;
		}

		tmp_wrote =
		    snprintf(str, tgt_size - wrote, "%.2X", (unsigned char)*c);
		str += tmp_wrote;
		wrote += tmp_wrote;

	}

	return wrote;

}

/* test if a letter is hexa */
#define IS_HEXA(c)  \
	((((c) >= '0') && ((c) <= '9')) || (((c) >= 'A') && ((c) <= 'F')) \
	 || (((c) >= 'a') && ((c) <= 'f')))

/* converts an hexa letter */
#define HEXA2BYTE(c)							\
	((unsigned char)						\
	 (((c) >= '0') && ((c) <= '9') ?				\
	  ((c) - '0') : (((c) >= 'A') && ((c) <= 'F') ?			\
			 ((c) - 'A' + 10) : (((c) >= 'a') &&		\
					     ((c) <= 'f') ?		\
					     ((c) - 'a' + 10) : 0))))

/**
 * @brief Read a hexadecimal string into memory
 *
 * @param[out] target     Where memory is to be written
 * @param[in]  tgt_size   Size of the target buffer
 * @param[in]  str_source Hexadecimal string
 *
 * @retval The number of bytes read in the source string.
 * @retval -1 on error.
 */

int
sscanmem(void *target, size_t tgt_size, const char *str_source)
{

	unsigned char *mem;	/* the current byte to be set */

	const char *src;	/* pointer to the current char to be read. */

	int nb_read = 0;

	src = str_source;

	for (mem = (unsigned char *)target;
	     mem < ((unsigned char *)target + tgt_size); mem++) {

		unsigned char tmp_val;

		/* we must read 2 bytes (written in hexa) to have 1
		   target byte value. */
		if ((*src == '\0') || (*(src + 1) == '\0')) {
			/* error, the source string is too small */
			return -1;
		}

		/* they must be hexa values */
		if (!IS_HEXA(*src) || !IS_HEXA(*(src + 1)))
			return -1;

		/* we read hexa values. */
		tmp_val = (HEXA2BYTE(*src) << 4) + HEXA2BYTE(*(src + 1));

		/* we had them to the target buffer */
		(*mem) = tmp_val;

		src += 2;
		nb_read += 2;

	}

	return nb_read;

}
