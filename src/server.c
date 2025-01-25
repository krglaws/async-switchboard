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

enum context_state {
    READING_EXT_HDRS,
    WRITING_EXT_HDRS,
    READING_EXT_BODY,
    /* ^
     * | cycle through these 
     * | until body is written
     * v
     */
    WRITING_EXT_BODY,

    READING_INT_HDRS,
    WRITING_INT_HDRS,
    READING_INT_BODY,
    /* ^
     * | cycle through these 
     * | until body is written
     * v
     */
    WRITING_INT_BODY
};

struct context {
    int external_sock;
    int internal_sock;
    size_t r_offset;
    size_t w_offset;
    size_t buffer_size;
    char *buffer;
    enum context_state state;
};

static int setnonblocking(int sock) {
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

static int create_listen_sock(const char *address, uint16_t port) {
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

static int new_connection(int epollfd, int listen_sock) {
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
    struct context *ctx = calloc(1, sizeof(struct context));
    ctx->external_sock = conn_sock;
    ev.data.ptr = ctx;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
        free(ctx);
        close(conn_sock);
        LOG_ERROR("failed on call to epoll_ctl(): %s", strerror(errno));
        return -1;
    }

    return 0;
}

int read_until_wouldblock(int sock, char *buffer, size_t size) {
    int rb, total = 0;
    while ((rb = recv(sock, buffer, size, 0)) != -1) {
        total += rb;
    }
    if (errno != EWOULDBLOCK) {
        LOG_ERROR("failed on call to recv(): %s", strerror(errno));
        return -1;
    }
    return total;
}

/*
 * READING_EXTERNAL -> (WRITING_INTERNAL < - > READING_EXTERNAL) -> READING_INTERNAL -> (WRITING_EXTERNAL < - > READING_INTERNAL)
 */
int handle_event(struct context *ctx) {
    int rwb;
    switch (ctx->state) {
        case READING_EXTERNAL:
            rwb = read_until_wouldblock(ctx->external_sock, ctx->buffer, ctx->buffer_size);
            if (rwb == -1) {
                LOG_ERROR("failed to read client socket");
            }
            ctx->w_offset += rwb;
            char *eoh = strstr(ctx->buffer, "\r\n\r\n");
            if (eoh != NULL) {
                // parse headers
                // grab internal host header
                // connect to internal host
                // set to wrtiing 
            }
            break;
        case WRITING_INTERNAL:
            break;
        case READING_INTERNAL:
            break;
        case WRITING_EXTERNAL:
            break;
        default:
            LOG_ERROR("client context is in undefined state: %d", ctx->state);
            return -1;
    }
    return 0;
}

int serve(const char *address, uint16_t port, int max_events) {
    struct epoll_event ev, events[max_events];

    int listen_sock = create_listen_sock(address, port);
    if (listen_sock == -1) {
        LOG_ERROR("failed to create listening socket");
        return -1;
    }

    int epollfd = epoll_create1(0);
    if (epollfd == -1) {
        LOG_ERROR("failed on call to epoll_create(): %s", strerror(errno));
        return -1;
    }

    struct context *server_ctx = malloc(sizeof(struct context));
    server_ctx->internal_sock = listen_sock;
    ev.events = EPOLLIN;
    ev.data.ptr = server_ctx;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
        LOG_ERROR("failed on call to epoll_ctl(): %s", strerror(errno));
        return -1;
    }

    // TODO: need to use a hashmap to track internal/external socks -> context for cleanup

    while(true) {
        int nfds = epoll_wait(epollfd, events, max_events, -1);
        if (nfds == -1) {
            LOG_ERROR("failed on call to epoll_wait(): %s", strerror(errno));
            goto ERROR;
        }
        struct context *ctx;
        for (int n = 0; n < nfds; ++n) {
            ctx = (struct context *) events[n].data.ptr;
            if (ctx->internal_sock == listen_sock) {
                if (new_connection(epollfd, listen_sock) == -1) {
                    LOG_ERROR("failed to add new client connection");
                }
            } else {
                handle_event(ctx);
            }
        }
    }

ERROR:
    return -1;
}
