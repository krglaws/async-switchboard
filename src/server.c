#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

int serve() {
    /* create server socket */
    int listen_sock;
    if ((listen_sock = socket(AF_INET6, SOCK_STREAM, 0)) == -1) {
        perror("socket()");
        return -1;
    }

    /* enable IPv4 and IPv6 dual-stack */
    int optval = 0;
    if (setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &optval, sizeof(optval)) == -1) {
        perror("setsockopt()");
        return -1;
    }

    /* prepare bind to address:port */
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(8080);
    if (inet_pton(AF_INET6, "", &addr.sin6_addr) != 1)

    return 0;
}

int main() {
}
