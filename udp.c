#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <time.h>
#include <errno.h>

#define MAX_SLAVES 16
#define BUFFER_SIZE (1024 * 1024)  // 1MB buffer
#define CONFIG_FILE "config.txt"
#define MULTICAST_GROUP "239.255.255.250"
#define MULTICAST_PORT 19000
#define CHUNK_SIZE 100             // Rows per chunk
#define MAX_RETRIES 3
#define ACK_TIMEOUT 1              // Seconds

typedef struct {
    char ip[16];
    int port;
} SlaveInfo;

typedef struct {
    int n;               // Matrix size
    int p;               // Port number
    int s;               // Status (0=master, 1=slave)
    int t;               // Slave count
    int use_multicast;   // Flag for multicast
    char multicast_group[16];
    int multicast_port;
    SlaveInfo slaves[MAX_SLAVES];
    int **matrix;
} ProgramState;


void read_config(ProgramState *state, int required_slaves) {
    FILE *file = fopen(CONFIG_FILE, "r");
    if (!file) {
        perror("Failed to open config file");
        exit(EXIT_FAILURE);
    }

    state->t = 0;
    char line[100];
    while (state->t < required_slaves && fgets(line, sizeof(line), file)) {
        sscanf(line, "%s %d", state->slaves[state->t].ip, &state->slaves[state->t].port);
        state->t++;
    }
    fclose(file);
}

void allocate_matrix(ProgramState *state) {
    state->matrix = (int **)malloc(state->n * sizeof(int *));
    if (!state->matrix) {
        perror("Matrix allocation failed");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < state->n; i++) {
        state->matrix[i] = (int *)malloc(state->n * sizeof(int));
        if (!state->matrix[i]) {
            perror("Matrix row allocation failed");
            exit(EXIT_FAILURE);
        }
    }
}

void free_matrix(ProgramState *state) {
    if (state->matrix) {
        for (int i = 0; i < state->n; i++) {
            free(state->matrix[i]);
        }
        free(state->matrix);
    }
}

void create_matrix(ProgramState *state) {
    srand(time(NULL));
    for (int i = 0; i < state->n; i++) {
        for (int j = 0; j < state->n; j++) {
            state->matrix[i][j] = rand() % 100 + 1;
        }
    }
}


void setup_multicast_sender(int *sock) {
    *sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (*sock < 0) {
        perror("Multicast socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Allow multiple sockets to use the same port
    int reuse = 1;
    if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("Setsockopt SO_REUSEADDR failed");
        close(*sock);
        exit(EXIT_FAILURE);
    }

    // Set TTL (time-to-live) for multicast packets
    unsigned char ttl = 1; // Local network only
    if (setsockopt(*sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        perror("Setsockopt IP_MULTICAST_TTL failed");
        close(*sock);
        exit(EXIT_FAILURE);
    }
}

void setup_multicast_receiver(int *sock) {
    *sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (*sock < 0) {
        perror("Multicast socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Allow multiple sockets to use the same port
    int reuse = 1;
    if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("Setsockopt SO_REUSEADDR failed");
        close(*sock);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(MULTICAST_PORT);

    if (bind(*sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(*sock);
        exit(EXIT_FAILURE);
    }

    // Join multicast group
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(*sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("Setsockopt IP_ADD_MEMBERSHIP failed");
        close(*sock);
        exit(EXIT_FAILURE);
    }
}

void distribute_multicast(ProgramState *state) {
    struct timeval time_before, time_after;
    gettimeofday(&time_before, NULL);

    int sock;
    setup_multicast_sender(&sock);

    struct sockaddr_in multicast_addr;
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    // Calculate maximum rows per chunk based on UDP packet size
    #define MAX_UDP_PACKET_SIZE 1400  // Safe size for UDP payload
    int max_rows_per_chunk = MAX_UDP_PACKET_SIZE / (state->n * sizeof(int));

    printf("Broadcasting matrix via multicast...\n");
    for (int i = 0; i < state->n; i += max_rows_per_chunk) {
        int rows_to_send = (i + max_rows_per_chunk > state->n) ? state->n - i : max_rows_per_chunk;
        int total_bytes = rows_to_send * state->n * sizeof(int);

        if (sendto(sock, &state->matrix[i][0], total_bytes, 0,
                   (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) != total_bytes) {
            perror("Failed to send matrix chunk");
            exit(EXIT_FAILURE);
        }
    }

    // Send end marker
    int end_marker = -1;
    if (sendto(sock, &end_marker, sizeof(end_marker), 0,
               (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) != sizeof(end_marker)) {
        perror("Failed to send end marker");
        exit(EXIT_FAILURE);
    }

    close(sock);

    gettimeofday(&time_after, NULL);
    double elapsed = (time_after.tv_sec - time_before.tv_sec) +
                     (time_after.tv_usec - time_before.tv_usec) / 1000000.0;
    printf("Multicast distribution completed in %.6f seconds\n", elapsed);
}

void receive_multicast(ProgramState *state) {
    struct timeval time_before, time_after;
    gettimeofday(&time_before, NULL);

    int sock;
    setup_multicast_receiver(&sock);

    printf("Slave listening for multicast on %s:%d...\n", MULTICAST_GROUP, MULTICAST_PORT);

    // First receive metadata
    int metadata[4];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    
    if (recvfrom(sock, metadata, sizeof(metadata), 0,
                (struct sockaddr *)&sender_addr, &sender_len) != sizeof(metadata)) {
        perror("Failed to receive metadata");
        exit(EXIT_FAILURE);
    }

    state->n = metadata[0];
    state->t = metadata[1];
    int master_port = metadata[2];
    int use_multicast = metadata[3];

    printf("Receiving %dx%d matrix via multicast...\n", state->n, state->n);

    // Allocate matrix
    allocate_matrix(state);

    // Receive data chunks
    int end_marker = 0;
    int rows_received = 0;
    while (rows_received < state->n) {
        int max_rows = (state->n - rows_received > CHUNK_SIZE) ? CHUNK_SIZE : state->n - rows_received;
        int expected_bytes = max_rows * state->n * sizeof(int);
        
        ssize_t bytes = recvfrom(sock, &state->matrix[rows_received][0], expected_bytes, 0,
                                (struct sockaddr *)&sender_addr, &sender_len);
        
        if (bytes == sizeof(int) && memcmp(&state->matrix[rows_received][0], &end_marker, sizeof(int)) == 0) {
            break; // Received end marker
        }
        
        if (bytes != expected_bytes) {
            perror("Incomplete data received");
            exit(EXIT_FAILURE);
        }
        
        rows_received += max_rows;
        printf("Received %d rows (%d/%d)\n", max_rows, rows_received, state->n);
    }

    // Send acknowledgment to master (using TCP for reliability)
    int ack_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ack_sock < 0) {
        perror("ACK socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in master_addr;
    memset(&master_addr, 0, sizeof(master_addr));
    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(master_port);
    inet_pton(AF_INET, "127.0.0.1", &master_addr.sin_addr); // Assuming master is on localhost

    if (connect(ack_sock, (struct sockaddr *)&master_addr, sizeof(master_addr)) < 0) {
        perror("ACK connection failed");
        exit(EXIT_FAILURE);
    }

    if (send(ack_sock, "ACK", 4, 0) != 4) {
        perror("Failed to send ACK");
        exit(EXIT_FAILURE);
    }

    close(ack_sock);
    close(sock);

    gettimeofday(&time_after, NULL);
    double elapsed = (time_after.tv_sec - time_before.tv_sec) + 
                    (time_after.tv_usec - time_before.tv_usec) / 1000000.0;
    printf("Matrix reception completed in %.6f seconds\n", elapsed);
}

int main(int argc, char *argv[]) {
    if (argc != 4 && argc != 5) {
        printf("Usage: %s <matrix_size> <port> <status (0=master, 1=slave)> [slave_count]\n", argv[0]);
        return EXIT_FAILURE;
    }

    ProgramState state;
    state.n = atoi(argv[1]);
    state.p = atoi(argv[2]);
    state.s = atoi(argv[3]);
    state.matrix = NULL;
    state.t = 0;
    state.use_multicast = 1;
    strcpy(state.multicast_group, MULTICAST_GROUP);
    state.multicast_port = MULTICAST_PORT;

    if (state.n <= 0) {
        printf("Invalid matrix size. Must be positive\n");
        return EXIT_FAILURE;
    }

    if (state.s == 0) {
        if (argc == 5) {
            state.t = atoi(argv[4]);
        } else {
            printf("Error: Master requires slave count parameter\n");
            return EXIT_FAILURE;
        }
        
        printf("Running as master with %d slaves (using multicast)\n", state.t);
        
        read_config(&state, state.t);
        allocate_matrix(&state);
        create_matrix(&state);
        distribute_multicast(&state);
        free_matrix(&state);
    } else {
        receive_multicast(&state);
    }

    return EXIT_SUCCESS;
}