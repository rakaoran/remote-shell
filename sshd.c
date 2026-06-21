#define _GNU_SOURCE
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

typedef int fd_t;
const fd_t client_fdt = 0;
const fd_t master_fdt = 1;
const fd_t server_fdt = 2;
typedef struct Connection {
    fd_t fd_type;
    int master_fd;
    int client_fd;
    int server_fd;
    struct Connection *other;
} Connection;

void free_child_processes() {
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}

void register_connection(int epfd, Connection *conn) {
    struct epoll_event event = {.events = EPOLLIN | EPOLLRDHUP, .data.ptr = conn};
    switch (conn->fd_type) {
    case client_fdt:
        epoll_ctl(epfd, EPOLL_CTL_ADD, conn->client_fd, &event);
        break;
    case master_fdt:
        epoll_ctl(epfd, EPOLL_CTL_ADD, conn->master_fd, &event);
        break;
    case server_fdt:
        epoll_ctl(epfd, EPOLL_CTL_ADD, conn->server_fd, &event);
        break;
    }
}
void unregister_connection(int epfd, Connection *conn) {
    struct Connection *other_conn = conn->other;
    switch (conn->fd_type) {
    case client_fdt:
        epoll_ctl(epfd, EPOLL_CTL_DEL, conn->client_fd, NULL);
        epoll_ctl(epfd, EPOLL_CTL_DEL, other_conn->master_fd, NULL);
        close(conn->client_fd);
        close(other_conn->master_fd);
        break;
    case master_fdt:
        epoll_ctl(epfd, EPOLL_CTL_DEL, other_conn->client_fd, NULL);
        epoll_ctl(epfd, EPOLL_CTL_DEL, conn->master_fd, NULL);
        close(conn->master_fd);
        close(other_conn->client_fd);
        break;
    }
    free(conn);
    free(other_conn);
}

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
    Connection *server_conn = malloc(sizeof(Connection));
    server_conn->server_fd = server_fd;
    server_conn->fd_type = server_fdt;
    register_connection(epfd, server_conn);
    error = listen(server_fd, 5);
    if (error == -1) {
        perror("bind");
        exit(1);
    }
    char buf[10000];

    while (1) {
        epoll_wait(epfd, &event, 1, -1);
        uint32_t e = event.events;
        Connection *conn = event.data.ptr;
        if (e & EPOLLIN) {
            if (conn->fd_type == server_fdt) {
                int cfd = accept4(server_fd, NULL, NULL, SOCK_CLOEXEC);
                if (cfd == -1) {
                    perror("accept");
                    continue;
                }
                if (setsockopt(cfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) == -1) {
                    perror("setsockopt KEEPALIVE failed");
                    continue;
                }
                int masterfd;
                int pid = forkpty(&masterfd, NULL, NULL, NULL);
                if (pid == 0) {
                    execlp("bash", "bash", "-i", NULL);
                    perror("execlp(bash)");
                    return 1;
                } else {
                    Connection *master_conn = malloc(sizeof(Connection));
                    Connection *client_conn = malloc(sizeof(Connection));
                    master_conn->other = client_conn;
                    client_conn->other = master_conn;
                    master_conn->fd_type = master_fdt;
                    client_conn->fd_type = client_fdt;
                    master_conn->client_fd = cfd;
                    client_conn->client_fd = cfd;
                    master_conn->master_fd = masterfd;
                    client_conn->master_fd = masterfd;
                    register_connection(epfd, client_conn);
                    register_connection(epfd, master_conn);
                }
            } else if (conn->fd_type == client_fdt) {
                int n = read(conn->client_fd, buf, sizeof(buf));
                if (n <= 0) {
                    unregister_connection(epfd, conn);
                    continue;
                }
                n = write(conn->master_fd, buf, n);
                if (n == -1) {
                    unregister_connection(epfd, conn);
                    continue;
                }

            } else if (conn->fd_type == master_fdt) {
                int n = read(conn->master_fd, buf, sizeof(buf));
                if (n <= 0) {
                    unregister_connection(epfd, conn);
                    continue;
                }
                n = send(conn->client_fd, buf, n, MSG_NOSIGNAL | MSG_DONTWAIT);
                if (n == -1) {
                    unregister_connection(epfd, conn);
                    continue;
                }
            } else {
                fprintf(stderr, "Unexpected fd type: %d\n", conn->fd_type);
                exit(EXIT_FAILURE);
            }
        } else {
            unregister_connection(epfd, conn);

            // a pseudo-terminal might have hanged up
            free_child_processes();
        }
    }
}
