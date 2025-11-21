#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 65432
#define SERVER_IP "127.0.0.1"
#define REQUEST_FILE "source_file.txt"
#define SAVE_AS_FILE "received_source_file.txt"
#define BUFFER_SIZE 4096

/**
 * @brief Initializes and runs the TCP client for file transfer.
 */
int main()
{
    int sock = 0;
    struct sockaddr_in serv_addr;
    char header_buffer[1024] = {0};
    char file_buffer[BUFFER_SIZE];
    long long file_size = 0;
    long long bytes_received = 0;
    ssize_t valread;
    FILE *fp = NULL;

    printf("Attempting to connect to Server at %s:%d\n", SERVER_IP, PORT);

    // Create a socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0)
    {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    // 1. Client connects to the server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection Failed");
        return -1;
    }
    printf("Successfully connected to the server.\n");

    // 2. Client sends the desired filename (Protocol Step 1)
    if (send(sock, REQUEST_FILE, strlen(REQUEST_FILE), 0) < 0)
    {
        perror("Error sending filename");
        goto cleanup;
    }
    printf("Requested file: '%s'\n", REQUEST_FILE);

    // 3. Client waits for server response (Protocol Step 2: OK:<size> or ERROR:...)
    if ((valread = recv(sock, header_buffer, sizeof(header_buffer) - 1, 0)) <= 0)
    {
        printf("Connection closed or error during header reception.\n");
        goto cleanup;
    }
    header_buffer[valread] = '\0'; // Null-terminate

    if (strncmp(header_buffer, "ERROR:", 6) == 0)
    {
        printf("Server returned an error: %s\n", header_buffer);
        goto cleanup;
    }

    if (strncmp(header_buffer, "OK:", 3) != 0)
    {
        printf("Unexpected server response header: %s\n", header_buffer);
        goto cleanup;
    }

    // Parse file size from the header
    if (sscanf(header_buffer, "OK:%lld", &file_size) != 1)
    {
        printf("Error parsing file size from header: %s\n", header_buffer);
        goto cleanup;
    }

    printf("Server acknowledged file. Total size to receive: %lld bytes.\n", file_size);
    printf("Receiving file and saving as: %s\n", SAVE_AS_FILE);

    // Open file for writing
    fp = fopen(SAVE_AS_FILE, "wb");
    if (fp == NULL)
    {
        perror("Error opening file to save data");
        goto cleanup;
    }

    // 4. Client receives file data (Protocol Step 3)
    while (bytes_received < file_size)
    {
        long long remaining_bytes = file_size - bytes_received;
        // Receive up to BUFFER_SIZE or the remaining amount
        ssize_t bytes_to_recv = (remaining_bytes < BUFFER_SIZE) ? remaining_bytes : BUFFER_SIZE;

        valread = recv(sock, file_buffer, bytes_to_recv, 0);

        if (valread <= 0)
        {
            printf("\nConnection closed prematurely or error occurred.\n");
            break;
        }

        // Write received data to file
        fwrite(file_buffer, 1, valread, fp);
        bytes_received += valread;

        // Simple progress indicator (optional)
        double progress = ((double)bytes_received / file_size) * 100.0;
        printf("Progress: %.2f%% (%lld/%lld bytes)\r", progress, bytes_received, file_size);
        fflush(stdout); // Force print
    }

    printf("\nFile transfer complete! Received %lld bytes.\n", bytes_received);

    if (bytes_received == file_size)
    {
        printf("Verification successful: Received size matches expected size.\n");
    }
    else
    {
        printf("Warning: Expected %lld bytes but received %lld bytes.\n", file_size, bytes_received);
    }

cleanup:
    if (fp != NULL)
    {
        fclose(fp);
    }
    // 5. Client closes the connection
    close(sock);
    return 0;
}