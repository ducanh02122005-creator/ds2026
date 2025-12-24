#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stddef.h>

#define CHUNK_SIZE 4096
#define FILENAME_MAX_LEN 256
#define SERVER_RANK 0
#define CLIENT_RANK 1

typedef struct
{
    char method[32];
    char filename[FILENAME_MAX_LEN];
    long long filesize;
} Metadata;

void create_metadata_type(MPI_Datatype *message_type_ptr)
{
    int blocklengths[3] = {32, FILENAME_MAX_LEN, 1};
    MPI_Datatype types[3] = {MPI_CHAR, MPI_CHAR, MPI_LONG_LONG};
    MPI_Aint offsets[3];

    offsets[0] = offsetof(Metadata, method);
    offsets[1] = offsetof(Metadata, filename);
    offsets[2] = offsetof(Metadata, filesize);

    MPI_Type_create_struct(3, blocklengths, offsets, types, message_type_ptr);
    MPI_Type_commit(message_type_ptr);
}

void run_server(MPI_Datatype meta_type)
{
    Metadata meta;
    MPI_Status status;

    // 1. Receive metadata
    MPI_Recv(&meta, 1, meta_type, CLIENT_RANK, 0, MPI_COMM_WORLD, &status);
    printf("[Server] Receiving file: %s (%lld bytes)\n", meta.filename, meta.filesize);

    // 2. Send ACK (200 OK)
    int ack = 200;
    MPI_Send(&ack, 1, MPI_INT, CLIENT_RANK, 1, MPI_COMM_WORLD);

    // 3. Receive file stream
    FILE *f = fopen("received_output.bin", "wb");
    char buffer[CHUNK_SIZE];
    long long total_received = 0;

    while (total_received < meta.filesize)
    {
        int count;
        MPI_Recv(buffer, CHUNK_SIZE, MPI_CHAR, CLIENT_RANK, 2, MPI_COMM_WORLD, &status);
        MPI_Get_count(&status, MPI_CHAR, &count);
        fwrite(buffer, 1, count, f);
        total_received += count;
    }
    fclose(f);

    // 4. Final status
    int final_status = (total_received == meta.filesize) ? 201 : 500;
    MPI_Send(&final_status, 1, MPI_INT, CLIENT_RANK, 3, MPI_COMM_WORLD);
    printf("[Server] Transfer complete. Status: %d\n", final_status);
}

void run_client(MPI_Datatype meta_type, const char *filepath)
{
    struct stat st;
    if (stat(filepath, &st) != 0)
    {
        perror("File error");
        return;
    }

    // 1. Send metadata
    Metadata meta;
    strncpy(meta.method, "UploadFile", 32);
    strncpy(meta.filename, filepath, FILENAME_MAX_LEN);
    meta.filesize = st.st_size;
    MPI_Send(&meta, 1, meta_type, SERVER_RANK, 0, MPI_COMM_WORLD);

    // 2. Wait for ACK
    int ack;
    MPI_Recv(&ack, 1, MPI_INT, SERVER_RANK, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (ack == 200)
    {
        // 3. Send file data in chunks
        FILE *f = fopen(filepath, "rb");
        char buffer[CHUNK_SIZE];
        size_t n;
        while ((n = fread(buffer, 1, CHUNK_SIZE, f)) > 0)
        {
            MPI_Send(buffer, n, MPI_CHAR, SERVER_RANK, 2, MPI_COMM_WORLD);
        }
        fclose(f);
    }

    // 4. Receive final status
    int final_status;
    MPI_Recv(&final_status, 1, MPI_INT, SERVER_RANK, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    printf("[Client] Upload status: %d\n", final_status);
}

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 2)
    {
        if (rank == 0)
            printf("Requires 2 processes.\n");
        MPI_Finalize();
        return 0;
    }

    MPI_Datatype meta_type;
    create_metadata_type(&meta_type);

    if (rank == SERVER_RANK)
    {
        run_server(meta_type);
    }
    else if (rank == CLIENT_RANK)
    {
        if (argc < 2)
        {
            printf("Usage: mpirun -np 2 %s <filename>\n", argv[0]);
        }
        else
        {
            run_client(meta_type, argv[1]);
        }
    }

    MPI_Type_free(&meta_type);
    MPI_Finalize();
    return 0;
}