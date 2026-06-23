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
#include <termios.h>
#include <unistd.h>

#define DEFAULT_PORT 10987

void enable_raw_mode();
void disable_raw_mode();

int main(int argc, char **argv) {
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

    int err = connect(server_fd, (struct sockaddr *)&sa, sizeof(sa));

    if (err == -1) {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    enable_raw_mode();

    struct epoll_event server_event;
    server_event.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP | EPOLLERR;
    server_event.data.fd = server_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &server_event);
    struct epoll_event stdin_event;
    stdin_event.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP | EPOLLERR;
    stdin_event.data.fd = STDIN_FILENO;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_event);

    struct epoll_event event;
    char c;
    while (1) {
        err = epoll_wait(epfd, &event, 1, -1);
        if (err == -1) {
            perror("epoll_wait");
            break;
        }
        if (event.events & (EPOLLIN)) {
            if (event.data.fd == server_fd) {
                err = read(server_fd, &c, 1);
                if (err == -1) {
                    perror("read from server");
                    break;
                }
                if (err == 0) {
                    break;
                }
                err = write(STDIN_FILENO, &c, 1);
                if (err == -1) {
                    perror("read from server");
                    break;
                }
            } else {
                err = read(STDIN_FILENO, &c, 1);
                if (err <= 0) {
                    perror("read from stdin");
                    break;
                }
                err = send(server_fd, &c, 1, MSG_NOSIGNAL);
                if (err == -1) {
                    perror("read from server");
                    break;
                }
            }
        } else {
            break;
        }
    }
    close(server_fd);
    close(epfd);
    disable_raw_mode();
}
struct termios old, new;
void enable_raw_mode() {
    tcgetattr(0, &old);

    cfmakeraw(&new);
    tcsetattr(STDIN_FILENO, 0, &new);
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, 0, &old);
}
