#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "variables.h"
#include "builtins.h"
#include "io_helpers.h"
#include <stdio.h>
#include "commands.h"
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "helper.h"

// Server structure to manage connections and clients
static Server server = {0};

// Background processes storage
Backgr bg[MAX_STR_LEN]; // Array to store background processes
size_t bg_count = 0;    // Count of active background processes

// External variable list from variables.h
extern Variable *var_list;

// ====== Command execution =====

/* Check if a command matches a builtin
 * Return: index of builtin or -1 if cmd doesn't match a builtin
 */
bn_ptr check_builtin(const char *cmd) {
    if (!cmd) return NULL;
    ssize_t cmd_num = 0;
    // Iterate through builtin commands to find a match
    while (cmd_num < BUILTINS_COUNT &&
           strncmp(BUILTINS[cmd_num], cmd, MAX_STR_LEN) != 0) {
        cmd_num += 1;
    }
    // Return corresponding builtin function pointer
    return BUILTINS_FN[cmd_num];
}

// Thread function to accept incoming client connections
void* accept_clients(void* arg) {
    Server* srv = (Server*)arg;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (srv->running) {
        // Accept new client connection
        int client_fd = accept(srv->sockfd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (srv->running) perror("accept");
            continue;
        }

        // Lock server mutex for thread-safe client management
        pthread_mutex_lock(&srv->lock);
        // Check if server has room for more clients
        if (srv->client_count >= MAX_CLIENTS) {
            close(client_fd);
            pthread_mutex_unlock(&srv->lock);
            continue;
        }

        // Add new client to server's client list
        Client* client = &srv->clients[srv->client_count++];
        client->sockfd = client_fd;
        snprintf(client->id, CLIENT_ID_LEN, "client%d:", srv->client_count);

        // Create thread to handle client communication
        pthread_create(&client->thread, NULL, handle, client);
        pthread_mutex_unlock(&srv->lock);
    }
    return NULL;
}

// Thread function to handle client communication
void *handle(void* arg) {
    Client* client = (Client*)arg;
    char buffer[BUF_SIZE];
    ssize_t bytes_read;

    // Read messages from client
    while ((bytes_read = read(client->sockfd, buffer, sizeof(buffer)-1)) > 0) {
        buffer[bytes_read] = '\0';

        // Handle special "connected" command
        if (strcmp(buffer, "\\connected") == 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "%d clients connected", server.client_count);
            write(client->sockfd, msg, strlen(msg));
            continue;
        }

        /* SAFE MESSAGE OUTPUT */
        // Format message with client ID
        size_t needed_size = strlen(client->id) + strlen(buffer) + 3 + 1;
        char local_output[needed_size];
        snprintf(local_output, needed_size, "%s: %s\n", client->id, buffer);
        write(STDOUT_FILENO, local_output, strlen(local_output));

        // Echo message back to client
        write(client->sockfd, buffer, bytes_read);

        // Broadcast message to all other clients
        pthread_mutex_lock(&server.lock);
        for (int i = 0; i < server.client_count; i++) {
            if (server.clients[i].sockfd != client->sockfd) {
                size_t msg_size = strlen(client->id) + strlen(buffer) + 2;
                char full_msg[msg_size];
                snprintf(full_msg, msg_size, "%s %s", client->id, buffer);
                write(server.clients[i].sockfd, full_msg, strlen(full_msg));
            }
        }
        pthread_mutex_unlock(&server.lock);
    }

    // Cleanup disconnected client
    pthread_mutex_lock(&server.lock);
    for (int i = 0; i < server.client_count; i++) {
        if (server.clients[i].sockfd == client->sockfd) {
            close(client->sockfd);
            // Remove client from array
            memmove(&server.clients[i], &server.clients[i+1],
                   (server.client_count-i-1)*sizeof(Client));
            server.client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&server.lock);
    return NULL;
}

// Check if path is a directory
int is_dir(const char *path){
    struct stat statbuf;
    if (stat(path, &statbuf)) return 0;
    return S_ISDIR(statbuf.st_mode);
}

// Check if substring exists in string
int sub(const char *str, const char *substr) {
    return strstr(str, substr) != NULL;
}

// Recursive directory listing with optional filtering and depth control
void ls_recursive(const char *path, const char *substr, int depth, int max_depth) {
    // Base case: Stop if max_depth is set and depth exceeds it
    if (max_depth >= 0 && depth >= max_depth) {
        return;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        display_error("ERROR: Invalid path: ", path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".." only if they are not explicitly requested
        if (substr == NULL && (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)) {
            display_message(entry->d_name);
            display_message("\n");
            continue;
        }

        // Build full path for recursive calls
        size_t path_len = strlen(path);
        size_t name_len = strlen(entry->d_name);
        if (path_len + name_len + 1 >= MAX_STR_LEN) {
            display_error("ERROR: Path too long: ", path);
            continue;
        }

        char full_path[MAX_STR_LEN];
        strncpy(full_path, path, MAX_STR_LEN - 1);
        full_path[MAX_STR_LEN - 1] = '\0';
        strncat(full_path, "/", MAX_STR_LEN - strlen(full_path) - 1);
        strncat(full_path, entry->d_name, MAX_STR_LEN - strlen(full_path) - 1);

        // Display entry if it matches filter (or no filter)
        if (substr == NULL || sub(entry->d_name, substr)) {
            display_message(entry->d_name);
            display_message("\n");
        }

        // Recurse into subdirectories if allowed by depth
        if (is_dir(full_path) && (max_depth < 0 || depth < max_depth)) {
            ls_recursive(full_path, substr, depth + 1, max_depth);
        }
    }

    closedir(dir);
}

// ===== Builtins =====

/* Echo command implementation
 * Prereq: tokens is a NULL terminated sequence of strings.
 * Return 0 on success and -1 on error ... but there are no errors on echo.
 */
ssize_t bn_echo(char **tokens) {
    if (tokens[1] == NULL) {
        display_message("\n");
        return 0;
    }

    // Build output string from tokens
    char output[MAX_STR_LEN] = {0};
    size_t output_len = 0;
    for (size_t index = 1; tokens[index] != NULL; index++) {
         size_t rem = MAX_STR_LEN - output_len - 1;
         if (rem > 0) {
             strncat(output, tokens[index], rem);
             output_len = strlen(output);
                    }
        // Add space between tokens if there's room
        if (tokens[index + 1] != NULL && output_len < MAX_STR_LEN - 1) {
            output[output_len++] = ' ';
            output[output_len] = '\0';
        }
        if (output_len >= MAX_STR_LEN - 1) {
            break;
        }
    }
    // Truncate very long output
    if (output_len > 128) {
        output[128] = '\0';}
    display_message(output);
    display_message("\n");
    return 0;
}

// List directory contents command
ssize_t bn_ls(char **tokens) {
    char *path = ".";    // Default path
    int max_depth = -1;  // Unlimited depth by default
    int recursive = 0;   // Non-recursive by default
    char *substr = NULL; // No substring filter by default

    // Parse command options
    for (int i = 1; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "--f") == 0) {
            // Substring filter option
            if (tokens[i + 1] != NULL) {
                substr = tokens[i + 1];
                i++;
            } else {
                display_error("ERROR: Missing substring for --f\n", "");
                return -1;
            }
        } else if (strcmp(tokens[i], "--rec") == 0) {
            // Recursive option
            recursive = 1;
        } else if (strcmp(tokens[i], "--d") == 0) {
            // Depth limit option
            if (tokens[i + 1] != NULL) {
                max_depth = atoi(tokens[i + 1]);
                i++;
            } else {
                display_error("ERROR: Missing depth for --d\n", "");
                return -1;
            }
        } else {
            // Path argument
            path = tokens[i];
        }
    }

    // Validate options
    if (max_depth >= 0 && !recursive) {
        display_error("ERROR: --d requires --rec\n", "");
        return -1;
    }

    // Execute listing
    if (recursive) {
        ls_recursive(path, substr, 0, max_depth);
    } else {
        // Non-recursive listing
        DIR *dir = opendir(path);
        if (!dir) {
            display_error("ERROR: Invalid path: ", path);
            return -1;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            // Display entry if it matches filter (or no filter)
            if (substr == NULL || sub(entry->d_name, substr)) {
                display_message(entry->d_name);
                display_message("\n");
            }
        }

        closedir(dir);
    }

    return 0;
}

// Change directory command
ssize_t bn_cd(char **tokens) {
    // Handle no argument case (change to home)
    if (tokens[1] == NULL) {
        const char *home_dir = getenv("HOME");
        if (home_dir == NULL) {
            display_error("ERROR: HOME environment variable not set", "");
            return -1;
        }
        if (chdir(home_dir) != 0) {
            display_error("ERROR: Invalid path: ", home_dir);
            return -1;
        }
        return 0;
    }

    // Special directory shortcuts
    if (strcmp(tokens[1], ".") == 0) {
        // Current directory (no change)
        return 0;
    } else if (strcmp(tokens[1], "..") == 0) {
        // Parent directory
        if (chdir("..") != 0) {
            display_error("ERROR: Invalid path: ", "..");
            return -1;
        }
        return 0;
    } else if (strcmp(tokens[1], "...") == 0) {
        // Grandparent directory
        if (chdir("../..") != 0) {
            display_error("ERROR: Invalid path: ", "../..");
            return -1;
        }
        return 0;
    } else if (strcmp(tokens[1], "....") == 0) {
        // Great-grandparent directory
        if (chdir("../../..") != 0) {
            display_error("ERROR: Invalid path: ", "../../..");
            return -1;
        }
        return 0;
    }

    // Normal directory change
    if (chdir(tokens[1]) != 0) {
        display_error("ERROR: Invalid path: ", tokens[1]);
        return -1;
    }

    return 0;
}

// Concatenate and display file contents
ssize_t bn_cat(char **tokens) {
    // Default to stdin if no file specified
    FILE *file = stdin;
    if (tokens[1] != NULL) {
        file = fopen(tokens[1], "r");
        // Error checking
        if (file == NULL) {
            display_error("ERROR: Cannot open file: ", tokens[1]);
            return -1;
        }
    }
    
    // Read and display file contents line by line
    char buffer[MAX_STR_LEN];
    while (fgets(buffer, MAX_STR_LEN, file) != NULL) {
        display_message(buffer);
    }

    // Cleanup
    if (file != stdin) {
        fclose(file);
    }
    return 0;
}

// Word count command
ssize_t bn_wc(char **tokens) {
    // Default to stdin if no file specified
    FILE *file = stdin;
    if (tokens[1] != NULL) {
        file = fopen(tokens[1], "r");
        // Error checking
        if (file == NULL) {
            display_error("ERROR: Cannot open file: ", tokens[1]);
            return -1;
        }
    }

    // Initialize counters
    int line_count = 0;
    int word_count = 0;
    int char_count = 0;
    int in_word = 0;
    char ch;

    // Count characters, words, and lines
    while ((ch = fgetc(file)) != EOF) {
        char_count++;
        if (ch == '\n') line_count++;
        if (isspace(ch)) {
            if (in_word) word_count++;
            in_word = 0;
        } else {
            in_word = 1;
        }
    }

    // Count final word if file doesn't end with whitespace
    if (in_word) word_count++;

    // Format and display results
    char output[MAX_STR_LEN];
    snprintf(output, MAX_STR_LEN, "word count %d\ncharacter count %d\nnewline count %d\n",
             word_count, char_count, line_count);
    display_message(output);

    // Cleanup
    if (file != stdin) {
        fclose(file);
    }
    return 0;
}

#include <signal.h>
#include <errno.h>

// Kill process command
ssize_t bn_kill(char **tokens) {
    // Validate arguments
    if (tokens[1] == NULL) {
        display_error("ERROR: kill requires at least a process ID", "");
        return -1;
    }

    // Parse process ID
    pid_t pid = (pid_t)atoi(tokens[1]);
    if (pid <= 0) {
        display_error("ERROR: Invalid process ID", "");
        return -1;
    }

    // Parse signal number (default to SIGTERM)
    int signum = SIGTERM;
    if (tokens[2] != NULL) {
        signum = atoi(tokens[2]);
        if (signum <= 0) {
            display_error("ERROR: Invalid signal specified", "");
            return -1;
        }
    }

    // Send signal to process
    if (kill(pid, signum) == -1) {
        // Handle specific errors
        if (errno == ESRCH) {
            display_error("ERROR: The process does not exist", "");
        } else {
            display_error("ERROR: Invalid signal specified", "");
        }
        return -1;
    }

    return 0;
}

// Process status command
ssize_t bn_ps(char **tokens) {
    (void)tokens; // Unused parameter

    // Display all active background processes
    for (size_t i = 0; i < bg_count; i++) {
        if (bg[i].pid != -1) { // Check if process is active
            char ps_output[MAX_STR_LEN];
            snprintf(ps_output, MAX_STR_LEN, "%s %d\n", bg[i].command, bg[i].pid);
            display_message(ps_output);
        }
    }
    return 0;
}

// Start server command
ssize_t bn_start_server(char **tokens) {
    // Validate port argument
    if (!tokens[1]) {
        display_error("ERROR: No port provided", "");
        return -1;
    }

    int port = atoi(tokens[1]);
    if (port <= 0 || port > 65535) {
        display_error("ERROR: Invalid port number", "");
        return -1;
    }

    // Create server socket
    server.sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server.sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Set socket options
    int opt = 1;
    setsockopt(server.sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Configure server address
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    // Bind socket to address
    if (bind(server.sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        display_error("ERROR: Adress already in use", "");
        close(server.sockfd);
        return -1;
    }

    // Start listening for connections
    if (listen(server.sockfd, 5) < 0) {
        perror("listen");
        close(server.sockfd);
        return -1;
    }

    // Initialize server state
    server.port = port;
    server.running = 1;
    server.client_count = 0;
    pthread_mutex_init(&server.lock, NULL);

    // Start accept thread
    pthread_t thread;
    pthread_create(&thread, NULL, accept_clients, &server);

    return 0;
}

// Close server command
ssize_t bn_close_server(char **tokens) {
    (void)tokens; // Unused parameter

    // Check if server is running
    if (!server.running) {
        display_error("ERROR: No server running", "");
        return -1;
    }

    // Shutdown server
    server.running = 0;
    close(server.sockfd);

    // Cleanup clients
    pthread_mutex_lock(&server.lock);
    for (int i = 0; i < server.client_count; i++) {
        close(server.clients[i].sockfd);
    }
    server.client_count = 0;
    pthread_mutex_unlock(&server.lock);

    return 0;
}

// Send message command
ssize_t bn_send(char **tokens) {
    // Validate arguments
    if (!tokens[1] || !tokens[2] || !tokens[3]) {
        display_error("ERROR: Usage: send <port> <host> <message>", "");
        return -1;
    }

    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Configure server address
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(atoi(tokens[1]))
    };

    // Convert IP address
    if (inet_pton(AF_INET, tokens[2], &serv_addr.sin_addr) <= 0) {
        display_error("ERROR: Invalid address", "");
        close(sockfd);
        return -1;
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    // Combine message tokens
    char message[BUF_SIZE] = {0};
    for (int i = 3; tokens[i]; i++) {
        strncat(message, tokens[i], sizeof(message)-strlen(message)-1);
        if (tokens[i+1]) strncat(message, " ", sizeof(message)-strlen(message)-1);
    }

    // Send message and close connection
    send(sockfd, message, strlen(message), 0);
    close(sockfd);
    return 0;
}

// Start client command (interactive mode)
ssize_t bn_start_client(char **tokens) {
    // Validate arguments
    if (!tokens[1] || !tokens[2]) {
        display_error("ERROR: Usage: start-client <port> <host>", "");
        return -1;
    }

    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Configure server address
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(atoi(tokens[1]))
    };

    // Convert IP address
    if (inet_pton(AF_INET, tokens[2], &serv_addr.sin_addr) <= 0) {
        display_error("ERROR: Invalid address", "");
        close(sockfd);
        return -1;
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    printf("Connected to server. Type messages or \\connected to check connections.\n");

    // Interactive input loop
    char buffer[BUF_SIZE];
    while (fgets(buffer, sizeof(buffer), stdin)) {
        // Remove newline and send message
        buffer[strcspn(buffer, "\n")] = 0;
        if (send(sockfd, buffer, strlen(buffer), 0) < 0) {
            perror("send");
            break;
        }

        // Check for incoming messages with timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};

        if (select(sockfd+1, &fds, NULL, NULL, &tv) > 0) {
            char recv_buf[BUF_SIZE];
            ssize_t n = recv(sockfd, recv_buf, sizeof(recv_buf)-1, 0);
            if (n > 0) {
                recv_buf[n] = '\0';
                printf("%s\n", recv_buf);
            }
        }
    }

    // Cleanup
    close(sockfd);
    return 0;
}
