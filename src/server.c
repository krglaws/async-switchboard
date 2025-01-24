#include <arpa/inet.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "logger.h"
#include <errno.h>


struct client_context {
    int external_fd;          // client socket
    int internal_fd;          // target server socket
    size_t request_size;      // number of bytes in the request
    size_t write_offset;      // number of bytes sent so far
    uint8_t* request_buffer;  // the request
};

int setnonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        LOG_ERROR("failed on call to fcntl(F_GETFL): %s", strerror(errno));
        return -1;
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERROR("failed on call to fcntl(F_SETFL): %s", strerror(errno));
        return -1;
    }
    return 0;
}

int create_listen_sock(const char *address, uint16_t port) {
    // create server socket
    int listen_sock = socket(AF_INET6, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        LOG_ERROR("failed on call to socket(): %s", strerror(errno));
        return -1;
    }

    if (setnonblocking(listen_sock) == -1) {
        LOG_ERROR("failed to set listening socket to non-blocking");
        return -1;
    }

    // enable IPv4 and IPv6 dual-stack
    int optval = 0;
    if (setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &optval,
                   sizeof(optval)) == -1) {
        LOG_ERROR("failed to enable dual-stack IP: %s", strerror(errno));
        return -1;
    }

    // prepare bind to address:port
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    int ret = inet_pton(AF_INET6, address, &addr.sin6_addr);
    if (ret == 0) {
        LOG_ERROR("failed to parse address: '%s'", address);
        return -1;
    }
    if (ret == -1) {
        LOG_ERROR("failed on call to inet_pton(): %s", strerror(errno));
        return -1;
    }

    // bind socket
    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        LOG_ERROR("failed on call to bind(): %s", strerror(errno));
        return -1;
    }

    return listen_sock;
}

int new_connection(int epollfd, int listen_sock) {
    struct sockaddr_in6 addr = {0};
    socklen_t addrlen = sizeof(addr);
    int conn_sock = accept(listen_sock, (struct sockaddr*)&addr, &addrlen);
    if (conn_sock == -1) {
        LOG_ERROR("failed on call to accept(): %s", strerror(errno));
        return -1;
    }

    if (setnonblocking(conn_sock) == -1) {
        close(conn_sock);
        LOG_ERROR("failed to set client socket to non-blocking");
        return -1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    struct client_context *ctx = malloc(sizeof(struct client_context));
    ctx->external_fd = conn_sock;
    ev.data.ptr = ctx;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
        free(ctx);
        close(conn_sock);
        LOG_ERROR("failed on call to epoll_ctl(): %s", strerror(errno));
        return -1;
    }

    return 0;
}

int handle_event() {
    
    return 0;
}

int serve(const char *address, uint16_t port, int max_events) {
    struct epoll_event ev, events[max_events];

    int listen_sock = create_listen_sock(address, port);
    if (listen_sock == -1) {
        LOG_ERROR("failed to create listening socket");
        return -1; }

    int epollfd = epoll_create1(0);
    if (epollfd == -1) {
        LOG_ERROR("failed on call to epoll_create(): %s", strerror(errno));
        return -1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = listen_sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
        LOG_ERROR("failed on call to epoll_ctl(): %s", strerror(errno));
        return -1;
    }

    while(true) {
        int nfds = epoll_wait(epollfd, events, max_events, -1);
        if (nfds == -1) {
            LOG_ERROR("failed on call to epoll_wait(): %s", strerror(errno));
            goto ERROR;
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == listen_sock) {
                if (new_connection(epollfd, listen_sock) == -1) {
                    LOG_ERROR("failed to add new client connection");
                }
            } else {
                //do_use_fd(events[n].data.fd);
            }
        }
    }

ERROR:
    return -1;
}
