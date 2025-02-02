#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>

#include "logger.h"
#include "error_response.h"

#define ERROR_RESPONSE_TMPLT "HTTP/1.1 %d %s\r\n" \
                             "Content-Type: text/plain\r\n" \
                             "Content-Length: %lu\r\n\r\n" \
                             "%d %s"

static const char *http_status_to_string(int code) {
    switch (code) {
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 431: return "Request Header Fields Too Large";
        case 414: return "URI Too Long";
        case 426: return "Upgrade Required";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return NULL;
    }
}

ssize_t build_error_response(http_status status, char *buffer, size_t size) {
    const char *msg = http_status_to_string(status);

    ssize_t len = snprintf(buffer, size, ERROR_RESPONSE_TMPLT, status, msg, strlen(msg), status, msg);

    if (len < 0) {
        LOG_ERROR("failed on call to snprintf(): %s", strerror(errno));
        return -1;
    }

    if (((size_t) len) == size) {
        LOG_ERROR("error response too big (>%d bytes)", size);
        return -1;
    }

    return len;
}

