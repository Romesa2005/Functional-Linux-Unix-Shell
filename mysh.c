#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "builtins.h"
#include "io_helpers.h"
#include "variables.h"
#include "helper.h"
#include "commands.h"
char *token_arr[MAX_STR_LEN] = {NULL};
Server server = {0};
size_t token_count = 0;
// Function prototype for execute_single_command
void execute_single_command(char **tokens, int is_background);
extern Variable *var_list;  

void free_tokens(char **tokens) {
    if (tokens == NULL) return;
    
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
}

void backproc() {
    for (size_t i = 0; i < bg_count; i++) {
        if (bg[i].pid != -1) {
            int status;
            pid_t result = waitpid(bg[i].pid, &status, WNOHANG);
            if (result > 0) {
                // Process has completed
                char done_msg[MAX_STR_LEN];
                snprintf(done_msg, MAX_STR_LEN, "[%zu]+  Done %s", i + 1, bg[i].command);
                
                // Use display_message instead of direct write
                display_message(done_msg);
                display_message("\n");
                // Free the command string
                free(bg[i].command);
                bg[i].pid = -1; 
            }
            else if (result == -1) {
                // Error case - still use display_message for consistency
                display_message("ERROR: Failed to check background process status\n");
                free(bg[i].command);
                bg[i].pid = -1;
            }
        }
    }

    // Compact the background process array
    size_t new_count = 0;
    for (size_t i = 0; i < bg_count; i++) {
        if (bg[i].pid != -1) {
            if (i != new_count) {
                bg[new_count] = bg[i];
            }
            new_count++;
        }
    }
    bg_count = new_count;
}

void execute_command(char **tokens, int is_background) {
    // Count pipes and store their positions
    int pipe_count = 0;
    int pipe_positions[MAX_STR_LEN] = {0};

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            pipe_positions[pipe_count] = i;
            pipe_count++;
            free(tokens[i]);
            tokens[i] = NULL;
        }
    }

    if (pipe_count == 0) {
        execute_single_command(tokens, is_background);
        return;  // main() handles token freeing
    }

    // Create pipes
    int pipes[pipe_count][2];
    for (int i = 0; i < pipe_count; i++) {
        if (pipe(pipes[i]) == -1) {
            display_error("ERROR: Failed to create pipe", "");
            return;
        }
    }

    // Save original variables
    Variable *original_vars = var_list;
    var_list = copy_vars(original_vars);

    int cmd_start = 0;
    for (int i = 0; i <= pipe_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {  // Child process
            // Set up pipes
            if (i > 0) dup2(pipes[i-1][0], STDIN_FILENO);
            if (i < pipe_count) dup2(pipes[i][1], STDOUT_FILENO);

            // Close all pipes
            for (int j = 0; j < pipe_count; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execute_single_command(&tokens[cmd_start], 0);
            exit(0);
        }
        cmd_start = pipe_positions[i] + 1;
    }

    // Parent cleanup
    for (int i = 0; i < pipe_count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for children
    for (int i = 0; i <= pipe_count; i++) {
        wait(NULL);
    }

    // Restore variables
    freeVars(var_list);  // Free the temporary copy
    var_list = original_vars;
}



void execute_single_command(char **tokens, int is_background) {
    if (tokens == NULL || tokens[0] == NULL) {
        return;  // Skip empty commands
    }    
    if (strchr(tokens[0], '=') != NULL) {
    // Process variable assignment
    char *equals_sign = strchr(tokens[0], '=');
    *equals_sign = '\0';
    char *var_name = tokens[0];
    char *var_value = equals_sign + 1;
    setVar(&var_list, var_value, var_name);
    return;  // Skip command execution
}
    
    bn_ptr builtin_fn = check_builtin(tokens[0]);
    if (builtin_fn != NULL && is_background == 0) {
        ssize_t err = builtin_fn(tokens);
        if (err == -1) {
            display_error("ERROR: Builtin failed: ", tokens[0]);

        }
    } else {
        // Not a builtin, try to execute from /bin or /usr/bin
        pid_t pid = fork();

        if (pid == 0) { // Child process
            signal(SIGINT, SIG_DFL);  // Reset SIGINT handling for the child
            execvp(tokens[0], tokens);  // Run the command

            // If execvp fails, print an error
            display_error("ERROR: Unknown command: ", tokens[0]);
            exit(1);

        } else if (pid > 0) { // Parent process
            if (is_background) {
		usleep(10000);
                // If it's a background process, display the background job message
                char bg_msg[MAX_STR_LEN];
                snprintf(bg_msg, MAX_STR_LEN, "[%zu] %d\n", bg_count + 1, pid);
                display_message(bg_msg); // Display job number and PID

                // Add the process to the background job list
                if (bg_count < MAX_STR_LEN) {
                    bg[bg_count].pid = pid;

                    // Store the full command (including arguments)
                    char full_command[MAX_STR_LEN] = {0};
                    for (int i = 0; tokens[i] != NULL; i++) {
                        strncat(full_command, tokens[i], MAX_STR_LEN - strlen(full_command) - 1);
                        if (tokens[i + 1] != NULL) {
                            strncat(full_command, " ", MAX_STR_LEN - strlen(full_command) - 1);
                        }
                    }
                    bg[bg_count].command = strdup(full_command); // Store the full command
                    bg_count++;
                } else {
                    display_error("ERROR: Too many background processes", "");
                }
            } else {
                // Foreground process: wait for the process to finish
                waitpid(pid, NULL, 0);
            }
        } else {
            // Error in forking
            display_error("ERROR: Failed to fork process", "");
        }
    }
}
void handle_sigint(int sig) {
    (void)sig;  // Unused parameter
    display_message("\nmysh$ ");
}

int main(__attribute__((unused)) int argc, __attribute__((unused)) char* argv[]) {
    // Handle SIGINT (Ctrl+C) to print a newline
    signal(SIGINT, handle_sigint);

    char *prompt = "mysh$ ";
    char input_buf[MAX_STR_LEN + 1];
    input_buf[MAX_STR_LEN] = '\0';
    char *token_arr[MAX_STR_LEN] = {NULL};

    while (1) {
        // Check for completed background processes
        backproc();

        // Display prompt and get input
        display_message(prompt);
        int ret = get_input(input_buf);

        // Handle EOF (Ctrl+D)
        if (ret == -1) {
            break; // Exit the shell
        }

        // Tokenize the input
        size_t token_count = tokenize_input(input_buf, token_arr, &var_list);

        // Skip empty input (user pressed Enter without typing anything)
        if (token_count == 0 || ret == -2) {
            continue;
        }

        // Check for the "exit" command
        if (strcmp(token_arr[0], "exit") == 0) {
	    free(token_arr[0]);
            break; // Exit the shell
        }

        // Check for background process
        int is_background = 0;
        if (token_count > 0 && strcmp(token_arr[token_count - 1], "&") == 0) {
            is_background = 1;
            token_arr[token_count - 1] = NULL; // Remove '&' from tokens
        }

        // Execute the command with the background flag
        execute_command(token_arr, is_background);

        // Free tokens after executing the command (only in main)
        for (size_t i = 0; i < token_count; i++) {
            free(token_arr[i]);
            token_arr[i] = NULL;
        }
    }



    freeVars(var_list);

    // Free background process commands
    for (size_t i = 0; i < bg_count; i++) {
        if (bg[i].command != NULL) {
            free(bg[i].command);
        }
    }


// Right before the final return 0
if (server.running) {
    char *close_cmd[] = {"close-server", NULL};
    bn_close_server(close_cmd);
}
pthread_mutex_destroy(&server.lock);
    return 0;
}
