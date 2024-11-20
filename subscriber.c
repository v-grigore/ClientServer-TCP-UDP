#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>

#include "sock_util.h"
#include "helpers.h"
#include "w_epoll.h"
#include "server.h"

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // Set non-blocking mode for stdin
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    int rc;
    int sock_opt;

    // Parsam port-ul ca un numar
    uint16_t port;
    rc = sscanf(argv[3], "%hu", &port);
    DIE(rc != 1, "Given port is invalid");

    int sockfd = tcp_connect_to_server(argv[2], port);

    // Set non-blocking mode for socket
    flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    sock_opt = 1;
    rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
                    &sock_opt, sizeof(int));
    DIE(rc < 0, "setsockopt");

    rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                    &sock_opt, sizeof(int));
    DIE(rc < 0, "setsockopt");

    // Send id
    char id[ID_LEN];
    strcpy(id, argv[1]);
    rc = send_all(sockfd, id, ID_LEN);
    DIE(rc < 0, "send");

    int epollfd = w_epoll_create();
    DIE(epollfd < 0, "w_epoll_create");

    rc = w_epoll_add_fd_in(epollfd, sockfd);
    DIE(rc < 0, "epoll");

    rc = w_epoll_add_fd_in(epollfd, STDIN_FILENO);
    DIE(rc < 0, "epoll");

    while (1) {
        struct epoll_event rev;

        rc = w_epoll_wait_infinite(epollfd, &rev);
        DIE(rc < 0, "epoll_wait");

        if (rev.data.fd == sockfd) {
            char buffer[1574];
            struct sockaddr_in address;

            buffer[1573] = 0;

            rc = recv_all(sockfd, buffer, sizeof(struct tcp_header));
            DIE(rc < 0, "recv");

            struct tcp_header *header = (struct tcp_header *)buffer;

            if (header->type == DUPLICATE_ID)
                printf("Client %s already connected.\n", id);

            if (header->type == DUPLICATE_ID || header->type == EXIT)
                return 0;

            rc = recv_all(sockfd, buffer + sizeof(struct tcp_header), header->len);
            DIE(rc < 0, "recv");

            char *delimiter = strstr(buffer + sizeof(struct tcp_header), "//");
            *delimiter = 0;

            address = header->address;

            printf("%s:%d - %s - ",
                   inet_ntoa(address.sin_addr), ntohs(address.sin_port), buffer + sizeof(struct tcp_header));

            char *content = delimiter + 2;

            switch (header->type) {
                case INT: {
                    printf("INT - ");
                    uint32_t num = ntohl(*((uint32_t *) (content + 1)));
                    if (*content && num)
                        printf("-");
                    printf("%u\n", num);
                    break;
                }
                case SHORT_REAL: {
                    printf("SHORT_REAL - %.2f\n", 1. * ntohs(*((uint16_t *) (content))) / 100);
                    break;
                }
                case FLOAT: {
                    printf("FLOAT - ");
                    if (*content)
                        printf("-");
                    double num = (double) ntohl(*((uint32_t *) (content + 1)));
                    uint8_t pos = *((uint8_t *) (content + 1 + sizeof(uint32_t)));
                    unsigned int pow = 1;
                    while (pos--)
                        pow *= 10;
                    printf("%f\n", num / pow);
                    break;
                }
                case STRING: {
                    printf("STRING - %s\n", content);
                    break;
                }
            }
        } else if (rev.data.fd == STDIN_FILENO) {
            char buf[BUFSIZ];
            if (!fgets(buf, sizeof(buf), stdin) || isspace(buf[0])) {
                close(epollfd);
                return 0;
            }

            char *temp = strchr(buf, '\n');
            if (temp)
                *temp = 0;

            struct tcp_packet packet;

            if (strstr(buf, "unsubscribe ")) {
                packet.type = UNSUBSCRIBE;
                strcpy(packet.topic, buf + 12);
                send_all(sockfd, &packet, sizeof(struct tcp_packet));
                printf("Unsubscribed to topic %s\n", packet.topic);
            } else if (strstr(buf, "subscribe ")) {
                packet.type = SUBSCRIBE;
                strcpy(packet.topic, buf + 10);
                send_all(sockfd, &packet, sizeof(struct tcp_packet));
                printf("Subscribed to topic %s\n", packet.topic);
            } else if (strstr(buf, "exit")) {
                packet.type = EXIT;
                send_all(sockfd, &packet, sizeof(struct tcp_packet));
                return 0;
            }
        }
    }

    return 0;
}
