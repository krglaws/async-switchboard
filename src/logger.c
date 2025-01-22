#define _GNU_SOURCE
#include "logger.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <kylestructs.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int in_fd;
    int out_fd;
} logger_args_t;

typedef struct {
    char *msg;
    size_t size;
    size_t written;
} logmsg_t;

static int write_pipe;
static ks_list *log_queue = NULL;

static void *logger_main(void *args) {
    int in_fd = ((logger_args_t *)args)->in_fd;
    int out_fd = ((logger_args_t *)args)->out_fd;
    char buffer[LOG_BUFFER_SIZE];

    while (true) {
        int read_bytes = read(in_fd, buffer, LOG_BUFFER_SIZE);
        if (read_bytes < 0) {
            perror("read()");
            exit(EXIT_FAILURE);
        }

        ssize_t written_bytes = 0;
        do {
            ssize_t curr = write(out_fd, buffer, read_bytes);
            if (curr == -1) {
                perror("write()");
                exit(EXIT_FAILURE);
            }
            written_bytes += curr;
        } while (written_bytes < read_bytes);
    }
}

static void end_logger() {
    ks_iterator *iter = ks_iterator_new(log_queue, KS_LIST);
    const ks_datacont *dc;
    while ((dc = ks_iterator_next(iter)) != NULL) {
        free(((logmsg_t *)dc->vp)->msg);
        free(dc->vp);
    }
    ks_list_delete(log_queue);
    ks_iterator_delete(iter);
}

int init_logger() {
    log_queue = ks_list_new();

    logger_args_t args;
    int rw[2];
    if (pipe(rw) == -1) {
        perror("pipe()");
        return -1;
    }

    args.in_fd = rw[0];
    args.out_fd = 1;
    write_pipe = rw[1];

    int flags = fcntl(rw[0], F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(write_pipe, F_SETFL, flags) == -1) {
        perror("fcntl(F_SETFL)");
        return -1;
    }

    int err;
    pthread_t thread;
    if ((err = pthread_create(&thread, NULL, logger_main, &args)) != 0) {
        fprintf(stderr, "pthread_create(): %s\n", strerror(err));
        return -1;
    }

    if (atexit(end_logger) != 0) {
        perror("atexit()");
        return -1;
    }

    return 0;
}

static int filter_out_specials(const char *src, size_t src_len, char *dst,
                               size_t dst_len) {
    const char *src_end = src + src_len;
    const char *dst_end = dst + dst_len - 1;

    while (src < src_end && dst < dst_end) {
        char c = *src++;
        if (isprint(c)) {
            *dst++ = c;
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (dst + 2 >= dst_end) {
                return -1;
            }
            *dst++ = '\\';
            *dst++ = (c == '\n') ? 'n' : 'r';
            continue;
        }
        if (dst + 3 >= dst_end) {
            return -1;
        }
        *dst++ = '<';
        *dst++ = '?';
        *dst++ = '>';
    }
    if (dst >= dst_end) {
        return -1;
    }
    *dst = '\0';
    return 0;
}

#define LOG_TOO_BIG_MSG "Omitting log message at %s:%d -- too big!"

void emit_log(const char *level, const char *filename, int lineno,
              const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    char *logbuff = malloc(sizeof(char) * LOG_BUFFER_SIZE);
    size_t len;
    if ((len = vsnprintf(logbuff, LOG_BUFFER_SIZE, fmt, ap)) >=
        LOG_BUFFER_SIZE) {
        len = sprintf(logbuff, LOG_TOO_BIG_MSG, filename, lineno);
    }
    va_end(ap);

    char filtered[LOG_BUFFER_SIZE];
    if (filter_out_specials(logbuff, len, filtered, LOG_BUFFER_SIZE) == -1) {
        sprintf(filtered, LOG_TOO_BIG_MSG, filename, lineno);
    }

    char timebuff[32];
    time_t rawtime = time(NULL);
    struct tm *ptm = localtime(&rawtime);
    strftime(timebuff, sizeof(timebuff), "%x %T", ptm);

    if ((len = snprintf(logbuff, LOG_BUFFER_SIZE, "(%s)%s:%d -- %s%s\n",
                        timebuff, filename, lineno, level, filtered)) >=
        LOG_BUFFER_SIZE) {
        len = sprintf(logbuff, LOG_TOO_BIG_MSG, filename, lineno);
    }

    logmsg_t *logmsg = malloc(sizeof(logmsg_t));
    logmsg->msg = logbuff;
    logmsg->size = len;
    logmsg->written = 0;
    ks_datacont *log_dc = ks_datacont_new(logmsg, KS_VOIDP, 0);
    ks_list_enqueue(log_queue, log_dc);
}

int log_queue_size() { return ks_list_length(log_queue); }

int flush_logs() {
    ks_datacont *log_dc;
    while ((log_dc = ks_list_dequeue(log_queue)) != NULL) {
        logmsg_t *logmsg = log_dc->vp;
        size_t len;
        do {
            len = write(write_pipe, logmsg->msg + logmsg->written,
                        logmsg->size - logmsg->written);
            if (len >= 0) {
                logmsg->written += len;
            } else if (errno != EWOULDBLOCK) {
                perror("write()");
                free(logmsg->msg);
                free(logmsg);
                ks_datacont_delete(log_dc);
                exit(EXIT_FAILURE);
            } else {
                break;
            }
        } while (logmsg->written < logmsg->size);
        if (logmsg->written < logmsg->size) {
            ks_list_insert(log_queue, log_dc, -1);
            return -1;
        } else {
            free(logmsg->msg);
            free(logmsg);
            ks_datacont_delete(log_dc);
        }
    }
    return 0;
}
