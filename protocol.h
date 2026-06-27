#include <stdint.h>
#include <sys/types.h>

#define MAX_PACKET_SIZE 16 * 1024

typedef struct _connection {
    int tcp_fd;
    char *inbuf;
    int16_t inlen;
    char *outbuf;
    int16_t outlen;
    int16_t pending_in;
    int16_t pending_out;
    int16_t in_ptr;
    int16_t out_ptr;
} connection;

// Takes address, port and return a file descriptor.
// int proto_listen(char *address, char *port);

int queue_bytes(connection *conn, char *buf, ssize_t len);
int proto_flush(connection *conn);
int proto_load(connection *conn);
int proto_read(connection *conn, char *buf, ssize_t len);
int proto_write(connection *conn, char *buf, ssize_t len);
