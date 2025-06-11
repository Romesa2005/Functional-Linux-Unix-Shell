#ifndef VARIABLES_H
#define VARIABLES_H

#include <stdlib.h>  // For size_t, malloc, free
#include <string.h>  // For string manipulation functions

// Maximum length for strings stored in variables
#define MAX_STR_LEN 128

/**
 * Structure representing a variable in the shell
 * Forms a linked list of variables
 */
typedef struct Variable {
    char *title;            // Name of the variable
    char *val;             // Value of the variable
    struct Variable *next;  // Pointer to next variable in list
} Variable;

/**
 * Creates a deep copy of a variable list
 * @param src Source variable list to copy
 * @return Newly allocated copy of the list
 */
Variable *copy_vars(Variable *src);

/**
 * Sets or updates a variable in the list
 * @param front Pointer to the list head pointer
 * @param input Value to set (may contain variables to expand)
 * @param newtitle Name of the variable to set/update
 */
void setVar(Variable **front, const char *input, const char *newtitle);

/**
 * Retrieves a variable's value
 * @param front Variable list head pointer
 * @param title Name of variable to look up
 * @return Value of variable or NULL if not found
 */
char *getVar(Variable *front, const char *title);

/**
 * Frees all memory used by a variable list
 * @param front Variable list head pointer
 */
void freeVars(Variable *front);

/**
 * Expands variables in a string (in-place version)
 * @param input String containing variables to expand
 * @param output Buffer to store expanded result (must be at least MAX_STR_LEN)
 * @param var_list Variable list to use for expansion
 */
void expand_variables(const char *input, char *output, Variable *var_list);

/**
 * Expands variables in a string (allocating version)
 * @param front Variable list head pointer
 * @param input String containing variables to expand
 * @return Newly allocated string with variables expanded (must be freed by caller)
 */
char *expandVars(Variable *front, const const char *input);

#endif
