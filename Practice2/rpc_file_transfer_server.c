#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

// --- Configuration ---
#define PORT 65432
#define CHUNK_SIZE 4096
#define FILENAME_MAX_LEN 256
#define OUTPUT_DIR "received_files"

// --- RPC-like Metadata Structure (Fixed-Size Header) ---
typedef struct
{
    char method[32]; // e.g., "UploadFile"
    char filename[FILENAME_MAX_LEN];
    long long filesize; // Use long long for large file sizes
} Metadata;

// Utility function to receive exactly 'len' bytes
ssize_t recv_all(int sockfd, void *buf, size_t len)
{
    size_t total = 0;
    ssize_t n;
    while (total < len)
    {
        n = recv(sockfd, buf + total, len - total, 0);
        if (n <= 0)
        {
            // Error or connection closed
            return n;
        }
        total += n;
    }
    return total;
}

// --- Server RPC Implementation (Skeleton) ---

void handle_client(int conn_fd, struct sockaddr_in *client_addr)
{
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    printf("[Server] Connection established with %s:%d\n", client_ip, ntohs(client_addr->sin_port));

    Metadata metadata;
    ssize_t bytes_read;

    // 1. Receive RPC method call (Metadata Header)
    bytes_read = recv_all(conn_fd, &metadata, sizeof(Metadata));
    if (bytes_read <= 0)
    {
        perror("[Server] Error receiving metadata or connection closed");
        close(conn_fd);
        return;
    }

    // Ensure strings are null-terminated for safety
    metadata.method[sizeof(metadata.method) - 1] = '\0';
    metadata.filename[sizeof(metadata.filename) - 1] = '\0';

    if (strcmp(metadata.method, "UploadFile") != 0)
    {
        printf("[Server] Invalid RPC method: %s\n", metadata.method);
        // In a real system, send an error response back.
        close(conn_fd);
        return;
    }

    printf("[Server] Received RPC request: %s. File: '%s', Size: %lld bytes\n",
           metadata.method, metadata.filename, metadata.filesize);

    // 2. Send acknowledgment to start streaming (Status Code 200/OK)
    int ack_code = 200;
    send(conn_fd, &ack_code, sizeof(ack_code), 0);

    // 3. Handle file streaming (The core data transfer)
    long long received_size = 0;
    char buffer[CHUNK_SIZE];
    char output_path[FILENAME_MAX_LEN + sizeof(OUTPUT_DIR) + 2]; // +2 for '/' and '\0'
    int fd;

    // Create output directory if it doesn't exist
    mkdir(OUTPUT_DIR, 0777);

    // Construct output file path
    snprintf(output_path, sizeof(output_path), "%s/%s", OUTPUT_DIR, metadata.filename);

    // Open file descriptor for writing
    if ((fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
    {
        perror("[Server] Failed to open output file");
        close(conn_fd);
        return;
    }

    printf("[Server] Receiving file '%s'...\n", metadata.filename);

    while (received_size < metadata.filesize)
    {
        // Calculate remaining bytes to receive
        size_t to_receive = (metadata.filesize - received_size > CHUNK_SIZE) ? CHUNK_SIZE : (size_t)(metadata.filesize - received_size);

        // Receive chunk
        bytes_read = recv(conn_fd, buffer, to_receive, 0);

        if (bytes_read < 0)
        {
            perror("[Server] Error during file reception");
            // Attempt to clean up partial file
            close(fd);
            unlink(output_path);
            close(conn_fd);
            return;
        }
        if (bytes_read == 0)
        {
            // Connection closed by client before full file received
            break;
        }

        // Write chunk to file
        if (write(fd, buffer, bytes_read) < 0)
        {
            perror("[Server] Error writing to file");
            close(fd);
            unlink(output_path);
            close(conn_fd);
            return;
        }

        received_size += bytes_read;
    }

    // Clean up resources
    close(fd);

    // 4. Send final RPC response (UploadStatus)
    int response_code;
    if (received_size == metadata.filesize)
    {
        response_code = 201; // Created
        printf("[Server] Successfully received %lld bytes for '%s'. Transfer Complete.\n",
               received_size, metadata.filename);
    }
    else
    {
        response_code = 500; // Internal Error
        printf("[Server] Transfer failed. Expected %lld, received %lld.\n",
               metadata.filesize, received_size);
        // Clean up partial file on failure
        unlink(output_path);
    }

    send(conn_fd, &response_code, sizeof(response_code), 0);

    close(conn_fd);
    printf("[Server] Connection closed.\n");
}

void start_server()
{
    int listen_fd = 0, conn_fd = 0;
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // 1. Create socket file descriptor
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("[Server] socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options (optional, allows reuse of address immediately after closure)
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("[Server] setsockopt failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    // 2. Bind the socket to the port
    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("[Server] bind failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // 3. Listen for incoming connections
    if (listen(listen_fd, 10) < 0)
    {
        perror("[Server] listen failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("[Server] Listening on port %d...\n", PORT);

    while (1)
    {
        // 4. Accept connection
        conn_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (conn_fd < 0)
        {
            perror("[Server] accept failed");
            continue;
        }

        // 5. Handle the client request
        handle_client(conn_fd, &client_addr);
    }

    // This part is unreachable in the current infinite loop structure
    close(listen_fd);
}

int main()
{
    start_server();
    return 0;
}