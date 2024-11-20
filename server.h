#ifndef SERVER_H
#define SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arpa/inet.h>

#define MAX_CONNECTIONS 32
#define ID_LEN 11

enum Status {
    CONNECTED,
    DISCONNECTED,
    SUBSCRIBED,
    UNSUBSCRIBED
};

enum Type {
    INT,
    SHORT_REAL,
    FLOAT,
    STRING,
    DUPLICATE_ID,
    SUBSCRIBE,
    UNSUBSCRIBE,
    EXIT
};

struct udp_packet {
    char topic[50];
    unsigned char type;
    char content[1501];
} __attribute__((packed));

struct tcp_packet {
    unsigned char type;
    char topic[51];
};

// | address | type | len | topic | "//" | content |
struct tcp_header {
    struct sockaddr_in address;
    unsigned char type;
    unsigned short len;
};

struct client {
    char id[ID_LEN];
    int sockfd;
    enum Status status;
    unsigned int index;
};

#ifdef __cplusplus
}
#endif

#endif
