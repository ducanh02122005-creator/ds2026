#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

// --- Configuration ---
#define HOST "127.0.0.1"
#define PORT 65432
#define CHUNK_SIZE 4096
#define FILENAME_MAX_LEN 256

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

// Utility function to get file size
long long get_file_size(const char *filepath)
{
    struct stat st;
    if (stat(filepath, &st) == 0)
    {
        return st.st_size;
    }
    return -1;
}

// --- Client RPC Implementation (Stub) ---

void client_upload_file(const char *filepath)
{
    int sock_fd = 0;
    struct sockaddr_in serv_addr;
    long long file_size = get_file_size(filepath);

    if (file_size < 0)
    {
        perror("[Client] Error: File not found or cannot be accessed");
        return;
    }

    // Extract filename from full path
    const char *filename_ptr = strrchr(filepath, '/');
    if (!filename_ptr)
    {
        filename_ptr = strrchr(filepath, '\\'); // Handle Windows paths
    }
    if (!filename_ptr)
    {
        filename_ptr = filepath;
    }
    else
    {
        filename_ptr++; // Move past the separator
    }

    // Check filename length
    if (strlen(filename_ptr) >= FILENAME_MAX_LEN)
    {
        printf("[Client] Error: Filename is too long.\n");
        return;
    }

    // 1. Build RPC request for UploadFile (Metadata)
    Metadata metadata;
    memset(&metadata, 0, sizeof(Metadata));
    strncpy(metadata.method, "UploadFile", sizeof(metadata.method) - 1);
    strncpy(metadata.filename, filename_ptr, sizeof(metadata.filename) - 1);
    metadata.filesize = file_size;

    // 2. Create socket
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("[Client] Socket creation failed");
        return;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, HOST, &serv_addr.sin_addr) <= 0)
    {
        perror("[Client] Invalid address/ Address not supported");
        close(sock_fd);
        return;
    }

    // 3. Connect to the server
    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("[Client] Connection failed");
        close(sock_fd);
        return;
    }
    printf("[Client] Connected to server at %s:%d\n", HOST, PORT);

    // 4. Send RPC metadata/request
    if (send(sock_fd, &metadata, sizeof(Metadata), 0) < 0)
    {
        perror("[Client] Failed to send metadata");
        close(sock_fd);
        return;
    }

    // 5. Wait for Server Acknowledgment (Status Code)
    int ack_code;
    if (recv_all(sock_fd, &ack_code, sizeof(ack_code)) <= 0 || ack_code != 200)
    {
        printf("[Client] Server not ready or sent invalid acknowledgment (%d).\n", ack_code);
        close(sock_fd);
        return;
    }

    // 6. Stream file data (The core data transfer)
    printf("[Client] Sending file '%s' (%lld bytes)...\n", metadata.filename, metadata.filesize);

    FILE *file = fopen(filepath, "rb");
    if (!file)
    {
        perror("[Client] Failed to open file for reading");
        close(sock_fd);
        return;
    }

    char buffer[CHUNK_SIZE];
    size_t bytes_read;
    long long bytes_sent = 0;
    clock_t start_time = clock();

    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, file)) > 0)
    {
        ssize_t sent = 0;
        // Ensure all bytes read are sent (handle partial sends)
        while (sent < bytes_read)
        {
            ssize_t n = send(sock_fd, buffer + sent, bytes_read - sent, 0);
            if (n < 0)
            {
                perror("[Client] Send error");
                fclose(file);
                close(sock_fd);
                return;
            }
            sent += n;
            bytes_sent += n;
        }
    }

    clock_t end_time = clock();
    double time_taken = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    // 7. Signal EOF (Shutdown write side)
    if (shutdown(sock_fd, SHUT_WR) < 0)
    {
        perror("[Client] Shutdown failed");
    }

    fclose(file);

    // 8. Receive final RPC response (UploadStatus Code)
    int response_code;
    if (recv_all(sock_fd, &response_code, sizeof(response_code)) <= 0)
    {
        printf("\n[Client] Did not receive final status from server.\n");
    }
    else
    {
        if (response_code == 201)
        {
            printf("\n[Client] SUCCESS: File received successfully (HTTP 201 Created).\n");
            printf("[Client] Sent %lld bytes in %.2f seconds.\n", bytes_sent, time_taken);
        }
        else
        {
            printf("\n[Client] FAILURE: Server returned status code %d.\n", response_code);
        }
    }

    close(sock_fd);
}

int main(int argc, char const *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <path_to_file_to_send>\n", argv[0]);
        return EXIT_FAILURE;
    }

    client_upload_file(argv[1]);

    return 0;
}