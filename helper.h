#ifndef HELPER_H
#define HELPER_H

#include <pthread.h>  // Required for pthread functions and types

// Maximum number of simultaneous client connections
#define MAX_CLIENTS 10

// Length of client ID string (including null terminator)
#define CLIENT_ID_LEN 16

// Size of communication buffer
#define BUF_SIZE 1024

// Client structure to store information about connected clients
typedef struct Client {
    int sockfd;                 // Socket file descriptor for client connection
    char id[CLIENT_ID_LEN];     // Unique identifier for the client
    pthread_t thread;           // Thread handling this client's communication
} Client;

// Server structure to manage the server state
typedef struct Server {
    int sockfd;                 // Main server socket file descriptor
    int port;                   // Port number server is listening on
    int running;                // Flag indicating if server is active (1) or shutting down (0)
    int client_count;           // Current number of connected clients
    Client clients[MAX_CLIENTS];// Array of connected clients
    pthread_mutex_t lock;       // Mutex for thread-safe access to server data
} Server;

// Function prototypes:

/**
 * @brief Handles communication with a connected client
 * @param arg Pointer to Client structure for this connection
 * @return void* Always returns NULL
 */
void *handle(void *arg);

/**
 * @brief Broadcasts a message to all connected clients
 * @param server Pointer to Server structure
 * @param message The message to broadcast
 * @param id Identifier of the sending client (for excluding from broadcast)
 */
void broadcast_message(struct Server *server, const char *message, const char *id);

#endif
