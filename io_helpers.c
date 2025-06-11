#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include "io_helpers.h"
#include "variables.h"
#include <ctype.h>

// ===== Output helpers =====

/**
 * Displays a message to standard output
 * @param str Null-terminated string to display
 * Note: Limits output to MAX_STR_LEN characters for safety
 */
void display_message(char *str) {
    write(STDOUT_FILENO, str, strnlen(str, MAX_STR_LEN));
}

/**
 * Displays an error message to standard error
 * @param pre_str Prefix error message
 * @param str Main error message
 * Note: Combines both strings and adds a newline
 */
void display_error(const char *pre_str, const char *str) {
    write(STDERR_FILENO, pre_str, strnlen(pre_str, MAX_STR_LEN));
    write(STDERR_FILENO, str, strnlen(str, MAX_STR_LEN));
    write(STDERR_FILENO, "\n", 1); // Add a newline
}

// ===== Input handling =====

/**
 * Reads input from stdin with safety checks
 * @param in_ptr Buffer to store input (must be > MAX_STR_LEN)
 * @return Number of bytes read, -1 on EOF/error, -2 if input was truncated
 * Note: Ensures null-termination and handles buffer overflow
 */
ssize_t get_input(char *in_ptr) {
    // Read up to MAX_STR_LEN bytes to avoid overflow
    ssize_t retval = read(STDIN_FILENO, in_ptr, MAX_STR_LEN);

    if (retval == 0) {
        // EOF condition (Ctrl+D)
        return -1;
    } else if (retval == -1) {
        // Read error
        perror("read");
        return -1;
    }

    // Null-terminate the input
    in_ptr[retval] = '\0';

    // Check for potential truncation
    if (retval == MAX_STR_LEN) {
        display_error("ERROR: input line too long", "");

        // Flush remaining input to prevent problems with next read
        int c;
        while ((c = getchar()) != '\n' && c != EOF);

        // Special return code for truncated input
        return -2;
    }

    return retval;
}

/**
 * Tokenizes input string and handles variable expansion
 * @param input_buf Input string to tokenize (will be modified)
 * @param token_arr Array to store resulting tokens
 * @param var_list Pointer to variable list for expansion
 * @return Number of tokens generated
 * Note: Handles special characters ($, |) and variable expansion
 */
size_t tokenize_input(char *input_buf, char *token_arr[], Variable **var_list) {
    size_t token_count = 0;
    char *token = strtok(input_buf, " \t\r\n"); // Split on whitespace

    while (token != NULL) {
        // Special case: standalone "$" character
        if (strcmp(token, "$") == 0) {
            token_arr[token_count++] = strdup("$");
            token = strtok(NULL, " \t\r\n");
            continue;
        }

        // Special case: pipe character
        if (strcmp(token, "|") == 0) {
            token_arr[token_count++] = strdup("|");
            token = strtok(NULL, " \t\r\n");
            continue;
        }

        // Buffer for expanded variables
        char expanded[MAX_STR_LEN] = {0};

        // Handle variable expansion if token starts with '$'
        if (token[0] == '$' && strlen(token) > 1) {
            expand_variables(token, expanded, *var_list);
        } else {
            strncpy(expanded, token, MAX_STR_LEN - 1);
        }

        // Store the token (either expanded or original)
        token_arr[token_count] = strdup(expanded);
        if (token_arr[token_count] == NULL) {
            // Cleanup on allocation failure
            for (size_t i = 0; i < token_count; i++) {
                free(token_arr[i]);
            }
            perror("strdup failed");
            exit(EXIT_FAILURE);
        }
        token_count++;

        token = strtok(NULL, " \t\r\n");
    }

    // Null-terminate the token array
    token_arr[token_count] = NULL;
    return token_count;
}
