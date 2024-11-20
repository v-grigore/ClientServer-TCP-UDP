#ifndef SOCK_UTIL_H_
#define SOCK_UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

int send_all(int sockfd, void *buff, size_t len);

int recv_all(int sockfd, void *buff, size_t len);

/* "shortcut" for struct sockaddr structure */
#define SSA			struct sockaddr

int tcp_connect_to_server(const char *name, unsigned short port);
int tcp_close_connection(int s);
int tcp_create_listener(unsigned short port, int backlog);
int get_peer_address(int sockfd, char *buf, size_t len);

int udp_create_socket(unsigned short port);

#ifdef __cplusplus
}
#endif

#endif
