#ifndef __IO_HELPERS_H__
#define __IO_HELPERS_H__

#include <sys/types.h>
#include <stddef.h>
#include "variables.h"

#define MAX_STR_LEN 128
#define DELIMITERS " \t\n"     // Assumption: all input tokens are whitespace delimited


/* Prereq: pre_str, str are NULL terminated string
 */
void display_message(char *str);
void display_error(const char *pre_str, const char *str);


/* Prereq: in_ptr points to a character buffer of size > MAX_STR_LEN
 * Return: number of bytes read
 */
ssize_t get_input(char *in_ptr);


/* Prereq: in_ptr is a string, tokens is of size >= len(in_ptr)
 * Warning: in_ptr is modified
 * Return: number of tokens.
 */
size_t tokenize_input(char *input_buf, char *token_arr[], Variable **var_list);
#endif
