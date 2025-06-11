#ifndef COMMANDS_H
#define COMMANDS_H

#include <sys/types.h>

#define MAX_STR_LEN 128
// Creating a struct called Backgr
typedef struct {
    char *command;
    pid_t pid;
} Backgr;

extern Backgr bg[MAX_STR_LEN]; // Declare bg as extern
extern size_t bg_count;        // Declare bg_count as extern

#endif // COMMANDS_H
