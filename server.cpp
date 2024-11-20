#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <ctype.h>
#include <vector>
#include <unordered_set>

#include "sock_util.h"
#include "helpers.h"
#include "w_epoll.h"

static int epollfd;

static int listen_tcp;

static int listen_udp;

static std::vector<struct client> clients;
unsigned int clients_count;

static std::vector<std::tuple<unsigned int, char[51], enum Status>> subs;

char *next_word(char *str) {
    while (*str && *str != '/')
        ++str;
    if (!*str)
        return NULL;
    return str + 1;
}

char *custom_strstr(char *topic, char *wild) {
    char temp[51] = { 0 };
    int k = 0;
    while (*wild && *wild != '/') {
        temp[k] = *wild;
        k++;
        wild++;
    }
    temp[++k] = 0;
    return strstr(topic, temp);
}

bool invalid(char *topic, char *wild) {
    if (*topic == '/' && *wild == '/') {
        topic++;
        wild++;
    }

    if (*wild == '*' && *(wild + 1) == 0)
        return false;

    while (*topic && *wild) {
        if (*wild == '+') {
            if (*topic == '/')
                return true;
            topic = next_word(topic);
            wild = next_word(wild);
            if (topic == wild)
                return false;
            if (!topic || !wild)
                return true;
        } else if (*wild == '*') {
            if (*topic == '/')
                return true;
            wild = next_word(wild);
            if (!wild)
                return false;
            topic = next_word(topic);
            if (!topic)
                return true;
            topic = custom_strstr(topic, wild);
            if (!topic)
                return true;
        } else {
            if (*wild != *topic)
                return true;
            wild++;
            topic++;
        }
    }
    return *topic != *wild;
}

void handle_new_connection() {
    int sockfd;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    struct sockaddr_in addr;
    int rc;
    int flags;
    int sock_opt;
    char id[ID_LEN];

    // Accept new connection
    sockfd = accept(listen_tcp, (SSA *) &addr, &addrlen);
    DIE(sockfd < 0, "accept");

    sock_opt = 1;
    rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY,
                    &sock_opt, sizeof(int));
    DIE(rc < 0, "setsockopt");

    rc = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                    &sock_opt, sizeof(int));
    DIE(rc < 0, "setsockopt");

    // Set non-blocking mode for socket
    flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    // Receive id
    rc = recv_all(sockfd, id, ID_LEN);
    DIE(rc < 0, "recv");

    // Check if id exists
    for (int i = 0; i < clients_count; i++) {
        if (strcmp(id, clients[i].id) != 0)
            continue;

        if (clients[i].status == DISCONNECTED) {
            clients[i].status = CONNECTED;
            clients[i].sockfd = sockfd;
            rc = w_epoll_add_ptr_in(epollfd, sockfd, &clients[i]);
            DIE(rc < 0, "w_epoll_add_in");

            printf("New client %s connected from %s:%d\n",
                   id, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            return;
        }

        char buffer[32];
        struct tcp_header *header = (struct tcp_header *)buffer;
        header->len = 0;
        header->type = DUPLICATE_ID;

        printf("Client %s already connected.\n", id);

        send_all(sockfd, buffer, sizeof(struct tcp_header));

        tcp_close_connection(sockfd);

        return;
    }

    // Add client to cache
    struct client client = {"", sockfd, CONNECTED, clients_count};
    strcpy(client.id, id);
    clients.push_back(client);

    rc = w_epoll_add_ptr_in(epollfd, sockfd, &clients[clients_count]);
    DIE(rc < 0, "w_epoll_add_in");

    clients_count++;

    printf("New client %s connected from %s:%d\n",
           id, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

void handle_tcp_client(struct client *client) {
    char buffer[sizeof(struct tcp_packet)];
    struct tcp_packet *packet = (struct tcp_packet *)buffer;
    int rc;

    rc = recv_all(client->sockfd, buffer, sizeof(struct tcp_packet));
    DIE(rc < 0, "recv");

    if (rc == 0 || packet->type == EXIT) {
        rc = w_epoll_remove_ptr(epollfd, client->sockfd, client);
        DIE(rc < 0, "w_epoll_remove_ptr");

        close(client->sockfd);
        client->status = DISCONNECTED;

        printf("Client %s disconnected.\n", client->id);
        return;
    }

    if (packet->type == SUBSCRIBE) {
        std::tuple<unsigned int, char[51], enum Status> tuple;
        std::get<0>(tuple) = client->index;
        strcpy(std::get<1>(tuple), packet->topic);
        std::get<2>(tuple) = SUBSCRIBED;

        subs.push_back(tuple);
    } else if (packet->type == UNSUBSCRIBE) {
        for (auto & sub : subs) {
            if (std::get<0>(sub) != client->index)
                continue;
            if (strcmp(std::get<1>(sub), packet->topic) != 0)
                continue;
            std::get<2>(sub) = UNSUBSCRIBED;
        }
    }
}

void handle_udp_client() {
    char buffer[1552];
    memset(buffer, 0, 1552);
    struct sockaddr_in address;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    ssize_t recv_len;

    recv_len = recvfrom(listen_udp, buffer, 1551, 0, (SSA *) &address, &addr_len);
    DIE(recv_len < 0, "recvfrom");

    struct udp_packet *packet = (struct udp_packet *)buffer;

    char topic_name[51];
    memset(topic_name, 0, 51);
    memcpy(topic_name, packet->topic, 50);

    char new_buf[1573];
    memset(new_buf, 0, 1573);
    struct tcp_header *header = (struct tcp_header *)new_buf;

    header->address = address;
    header->type = packet->type;

    unsigned int topic_len = strlen(topic_name);
    memcpy(new_buf + sizeof(struct tcp_header), topic_name, topic_len);
    memset(new_buf + sizeof(struct tcp_header) + topic_len, '/', 2);

    header->len = topic_len + 2;

    switch (header->type) {
        case INT:
            header->len += 1 + sizeof(uint32_t);
            break;
        case SHORT_REAL:
            header->len += sizeof(uint16_t);
            break;
        case FLOAT:
            header->len += 1 + sizeof(uint32_t) + sizeof(uint8_t);
            break;
        case STRING:
            header->len += strlen(packet->content);
            break;
        default:
            header->len += 0;
            break;
    }

    memcpy(new_buf + sizeof(struct tcp_header) + topic_len + 2, packet->content, header->len);

    // Set for subscriber indexes
    std::unordered_set<unsigned int> set;
    for (auto & sub : subs) {
        if (std::get<2>(sub) == UNSUBSCRIBED)
            continue;
        if (set.find(std::get<0>(sub)) != set.end())
            continue;
        if (invalid(topic_name, std::get<1>(sub)))
            continue;
        set.insert(std::get<0>(sub));
    }

    for (auto index : set) {
        if (clients[index].status == CONNECTED) {
            send_all(clients[index].sockfd, new_buf, sizeof(struct tcp_header) + header->len);
        }
    }
}

void close_server() {
    for (int i = 0; i < clients_count; ++i) {
        if (clients[i].status == DISCONNECTED)
            continue;

        char buf[32];
        struct tcp_header *header = (struct tcp_header *)buf;
        header->len = 0;
        header->type = EXIT;

        send_all(clients[i].sockfd, buf, sizeof(struct tcp_header));
        tcp_close_connection(clients[i].sockfd);
    }
    tcp_close_connection(listen_tcp);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // Set non-blocking mode for stdin
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    int rc;

    uint16_t port;
    rc = sscanf(argv[1], "%hu", &port);
    DIE(rc != 1, "Given port is invalid");

    epollfd = w_epoll_create();
    DIE(epollfd < 0, "epoll_create");

    listen_tcp = tcp_create_listener(port, MAX_CONNECTIONS);
    DIE(listen_tcp < 0, "tcp_create_listener");

    listen_udp = udp_create_socket(port);
    DIE(listen_udp < 0, "udp_create_socket");

    rc = w_epoll_add_fd_in(epollfd, listen_tcp);
    DIE(rc < 0, "epoll_add");

    rc = w_epoll_add_fd_in(epollfd, listen_udp);
    DIE(rc < 0, "epoll_add");

    rc = w_epoll_add_fd_in(epollfd, STDIN_FILENO);
    DIE(rc < 0, "epoll");

    while (1) {
        struct epoll_event rev;

        rc = w_epoll_wait_infinite(epollfd, &rev);
        DIE(rc < 0, "epoll_wait");

        if (!(rev.events & EPOLLIN))
            continue;

        if (rev.data.fd == STDIN_FILENO) {
            char buffer[BUFSIZ];
            if (!fgets(buffer, sizeof(buffer), stdin) || isspace(buffer[0])) {
                close_server();
                return 0;
            }
            if (strcmp(buffer, "exit\n") == 0) {
                close_server();
                return 0;
            }
            continue;
        }

        if (rev.data.fd == listen_tcp) {
            handle_new_connection();
        } else if (rev.data.fd == listen_udp) {
            handle_udp_client();
        } else {
            handle_tcp_client((struct client *)rev.data.ptr);
        }
    }

    return 0;
}
