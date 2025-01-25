#define _GNU_SOURCE
#include <arpa/inet.h>
#include <stdio.h>
#include <stdbool.h>
#include <kylestructs.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include "logger.h"
#include <errno.h>
#include "map.h"

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
    char hostname[HOST_NAME_MAX];
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

ssize_t read_until_wouldblock(int sock, char *buffer, ssize_t size) {
    ssize_t rb, total = 0;
    while ((rb = recv(sock, buffer, size, 0)) != -1) {
        total += rb;
        if (total == size) {
            break;
        }
    }
    if (errno != EWOULDBLOCK) {
        LOG_ERROR("failed on call to recv(): %s", strerror(errno));
        return -1;
    }
    return total;
}

ssize_t write_until_wouldblock(int sock, char *buffer, ssize_t size) {
    ssize_t wb, total = 0;
    while ((wb = send(sock, buffer + total, size - total, 0)) != -1) {
        total += wb;
        if (total == size) {
            break;
        }
    }
    if (errno != EWOULDBLOCK) {
        LOG_ERROR("failed on call to send(): %s", strerror(errno));
        return -1;
    }
    return total;
}

int get_header(const char *headers_start, const char *headers_end, const char *key, char *value, size_t size) {
    char search_key[128];
    int len = snprintf(search_key, sizeof(search_key), "\r\n%s:", key);
    if (len < 0 || len == sizeof(search_key)) {
        LOG_ERROR("key '%s' is too long (max: %d)", key, sizeof(search_key));
        return -1;
    }

    char *val_start = strcasestr(headers_start, key);
    if (val_start == NULL || val_start > headers_end) {
        return -1;
    }
    val_start += strlen(search_key);
    while (isspace(*val_start)) {
        val_start++;
    }

    char *val_end = val_start;
    while (*val_end != '\r') {
        val_end++;
    }

    len = (val_end - val_start);
    if (len < 1) {
        return -1;
    }

    if ((size_t)(len + 1) > size) {
        return -1;
    }

    memcpy(val_start, value, len);
    val_start[len] = '\0';

    return 0;
}

void read_external_headers(struct context *ctx) {
    ssize_t wb = read_until_wouldblock(ctx->external_sock, ctx->buffer, ctx->buffer_size);
    if (wb == -1) {
        LOG_ERROR("failed to read client socket");
        // close everything and delete context
    }
    ctx->w_offset += wb;

    char *eoh = strstr(ctx->buffer, "\r\n\r\n");
    if (eoh == NULL) {
        if (ctx->w_offset >= ctx->buffer_size) {
            // return 400 (headers too big), disconnect & delete context
        }
        return;
    }
    char host_buffer[HOST_NAME_MAX];
    if (get_header(ctx->buffer, eoh, "host", host_buffer, sizeof(host_buffer)) == -1){
        // return 400 (host header is required)
    }
    const char *ip = get_host_ip(host_buffer);
    if (ip == NULL) {
        // return 404 (host not found)
    }


}


/*
 * READING_EXTERNAL -> (WRITING_INTERNAL <-> READING_EXTERNAL) -> READING_INTERNAL -> (WRITING_EXTERNAL <-> READING_INTERNAL)
 */
int handle_event(struct context *ctx) {
    ssize_t rwb;
    switch (ctx->state) {
        case READING_EXT_HDRS:
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
