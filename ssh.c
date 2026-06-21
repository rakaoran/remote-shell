#include "stdio.h"
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <pty.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_PORT 10987
int main(int argc, char **argv) {
    struct epoll_event event;
    int epfd = epoll_create1(EPOLL_CLOEXEC);

    unsigned short port;
    if (argc == 1) {
        port = DEFAULT_PORT;
    } else {
        int p = atoi(*(argv + 1));
        if (p < 1 || p > USHRT_MAX) {
            fprintf(stderr, "Invalid port range.\n");
            exit(1);
        }
        port = p;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(0);
    }
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_REUSEADDR failed");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in sa;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    int error = bind(server_fd, (struct sockaddr *)&sa, sizeof(sa));
    if (error == -1) {
        perror("bind");
        exit(1);
    }
}
