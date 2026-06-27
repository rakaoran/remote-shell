#include "protocol.h"
#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int queue_bytes(connection *conn, char *buf, ssize_t len) {
    if (conn->outlen - conn->pending_out < len) {
        return -1;
    }
    int head = (conn->out_ptr + conn->pending_out) % conn->outlen;

    if (head + len - 1 >= conn->outlen) {
        int part1len = conn->outlen - head;
        int part2len = len - part1len;
        memcpy(conn->outbuf, buf + head, part1len);
        memcpy(conn->outbuf, buf, part2len);
    } else {
        memcpy(conn->outbuf, buf + conn->out_ptr, len);
    }
    return 0;
}

int proto_flush(connection *conn) {
    while (conn->pending_out != 0) {
        if (conn->out_ptr + conn->pending_out - 1 >= conn->outlen) {
            int n = send(conn->tcp_fd, conn->outbuf + conn->out_ptr, conn->outlen - conn->out_ptr,
                         MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return conn->pending_out;
                }
                return -1;
            }
            conn->pending_out -= n;
            conn->out_ptr = (conn->out_ptr + n) % conn->outlen;
        } else {
            int n = send(conn->tcp_fd, conn->outbuf + conn->out_ptr, conn->pending_out, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return conn->pending_out;
                }
                return -1;
            }
            conn->pending_out -= n;
            conn->out_ptr = (conn->out_ptr + n) % conn->outlen;
        }
    }
    return 0;
}

int proto_load(connection *conn) {
    if (conn->pending_in == conn->inlen) {
        return -1;
    }

    int head = (conn->in_ptr + conn->pending_in) % conn->inlen;
    ssize_t available_space = conn->inlen - conn->pending_in;
    if (conn->in_ptr > head) {
        ssize_t n = read(conn->tcp_fd, conn->inbuf + head, available_space);
        if (n <= 0) {
            return -1;
        }
        conn->pending_in += n;
        return 0;

    } else {
        ssize_t n = read(conn->tcp_fd, conn->inbuf + head, conn->inlen - head);
        if (n <= 0) {
            return -1;
        }

        conn->pending_in += n;
        available_space -= n;
        if (available_space == 0) {
            return 0;
        }
        n = read(conn->tcp_fd, conn->inbuf, available_space);
        if (n <= 0) {
            return -1;
        }
        conn->pending_in += n;
        return 0;
    }
}
/*
PACKET:
[2 size] [n body]
*/
int proto_read(connection *conn, char *buf, ssize_t len) {
    if (conn->pending_in == 0) {
        return 0;
    }
    uint16_t packlen;
    memcpy(&packlen, conn->inbuf, 2);
    packlen = ntohs(packlen);
    if (packlen > MAX_PACKET_SIZE) {
        return -1;
    }
    if (packlen > conn->pending_in) {
        return 0;
    }
    uint16_t payloadlen = packlen - 2;
    if (len < payloadlen) {
        return -1;
    }
    conn->in_ptr += 2;

    if (conn->in_ptr + payloadlen - 1 >= conn->inlen) {
        int part1len = conn->inlen - conn->in_ptr;
        int part2len = payloadlen - payloadlen;
        memcpy(buf, conn->inbuf + conn->in_ptr, part1len);
        memcpy(buf + part1len, conn->inbuf, part2len);
        conn->in_ptr = part2len;
    } else {
        memcpy(buf, conn->inbuf + conn->in_ptr, payloadlen);
        conn->in_ptr = (conn->in_ptr + payloadlen) % conn->inlen;
    }
    conn->pending_in -= packlen;
    return payloadlen;
}

int proto_write(connection *conn, char *buf, ssize_t len) {
    int rv = proto_flush(conn);
    if (rv == -1) {
        return queue_bytes(conn, buf, len);
    }

    int total = 0;
    while (total != len) {
        int sentnow = send(conn->tcp_fd, buf + total, len - total, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sentnow == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return -1;
        }
        total += sentnow;
    }

    return queue_bytes(conn, buf + total, len - total);
}
