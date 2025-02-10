#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <kylestructs.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "logger.h"
#include "map.h"
#include "error_response.h"

#define CLIENT_BUFFER_SIZE (1024 * 8)

typedef enum {
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
    WRITING_INT_BODY,

    /* error response states which can
     * be entered at any time
     */
    WRITING_EXT_ERR,  // -> READING_EXT_HDRS
    WRITING_EXT_ERR_CLOSE, // -> close
} context_state;

struct context {
    int external_sock;
    int internal_sock;
    enum context_state state;
    size_t r_offset;
    size_t w_offset;
    size_t remaining;
    char *external_address;
    char *internal_hostname;
    char *buffer;
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
    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        LOG_ERROR("failed on call to bind(): %s", strerror(errno));
        return -1;
    }

    return listen_sock;
}

static void free_context(struct context *ctx) {
    if (ctx->internal_hostname != NULL) {
        free(ctx->internal_hostname);
    }
    if (ctx->external_address != NULL) {
        free(ctx->external_address);
    }
    if (ctx->buffer != NULL) {
        free(ctx->buffer);
    }
    free(ctx);
}

static void close_context(int epollfd, struct context *ctx) {
    // TODO: might want to rewrite this so that it checks for
    // which of these sockets is actually tracked by epollfd
    if (ctx->external_sock != 0) {
        epoll_ctl(epollfd, EPOLL_CTL_DEL, ctx->external_sock, NULL);
        close(ctx->external_sock);
    }
    if (ctx->internal_sock != 0) {
        epoll_ctl(epollfd, EPOLL_CTL_DEL, ctx->internal_sock, NULL);
        close(ctx->internal_sock);
    }
    free_context(ctx);
}

static int new_context(int epollfd, int listen_sock) {
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    int client_sock = accept(listen_sock, (struct sockaddr *)&addr, &addrlen);
    if (client_sock == -1) {
        LOG_ERROR("failed on call to accept(): %s", strerror(errno));
        return -1;
    }

    if (setnonblocking(client_sock) == -1) {
        close(client_sock);
        LOG_ERROR("failed to set client socket to non-blocking");
        return -1;
    }

    struct context *ctx = calloc(1, sizeof(struct context));
    ctx->external_sock = client_sock;
    ctx->internal_hostname = malloc(HOST_NAME_MAX);
    ctx->buffer = malloc(CLIENT_BUFFER_SIZE);
    ctx->external_address = malloc(MAX_IP_LEN);
    const void *sin_addr = &(((struct sockaddr_in *) &addr)->sin_addr);
    if (addr.ss_family == AF_INET6) {
        sin_addr = &(((struct sockaddr_in6 *) &addr)->sin6_addr);
    }
    if (inet_ntop(addr.ss_family, sin_addr, ctx->external_address, MAX_IP_LEN) == NULL) {
        LOG_ERROR("failed to read client address: %s", strerror(errno));
        strcpy(ctx->external_address, "UNKNOWN");
    }

    struct epoll_event ev = {
        .events=EPOLLIN,
        .data.ptr=ctx,
    };
    ev.events = EPOLLIN;
    ev.data.ptr = ctx;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sock, &ev) == -1) {
        free_context(ctx);
        close(client_sock);
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

int get_header(const char *headers_start, const char *headers_end,
               const char *key, char *value, size_t size) {
    char search_key[128];
    int len = snprintf(search_key, sizeof(search_key), "\r\n%s:", key);
    if (len == sizeof(search_key)) {
        LOG_ERROR("key '%s' is too long (max: %d)", key, sizeof(search_key));
        return -1;
    }

    if (len < 0) {
        LOG_ERROR("failed on call to snprintf(): %s", strerror(errno));
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

static int change_context_direction(int epollfd, struct context *ctx, int add_sock, int epoll_event) {
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, ctx->internal_sock, NULL) == -1) {
        if (errno != ENOENT) {
            LOG_ERROR("failed on call to epoll_ctl(): %s", strerror(errno));
            return -1;
        }
    }
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, ctx->external_sock, NULL) == -1) {
        if (errno != ENOENT) {
            LOG_ERROR("failed on call to epoll_ctl(): %s", strerror(errno));
            return -1;
        }
    }
    struct epoll_event ev = {
        .events=epoll_event,
        .data.ptr=ctx,
    };
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, add_sock, &ev) == -1) {
        LOG_ERROR("failed on call to epoll_ctl(): %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int set_reading_external(int epollfd, struct context *ctx) {
    return change_context_direction(epollfd, ctx, ctx->external_sock, EPOLLIN);
}

static int set_writing_external(int epollfd, struct context *ctx) {
    return change_context_direction(epollfd, ctx, ctx->external_sock, EPOLLOUT);
}

static int set_reading_internal(int epollfd, struct context *ctx) {
    return change_context_direction(epollfd, ctx, ctx->internal_sock, EPOLLIN);
}

static int set_writing_internal(int epollfd, struct context *ctx) {
    return change_context_direction(epollfd, ctx, ctx->internal_sock, EPOLLOUT);
}

static int set_sending_error(int epollfd, struct context *ctx, http_status status) {
    if (set_writing_external(epollfd, ctx) == -1) {
        LOG_ERROR("failed to monitor external socket for writing");
        return -1;
    }
    ssize_t size = build_error_response(status, ctx->buffer, CLIENT_BUFFER_SIZE);
    if (size == -1) {
        LOG_ERROR("failed to build error response");
        return -1;
    }
    ctx->r_offset = 0;
    ctx->w_offset = size;
    ctx->remaining = size;
}

void read_external_headers(int epollfd, struct context *ctx) {
    ssize_t wb = read_until_wouldblock(ctx->external_sock, ctx->buffer + ctx->w_offset,
                                       CLIENT_BUFFER_SIZE - ctx->w_offset);
    if (wb == -1) {
        LOG_ERROR("failed to read client socket");
        close_context(epollfd, ctx);
        return;
    }

    if (wb == 0) {
        LOG_INFO("connection to %s closed", ctx->external_address);
        close_context(epollfd, ctx);
        return;
    }

    ctx->w_offset += wb;

    char *eoh = strstr(ctx->buffer, "\r\n\r\n");
    if (eoh == NULL) {
        if (ctx->w_offset >= CLIENT_BUFFER_SIZE) {
            // headers are too big
            ctx->state = WRITING_EXT_ERR_CLOSE;
            if (set_sending_error(epollfd, ctx, HTTP_REQUEST_HEADER_FIELDS_TOO_LARGE) == -1) {
                LOG_ERROR("failed to send error message to client");
                close_context(epollfd, ctx);
            }
            return;
        }
    }
    char host_buffer[HOST_NAME_MAX];
    if (get_header(ctx->buffer, eoh, "host", host_buffer,
                   sizeof(host_buffer)) == -1) {
        // host header missing
        ctx->state = WRITING_EXT_ERR;
        if (set_sending_error(epollfd, ctx, HTTP_BAD_REQUEST) == -1) {
            LOG_ERROR("failed to send error message to client");
            close_context(epollfd, ctx);
        }
        return;
    }
    if (strcmp(host_buffer, ctx->internal_hostname) == 0) {
        ctx->state = WRITING_EXT_HDRS;
        if (set_writing_internal(epollfd, ctx) == 0) {
            ctx->r_offset = 0;
            return;
        }
            LOG_ERROR("failed to send error message to client");
            ctx->state = WRITING_EXT_ERR;
            if (set_sending_error(epollfd, ctx, HTTP_REQUEST_HEADER_FIELDS_TOO_LARGE) == -1) {
                LOG_ERROR("failed to send error message to client");
            }
            close_context(epollfd, ctx);
        }
        return;
    }
    const struct host_info* hi = get_host_info(host_buffer);
    if (hi == NULL) {
        ctx->state = WRITING_EXT_ERR;
        if (set_writing_external(epollfd, ctx) == -1) {
            // host not found
            LOG_ERROR("failed to send error message to client");
            close_context(epollfd, ctx);
        }
        return;
    }

    if (ctx->internal_sock != 0) {
        close(ctx->internal_sock);
    }
    ctx->internal_sock = socket(AF_INET6, SOCK_STREAM, 0);
    if (setnonblocking(ctx->internal_sock) == -1) {
        LOG_ERROR("failed to set client socket to non-blocking");
        // return 500
    }

    struct in6_addr addr;
    if (inet_pton(AF_INET6, hi->ip, &addr) == -1) {
        LOG_ERROR("failed to convert address '%s': %s", host_buffer, strerror(errno));
        // return 500
    }

    if (connect(ctx->internal_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1 && errno != EINPROGRESS) {
        // return 500
    }

    ctx->state = WRITING_EXT_HDRS;
    struct epoll_event ev = {
        .events=EPOLLOUT,
        .data.ptr=ctx
    };
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, ctx->external_sock, NULL) == -1) {
        // return 500
    }
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, ctx->internal_sock, &ev) == -1) {
        // return 500
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

    struct context *server_ctx = calloc(1, sizeof(struct context));
    server_ctx->internal_sock = listen_sock;
    ev.events = EPOLLIN;
    ev.data.ptr = server_ctx;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
        LOG_ERROR("failed on call to epoll_ctl(): %s", strerror(errno));
        return -1;
    }

    // TODO: need to use a hashmap to track internal/external socks -> context for cleanup

    while (true) {
        int nfds = epoll_wait(epollfd, events, max_events, -1);
        if (nfds == -1) {
            LOG_ERROR("failed on call to epoll_wait(): %s", strerror(errno));
            goto ERROR;
        }
        struct context *ctx;
        for (int n = 0; n < nfds; ++n) {
            ctx = (struct context *)events[n].data.ptr;
            if (ctx->internal_sock == listen_sock) {
                if (new_context(epollfd, listen_sock) == -1) {
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
