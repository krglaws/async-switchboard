#include <sys/epoll.h>
#include <sys/resource.h>
#include <time.h>

#define MAX_CLIENTS 1024
#define MAX_REQUEST_SIZE (10 * 1024)
#define MAX_IDLE_SECONDS 3600

enum context_state {
    IDLE,
    READING_EXTERNAL,
    WRITING_INTERNAL,
    READING_INTERNAL,
    WRITING_EXTERNAL
};

struct client_context {
    int external_fd;          // client socket
    int internal_fd;          // target server socket
    size_t request_size;      // number of bytes in the request
    size_t write_offset;      // number of bytes sent so far
    uint8_t* request_buffer;  // the request
};

int main(int argc, char** argv) {
    struct epoll_event ev, events[MAX_EVENTS];
    int listen_sock, conn_sock, nfds;

    /* Code to set up listening socket, 'listen_sock',
      (socket(), bind(), listen()) omitted */

    int epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = listen_sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (n = 0; n < nfds; ++n) {
            if (events[n].data.fd == listen_sock) {
                conn_sock =
                    accept(listen_sock, (struct sockaddr*)&addr, &addrlen);
                if (conn_sock == -1) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
                setnonblocking(conn_sock);
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = conn_sock;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
                    perror("epoll_ctl: conn_sock");
                    exit(EXIT_FAILURE);
                }
            } else {
                do_use_fd(events[n].data.fd);
            }
        }
    }
}
