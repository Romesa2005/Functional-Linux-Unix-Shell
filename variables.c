#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "variables.h"
#include <ctype.h>
#include "io_helpers.h"
#define MAX_STR_LEN 128

// Global variable list (linked list head pointer)
Variable *var_list = NULL;

/**
 * Creates a deep copy of a variable list
 * @param src Source variable list to copy
 * @return Newly allocated copy of the list
 * Note: Uses setVar() to ensure proper variable expansion in the copy
 */
Variable *copy_vars(Variable *src) {
    Variable *new_list = NULL;
    Variable *curr = src;

    // Iterate through source list and copy each variable
    while (curr != NULL) {
        setVar(&new_list, curr->val, curr->title);
        curr = curr->next;
    }
    return new_list;
}

/**
 * Expands variables in a string (in-place version)
 * @param input String containing variables to expand
 * @param output Buffer to store expanded result
 * @param var_list Variable list to use for expansion
 * Note: Handles special cases like standalone $ and invalid variable names
 */
void expand_variables(const char *input, char *output, Variable *var_list) {
    const char *src = input;
    char *dest = output;
    *dest = '\0'; // Initialize output as empty string

    while (*src) {
        if (*src == '$') {
            // Check if this is a valid variable reference
            if (src[1] == ' ' || src[1] == '\t' || src[1] == '\0') {
                // Handle standalone $ character
                *dest++ = *src++;
                *dest = '\0';
                continue;
            }

            src++; // Skip $
            char var_name[MAX_STR_LEN] = {0};
            int i = 0;

            // Extract variable name (alphanumeric + underscore)
            while (*src && (isalnum(*src) || *src == '_')) {
                var_name[i++] = *src++;
            }
            var_name[i] = '\0';

            if (i > 0) {  // Valid variable name found
                char *var_value = getVar(var_list, var_name);
                if (var_value) {
                    // Append variable value to output
                    strncat(dest, var_value, MAX_STR_LEN - strlen(output) - 1);
                    dest += strlen(var_value);
                }
            } else {
                // Invalid variable name, keep the $
                *dest++ = '$';
                *dest = '\0';
            }
        } else {
            // Copy regular characters
            *dest++ = *src++;
            *dest = '\0';
        }
    }
}

/**
 * Expands variables in a string (allocating version)
 * @param front Variable list head pointer
 * @param input String containing variables to expand
 * @return Newly allocated string with variables expanded
 * Note: Caller must free the returned string
 */
char* expandVars(Variable *front, const char *input) {
    char *output = malloc(MAX_STR_LEN);
    if (output == NULL) return NULL;
    output[0] = '\0'; // Initialize empty string

    for (size_t i = 0; input[i] != '\0'; i++) {
        if (input[i] == '$') {
            // Handle special case: $ followed by space
            if (input[i+1] == ' ' || input[i+1] == '\t' || input[i+1] == '\0') {
                strncat(output, "$", MAX_STR_LEN - strlen(output) - 1);
                continue;
            }

            i++;  // Skip $
            char var_name[MAX_STR_LEN] = {0};
            size_t var_name_len = 0;

            // Extract variable name
            while (input[i] != '\0' && (isalnum(input[i]) || input[i] == '_')) {
                var_name[var_name_len++] = input[i++];
            }
            var_name[var_name_len] = '\0';
            i--;  // Adjust position after extraction

            // Look up and expand variable
            char *var_value = getVar(front, var_name);
            if (var_value) {
                strncat(output, var_value, MAX_STR_LEN - strlen(output) - 1);
            } else {
                // Variable not found, keep original syntax
                strncat(output, "$", MAX_STR_LEN - strlen(output) - 1);
                strncat(output, var_name, MAX_STR_LEN - strlen(output) - 1);
            }
        } else {
            // Copy regular characters
            size_t len = strlen(output);
            if (len < MAX_STR_LEN - 1) {
                output[len] = input[i];
                output[len+1] = '\0';
            }
        }
    }
    return output;
}

/**
 * Sets or updates a variable in the list
 * @param front Pointer to the list head pointer
 * @param input Variable value (may contain other variables to expand)
 * @param newtitle Variable name
 * Note: Handles memory allocation and variable expansion
 */
void setVar(Variable **front, const char *input, const char *newtitle) {
    // First expand any variables in the input value
    char *expanded_value = expandVars(*front, input);
    if (!expanded_value) return;

    // Check if variable already exists
    Variable *curr = *front;
    while (curr != NULL) {
        if (strcmp(curr->title, newtitle) == 0) {
            // Update existing variable
            free(curr->val);
            curr->val = strdup(expanded_value);
            free(expanded_value);
            return;
        }
        curr = curr->next;
    }

    // Create new variable node
    Variable *node = (Variable *)malloc(sizeof(Variable));
    if (node == NULL) {
        free(expanded_value);
        return;
    }

    // Initialize new node
    node->title = strdup(newtitle);
    node->val = strdup(expanded_value);
    free(expanded_value);

    // Check for allocation errors
    if (node->title == NULL || node->val == NULL) {
        free(node->title);
        free(node->val);
        free(node);
        return;
    }

    // Insert at front of list
    node->next = *front;
    *front = node;
}

/**
 * Gets the value of a variable
 * @param front Variable list head pointer
 * @param title Variable name to look up
 * @return Variable value or NULL if not found
 */
char* getVar(Variable *front, const char *title) {
    Variable *curr = front;
    while (curr != NULL) {
        if (strcmp(title, curr->title) == 0) {
            return curr->val;
        }
        curr = curr->next;
    }
    return NULL;  // Variable not found
}

/**
 * Frees all memory used by the variable list
 * @param front Variable list head pointer
 */
void freeVars(Variable *front) {
    Variable *curr = front;
    while (curr != NULL) {
        Variable *temp = curr->next;
        free(curr->title);
        free(curr->val);
        free(curr);
        curr = temp;
    }
}
