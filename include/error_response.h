#ifndef ERROR_RESPONSE_H
#define ERROR_RESPONSE_H

#include <sys/types.h>

typedef enum {
    HTTP_BAD_REQUEST                     = 400, // Client sent a malformed request
    HTTP_UNAUTHORIZED                    = 401, // Authentication required
    HTTP_FORBIDDEN                       = 403, // Client is not allowed to access the resource
    HTTP_NOT_FOUND                       = 404, // Target resource not found
    HTTP_METHOD_NOT_ALLOWED              = 405, // Request method not allowed
    HTTP_REQUEST_TIMEOUT                 = 408, // Client took too long to send the request
    HTTP_REQUEST_HEADER_FIELDS_TOO_LARGE = 431, // Request headers are too large
    HTTP_URI_TOO_LONG                    = 414, // Request URI is too long
    HTTP_UPGRADE_REQUIRED                = 426, // Client must upgrade to a different protocol
    HTTP_INTERNAL_ERROR                  = 500, // Generic server error
    HTTP_BAD_GATEWAY                     = 502, // Invalid response from an upstream server
    HTTP_SERVICE_UNAVAILABLE             = 503, // Upstream server is unavailable
    HTTP_GATEWAY_TIMEOUT                 = 504, // Upstream server took too long to respond
    HTTP_VERSION_NOT_SUPPORTED           = 505  // HTTP version is not supported
} http_status;

ssize_t build_error_response(http_status status, char *buffer, size_t size);

#endif
