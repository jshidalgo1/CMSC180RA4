#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define MAX_SLAVES 10
#define BUFFER_SIZE 1024

typedef struct {
    char ip[100];
    int port;
} ServerInfo;

typedef struct {
    ServerInfo master;
    int num_slaves;
    ServerInfo *slaves;
} Config;

int readConfig(const char *filename, Config *config) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open file");
        return -1;
    }

    fscanf(file, "%99[^:]:%d\n", config->master.ip, &config->master.port);
    fscanf(file, "%d\n", &config->num_slaves);
    config->slaves = malloc(config->num_slaves * sizeof(ServerInfo));
    if (!config->slaves) {
        fclose(file);
        return -1;
    }
    for (int i = 0; i < config->num_slaves; i++) {
        fscanf(file, "%99[^:]:%d\n", config->slaves[i].ip, &config->slaves[i].port);
    }
    fclose(file);
    return 0;
}

void freeConfig(Config *config) {
    free(config->slaves);
}

int create_socket() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Failed to create socket");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

void init_server(int sockfd, int port) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(sockfd, MAX_SLAVES) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
}

void connect_to_server(int sockfd, const char* ip, int port) {
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connect failed");
        exit(EXIT_FAILURE);
    }
}

// Takes a large matrix and divides it into smaller submatrices. 
// Specifically, it divides the large matrix into t smaller submatrices of equal height.
void **createSubmatrices(int **matrix, int n, int t) {
    int submatrixHeight = n / t;
    void **submatrices = malloc(t * sizeof(int *));
    if (!submatrices) {
        perror("Memory allocation failed");
        return NULL;
    }
    for (int i = 0; i < t; i++) {
        submatrices[i] = (int *)(matrix + i * submatrixHeight);
    }
    return submatrices;
}

void printMatrix(int **matrix, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%d ", matrix[i][j]);
        }
        printf("\n");
    }
}

// Function to print submatrices
void printSubmatrices(void **submatrices, int n, int t) {
    int submatrixHeight = n / t;
    for (int i = 0; i < t; i++) {
        int **submatrix = (int **)submatrices[i];
        printf("Submatrix %d:\n", i + 1);
        for (int j = 0; j < submatrixHeight; j++) {
            for (int k = 0; k < n; k++) {
                printf("%d ", submatrix[j][k]);
            }
            printf("\n");
        }
        printf("\n");
    }
}

int send_submatrix(int **submatrix, int submatrixHeight, int n, const char* ip, int port) {
    int sockfd = create_socket();
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = inet_addr(ip);
    if (connect(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Connect failed");
        close(sockfd);
        return -1;
    }
    for (int i = 0; i < submatrixHeight; i++) {
        if (send(sockfd, submatrix[i], n * sizeof(int), 0) < 0) {
            perror("Send failed");
            close(sockfd);
            return -1;
        }
    }
    char buffer[4];
    if (recv(sockfd, buffer, sizeof(buffer), 0) < 0) {
        perror("Receive failed");
        close(sockfd);
        return -1;
    }
    close(sockfd);
    return strncmp(buffer, "ack", 3) == 0 ? 0 : -1;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <matrix size n> <port> <status: 0 for master, 1 for slave>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int n = atoi(argv[1]);
    int port = atoi(argv[2]);
    int status = atoi(argv[3]);

    Config config;
    if (readConfig("./configuration_8.txt", &config) != 0) {
        fprintf(stderr, "Error reading configuration.\n");
        return 1;
    }

    if (status == 0) { // Master
        int **matrix = malloc(n * sizeof(int *));
        for (int i = 0; i < n; i++) {
            matrix[i] = malloc(n * sizeof(int));
            for (int j = 0; j < n; j++) {
                matrix[i][j] = rand() % 100 + 1;
            }
        }
        printf("Master: %s:%d\n", config.master.ip, config.master.port);
        printf("Number of slaves: %d\n", config.num_slaves);
        for (int i = 0; i < config.num_slaves; i++) {
            printf("Slave %d: %s:%d\n", i + 1, config.slaves[i].ip, config.slaves[i].port);
        }
        int t = config.num_slaves;
        int submatrixHeight = n / t;
        int acknowledgments = 0;
        void **submatrices = createSubmatrices(matrix, n, t);
        if (submatrices) {
            //printSubmatrices(submatrices, n, t);
        }
        
        clock_t start_time = clock();
        for (int i = 0; i < t; i++) {
            if (send_submatrix((int **)submatrices[i], submatrixHeight, n, config.slaves[i].ip, config.slaves[i].port) == 0) {
                acknowledgments++;
            }
        }
        if (acknowledgments == t) {
            clock_t end_time = clock();
            double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
            printf("All acknowledgments received. Time elapsed: %f seconds\n", elapsed_time);
        } else {
            printf("Failed to receive all acknowledgments.\n");
        }
        for (int i = 0; i < n; i++) {
            free(matrix[i]);
        }
        free(matrix);
    } else { // Slave
        printf("Slave running. Waiting for master at %s:%d\n", config.master.ip, config.master.port);
        int sockfd = create_socket();
        struct sockaddr_in serv_addr;
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("Bind failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        if (listen(sockfd, 1) < 0) {
            perror("Listen failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
        struct sockaddr_in master_addr;
        socklen_t master_addr_len = sizeof(master_addr);
        int new_sockfd = accept(sockfd, (struct sockaddr *)&master_addr, &master_addr_len);
        if (new_sockfd < 0) {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }
        int submatrixHeight = n / config.num_slaves;
        int **submatrix = malloc(submatrixHeight * sizeof(int *));
        for (int i = 0; i < submatrixHeight; i++) {
            submatrix[i] = malloc(n * sizeof(int));
        }
        clock_t time_before = clock();
        for (int i = 0; i < submatrixHeight; i++) {
            if (recv(new_sockfd, submatrix[i], n * sizeof(int), 0) < 0) {
                perror("Receive failed");
                close(new_sockfd);
                exit(EXIT_FAILURE);
            }
        }
        if (send(new_sockfd, "ack", strlen("ack"), 0) < 0) {
            perror("Send failed");
            close(new_sockfd);
            exit(EXIT_FAILURE);
        }
        clock_t time_after = clock();
        double elapsed_time = (double)(time_after - time_before) / CLOCKS_PER_SEC;
        printf("Received submatrix. Processing time: %f seconds\n", elapsed_time);
        for (int i = 0; i < submatrixHeight; i++) {
            free(submatrix[i]);
        }
        free(submatrix);
        close(new_sockfd);
        close(sockfd);
    }
    freeConfig(&config);
    return 0;
}
