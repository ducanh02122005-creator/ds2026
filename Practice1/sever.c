#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <pthread.h>

#define PORT 65432
#define BUFFER_SIZE 4096
#define TRANSFER_FILE "source_file.txt"
#define MAX_PENDING 5

// Struct to pass data to the client handling thread
struct thread_data
{
    int client_socket;
    struct sockaddr_in client_addr;
};

// Function prototypes
void create_dummy_file();
void *handle_client(void *arg);

/**
 * @brief Creates a dummy file for testing the transfer.
 */
void create_dummy_file()
{
    FILE *fp = fopen(TRANSFER_FILE, "r");
    if (fp == NULL)
    {
        printf("Creating dummy file: %s\n", TRANSFER_FILE);
        fp = fopen(TRANSFER_FILE, "w");
        if (fp == NULL)
        {
            perror("Failed to create dummy file");
            return;
        }
        fprintf(fp, "This is the content of the file being transferred.\n");
        fprintf(fp, "Line 2: The quick brown fox jumps over the lazy dog.\n");
        fprintf(fp, "Line 3: Distributed Systems Practical Work 1 - TCP File Transfer (C version).\n");
        fclose(fp);
        printf("File created successfully.\n");
    }
    else
    {
        printf("Using existing file: %s\n", TRANSFER_FILE);
        fclose(fp);
    }
}

/**
 * @brief Handles a single client connection and file transfer.
 * @param arg Pointer to thread_data structure containing client info.
 * @return NULL
 */
void *handle_client(void *arg)
{
    struct thread_data *data = (struct thread_data *)arg;
    int client_socket = data->client_socket;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(data->client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

    printf("Connected by %s:%d\n", client_ip, ntohs(data->client_addr.sin_port));

    char filename_buffer[1024];
    long long file_size = 0;
    struct stat file_stat;
    FILE *fp = NULL;
    ssize_t bytes_read, bytes_sent;
    char file_buffer[BUFFER_SIZE];
    char header_buffer[64];

    // 1. Server waits for filename request (Protocol Step 1)
    if (recv(client_socket, filename_buffer, sizeof(filename_buffer) - 1, 0) <= 0)
    {
        perror("Error receiving filename or connection closed");
        goto cleanup;
    }
    filename_buffer[sizeof(filename_buffer) - 1] = '\0'; // Null-terminate
    printf("Client requested file: '%s'\n", filename_buffer);

    // Check if the requested file is the one we serve
    if (strcmp(filename_buffer, TRANSFER_FILE) != 0)
    {
        // File not found response
        snprintf(header_buffer, sizeof(header_buffer), "ERROR:File Not Found");
        send(client_socket, header_buffer, strlen(header_buffer), 0);
        printf("Sent error response: File not found.\n");
        goto cleanup;
    }

    // Open file and get size
    if (stat(TRANSFER_FILE, &file_stat) < 0)
    {
        perror("Error getting file stats");
        snprintf(header_buffer, sizeof(header_buffer), "ERROR:Internal Server Error");
        send(client_socket, header_buffer, strlen(header_buffer), 0);
        goto cleanup;
    }
    file_size = file_stat.st_size;

    fp = fopen(TRANSFER_FILE, "rb");
    if (fp == NULL)
    {
        perror("Error opening file");
        snprintf(header_buffer, sizeof(header_buffer), "ERROR:Internal Server Error");
        send(client_socket, header_buffer, strlen(header_buffer), 0);
        goto cleanup;
    }

    // 2. Server sends file size (Protocol Step 2)
    // Protocol: Send 'OK:<file_size>'
    snprintf(header_buffer, sizeof(header_buffer), "OK:%lld", file_size);
    if (send(client_socket, header_buffer, strlen(header_buffer), 0) < 0)
    {
        perror("Error sending header");
        goto cleanup;
    }
    printf("Sent OK response with size: %lld\n", file_size);

    // 3. Server sends file data (Protocol Step 3)
    long long total_bytes_sent = 0;
    while (total_bytes_sent < file_size && (bytes_read = fread(file_buffer, 1, BUFFER_SIZE, fp)) > 0)
    {
        bytes_sent = send(client_socket, file_buffer, bytes_read, 0);
        if (bytes_sent < 0)
        {
            perror("Error sending file data");
            break;
        }
        total_bytes_sent += bytes_sent;
    }

    printf("Successfully sent %lld bytes (File Transfer Complete).\n", total_bytes_sent);

cleanup:
    if (fp != NULL)
    {
        fclose(fp);
    }
    // 4. Server closes the connection (recv() on client will return EOF)
    close(client_socket);
    free(data);
    printf("Connection with %s:%d closed.\n", client_ip, ntohs(data->client_addr.sin_port));
    pthread_exit(NULL);
}

/**
 * @brief Initializes and runs the TCP server.
 */
int main()
{
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int opt = 1;

    create_dummy_file();

    // Create a socket (AF_INET for IPv4, SOCK_STREAM for TCP)
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Prevent "Address already in use" errors after quick restarts
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on any interface
    address.sin_port = htons(PORT);

    // Bind the socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, MAX_PENDING) < 0)
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d. Waiting for connections...\n", PORT);

    while (1)
    {
        printf("Waiting for a connection...\n");
        struct thread_data *data = (struct thread_data *)malloc(sizeof(struct thread_data));
        if (data == NULL)
        {
            perror("malloc failed for thread data");
            continue;
        }

        data->client_socket = accept(server_fd, (struct sockaddr *)&(data->client_addr), (socklen_t *)&addrlen);
        if (data->client_socket < 0)
        {
            perror("accept failed");
            free(data);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, (void *)data) != 0)
        {
            perror("pthread_create failed");
            close(data->client_socket);
            free(data);
        }
        else
        {
            // Detach thread so resources are automatically released when it finishes
            pthread_detach(tid);
        }
    }

    // This part is unreachable, but included for completeness
    close(server_fd);
    return 0;
}